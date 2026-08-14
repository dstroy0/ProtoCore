// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file smtp.c
 * @brief The client half of one SMTP session (RFC 5321) - implementation. See smtp.h for the model.
 *
 * ::SmtpNs::run is the dialogue: greeting, EHLO, optional STARTTLS, optional AUTH, MAIL / RCPT /
 * DATA, QUIT, every octet through the seam in ::SmtpNs::transport. ::SmtpNs::send binds that seam
 * to ::TcpClient and, when the security says so, to the client TLS session.
 */

#include "services/net/smtp/smtp.h"
#include "mmgr/membuild.h" // protocore_sb frame builder
#include "mmgr/protostr.h" // str.len: the bounded length this library uses
#include "protocore_config.h"
#include "server/clock/clock.h" // protocore_millis, pcdelay

#if PROTOCORE_ENABLE_SMTP

#include "network_drivers/presentation/codec/base64/base64.h" // Base64.encode: RFC 4648 sec 4

#if PROTOCORE_HAS_NET_STACK
#include "network_drivers/transport/tcp/client/client.h" // TcpClient: the outbound transport
#if PROTOCORE_ENABLE_SMTP_TLS
#include "network_drivers/tls/tls.h" // the client TLS session and its BIO seam
#endif
#endif

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

// Reply codes this client acts on (RFC 5321 sec 4.2.3, RFC 4954 sec 6, RFC 3207 sec 4).
#define SMTP_REPLY_SERVICE_READY 220 // <domain> Service ready, and STARTTLS ready to start TLS
#define SMTP_REPLY_OK 250            // Requested mail action okay, completed
#define SMTP_REPLY_WILL_FORWARD 251  // User not local; will forward to <forward-path>
#define SMTP_REPLY_START_INPUT 354   // Start mail input; end with <CRLF>.<CRLF>
#define SMTP_REPLY_AUTH_OK 235       // Authentication Succeeded
#define SMTP_REPLY_AUTH_CONTINUE 334 // server challenge, base64 in the text part

// The EHLO keyword whose presence decides whether the channel can be upgraded (RFC 3207 sec 3).
#define SMTP_KEYWORD_STARTTLS "STARTTLS"

// The Domain the EHLO argument carries when the caller names none (RFC 5321 sec 4.1.1.1).
#define SMTP_DEFAULT_CLIENT_NAME "protocore"

#if PROTOCORE_HAS_NET_STACK && PROTOCORE_ENABLE_SMTP_TLS
#if !defined(PROTOCORE_PLATFORM_TLS_WANT_READ) || !defined(PROTOCORE_PLATFORM_TLS_WANT_WRITE)
#error                                                                                                                 \
    "ProtoCore: the platform must state PROTOCORE_PLATFORM_TLS_WANT_READ and PROTOCORE_PLATFORM_TLS_WANT_WRITE - the value a BIO returns to the TLS engine when no octet moved and the call should be retried."
#endif
#endif

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

/** @brief The one live channel: its transport slot, its read deadline, and its TLS state. */
typedef struct
{
    int cid;               ///< the ::TcpClient slot this session rides
    uint32_t deadline;     ///< the protocore_millis() value the current read gives up at
    const char *host;      ///< the server name; the SNI an upgrade offers
    proto_bool tls_active; ///< every later record on this channel is encrypted
} SmtpChannel;

/**
 * @brief The module's compile-time storage: the live channel and every fixed buffer.
 *
 * All of it BSS, so a session costs no heap and nothing large lands on a task stack.
 */
struct SmtpStorage
{
    SmtpChannel chan;                     ///< the transport the seam implementations move octets over
    char line[PROTOCORE_SMTP_LINE_MAX];   ///< one command line, CRLF included (RFC 5321 sec 4.5.3.1.4: 512 octets)
    char b64[PROTOCORE_SMTP_LINE_MAX];    ///< one base64 AUTH client response plus its CRLF (RFC 4954 sec 4)
    char reply[PROTOCORE_SMTP_REPLY_MAX]; ///< one reply, continuation lines included (RFC 5321 sec 4.5.3.1.5)
    char content[PROTOCORE_SMTP_MSG_MAX]; ///< the assembled message and its end of mail data indication
    int code;                             ///< the code the last complete reply carried
    proto_bool keyword_seen;              ///< the last EHLO reply advertised the keyword that was probed for
};

/**
 * @brief The session's state and the calls that reach it - what SmtpNs points at.
 *
 * @var SmtpInternal::store  the live channel and every fixed buffer
 * @var SmtpInternal::ns     the handle a caller sets a call's members on
 */
struct SmtpInternal
{
    struct SmtpStorage *store;
    SmtpNs *ns;
};

static struct SmtpStorage s_store;

static struct SmtpInternal s_smtp = {.store = &s_store, .ns = &Smtp};

// ---------------------------------------------------------------------------
// Reply parsing (RFC 5321 sec 4.2)
// ---------------------------------------------------------------------------

// Is buf[0..len) a complete reply? RFC 5321 sec 4.2 gives
// Reply-line = *( Reply-code "-" [ textstring ] CRLF ) Reply-code [ SP textstring ] CRLF,
// so continuation lines carry '-' after the code and the final line carries SP or nothing. On a
// complete reply, write the three digits to *code and return true.
static proto_bool reply_complete(const char *buf, size_t len, int *code)
{
    size_t start = 0;
    for (size_t i = 0; i + 1 < len; i++)
    {
        if (buf[i] != '\r' || buf[i + 1] != '\n')
        {
            continue;
        }
        size_t line_len = i - start; // excludes the CRLF
        if (line_len >= 3 && buf[start] >= '0' && buf[start] <= '9' && buf[start + 1] >= '0' && buf[start + 1] <= '9' &&
            buf[start + 2] >= '0' && buf[start + 2] <= '9')
        {
            proto_bool final_line = (line_len == 3) || buf[start + 3] == ' ';
            if (final_line)
            {
                *code = (buf[start] - '0') * 100 + (buf[start + 1] - '0') * 10 + (buf[start + 2] - '0');
                return PROTO_TRUE;
            }
        }
        start = i + 2; // the next line begins after the CRLF
    }
    return PROTO_FALSE; // no final line yet - more octets are needed
}

// Case-insensitive compare of @p n octets. RFC 5321 sec 4.1.1.1: EHLO keywords may be specified in
// upper, lower, or mixed case and MUST be recognized case-insensitively.
static proto_bool keyword_ieq(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z')
        {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z')
        {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb)
        {
            return PROTO_FALSE;
        }
    }
    return PROTO_TRUE;
}

// Does @p keyword appear as a whole ehlo-keyword of its own? RFC 5321 sec 4.1.1.1 gives
// ehlo-line = ehlo-keyword *( SP ehlo-param ), one per reply line after the code and its separator,
// so the keyword starts at offset 4 and ends at a space or the CRLF. Matching it whole is what
// keeps "STARTTLSX" from reading as STARTTLS.
static proto_bool ehlo_has_keyword(const char *buf, size_t len, const char *keyword)
{
    size_t wlen = str.len(keyword, len + 1); // a whole keyword cannot outrun the reply that carries it
    size_t start = 0;
    for (size_t i = 0; i + 1 < len; i++)
    {
        if (buf[i] != '\r' || buf[i + 1] != '\n')
        {
            continue;
        }
        size_t line_len = i - start; // excludes the CRLF
        if (line_len > 4)            // three digits, the separator, and at least one keyword character
        {
            const char *kw = buf + start + 4;
            size_t klen = line_len - 4;
            if (klen >= wlen && keyword_ieq(kw, keyword, wlen) && (klen == wlen || kw[wlen] == ' '))
            {
                return PROTO_TRUE;
            }
        }
        start = i + 2;
    }
    return PROTO_FALSE;
}

// ---------------------------------------------------------------------------
// The dialogue (RFC 5321 sec 3)
// ---------------------------------------------------------------------------

// Publish one session's outcome on the handle.
static void finish(struct SmtpInternal *restrict ctx, SmtpResult r)
{
    ctx->ns->result = r;
    ctx->ns->ok = (r == SMTP_OK);
    ctx->ns->code = (int16_t)ctx->store->code;
}

// Write a whole command line; true only when every octet went out.
static proto_bool send_line(struct SmtpInternal *restrict ctx, const char *line)
{
    size_t n = str.len(line, PROTOCORE_SMTP_LINE_MAX + 1);
    return n == 0 || ctx->ns->transport.send(ctx->ns->transport.ctx, (const uint8_t *)line, n) == (int)n;
}

// Read one reply, continuation lines included, into store->reply and record its code. With
// @p keyword given, store->keyword_seen reports whether that ehlo-keyword appeared.
static SmtpResult read_reply(struct SmtpInternal *restrict ctx, const char *keyword)
{
    size_t len = 0;
    for (;;)
    {
        if (reply_complete(ctx->store->reply, len, &ctx->store->code))
        {
            if (keyword)
            {
                ctx->store->keyword_seen = ehlo_has_keyword(ctx->store->reply, len, keyword);
            }
            return SMTP_OK;
        }
        if (len >= sizeof(ctx->store->reply))
        {
            return SMTP_ERR_OVERFLOW;
        }
        int n = ctx->ns->transport.recv(ctx->ns->transport.ctx, (uint8_t *)ctx->store->reply + len,
                                        sizeof(ctx->store->reply) - len);
        if (n <= 0)
        {
            return SMTP_ERR_IO;
        }
        len += (size_t)n;
    }
}

// Send one CRLF-terminated command and read its reply. RFC 5321 sec 4.2: every command generates
// exactly one reply. The code lands in store->code.
static SmtpResult command(struct SmtpInternal *restrict ctx, const char *line)
{
    if (!send_line(ctx, line))
    {
        return SMTP_ERR_IO;
    }
    return read_reply(ctx, NULL);
}

// Send @p line and require reply code @p want; report @p bad for any other code.
static SmtpResult command_expect(struct SmtpInternal *restrict ctx, const char *line, int want, SmtpResult bad)
{
    SmtpResult r = command(ctx, line);
    if (r != SMTP_OK)
    {
        return r;
    }
    return (ctx->store->code == want) ? SMTP_OK : bad;
}

// The 220 Greeting, then EHLO. RFC 5321 sec 3.1 and sec 3.2: the server opens with a greeting and
// the client answers with EHLO, which requests the list of extensions the server supports. The
// command stays in store->line, which the STARTTLS path reissues verbatim.
static SmtpResult initiate_session(struct SmtpInternal *restrict ctx)
{
    SmtpResult r = read_reply(ctx, NULL);
    if (r != SMTP_OK)
    {
        return SMTP_ERR_IO;
    }
    if (ctx->store->code != SMTP_REPLY_SERVICE_READY)
    {
        return SMTP_ERR_PROTOCOL;
    }

    const char *client_name = ctx->ns->session.client_name;
    protocore_sb sb = {ctx->store->line, sizeof(ctx->store->line), 0, PROTO_TRUE};
    Sb.put(&sb, "EHLO ");
    Sb.put(&sb, (client_name && client_name[0]) ? client_name : SMTP_DEFAULT_CLIENT_NAME);
    Sb.put(&sb, "\r\n");
    (void)Sb.finish(&sb);
    if (!sb.ok)
    {
        return SMTP_ERR_OVERFLOW;
    }
    if (!send_line(ctx, ctx->store->line))
    {
        return SMTP_ERR_IO;
    }
    if (read_reply(ctx, SMTP_KEYWORD_STARTTLS) != SMTP_OK)
    {
        return SMTP_ERR_IO;
    }
    return (ctx->store->code == SMTP_REPLY_OK) ? SMTP_OK : SMTP_ERR_PROTOCOL;
}

// STARTTLS (RFC 3207 sec 4): the 220, the handshake, then the session starts over.
static SmtpResult upgrade_starttls(struct SmtpInternal *restrict ctx)
{
    // RFC 3207 sec 3: the keyword is how a server states it can negotiate TLS. Absent it, the
    // exchange stops here rather than carrying AUTH credentials over a cleartext channel.
    if (!ctx->store->keyword_seen)
    {
        return SMTP_ERR_NO_STARTTLS;
    }
    if (!ctx->ns->transport.starttls)
    {
        return SMTP_ERR_ARG; // asked to upgrade with no way to do it
    }
    SmtpResult r = command_expect(ctx, "STARTTLS\r\n", SMTP_REPLY_SERVICE_READY, SMTP_ERR_TLS);
    if (r != SMTP_OK)
    {
        return r;
    }
    if (!ctx->ns->transport.starttls(ctx->ns->transport.ctx))
    {
        return SMTP_ERR_TLS;
    }
    // RFC 3207 sec 4.2: the protocol is reset to its initial state and the client MUST discard any
    // knowledge obtained from the server that did not come from the TLS negotiation, so EHLO goes
    // out again and the encrypted reply is the extension list that counts.
    return command_expect(ctx, ctx->store->line, SMTP_REPLY_OK, SMTP_ERR_PROTOCOL);
}

// One client response of the AUTH exchange: base64 of @p secret on a line of its own
// (RFC 4954 sec 4; RFC 4648 sec 4 is the encoding). The reply code lands in store->code.
static SmtpResult auth_response(struct SmtpInternal *restrict ctx, const char *secret)
{
    size_t slen = str.len(secret, sizeof(ctx->store->b64));
    size_t elen = ((slen + 2) / 3) * 4; // base64 encodes three octets into four characters
    if (elen + 3 > sizeof(ctx->store->b64))
    {
        return SMTP_ERR_OVERFLOW; // the encoding plus CRLF plus NUL must fit
    }
    Base64.encode((const uint8_t *)secret, slen, ctx->store->b64);
    ctx->store->b64[elen] = '\r';
    ctx->store->b64[elen + 1] = '\n';
    ctx->store->b64[elen + 2] = '\0';
    return command(ctx, ctx->store->b64);
}

// AUTH LOGIN: the username, then the password, each answering a 334 challenge, and 235 on success
// (RFC 4954 sec 4 and sec 6). LOGIN itself is not RFC-defined; see the SmtpAuthArgs doc.
static SmtpResult authenticate(struct SmtpInternal *restrict ctx)
{
    SmtpResult r = command_expect(ctx, "AUTH LOGIN\r\n", SMTP_REPLY_AUTH_CONTINUE, SMTP_ERR_AUTH);
    if (r != SMTP_OK)
    {
        return r;
    }
    r = auth_response(ctx, ctx->ns->auth.user);
    if (r != SMTP_OK)
    {
        return r;
    }
    if (ctx->store->code != SMTP_REPLY_AUTH_CONTINUE)
    {
        return SMTP_ERR_AUTH;
    }
    r = auth_response(ctx, ctx->ns->auth.pass ? ctx->ns->auth.pass : "");
    if (r != SMTP_OK)
    {
        return r;
    }
    return (ctx->store->code == SMTP_REPLY_AUTH_OK) ? SMTP_OK : SMTP_ERR_AUTH;
}

// Assemble what DATA carries into store->content: the RFC 5322 header fields, the empty line that
// separates them from the body (sec 2.1), the body with every LF rewritten to CRLF, and the
// "<CRLF>.<CRLF>" end of mail data indication (RFC 5321 sec 4.1.1.4). RFC 5321 sec 4.5.2: before
// sending a line of mail text the client checks its first character, and a leading period gets one
// more period inserted ahead of it. Returns the length, or a negative ::SmtpResult.
static int build_content(struct SmtpInternal *restrict ctx)
{
    char *out = ctx->store->content;
    const size_t cap = sizeof(ctx->store->content);

    protocore_sb sb = {out, cap, 0, PROTO_TRUE};
    Sb.put(&sb, "From: <");
    Sb.put(&sb, ctx->ns->envelope.reverse_path);
    Sb.put(&sb, ">\r\nTo: <");
    Sb.put(&sb, ctx->ns->envelope.forward_path);
    Sb.put(&sb, ">\r\nSubject: ");
    Sb.put(&sb, ctx->ns->content.subject ? ctx->ns->content.subject : "");
    Sb.put(&sb, "\r\nMIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\n\r\n");
    size_t n = Sb.finish(&sb);
    if (!sb.ok)
    {
        return (int)SMTP_ERR_OVERFLOW;
    }

    const char *b = ctx->ns->content.body ? ctx->ns->content.body : "";
    proto_bool at_line_start = PROTO_TRUE;
    for (size_t i = 0; b[i]; i++)
    {
        char c = b[i];
        if (c == '\r')
        {
            continue; // CR is dropped and LF becomes CRLF, so every line ends exactly once
        }
        if (c == '\n')
        {
            if (n + 2 > cap)
            {
                return (int)SMTP_ERR_OVERFLOW;
            }
            out[n++] = '\r';
            out[n++] = '\n';
            at_line_start = PROTO_TRUE;
            continue;
        }
        if (at_line_start && c == '.')
        {
            if (n + 1 > cap)
            {
                return (int)SMTP_ERR_OVERFLOW;
            }
            out[n++] = '.'; // the inserted period of RFC 5321 sec 4.5.2
        }
        if (n + 1 > cap)
        {
            return (int)SMTP_ERR_OVERFLOW;
        }
        out[n++] = c;
        at_line_start = PROTO_FALSE;
    }
    // RFC 5321 sec 4.1.1.4: the first CRLF of the terminating sequence is the one that ends the
    // final line of the message, and an extra CRLF MUST NOT be added. n is past the fixed header
    // section so it always exceeds 2, and the only CR written above is the one the LF rewrite emits
    // immediately before its LF, so the pair test reads a real line end.
    if (!(n >= 2 && out[n - 2] == '\r' && out[n - 1] == '\n'))
    {
        if (n + 2 > cap)
        {
            return (int)SMTP_ERR_OVERFLOW;
        }
        out[n++] = '\r';
        out[n++] = '\n';
    }
    if (n + 3 > cap)
    {
        return (int)SMTP_ERR_OVERFLOW;
    }
    out[n++] = '.';
    out[n++] = '\r';
    out[n++] = '\n';
    return (int)n;
}

// MAIL then RCPT, both built into store->line. RFC 5321 sec 4.1.1.2 gives MAIL the reverse-path and
// sec 4.1.1.3 gives RCPT the forward-path; sec 4.2.3 lists 251 as "User not local; will forward to
// <forward-path>", which accepts the recipient as surely as 250 does.
static SmtpResult mail_transaction(struct SmtpInternal *restrict ctx)
{
    protocore_sb sb_mail = {ctx->store->line, sizeof(ctx->store->line), 0, PROTO_TRUE};
    Sb.put(&sb_mail, "MAIL FROM:<");
    Sb.put(&sb_mail, ctx->ns->envelope.reverse_path);
    Sb.put(&sb_mail, ">\r\n");
    (void)Sb.finish(&sb_mail);
    if (!sb_mail.ok)
    {
        return SMTP_ERR_OVERFLOW;
    }
    SmtpResult r = command_expect(ctx, ctx->store->line, SMTP_REPLY_OK, SMTP_ERR_PROTOCOL);
    if (r != SMTP_OK)
    {
        return r;
    }

    protocore_sb sb_rcpt = {ctx->store->line, sizeof(ctx->store->line), 0, PROTO_TRUE};
    Sb.put(&sb_rcpt, "RCPT TO:<");
    Sb.put(&sb_rcpt, ctx->ns->envelope.forward_path);
    Sb.put(&sb_rcpt, ">\r\n");
    (void)Sb.finish(&sb_rcpt);
    if (!sb_rcpt.ok)
    {
        return SMTP_ERR_OVERFLOW;
    }
    r = command(ctx, ctx->store->line);
    if (r != SMTP_OK)
    {
        return r;
    }
    if (ctx->store->code != SMTP_REPLY_OK && ctx->store->code != SMTP_REPLY_WILL_FORWARD)
    {
        return SMTP_ERR_PROTOCOL;
    }
    return SMTP_OK;
}

// DATA, the assembled content, then the reply that accepts or refuses the message. RFC 5321
// sec 4.1.1.4: the receiver normally sends 354 to DATA and then treats the lines that follow as
// mail data; on the end of mail data indication it MUST send an OK reply or a failure reply.
static SmtpResult data_transfer(struct SmtpInternal *restrict ctx)
{
    SmtpResult r = command_expect(ctx, "DATA\r\n", SMTP_REPLY_START_INPUT, SMTP_ERR_PROTOCOL);
    if (r != SMTP_OK)
    {
        return r;
    }
    int mlen = build_content(ctx);
    if (mlen < 0)
    {
        return (SmtpResult)mlen;
    }
    if (ctx->ns->transport.send(ctx->ns->transport.ctx, (const uint8_t *)ctx->store->content, (size_t)mlen) != mlen)
    {
        return SMTP_ERR_IO;
    }
    if (read_reply(ctx, NULL) != SMTP_OK)
    {
        return SMTP_ERR_IO;
    }
    return (ctx->store->code == SMTP_REPLY_OK) ? SMTP_OK : SMTP_ERR_PROTOCOL;
}

static void run_session(struct SmtpInternal *restrict ctx)
{
    ctx->store->code = 0;
    ctx->store->keyword_seen = PROTO_FALSE;

    if (!ctx->ns->transport.send || !ctx->ns->transport.recv || !ctx->ns->session.host ||
        !ctx->ns->envelope.reverse_path || !ctx->ns->envelope.reverse_path[0] || !ctx->ns->envelope.forward_path ||
        !ctx->ns->envelope.forward_path[0])
    {
        finish(ctx, SMTP_ERR_ARG);
        return;
    }

    SmtpResult r = initiate_session(ctx);
    if (r != SMTP_OK)
    {
        finish(ctx, r);
        return;
    }

    if (ctx->ns->session.security == SMTP_STARTTLS)
    {
        r = upgrade_starttls(ctx);
        if (r != SMTP_OK)
        {
            finish(ctx, r);
            return;
        }
    }

    if (ctx->ns->auth.user && ctx->ns->auth.user[0]) // RFC 4954 sec 4: AUTH runs only when named
    {
        r = authenticate(ctx);
        if (r != SMTP_OK)
        {
            finish(ctx, r);
            return;
        }
    }

    r = mail_transaction(ctx);
    if (r != SMTP_OK)
    {
        finish(ctx, r);
        return;
    }

    r = data_transfer(ctx);
    if (r != SMTP_OK)
    {
        finish(ctx, r);
        return;
    }

    // RFC 5321 sec 4.1.1.10: the sender MUST NOT close the channel until it sends QUIT and SHOULD
    // wait for the 221 reply. The message is already accepted, so that reply changes no outcome.
    (void)command(ctx, "QUIT\r\n");
    finish(ctx, SMTP_OK);
}

// ---------------------------------------------------------------------------
// The transport seam over ::TcpClient, plus the client TLS session for the two secure forms
// ---------------------------------------------------------------------------

#if PROTOCORE_HAS_NET_STACK

// Cleartext write. TcpClient clamps one send to 0xFFFF octets, so a longer buffer walks out in
// chunks and the call reports success only when all of it went.
static int plain_send(void *ctx, const uint8_t *data, size_t len)
{
    SmtpChannel *chan = (SmtpChannel *)ctx;
    size_t sent = 0;
    while (sent < len)
    {
        size_t chunk = len - sent;
        if (chunk > 0xFFFF)
        {
            chunk = 0xFFFF;
        }
        TcpClient.cid = chan->cid;
        TcpClient.io.data = data + sent;
        TcpClient.io.len = chunk;
        TcpClient.send(TcpClient.internal);
        if (!TcpClient.ok)
        {
            return -1;
        }
        sent += chunk;
    }
    return (int)len;
}

// Cleartext read. The deadline is taken fresh here, so PROTOCORE_SMTP_TIMEOUT_MS bounds one reply
// rather than the whole session.
static int plain_recv(void *ctx, uint8_t *buf, size_t cap)
{
    SmtpChannel *chan = (SmtpChannel *)ctx;
    chan->deadline = protocore_millis() + PROTOCORE_SMTP_TIMEOUT_MS;
    while ((int32_t)(chan->deadline - protocore_millis()) > 0)
    {
        TcpClient.cid = chan->cid;
        TcpClient.io.buf = buf;
        TcpClient.io.cap = cap;
        TcpClient.read(TcpClient.internal);
        if (TcpClient.n > 0)
        {
            return (int)TcpClient.n;
        }
        TcpClient.cid = chan->cid;
        TcpClient.is_closed(TcpClient.internal);
        if (TcpClient.ok)
        {
            TcpClient.cid = chan->cid;
            TcpClient.available(TcpClient.internal);
            if (TcpClient.n == 0)
            {
                return -1; // closed, and nothing left buffered to drain
            }
        }
        pcdelay(5);
    }
    return -1; // the reply never completed inside its budget
}

#if PROTOCORE_ENABLE_SMTP_TLS

// The ciphertext BIO the TLS engine handshakes and frames through. The engine passes its own ctx,
// not ours, so the channel is read from the one owned storage.
static int ciphertext_send(void *ctx, const unsigned char *buf, size_t len)
{
    (void)ctx;
    TcpClient.cid = s_store.chan.cid;
    TcpClient.io.data = buf;
    TcpClient.io.len = len;
    TcpClient.send(TcpClient.internal);
    return TcpClient.ok ? (int)len : PROTOCORE_PLATFORM_TLS_WANT_WRITE;
}

static int ciphertext_recv(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    TcpClient.cid = s_store.chan.cid;
    TcpClient.io.buf = buf;
    TcpClient.io.cap = len;
    TcpClient.read(TcpClient.internal);
    if (TcpClient.n > 0)
    {
        return (int)TcpClient.n;
    }
    TcpClient.cid = s_store.chan.cid;
    TcpClient.is_closed(TcpClient.internal);
    return TcpClient.ok ? 0 : PROTOCORE_PLATFORM_TLS_WANT_READ;
}

// Application write and read over the established session.
static int secure_send(void *ctx, const uint8_t *data, size_t len)
{
    (void)ctx;
    return protocore_tls_client_session_write(data, len) == (int)len ? (int)len : -1;
}

static int secure_recv(void *ctx, uint8_t *buf, size_t cap)
{
    SmtpChannel *chan = (SmtpChannel *)ctx;
    chan->deadline = protocore_millis() + PROTOCORE_SMTP_TIMEOUT_MS;
    while ((int32_t)(chan->deadline - protocore_millis()) > 0)
    {
        int n = protocore_tls_client_session_read(buf, cap);
        if (n > 0)
        {
            return n;
        }
        if (n < 0 && n != PROTOCORE_PLATFORM_TLS_WANT_READ && n != PROTOCORE_PLATFORM_TLS_WANT_WRITE)
        {
            return -1;
        }
        pcdelay(5);
    }
    return -1; // the reply never completed inside its budget
}

// Step the handshake to its end on a budget of its own. A handshake that inherited whatever was
// left of a read deadline could be abandoned before its first flight went out.
static proto_bool tls_handshake(struct SmtpInternal *restrict ctx)
{
    ctx->store->chan.deadline = protocore_millis() + PROTOCORE_SMTP_TIMEOUT_MS;
    protocore_tls_state st = protocore_tls_client_session_handshake();
    while (st == PROTOCORE_TLS_BUSY && (int32_t)(ctx->store->chan.deadline - protocore_millis()) > 0)
    {
        pcdelay(5);
        st = protocore_tls_client_session_handshake();
    }
    return st == PROTOCORE_TLS_READY;
}

#endif // PROTOCORE_ENABLE_SMTP_TLS

// The seam the engine holds for the whole session. A STARTTLS upgrade flips what these reach, so
// the engine never swaps transports mid-session and cannot keep writing cleartext after the switch.
static int wire_send(void *ctx, const uint8_t *data, size_t len)
{
#if PROTOCORE_ENABLE_SMTP_TLS
    if (((SmtpChannel *)ctx)->tls_active)
    {
        return secure_send(ctx, data, len);
    }
#endif
    return plain_send(ctx, data, len);
}

static int wire_recv(void *ctx, uint8_t *buf, size_t cap)
{
#if PROTOCORE_ENABLE_SMTP_TLS
    if (((SmtpChannel *)ctx)->tls_active)
    {
        return secure_recv(ctx, buf, cap);
    }
#endif
    return plain_recv(ctx, buf, cap);
}

// The in-place upgrade RFC 3207 sec 4 asks for, run after the server's 220.
static proto_bool wire_starttls(void *ctx)
{
    SmtpChannel *chan = (SmtpChannel *)ctx;
#if PROTOCORE_ENABLE_SMTP_TLS
    if (!protocore_tls_client_session_begin(chan->host, ciphertext_send, ciphertext_recv))
    {
        return PROTO_FALSE;
    }
    if (!tls_handshake(&s_smtp))
    {
        protocore_tls_client_session_end();
        return PROTO_FALSE;
    }
    chan->tls_active = PROTO_TRUE; // every later wire_send / wire_recv rides the session
    return PROTO_TRUE;
#else
    (void)chan;
    return PROTO_FALSE; // STARTTLS asked for in a build without TLS
#endif
}

// Dial the server, step the open to a connection, walk the session, close.
static void send_message(struct SmtpInternal *restrict ctx)
{
    ctx->store->code = 0;
    if (!ctx->ns->session.host)
    {
        finish(ctx, SMTP_ERR_ARG);
        return;
    }

    SmtpChannel *chan = &ctx->store->chan;
    TcpClient.dial.host = ctx->ns->session.host;
    TcpClient.dial.port = ctx->ns->session.port;
    TcpClient.dial.timeout_ms = PROTOCORE_SMTP_TIMEOUT_MS;
    TcpClient.open(TcpClient.internal);
    if (TcpClient.i32 < 0)
    {
        finish(ctx, SMTP_ERR_CONNECT);
        return;
    }
    chan->cid = TcpClient.i32;
    chan->host = ctx->ns->session.host;
    chan->tls_active = PROTO_FALSE;
    chan->deadline = protocore_millis() + PROTOCORE_SMTP_TIMEOUT_MS;

    // open() takes a slot and starts the name lookup; the connection exists only once connected()
    // says so, so the open is stepped here until it lands, closes, or runs out of budget.
    for (;;)
    {
        TcpClient.cid = chan->cid;
        TcpClient.connected(TcpClient.internal);
        if (TcpClient.ok)
        {
            break;
        }
        TcpClient.cid = chan->cid;
        TcpClient.is_closed(TcpClient.internal);
        if (TcpClient.ok || (int32_t)(chan->deadline - protocore_millis()) <= 0)
        {
            TcpClient.cid = chan->cid;
            TcpClient.close(TcpClient.internal);
            finish(ctx, SMTP_ERR_CONNECT);
            return;
        }
        pcdelay(5);
    }

    ctx->ns->transport.ctx = chan;

    if (ctx->ns->session.security == SMTP_TLS)
    {
#if PROTOCORE_ENABLE_SMTP_TLS
        // RFC 8314 sec 3.3: on the submissions service the TLS handshake begins immediately, so
        // there is no cleartext leg and no STARTTLS to offer the engine.
        if (!protocore_tls_client_session_begin(ctx->ns->session.host, ciphertext_send, ciphertext_recv) ||
            !tls_handshake(ctx))
        {
            protocore_tls_client_session_end();
            TcpClient.cid = chan->cid;
            TcpClient.close(TcpClient.internal);
            finish(ctx, SMTP_ERR_TLS);
            return;
        }
        chan->tls_active = PROTO_TRUE;
        ctx->ns->transport.send = secure_send;
        ctx->ns->transport.recv = secure_recv;
        ctx->ns->transport.starttls = NULL;
        run_session(ctx);
        protocore_tls_client_session_end();
#else
        TcpClient.cid = chan->cid;
        TcpClient.close(TcpClient.internal);
        finish(ctx, SMTP_ERR_TLS); // implicit TLS asked for in a build without TLS
        return;
#endif
    }
    else
    {
        ctx->ns->transport.send = wire_send;
        ctx->ns->transport.recv = wire_recv;
        ctx->ns->transport.starttls = wire_starttls;
        run_session(ctx);
#if PROTOCORE_ENABLE_SMTP_TLS
        if (chan->tls_active)
        {
            protocore_tls_client_session_end();
        }
#endif
    }

    TcpClient.cid = chan->cid;
    TcpClient.close(TcpClient.internal);
    chan->cid = -1;
}

#else // no outbound transport is built: run() over a caller's seam still works, send() cannot dial.

static void send_message(struct SmtpInternal *restrict ctx)
{
    finish(ctx, SMTP_ERR_CONNECT);
}

#endif // PROTOCORE_HAS_NET_STACK

// Designated, so a member's position in the struct does not decide what it binds to.
SmtpNs Smtp = {.run = run_session, .send = send_message, .internal = &s_smtp};

#endif // PROTOCORE_ENABLE_SMTP
