// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file smtp.cpp
 * @brief Outbound SMTP client (RFC 5321) - implementation. See smtp.h for the model.
 *
 * smtp_run() is the pure dialogue engine (host-testable via the send/recv seam);
 * smtp_send() binds it to the real transport on Arduino (pc_client, +pc_tls csess).
 */

#include "services/net/smtp/smtp.h"
#include "mmgr/membuild.h" // pc_sb frame builder
#include "protocore_config.h"
#include "services/system/clock.h" // pcdelay

#if PC_ENABLE_SMTP

#include "network_drivers/presentation/codec/base64/base64.h"
#include <stdio.h>  // snprintf
#include <string.h> // strlen, memcmp

#if PROTOCORE_HOT
#include "network_drivers/transport/client.h"
#include <Arduino.h> // millis, delay
#endif
#if PROTOCORE_HOT && PC_ENABLE_SMTP_TLS
#include "network_drivers/tls/tls.h"
#include <mbedtls/ssl.h> // MBEDTLS_ERR_SSL_WANT_* for the BIO callbacks
#endif
// Send an entire C string; returns true only if every byte went out.
static proto_bool send_str(SmtpSendFn send, void *ctx, const char *s)
{
    size_t n = strnlen(s, PC_SMTP_LINE_MAX + 1);
    // Every caller passes a CRLF-terminated command - a string literal, or a snprintf'd line with a
    // fixed non-empty prefix - so n is never 0; the check just keeps send_str total for any string.
    return n == 0 || send(ctx, (const uint8_t *)s, n) == (int)n; // GCOVR_EXCL_LINE  n == 0 unreachable
}

// Is buf[0..len) a complete SMTP reply? A reply is one or more CRLF lines that share a
// 3-digit code; the FINAL line has a space (or nothing) after the code, continuation
// lines have '-'. On a complete reply, set *code to the 3-digit value and return true.
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
        start = i + 2; // next line begins after the CRLF
    }
    return PROTO_FALSE; // no final line yet - need more bytes
}

// Case-insensitive compare of @p n bytes. EHLO keywords are case-insensitive (RFC 5321 sec 2.4)
// and strncasecmp is not portable across every toolchain this builds under.
static proto_bool ieq(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z')
        {
            ca = (char)(ca - 'A' + 'a');
        }
        // b is always the caller's `want`, and reply_has_cap's only call site passes the literal
        // "STARTTLS", so cb is always an upper-case letter here. Folding it keeps ieq symmetric.
        if (cb >= 'A' && cb <= 'Z') // GCOVR_EXCL_LINE  cb is always 'A'..'Z' (see above)
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

// Does @p want appear as its own EHLO capability line? Each line is "NNN<sep>KEYWORD[ params]",
// so the keyword starts at offset 4 and is matched whole - a server advertising "STARTTLSX" must
// not read as one advertising STARTTLS, since that decides whether credentials go out in clear.
static proto_bool reply_has_cap(const char *buf, size_t len, const char *want)
{
    size_t wlen = strnlen(want, len + 1); // a whole capability keyword cannot exceed the reply
    size_t start = 0;
    for (size_t i = 0; i + 1 < len; i++)
    {
        if (buf[i] != '\r' || buf[i + 1] != '\n')
        {
            continue;
        }
        size_t line_len = i - start; // excludes the CRLF
        if (line_len > 4)            // "NNN" + separator + at least one keyword character
        {
            const char *kw = buf + start + 4;
            size_t klen = line_len - 4;
            if (klen >= wlen && ieq(kw, want, wlen) && (klen == wlen || kw[wlen] == ' '))
            {
                return PROTO_TRUE;
            }
        }
        start = i + 2;
    }
    return PROTO_FALSE;
}

// Read one (possibly multi-line) reply and return its code. When @p want is given, @p found
// reports whether that capability appeared in the reply.
static SmtpResult read_reply_cap(SmtpRecvFn recv, void *ctx, int *code, const char *want, proto_bool *found)
{
    char buf[PC_SMTP_REPLY_MAX];
    size_t len = 0;
    for (;;)
    {
        if (reply_complete(buf, len, code))
        {
            // The two call sites pass want and found together (read_reply passes neither,
            // greet_ehlo passes both), so the pair is never half-populated.
            if (want && found) // GCOVR_EXCL_LINE  want and found are always both set or both null
            {
                *found = reply_has_cap(buf, len, want);
            }
            return SMTP_OK;
        }
        if (len >= sizeof(buf))
        {
            return SMTP_ERR_OVERFLOW;
        }
        int n = recv(ctx, (uint8_t *)buf + len, sizeof(buf) - len);
        if (n <= 0)
        {
            return SMTP_ERR_IO;
        }
        len += (size_t)n;
    }
}

static SmtpResult read_reply(SmtpRecvFn recv, void *ctx, int *code)
{
    return read_reply_cap(recv, ctx, code, NULL, NULL);
}

// Send one command line (already CRLF-terminated) and return the reply code, or a
// negative ::SmtpResult on an I/O failure.
static int command(SmtpSendFn send, SmtpRecvFn recv, void *ctx, const char *line)
{
    if (!send_str(send, ctx, line))
    {
        return (int)SMTP_ERR_IO;
    }
    int code = 0;
    SmtpResult r = read_reply(recv, ctx, &code);
    return (r == SMTP_OK) ? code : (int)r;
}

// AUTH LOGIN leg: send @p secret base64-encoded + CRLF, return the reply code.
static int auth_send_b64(SmtpSendFn send, SmtpRecvFn recv, void *ctx, const char *secret)
{
    char line[PC_SMTP_LINE_MAX];
    char b64[PC_SMTP_LINE_MAX];
    size_t slen = strnlen(secret, sizeof(b64));
    if (((slen + 2) / 3) * 4 + 3 >= sizeof(b64)) // b64 + CRLF must fit
    {
        return (int)SMTP_ERR_OVERFLOW;
    }
    pc_base64_encode((const uint8_t *)secret, slen, b64);
    pc_sb sb_line = {line, sizeof(line), 0, PROTO_TRUE};
    pc_sb_put(&sb_line, b64);
    pc_sb_put(&sb_line, "\r\n");
    pc_sb_finish(&sb_line);
    // GCOVR_EXCL_BR_START  cannot fire: b64+CRLF was checked to fit above and sizeof(b64) == sizeof(line)
    if (!sb_line.ok)
    {
        return (int)SMTP_ERR_OVERFLOW;
    }
    // GCOVR_EXCL_BR_STOP
    return command(send, recv, ctx, line);
}

// Assemble the DATA payload (headers + body + terminating dot) into @p out, applying
// CRLF normalization and RFC 5321 sec 4.5.2 dot-stuffing. Returns the length, or <0.
static int build_message(char *out, size_t cap, const SmtpConfig *cfg, const SmtpMessage *msg)
{
    pc_sb sb_out = {out, cap, 0, PROTO_TRUE};
    pc_sb_put(&sb_out, "From: <");
    pc_sb_put(&sb_out, cfg->from);
    pc_sb_put(&sb_out, ">\r\nTo: <");
    pc_sb_put(&sb_out, msg->to);
    pc_sb_put(&sb_out, ">\r\nSubject: ");
    pc_sb_put(&sb_out, msg->subject ? msg->subject : "");
    pc_sb_put(&sb_out, "\r\nMIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\n\r\n");
    int hn = (int)pc_sb_finish(&sb_out);
    // hn < 0 is unreachable: snprintf only reports failure on an output/encoding error, which
    // formatting %s into a caller buffer cannot produce. The >= cap truncation check is live.
    if (!sb_out.ok) // GCOVR_EXCL_LINE  hn < 0 unreachable (see above)
    {
        return (int)SMTP_ERR_OVERFLOW;
    }
    size_t n = (size_t)hn;

    const char *b = msg->body ? msg->body : "";
    proto_bool at_line_start = PROTO_TRUE;
    for (size_t i = 0; b[i]; i++)
    {
        char c = b[i];
        if (c == '\r')
        {
            continue; // normalize: CR is dropped, LF becomes CRLF
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
            if (n + 1 > cap) // dot-stuff: a body line starting with '.' gets an extra '.'
            {
                return (int)SMTP_ERR_OVERFLOW;
            }
            out[n++] = '.';
        }
        if (n + 1 > cap)
        {
            return (int)SMTP_ERR_OVERFLOW;
        }
        out[n++] = c;
        at_line_start = PROTO_FALSE;
    }
    // Body must end with CRLF before the terminator. n >= 2 always holds (n starts at the
    // fixed-header length, well over 2), and the only CR ever written to out is the one the
    // LF->CRLF rewrite emits immediately before its LF (a body CR is dropped above), so
    // out[n-2]=='\r' implies out[n-1]=='\n' - both are guards, not reachable states.
    if (!(n >= 2 && out[n - 2] == '\r' && out[n - 1] == '\n')) // GCOVR_EXCL_LINE  see above
    {
        if (n + 2 > cap)
        {
            return (int)SMTP_ERR_OVERFLOW;
        }
        out[n++] = '\r';
        out[n++] = '\n';
    }
    if (n + 3 > cap) // terminating "."CRLF
    {
        return (int)SMTP_ERR_OVERFLOW;
    }
    out[n++] = '.';
    out[n++] = '\r';
    out[n++] = '\n';
    return (int)n;
}

// Send @p line and require reply code @p want; @p bad is what to report for any other code.
// A negative code is an I/O ::SmtpResult and passes straight through.
static SmtpResult cmd_expect(SmtpSendFn send, SmtpRecvFn recv, void *ctx, const char *line, int want, SmtpResult bad)
{
    int code = command(send, recv, ctx, line);
    if (code < 0)
    {
        return (SmtpResult)code;
    }
    return (code == want) ? SMTP_OK : bad;
}

// Greeting + EHLO. @p line keeps the EHLO command, which the STARTTLS path reissues verbatim.
static SmtpResult greet_ehlo(const SmtpConfig *cfg, SmtpSendFn send, SmtpRecvFn recv, void *ctx, char *line, size_t cap,
                             proto_bool *has_starttls)
{
    int code = 0;
    if (read_reply(recv, ctx, &code) != SMTP_OK)
    {
        return SMTP_ERR_IO;
    }
    if (code != 220)
    {
        return SMTP_ERR_PROTOCOL;
    }

    // The capability list is only trustworthy once the channel is secure, which is why the
    // STARTTLS path reissues this command after the upgrade.
    pc_sb sb_line2 = {line, cap, 0, PROTO_TRUE};
    pc_sb_put(&sb_line2, "EHLO ");
    pc_sb_put(&sb_line2, (cfg->helo && cfg->helo[0]) ? cfg->helo : "esp32");
    pc_sb_put(&sb_line2, "\r\n");
    int n = (int)pc_sb_finish(&sb_line2);
    if (!sb_line2.ok) // GCOVR_EXCL_LINE  n < 0 unreachable: snprintf of %s into memory cannot fail
    {
        return SMTP_ERR_OVERFLOW;
    }
    if (!send_str(send, ctx, line))
    {
        return SMTP_ERR_IO;
    }
    if (read_reply_cap(recv, ctx, &code, "STARTTLS", has_starttls) != SMTP_OK)
    {
        return SMTP_ERR_IO;
    }
    return (code == 250) ? SMTP_OK : SMTP_ERR_PROTOCOL;
}

// STARTTLS (RFC 3207): upgrade in band, then start the session over.
static SmtpResult upgrade_starttls(SmtpSendFn send, SmtpRecvFn recv, SmtpStartTlsFn starttls, void *ctx,
                                   const char *ehlo, proto_bool has_starttls)
{
    // Fail closed on a stripped advertisement. An attacker who can delete the capability line
    // would otherwise get the whole exchange - AUTH credentials included - in the clear.
    if (!has_starttls)
    {
        return SMTP_ERR_NO_STARTTLS;
    }
    if (!starttls)
    {
        return SMTP_ERR_ARG; // asked to upgrade with no way to do it
    }
    SmtpResult r = cmd_expect(send, recv, ctx, "STARTTLS\r\n", 220, SMTP_ERR_TLS);
    if (r != SMTP_OK)
    {
        return r;
    }
    if (!starttls(ctx))
    {
        return SMTP_ERR_TLS;
    }
    // RFC 3207 sec 4.2: discard everything learned in the clear and reissue EHLO - the real
    // capability list (AUTH mechanisms especially) is the one the server sends encrypted.
    return cmd_expect(send, recv, ctx, ehlo, 250, SMTP_ERR_PROTOCOL);
}

// AUTH LOGIN: the username then the password, each base64 on its own line.
static SmtpResult auth_login(const SmtpConfig *cfg, SmtpSendFn send, SmtpRecvFn recv, void *ctx)
{
    SmtpResult r = cmd_expect(send, recv, ctx, "AUTH LOGIN\r\n", 334, SMTP_ERR_AUTH);
    if (r != SMTP_OK)
    {
        return r;
    }
    int code = auth_send_b64(send, recv, ctx, cfg->user);
    if (code < 0)
    {
        return (SmtpResult)code;
    }
    if (code != 334)
    {
        return SMTP_ERR_AUTH;
    }
    code = auth_send_b64(send, recv, ctx, cfg->pass ? cfg->pass : "");
    if (code < 0)
    {
        return (SmtpResult)code;
    }
    return (code == 235) ? SMTP_OK : SMTP_ERR_AUTH;
}

// MAIL FROM + RCPT TO, both built into @p line.
static SmtpResult send_envelope(const SmtpConfig *cfg, const SmtpMessage *msg, SmtpSendFn send, SmtpRecvFn recv,
                                void *ctx, char *line, size_t cap)
{
    pc_sb sb_line3 = {line, cap, 0, PROTO_TRUE};
    pc_sb_put(&sb_line3, "MAIL FROM:<");
    pc_sb_put(&sb_line3, cfg->from);
    pc_sb_put(&sb_line3, ">\r\n");
    int n = (int)pc_sb_finish(&sb_line3);
    if (!sb_line3.ok) // GCOVR_EXCL_LINE  n < 0 unreachable: snprintf of %s into memory cannot fail
    {
        return SMTP_ERR_OVERFLOW;
    }
    SmtpResult r = cmd_expect(send, recv, ctx, line, 250, SMTP_ERR_PROTOCOL);
    if (r != SMTP_OK)
    {
        return r;
    }

    pc_sb sb_line4 = {line, cap, 0, PROTO_TRUE};
    pc_sb_put(&sb_line4, "RCPT TO:<");
    pc_sb_put(&sb_line4, msg->to);
    pc_sb_put(&sb_line4, ">\r\n");
    n = (int)pc_sb_finish(&sb_line4);
    if (!sb_line4.ok) // GCOVR_EXCL_LINE  n < 0 unreachable: snprintf of %s into memory cannot fail
    {
        return SMTP_ERR_OVERFLOW;
    }
    int code = command(send, recv, ctx, line);
    if (code < 0)
    {
        return (SmtpResult)code;
    }
    if (code != 250 && code != 251) // 251 = user not local; will forward
    {
        return SMTP_ERR_PROTOCOL;
    }
    return SMTP_OK;
}

// DATA, the assembled message, then the acceptance reply.
static SmtpResult send_data(const SmtpConfig *cfg, const SmtpMessage *msg, SmtpSendFn send, SmtpRecvFn recv, void *ctx)
{
    SmtpResult r = cmd_expect(send, recv, ctx, "DATA\r\n", 354, SMTP_ERR_PROTOCOL);
    if (r != SMTP_OK)
    {
        return r;
    }
    char body[PC_SMTP_MSG_MAX];
    int mlen = build_message(body, sizeof(body), cfg, msg);
    if (mlen < 0)
    {
        return (SmtpResult)mlen;
    }
    if (send(ctx, (const uint8_t *)body, (size_t)mlen) != mlen)
    {
        return SMTP_ERR_IO;
    }
    int code = 0;
    if (read_reply(recv, ctx, &code) != SMTP_OK)
    {
        return SMTP_ERR_IO;
    }
    return (code == 250) ? SMTP_OK : SMTP_ERR_PROTOCOL;
}

SmtpResult smtp_run(const SmtpConfig *cfg, const SmtpMessage *msg, SmtpSendFn send, SmtpRecvFn recv,
                    SmtpStartTlsFn starttls, void *ctx)
{
    if (!cfg || !msg || !send || !recv || !cfg->host || !cfg->from || !cfg->from[0] || !msg->to || !msg->to[0])
    {
        return SMTP_ERR_ARG;
    }

    char line[PC_SMTP_LINE_MAX]; // holds the EHLO command, then each envelope command
    proto_bool has_starttls = PROTO_FALSE;
    SmtpResult r = greet_ehlo(cfg, send, recv, ctx, line, sizeof(line), &has_starttls);
    if (r != SMTP_OK)
    {
        return r;
    }

    if (cfg->security == SMTP_STARTTLS)
    {
        r = upgrade_starttls(send, recv, starttls, ctx, line, has_starttls);
        if (r != SMTP_OK)
        {
            return r;
        }
    }

    if (cfg->user && cfg->user[0]) // AUTH LOGIN only when a username is configured
    {
        r = auth_login(cfg, send, recv, ctx);
        if (r != SMTP_OK)
        {
            return r;
        }
    }

    r = send_envelope(cfg, msg, send, recv, ctx, line, sizeof(line));
    if (r != SMTP_OK)
    {
        return r;
    }

    r = send_data(cfg, msg, send, recv, ctx);
    if (r != SMTP_OK)
    {
        return r;
    }

    // QUIT is best-effort - the message is already accepted.
    (void)command(send, recv, ctx, "QUIT\r\n");
    return SMTP_OK;
}

// ---------------------------------------------------------------------------
// Real-transport binding (Arduino): pc_client, plus a pc_tls csess for SMTPS.
// ---------------------------------------------------------------------------

#if PROTOCORE_HOT

static struct SmtpXport;

/** @brief Owned state: which transport the TLS BIO callbacks act on.
 *
 * pc_tls_client_session_begin() carries no context pointer, so mbedtls calls the BIO with a ctx
 * that is not ours. The active transport is parked here for the life of the session instead. */
typedef struct
{
    SmtpXport *xport;
} SmtpTlsCtx;

typedef struct SmtpXport
{
    int cid;
    uint32_t deadline;
    const char *host;      ///< TLS SNI name, needed when the upgrade happens mid-dialogue
    proto_bool tls_active; ///< set once a STARTTLS upgrade has completed on this connection
} SmtpXport;

static SmtpTlsCtx s_smtp_tls = {NULL};

// Plaintext seam over pc_client.
static int cl_send(void *ctx, const uint8_t *data, size_t len)
{
    SmtpXport *x = (SmtpXport *)ctx;
    size_t sent = 0;
    while (sent < len)
    {
        size_t chunk = len - sent;
        if (chunk > 0xFFFF)
        {
            chunk = 0xFFFF;
        }
        if (!pc_client_send(x->cid, data + sent, chunk))
        {
            return -1;
        }
        sent += chunk;
    }
    return (int)len;
}
static int cl_recv(void *ctx, uint8_t *buf, size_t cap)
{
    SmtpXport *x = (SmtpXport *)ctx;
    while ((int32_t)(x->deadline - millis()) > 0)
    {
        size_t n = pc_client_read(x->cid, buf, cap);
        if (n > 0)
        {
            return (int)n;
        }
        if (pc_client_is_closed(x->cid) && pc_client_available(x->cid) == 0)
        {
            return -1;
        }
        pcdelay(5);
    }
    return -1; // timeout
}

#if PC_ENABLE_SMTP_TLS
// TLS ciphertext BIO: the csess handshake/records read/write the wire via pc_client.
static int tls_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    (void)ctx; // not ours - see SmtpTlsCtx
    SmtpXport *x = s_smtp_tls.xport;
    if (!x)
    {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    return pc_client_send(x->cid, buf, len) ? (int)len : MBEDTLS_ERR_SSL_WANT_WRITE;
}
static int tls_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx; // not ours - see SmtpTlsCtx
    SmtpXport *x = s_smtp_tls.xport;
    if (!x)
    {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    size_t n = pc_client_read(x->cid, buf, len);
    if (n == 0)
    {
        return pc_client_is_closed(x->cid) ? 0 : MBEDTLS_ERR_SSL_WANT_READ;
    }
    return (int)n;
}
// Application seam over the established TLS session.
static int tls_send(void *ctx, const uint8_t *data, size_t len)
{
    (void)ctx;
    return pc_tls_client_session_write(data, len) == (int)len ? (int)len : -1;
}
static int tls_recv(void *ctx, uint8_t *buf, size_t cap)
{
    SmtpXport *x = (SmtpXport *)ctx;
    while ((int32_t)(x->deadline - millis()) > 0)
    {
        int n = pc_tls_client_session_read(buf, cap);
        if (n > 0)
        {
            return n;
        }
        if (n < 0 && n != MBEDTLS_ERR_SSL_WANT_READ && n != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            return -1;
        }
        pcdelay(5);
    }
    return -1; // timeout
}
#endif // PC_ENABLE_SMTP_TLS

// Switching seam. The dialogue engine gets exactly one send/recv pair for the whole exchange; a
// STARTTLS upgrade flips these underneath it, so the engine never swaps transports mid-conversation
// and cannot accidentally keep writing plaintext after the upgrade.
static int xp_send(void *ctx, const uint8_t *data, size_t len)
{
#if PC_ENABLE_SMTP_TLS
    if (((SmtpXport *)ctx)->tls_active)
    {
        return tls_send(ctx, data, len);
    }
#endif
    return cl_send(ctx, data, len);
}
static int xp_recv(void *ctx, uint8_t *buf, size_t cap)
{
#if PC_ENABLE_SMTP_TLS
    if (((SmtpXport *)ctx)->tls_active)
    {
        return tls_recv(ctx, buf, cap);
    }
#endif
    return cl_recv(ctx, buf, cap);
}

// Upgrade the live connection in place, after the server's 220 to STARTTLS.
static proto_bool xp_starttls(void *ctx)
{
    SmtpXport *x = (SmtpXport *)ctx;
#if PC_ENABLE_SMTP_TLS
    if (!pc_tls_client_session_begin(x->host, tls_bio_send, tls_bio_recv))
    {
        return PROTO_FALSE;
    }
    // Fresh budget: the deadline carried here was set at connect time and has already funded the
    // greeting, EHLO and STARTTLS round trips. Reusing whatever is left of it can abandon the
    // handshake before the ClientHello even goes out, which the server sees as a silent hang.
    x->deadline = millis() + PC_SMTP_TIMEOUT_MS;
    int h;
    while ((h = pc_tls_client_session_handshake()) == 0 && (int32_t)(x->deadline - millis()) > 0)
    {
        pcdelay(5);
    }
    if (h != 1) // 1 = established; 0 = still pending at timeout; <0 = fatal
    {
        pc_tls_client_session_end();
        return PROTO_FALSE;
    }
    x->tls_active = PROTO_TRUE; // every later xp_send/xp_recv now goes through the session
    return PROTO_TRUE;
#else
    (void)x;
    return PROTO_FALSE; // STARTTLS requested but TLS not built in
#endif
}

SmtpResult smtp_send(const SmtpConfig *cfg, const SmtpMessage *msg)
{
    if (!cfg || !cfg->host)
    {
        return SMTP_ERR_ARG;
    }

    SmtpXport x;
    x.cid = pc_client_open(cfg->host, cfg->port, PC_SMTP_TIMEOUT_MS);
    if (x.cid < 0)
    {
        return SMTP_ERR_CONNECT;
    }
    x.deadline = millis() + PC_SMTP_TIMEOUT_MS;
    x.host = cfg->host;
    x.tls_active = PROTO_FALSE;
#if PC_ENABLE_SMTP_TLS
    s_smtp_tls.xport = &x; // the BIO callbacks read this, not their ctx argument
#endif

    SmtpResult rc;
    if (cfg->security == SMTP_TLS)
    {
#if PC_ENABLE_SMTP_TLS
        if (!pc_tls_client_session_begin(cfg->host, tls_bio_send, tls_bio_recv))
        {
            pc_client_close(x.cid);
            return SMTP_ERR_TLS;
        }
        int h;
        while ((h = pc_tls_client_session_handshake()) == 0 && (int32_t)(x.deadline - millis()) > 0)
        {
            pcdelay(5);
        }
        if (h != 1) // 1 = established; 0 = still pending at timeout; <0 = fatal
        {
            pc_tls_client_session_end();
            pc_client_close(x.cid);
            return SMTP_ERR_TLS;
        }
        rc = smtp_run(cfg, msg, tls_send, tls_recv, NULL, &x);
        pc_tls_client_session_end();
#else
        pc_client_close(x.cid);
        return SMTP_ERR_TLS; // SMTPS requested but TLS not built in
#endif
    }
    else
    {
        rc = smtp_run(cfg, msg, xp_send, xp_recv, xp_starttls, &x);
    }

    pc_client_close(x.cid);
#if PC_ENABLE_SMTP_TLS
    s_smtp_tls.xport = NULL; // x is about to go out of scope
#endif
    return rc;
}

#else // host build: no lwIP. smtp_run() above is host-testable; smtp_send() is a stub.

SmtpResult smtp_send(const SmtpConfig *, const SmtpMessage *)
{
    return SMTP_ERR_CONNECT;
}

#endif // PROTOCORE_HOT

#endif // PC_ENABLE_SMTP
