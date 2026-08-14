// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file nats.c
 * @brief The NATS client protocol: the operation builders and the inbound operation parser.
 *
 * A builder lays one control line, and a payload where the operation carries one, into the caller's
 * buffer through a bounded cursor that stops at the first overflow. The parser splits the control
 * line at the head of the caller's buffer on space or tab and reports the octets the whole operation
 * occupies. See nats.h for the grammar and the reference it comes from.
 */

#include "services/iot/nats/nats.h"

#if PROTOCORE_ENABLE_NATS

#include "mmgr/protomem.h" // mem.cpy / mem.cmp: the spans an operation is laid from and matched against
#include "mmgr/protostr.h" // str.len: the bounded length of a caller's NUL-terminated field

// The terminator every protocol message ends with (NATS Protocol, Protocol conventions: Newlines).
#define NATS_CRLF "\r\n"
#define NATS_CRLF_LEN 2u

// Decimal digits a 64-bit count needs.
#define NATS_UINT_DIGITS 20u

// Fields a MSG control line carries: subject, sid, [reply-to], #bytes.
#define NATS_MSG_FIELDS 4u

// Fields an HMSG control line carries: subject, sid, [reply-to], #header bytes, #total bytes.
#define NATS_HMSG_FIELDS 5u

// Octets in the operation names a parse steps past before it reads the fields.
#define NATS_OP_LEN_MSG 3u  // MSG
#define NATS_OP_LEN_HMSG 4u // HMSG
#define NATS_OP_LEN_ARG 4u  // -ERR and INFO, whose remainder is one argument

/** @brief A bounded append cursor over the caller's buffer; ok clears at the first overflow. */
typedef struct
{
    char *p;       ///< the buffer being written
    size_t cap;    ///< octets it holds
    size_t pos;    ///< octets written so far
    proto_bool ok; ///< every append so far fit
} Buf;

/**
 * @brief The calls that read the handle - what NatsNs points at.
 *
 * No storage member: every octet a call touches belongs to the caller, so nothing survives a call.
 *
 * @var NatsInternal::ns  the handle a caller sets a call's members on
 */
struct NatsInternal
{
    NatsNs *ns;
};

static struct NatsInternal s_nats = {.ns = &Nats};

// Append a NUL-terminated string. A null string or a run past cap stops the cursor.
static void put_str(Buf *b, const char *s)
{
    if (!s)
    {
        b->ok = PROTO_FALSE;
        return;
    }
    if (!b->ok)
    {
        return;
    }
    size_t n = str.len(s, b->cap + 1);
    if (b->pos + n > b->cap)
    {
        b->ok = PROTO_FALSE;
        return;
    }
    mem.cpy(b->p + b->pos, s, n);
    b->pos += n;
}

// Append n octets.
static void put_bytes(Buf *b, const uint8_t *d, size_t n)
{
    if (!b->ok)
    {
        return;
    }
    if (b->pos + n > b->cap)
    {
        b->ok = PROTO_FALSE;
        return;
    }
    if (n)
    {
        mem.cpy(b->p + b->pos, d, n);
    }
    b->pos += n;
}

// Append one octet.
static void put_ch(Buf *b, char c)
{
    if (!b->ok)
    {
        return;
    }
    if (b->pos + 1 > b->cap)
    {
        b->ok = PROTO_FALSE;
        return;
    }
    b->p[b->pos++] = c;
}

// Append a count as decimal digits, most significant first.
static void put_uint(Buf *b, uint64_t v)
{
    char tmp[NATS_UINT_DIGITS];
    size_t n = 0;
    char rev[NATS_UINT_DIGITS];
    size_t r = 0;
    if (v == 0)
    {
        rev[r++] = '0';
    }
    while (v)
    {
        rev[r++] = (char)('0' + (int)(v % 10));
        v /= 10;
    }
    while (r)
    {
        tmp[n++] = rev[--r];
    }
    put_bytes(b, (const uint8_t *)tmp, n);
}

// Report the octets the cursor wrote, NUL-terminating when one byte is left over. A stopped cursor
// reports 0 octets and a false outcome.
static void finish(struct NatsInternal *restrict ctx, Buf *b)
{
    if (!b->ok)
    {
        ctx->ns->n = 0;
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    if (b->pos < b->cap)
    {
        b->p[b->pos] = '\0';
    }
    ctx->ns->n = b->pos;
    ctx->ns->ok = PROTO_TRUE;
}

// CONNECT {"option_name":option_value,...}
static void nats_connect(struct NatsInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->out.buf || !ctx->ns->client.options)
    {
        return;
    }
    Buf b = {ctx->ns->out.buf, ctx->ns->out.cap, 0, PROTO_TRUE};
    put_str(&b, "CONNECT ");
    put_str(&b, ctx->ns->client.options);
    put_str(&b, NATS_CRLF);
    finish(ctx, &b);
}

// PUB <subject> [reply-to] <#bytes>CRLF[payload]CRLF
static void nats_pub(struct NatsInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->out.buf || !ctx->ns->publish.subject || (ctx->ns->publish.payload_len && !ctx->ns->publish.payload))
    {
        return;
    }
    Buf b = {ctx->ns->out.buf, ctx->ns->out.cap, 0, PROTO_TRUE};
    put_str(&b, "PUB ");
    put_str(&b, ctx->ns->publish.subject);
    if (ctx->ns->publish.reply_to)
    {
        put_ch(&b, ' ');
        put_str(&b, ctx->ns->publish.reply_to);
    }
    put_ch(&b, ' ');
    put_uint(&b, ctx->ns->publish.payload_len);
    put_str(&b, NATS_CRLF);
    put_bytes(&b, ctx->ns->publish.payload, ctx->ns->publish.payload_len);
    put_str(&b, NATS_CRLF);
    finish(ctx, &b);
}

// HPUB <subject> [reply-to] <#header bytes> <#total bytes>CRLF[headers][payload]CRLF, where the
// header section carries its own terminating CR LF CR LF and #total bytes counts it plus the payload.
static void nats_hpub(struct NatsInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->out.buf || !ctx->ns->publish.subject || !ctx->ns->headers.block || ctx->ns->headers.bytes == 0 ||
        (ctx->ns->publish.payload_len && !ctx->ns->publish.payload))
    {
        return;
    }
    Buf b = {ctx->ns->out.buf, ctx->ns->out.cap, 0, PROTO_TRUE};
    put_str(&b, "HPUB ");
    put_str(&b, ctx->ns->publish.subject);
    if (ctx->ns->publish.reply_to)
    {
        put_ch(&b, ' ');
        put_str(&b, ctx->ns->publish.reply_to);
    }
    put_ch(&b, ' ');
    put_uint(&b, ctx->ns->headers.bytes);
    put_ch(&b, ' ');
    put_uint(&b, ctx->ns->headers.bytes + ctx->ns->publish.payload_len);
    put_str(&b, NATS_CRLF);
    put_bytes(&b, (const uint8_t *)ctx->ns->headers.block, ctx->ns->headers.bytes);
    put_bytes(&b, ctx->ns->publish.payload, ctx->ns->publish.payload_len);
    put_str(&b, NATS_CRLF);
    finish(ctx, &b);
}

// SUB <subject> [queue group] <sid>
static void nats_sub(struct NatsInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->out.buf || !ctx->ns->subscription.subject || !ctx->ns->subscription.sid)
    {
        return;
    }
    Buf b = {ctx->ns->out.buf, ctx->ns->out.cap, 0, PROTO_TRUE};
    put_str(&b, "SUB ");
    put_str(&b, ctx->ns->subscription.subject);
    if (ctx->ns->subscription.queue_group)
    {
        put_ch(&b, ' ');
        put_str(&b, ctx->ns->subscription.queue_group);
    }
    put_ch(&b, ' ');
    put_str(&b, ctx->ns->subscription.sid);
    put_str(&b, NATS_CRLF);
    finish(ctx, &b);
}

// UNSUB <sid> [max_msgs]
static void nats_unsub(struct NatsInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->out.buf || !ctx->ns->subscription.sid)
    {
        return;
    }
    Buf b = {ctx->ns->out.buf, ctx->ns->out.cap, 0, PROTO_TRUE};
    put_str(&b, "UNSUB ");
    put_str(&b, ctx->ns->subscription.sid);
    if (ctx->ns->subscription.with_max)
    {
        put_ch(&b, ' ');
        put_uint(&b, ctx->ns->subscription.max_msgs);
    }
    put_str(&b, NATS_CRLF);
    finish(ctx, &b);
}

// PING
static void nats_ping(struct NatsInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->out.buf)
    {
        return;
    }
    Buf b = {ctx->ns->out.buf, ctx->ns->out.cap, 0, PROTO_TRUE};
    put_str(&b, "PING" NATS_CRLF);
    finish(ctx, &b);
}

// PONG
static void nats_pong(struct NatsInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->out.buf)
    {
        return;
    }
    Buf b = {ctx->ns->out.buf, ctx->ns->out.cap, 0, PROTO_TRUE};
    put_str(&b, "PONG" NATS_CRLF);
    finish(ctx, &b);
}

// Index of the CR LF that ends the control line, or len when it is not buffered yet.
static size_t find_crlf(const char *buf, size_t len)
{
    for (size_t i = 0; i + 1 < len; i++)
    {
        if (buf[i] == '\r' && buf[i + 1] == '\n')
        {
            return i;
        }
    }
    return len;
}

// Decimal parse of [s, s+n); false on an empty run or a non-digit.
static proto_bool parse_uint(const char *s, size_t n, size_t *out)
{
    if (n == 0)
    {
        return PROTO_FALSE;
    }
    size_t v = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (s[i] < '0' || s[i] > '9')
        {
            return PROTO_FALSE;
        }
        v = v * 10 + (size_t)(s[i] - '0');
    }
    *out = v;
    return PROTO_TRUE;
}

// True when the control line opens with the operation name op and ends there or at a delimiter.
static proto_bool verb_is(const char *buf, size_t line_len, const char *op)
{
    size_t n = str.len(op, line_len + 1);
    if (line_len < n)
    {
        return PROTO_FALSE;
    }
    if (mem.cmp(buf, op, n) != 0)
    {
        return PROTO_FALSE;
    }
    return line_len == n || buf[n] == ' ' || buf[n] == '\t';
}

// Split the control line from index `from` into at most `max` fields and report how many were found.
// A space or a tab delimits, and repeated whitespace counts as one delimiter (NATS Protocol,
// Protocol conventions: Field Delimiter).
static size_t split_fields(const char *buf, size_t line_len, size_t from, const char **tok, size_t *tlen, size_t max)
{
    size_t ntok = 0;
    size_t i = from;
    while (i < line_len && ntok < max)
    {
        while (i < line_len && (buf[i] == ' ' || buf[i] == '\t'))
        {
            i++;
        }
        if (i >= line_len)
        {
            break;
        }
        size_t start = i;
        while (i < line_len && buf[i] != ' ' && buf[i] != '\t')
        {
            i++;
        }
        tok[ntok] = buf + start;
        tlen[ntok] = i - start;
        ntok++;
    }
    return ntok;
}

// Decode the operation at the head of in.buf into msg, and report the octets it occupies.
static void nats_parse(struct NatsInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->consumed = 0;

    const char *buf = ctx->ns->in.buf;
    const size_t len = ctx->ns->in.len;
    NatsMsg *out = &ctx->ns->msg;
    if (!buf)
    {
        return;
    }
    size_t crlf = find_crlf(buf, len);
    if (crlf == len)
    {
        return; // control line not fully buffered
    }
    size_t line_len = crlf;
    size_t after_line = crlf + NATS_CRLF_LEN;

    out->subject = out->sid = out->reply_to = out->arg = NULL;
    out->subject_len = out->sid_len = out->reply_to_len = out->arg_len = 0;
    out->payload = NULL;
    out->payload_len = 0;
    out->headers = NULL;
    out->header_bytes = 0;

    if (verb_is(buf, line_len, "PING"))
    {
        out->op = NATS_OP_PING;
        ctx->ns->consumed = after_line;
        ctx->ns->ok = PROTO_TRUE;
        return;
    }
    if (verb_is(buf, line_len, "PONG"))
    {
        out->op = NATS_OP_PONG;
        ctx->ns->consumed = after_line;
        ctx->ns->ok = PROTO_TRUE;
        return;
    }
    if (verb_is(buf, line_len, "+OK"))
    {
        out->op = NATS_OP_OK;
        ctx->ns->consumed = after_line;
        ctx->ns->ok = PROTO_TRUE;
        return;
    }
    if (verb_is(buf, line_len, "-ERR") || verb_is(buf, line_len, "INFO"))
    {
        out->op = (buf[0] == '-') ? NATS_OP_ERR : NATS_OP_INFO;
        size_t a = NATS_OP_LEN_ARG; // "-ERR" and "INFO" are both four octets
        while (a < line_len && (buf[a] == ' ' || buf[a] == '\t'))
        {
            a++;
        }
        out->arg = buf + a;
        out->arg_len = line_len - a;
        ctx->ns->consumed = after_line;
        ctx->ns->ok = PROTO_TRUE;
        return;
    }
    if (verb_is(buf, line_len, "MSG"))
    {
        // MSG <subject> <sid> [reply-to] <#bytes>
        const char *tok[NATS_MSG_FIELDS];
        size_t tlen[NATS_MSG_FIELDS];
        size_t ntok = split_fields(buf, line_len, NATS_OP_LEN_MSG, tok, tlen, NATS_MSG_FIELDS);
        if (ntok != 3 && ntok != NATS_MSG_FIELDS) // subject sid [reply-to] #bytes
        {
            return;
        }
        size_t size;
        if (!parse_uint(tok[ntok - 1], tlen[ntok - 1], &size)) // the last field is #bytes
        {
            return;
        }
        // Bound #bytes against the remaining capacity without adding it (a 32-bit size_t would wrap
        // if we computed after_line + size + NATS_CRLF_LEN first).
        if (after_line + NATS_CRLF_LEN > len || size > len - after_line - NATS_CRLF_LEN)
        {
            return; // payload plus its terminator not fully buffered
        }
        out->op = NATS_OP_MSG;
        out->subject = tok[0];
        out->subject_len = tlen[0];
        out->sid = tok[1];
        out->sid_len = tlen[1];
        if (ntok == NATS_MSG_FIELDS)
        {
            out->reply_to = tok[2];
            out->reply_to_len = tlen[2];
        }
        out->payload = (const uint8_t *)(buf + after_line);
        out->payload_len = size;
        ctx->ns->consumed = after_line + size + NATS_CRLF_LEN;
        ctx->ns->ok = PROTO_TRUE;
        return;
    }
    if (verb_is(buf, line_len, "HMSG"))
    {
        // HMSG <subject> <sid> [reply-to] <#header bytes> <#total bytes>
        const char *tok[NATS_HMSG_FIELDS];
        size_t tlen[NATS_HMSG_FIELDS];
        size_t ntok = split_fields(buf, line_len, NATS_OP_LEN_HMSG, tok, tlen, NATS_HMSG_FIELDS);
        if (ntok != 4 && ntok != NATS_HMSG_FIELDS) // subject sid [reply-to] #header bytes #total bytes
        {
            return;
        }
        size_t hdr_len, total_size;
        if (!parse_uint(tok[ntok - 2], tlen[ntok - 2], &hdr_len) ||
            !parse_uint(tok[ntok - 1], tlen[ntok - 1], &total_size))
        {
            return;
        }
        if (hdr_len > total_size) // #header bytes is part of #total bytes
        {
            return;
        }
        // Bound #total bytes against the remaining capacity without overflowing size_t.
        if (after_line + NATS_CRLF_LEN > len || total_size > len - after_line - NATS_CRLF_LEN)
        {
            return; // header section plus payload plus terminator not fully buffered
        }
        out->op = NATS_OP_MSG;
        out->subject = tok[0];
        out->subject_len = tlen[0];
        out->sid = tok[1];
        out->sid_len = tlen[1];
        if (ntok == NATS_HMSG_FIELDS)
        {
            out->reply_to = tok[2];
            out->reply_to_len = tlen[2];
        }
        out->headers = buf + after_line;
        out->header_bytes = hdr_len;
        out->payload = (const uint8_t *)(buf + after_line + hdr_len);
        out->payload_len = total_size - hdr_len;
        ctx->ns->consumed = after_line + total_size + NATS_CRLF_LEN;
        ctx->ns->ok = PROTO_TRUE;
        return;
    }

    out->op = NATS_OP_UNKNOWN;
    ctx->ns->consumed = after_line;
    ctx->ns->ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
NatsNs Nats = {
    .connect = nats_connect,
    .pub = nats_pub,
    .hpub = nats_hpub,
    .sub = nats_sub,
    .unsub = nats_unsub,
    .ping = nats_ping,
    .pong = nats_pong,
    .parse = nats_parse,
    .internal = &s_nats,
};

#endif // PROTOCORE_ENABLE_NATS
