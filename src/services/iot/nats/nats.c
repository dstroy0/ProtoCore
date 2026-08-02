// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file nats.c
 * @brief NATS client protocol builder + parser (pure, host-tested).
 */

#include "services/iot/nats/nats.h"

#if PC_ENABLE_NATS

#include <string.h>

// A tiny bounded append cursor; sets ok=false on overflow and stops.
typedef struct
{
    char *p;
    size_t cap;
    size_t pos;
    proto_bool ok;
} Buf;

static void put_str(Buf *b, const char *s)
{
    if (!b->ok || !s) // GCOVR_EXCL_BR_LINE  the !s half is unreachable: every put_str arg is a literal or a
                      // checked/guarded non-null string (see below)
    {
        if (!s) // GCOVR_EXCL_BR_LINE  unreachable for the same reason as the condition above
        {
            b->ok = PROTO_FALSE; // GCOVR_EXCL_LINE  unreachable: every put_str arg is a literal or a checked/guarded
                                 // non-null string
        }
        return;
    }
    size_t n = strnlen(s, b->cap + 1);
    if (b->pos + n > b->cap)
    {
        b->ok = PROTO_FALSE;
        return;
    }
    memcpy(b->p + b->pos, s, n);
    b->pos += n;
}

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
        memcpy(b->p + b->pos, d, n);
    }
    b->pos += n;
}

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

static void put_uint(Buf *b, uint64_t v)
{
    char tmp[20];
    size_t n = 0;
    char rev[20];
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

static size_t finish(Buf *b)
{
    if (!b->ok)
    {
        return 0;
    }
    if (b->pos < b->cap) // NUL-terminate when there's room (the returned length excludes it)
    {
        b->p[b->pos] = '\0';
    }
    return b->pos;
}

size_t pc_nats_build_connect(char *buf, size_t cap, const char *options_json)
{
    if (!buf || !options_json)
    {
        return 0;
    }
    Buf b = {buf, cap, 0, PROTO_TRUE};
    put_str(&b, "CONNECT ");
    put_str(&b, options_json);
    put_str(&b, "\r\n");
    return finish(&b);
}

size_t pc_nats_build_pub(char *buf, size_t cap, const char *subject, const char *reply_to, const uint8_t *payload,
                         size_t payload_len)
{
    if (!buf || !subject || (payload_len && !payload))
    {
        return 0;
    }
    Buf b = {buf, cap, 0, PROTO_TRUE};
    put_str(&b, "PUB ");
    put_str(&b, subject);
    if (reply_to)
    {
        put_ch(&b, ' ');
        put_str(&b, reply_to);
    }
    put_ch(&b, ' ');
    put_uint(&b, payload_len);
    put_str(&b, "\r\n");
    put_bytes(&b, payload, payload_len);
    put_str(&b, "\r\n");
    return finish(&b);
}

size_t pc_nats_build_hpub(char *buf, size_t cap, const char *subject, const char *reply_to, const char *headers,
                          size_t headers_len, const uint8_t *payload, size_t payload_len)
{
    if (!buf || !subject || !headers || headers_len == 0 || (payload_len && !payload))
    {
        return 0;
    }
    Buf b = {buf, cap, 0, PROTO_TRUE};
    put_str(&b, "HPUB ");
    put_str(&b, subject);
    if (reply_to)
    {
        put_ch(&b, ' ');
        put_str(&b, reply_to);
    }
    put_ch(&b, ' ');
    put_uint(&b, headers_len); // hdr_len
    put_ch(&b, ' ');
    put_uint(&b, headers_len + payload_len); // total_len = headers + payload
    put_str(&b, "\r\n");
    put_bytes(&b, (const uint8_t *)headers, headers_len);
    put_bytes(&b, payload, payload_len);
    put_str(&b, "\r\n");
    return finish(&b);
}

size_t pc_nats_build_sub(char *buf, size_t cap, const char *subject, const char *queue, const char *sid)
{
    if (!buf || !subject || !sid)
    {
        return 0;
    }
    Buf b = {buf, cap, 0, PROTO_TRUE};
    put_str(&b, "SUB ");
    put_str(&b, subject);
    if (queue)
    {
        put_ch(&b, ' ');
        put_str(&b, queue);
    }
    put_ch(&b, ' ');
    put_str(&b, sid);
    put_str(&b, "\r\n");
    return finish(&b);
}

size_t pc_nats_build_unsub(char *buf, size_t cap, const char *sid, uint32_t max_msgs, proto_bool with_max)
{
    if (!buf || !sid)
    {
        return 0;
    }
    Buf b = {buf, cap, 0, PROTO_TRUE};
    put_str(&b, "UNSUB ");
    put_str(&b, sid);
    if (with_max)
    {
        put_ch(&b, ' ');
        put_uint(&b, max_msgs);
    }
    put_str(&b, "\r\n");
    return finish(&b);
}

size_t pc_nats_build_ping(char *buf, size_t cap)
{
    Buf b = {buf, cap, 0, PROTO_TRUE};
    put_str(&b, "PING\r\n");
    return finish(&b);
}

size_t pc_nats_build_pong(char *buf, size_t cap)
{
    Buf b = {buf, cap, 0, PROTO_TRUE};
    put_str(&b, "PONG\r\n");
    return finish(&b);
}

// Find the CRLF that ends the control line; returns the index of '\r', or len if absent.
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

// Decimal parse of [s, s+n); false on a non-digit.
static proto_bool parse_uint(const char *s, size_t n, size_t *out)
{
    if (n == 0) // GCOVR_EXCL_BR_LINE  unreachable: the sole caller passes a space-delimited MSG token, always
                // length>=1 (see below)
    {
        return PROTO_FALSE; // GCOVR_EXCL_LINE  unreachable: the sole caller passes a space-delimited MSG token, always
                            // length>=1
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

// True when the verb token at buf matches op (and is followed by a space or end-of-token).
static proto_bool verb_is(const char *buf, size_t line_len, const char *op)
{
    size_t n = strnlen(op, line_len + 1);
    if (line_len < n)
    {
        return PROTO_FALSE;
    }
    if (memcmp(buf, op, n) != 0)
    {
        return PROTO_FALSE;
    }
    return line_len == n || buf[n] == ' ' || buf[n] == '\t';
}

proto_bool pc_nats_parse(const char *buf, size_t len, NatsMsg *out, size_t *consumed)
{
    if (!buf || !out || !consumed)
    {
        return PROTO_FALSE;
    }
    size_t crlf = find_crlf(buf, len);
    if (crlf == len)
    {
        return PROTO_FALSE; // control line not fully buffered
    }
    size_t line_len = crlf;
    size_t after_line = crlf + 2;

    out->subject = out->sid = out->reply = out->arg = NULL;
    out->subject_len = out->sid_len = out->reply_len = out->arg_len = 0;
    out->payload = NULL;
    out->payload_len = 0;
    out->headers = NULL;
    out->headers_len = 0;

    if (verb_is(buf, line_len, "PING"))
    {
        out->type = NATS_PING;
        *consumed = after_line;
        return PROTO_TRUE;
    }
    if (verb_is(buf, line_len, "PONG"))
    {
        out->type = NATS_PONG;
        *consumed = after_line;
        return PROTO_TRUE;
    }
    if (verb_is(buf, line_len, "+OK"))
    {
        out->type = NATS_OK;
        *consumed = after_line;
        return PROTO_TRUE;
    }
    if (verb_is(buf, line_len, "-ERR") || verb_is(buf, line_len, "INFO"))
    {
        out->type = (buf[0] == '-') ? NATS_ERR : NATS_INFO;
        size_t a = 4; // skip the verb: both "-ERR" and "INFO" are 4 chars
        while (a < line_len && (buf[a] == ' ' || buf[a] == '\t'))
        {
            a++;
        }
        out->arg = buf + a;
        out->arg_len = line_len - a;
        *consumed = after_line;
        return PROTO_TRUE;
    }
    if (verb_is(buf, line_len, "MSG"))
    {
        // MSG <subject> <sid> [reply-to] <#bytes>
        const char *tok[4];
        size_t tlen[4];
        size_t ntok = 0;
        size_t i = 3; // past "MSG"
        while (i < line_len && ntok < 4)
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
        if (ntok != 3 && ntok != 4) // subject sid [reply] size
        {
            return PROTO_FALSE;
        }
        size_t size;
        if (!parse_uint(tok[ntok - 1], tlen[ntok - 1], &size)) // the last token is the byte count
        {
            return PROTO_FALSE;
        }
        // Bound the byte count against the remaining capacity without adding it (a 32-bit
        // size_t would wrap if we computed after_line + size + 2 first).
        if (after_line + 2 > len || size > len - after_line - 2)
        {
            return PROTO_FALSE; // payload + trailing CRLF not fully buffered
        }
        size_t total = after_line + size + 2;
        out->type = NATS_MSG;
        out->subject = tok[0];
        out->subject_len = tlen[0];
        out->sid = tok[1];
        out->sid_len = tlen[1];
        if (ntok == 4)
        {
            out->reply = tok[2];
            out->reply_len = tlen[2];
        }
        out->payload = (const uint8_t *)(buf + after_line);
        out->payload_len = size;
        *consumed = total;
        return PROTO_TRUE;
    }
    if (verb_is(buf, line_len, "HMSG"))
    {
        // HMSG <subject> <sid> [reply-to] <hdr_len> <total_len>
        const char *tok[5];
        size_t tlen[5];
        size_t ntok = 0;
        size_t i = 4; // past "HMSG"
        while (i < line_len && ntok < 5)
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
        if (ntok != 4 && ntok != 5) // subject sid [reply] hdr_len total_len
        {
            return PROTO_FALSE;
        }
        size_t hdr_len, total_size;
        if (!parse_uint(tok[ntok - 2], tlen[ntok - 2], &hdr_len) ||
            !parse_uint(tok[ntok - 1], tlen[ntok - 1], &total_size))
        {
            return PROTO_FALSE;
        }
        if (hdr_len > total_size) // the header block cannot exceed the header+payload total
        {
            return PROTO_FALSE;
        }
        // Bound the total against the remaining capacity without overflowing size_t.
        if (after_line + 2 > len || total_size > len - after_line - 2)
        {
            return PROTO_FALSE; // headers + payload + trailing CRLF not fully buffered
        }
        size_t total = after_line + total_size + 2;
        out->type = NATS_MSG;
        out->subject = tok[0];
        out->subject_len = tlen[0];
        out->sid = tok[1];
        out->sid_len = tlen[1];
        if (ntok == 5)
        {
            out->reply = tok[2];
            out->reply_len = tlen[2];
        }
        out->headers = buf + after_line;
        out->headers_len = hdr_len;
        out->payload = (const uint8_t *)(buf + after_line + hdr_len);
        out->payload_len = total_size - hdr_len;
        *consumed = total;
        return PROTO_TRUE;
    }

    out->type = NATS_UNKNOWN;
    *consumed = after_line;
    return PROTO_TRUE;
}

#endif // PC_ENABLE_NATS
