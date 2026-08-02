// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file stomp.c
 * @brief STOMP 1.2 frame builder + parser (pure, host-tested).
 */

#include "services/iot/stomp/stomp.h"

#if PC_ENABLE_STOMP

#include <string.h>

// Append one octet's escaped form to buf[pos..cap); advance pos. Returns false on overflow.
static proto_bool emit_escaped(char *buf, size_t cap, size_t *pos, char c)
{
    char e0 = '\\';
    char e1;
    switch (c)
    {
    case '\r':
        e1 = 'r';
        break;
    case '\n':
        e1 = 'n';
        break;
    case ':':
        e1 = 'c';
        break;
    case '\\':
        e1 = '\\';
        break;
    default: // not special: one raw octet
        if (*pos + 1 > cap)
        {
            return PROTO_FALSE;
        }
        buf[(*pos)++] = c;
        return PROTO_TRUE;
    }
    if (*pos + 2 > cap)
    {
        return PROTO_FALSE;
    }
    buf[(*pos)++] = e0;
    buf[(*pos)++] = e1;
    return PROTO_TRUE;
}

// Append the escaped form of a NUL-terminated string. Returns false on overflow.
static proto_bool emit_escaped_str(char *buf, size_t cap, size_t *pos, const char *s)
{
    for (; *s; s++)
    {
        if (!emit_escaped(buf, cap, pos, *s))
        {
            return PROTO_FALSE;
        }
    }
    return PROTO_TRUE;
}

size_t pc_stomp_build_frame(char *buf, size_t cap, const char *command, const char *const *header_keys,
                            const char *const *header_vals, size_t nheaders, const char *body, size_t body_len)
{
    if (!buf || cap == 0 || !command || (nheaders && (!header_keys || !header_vals)))
    {
        return 0;
    }

    size_t pos = 0;

    // Command line (a command verb has no special octets, but escape defensively).
    if (!emit_escaped_str(buf, cap, &pos, command))
    {
        return 0;
    }
    if (pos + 1 > cap)
    {
        return 0;
    }
    buf[pos++] = '\n';

    // Header lines: key:value\n, both escaped.
    for (size_t i = 0; i < nheaders; i++)
    {
        if (!header_keys[i] || !header_vals[i])
        {
            return 0;
        }
        if (!emit_escaped_str(buf, cap, &pos, header_keys[i]))
        {
            return 0;
        }
        if (pos + 1 > cap)
        {
            return 0;
        }
        buf[pos++] = ':';
        if (!emit_escaped_str(buf, cap, &pos, header_vals[i]))
        {
            return 0;
        }
        if (pos + 1 > cap)
        {
            return 0;
        }
        buf[pos++] = '\n';
    }

    // Blank line, raw body, terminating NUL.
    if (pos + 1 > cap)
    {
        return 0;
    }
    buf[pos++] = '\n';
    if (body_len)
    {
        if (!body || pos + body_len > cap)
        {
            return 0;
        }
        memcpy(buf + pos, body, body_len);
        pos += body_len;
    }
    if (pos + 1 > cap)
    {
        return 0;
    }
    buf[pos++] = '\0';
    return pos;
}

// Parse an unsigned base-10 length from [s, s+len). Returns false on empty / non-digit /
// overflow (a content-length larger than size_t can never be satisfied by the buffer).
static proto_bool parse_len(const char *s, size_t len, size_t *out)
{
    if (len == 0)
    {
        return PROTO_FALSE;
    }
    size_t v = 0;
    for (size_t i = 0; i < len; i++)
    {
        if (s[i] < '0' || s[i] > '9')
        {
            return PROTO_FALSE;
        }
        if (v > (SIZE_MAX - 9) / 10) // would overflow on the next digit
        {
            return PROTO_FALSE;
        }
        v = v * 10 + (size_t)(s[i] - '0');
    }
    *out = v;
    return PROTO_TRUE;
}

// Length of a header line ending at the '\n' at index nl, trimming a trailing '\r'.
static size_t line_len(const char *buf, size_t start, size_t nl)
{
    size_t end = nl;
    if (end > start && buf[end - 1] == '\r')
    {
        end--;
    }
    return end - start;
}

proto_bool pc_stomp_parse_frame(const char *buf, size_t len, StompFrame *out, size_t *consumed)
{
    if (!buf || !out || !consumed)
    {
        return PROTO_FALSE;
    }

    // Skip leading EOL octets (heart-beats / inter-frame newlines).
    size_t i = 0;
    while (i < len && (buf[i] == '\r' || buf[i] == '\n'))
    {
        i++;
    }
    if (i >= len)
    {
        return PROTO_FALSE; // nothing but newlines so far
    }

    out->command = NULL;
    out->command_len = 0;
    out->header_count = 0;
    out->body = NULL;
    out->body_len = 0;

    // Command line.
    size_t nl = i;
    while (nl < len && buf[nl] != '\n')
    {
        nl++;
    }
    if (nl >= len)
    {
        return PROTO_FALSE; // command line incomplete
    }
    out->command = buf + i;
    out->command_len = line_len(buf, i, nl);
    if (out->command_len == 0) // GCOVR_EXCL_BR_LINE  the true arm is unreachable: the skip loop above
                               // guarantees buf[i] is neither '\r' nor '\n', so the search for nl (the
                               // next '\n' at or after i) always advances past i, giving nl>=i+1. If
                               // line_len trims a trailing '\r' the trimmed length is only 0 when that
                               // '\r' is buf[i] itself (nl==i+1), which cannot happen since buf[i]!='\r'
                               // is already established => command_len>=1 in every case
    {
        return PROTO_FALSE; // GCOVR_EXCL_LINE  unreachable for the same reason as the branch above
    }
    size_t cur = nl + 1;

    // Header lines until a blank line.
    size_t content_length = 0;
    proto_bool have_content_length = PROTO_FALSE;
    while (cur < len)
    {
        nl = cur;
        while (nl < len && buf[nl] != '\n')
        {
            nl++;
        }
        if (nl >= len)
        {
            return PROTO_FALSE; // header line incomplete
        }
        size_t ll = line_len(buf, cur, nl);
        if (ll == 0)
        {
            cur = nl + 1; // blank line: body starts here
            break;
        }
        // Split at the first ':'.
        size_t colon = cur;
        size_t line_end = cur + ll;
        while (colon < line_end && buf[colon] != ':')
        {
            colon++;
        }
        if (colon >= line_end)
        {
            return PROTO_FALSE; // header without a colon
        }
        if (out->header_count < PC_STOMP_MAX_HEADERS)
        {
            StompHeader *h = &out->headers[out->header_count++];
            h->key = buf + cur;
            h->key_len = colon - cur;
            h->val = buf + colon + 1;
            h->val_len = line_end - (colon + 1);
            // content-length drives the body length (only the first occurrence is used). A
            // present-but-unparseable / overflowing value is a malformed frame, not a fall-back
            // to NUL-delimited parsing.
            if (!have_content_length && h->key_len == 14 && memcmp(h->key, "content-length", 14) == 0)
            {
                if (!parse_len(h->val, h->val_len, &content_length))
                {
                    return PROTO_FALSE;
                }
                have_content_length = PROTO_TRUE;
            }
        }
        cur = nl + 1;
        if (cur > len) // GCOVR_EXCL_BR_LINE  the true arm is unreachable: the nl>=len check above guarantees
                       // nl<len, so cur=nl+1<=len
        {
            return PROTO_FALSE; // GCOVR_EXCL_LINE  unreachable for the same reason as the branch above
        }
    }

    // Body.
    if (have_content_length)
    {
        if (cur + content_length >= len) // need body + the terminating NUL
        {
            return PROTO_FALSE;
        }
        if (buf[cur + content_length] != '\0')
        {
            return PROTO_FALSE; // declared length does not land on the NUL terminator
        }
        out->body = buf + cur;
        out->body_len = content_length;
        *consumed = cur + content_length + 1;
        return PROTO_TRUE;
    }
    // No content-length: body runs to the first NUL.
    size_t b = cur;
    while (b < len && buf[b] != '\0')
    {
        b++;
    }
    if (b >= len)
    {
        return PROTO_FALSE; // NUL terminator not yet buffered
    }
    out->body = buf + cur;
    out->body_len = b - cur;
    *consumed = b + 1;
    return PROTO_TRUE;
}

proto_bool pc_stomp_header(const StompFrame *f, const char *name, const char **val, size_t *val_len)
{
    if (!f || !name)
    {
        return PROTO_FALSE;
    }
#define PC_name_max 128 // STOMP header names are short; bound the needle defensively
    size_t nlen = strnlen(name, PC_name_max);
    for (size_t i = 0; i < f->header_count; i++)
    {
        if (f->headers[i].key_len == nlen && memcmp(f->headers[i].key, name, nlen) == 0)
        {
            if (val)
            {
                *val = f->headers[i].val;
            }
            if (val_len)
            {
                *val_len = f->headers[i].val_len;
            }
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

size_t pc_stomp_unescape(char *dst, size_t cap, const char *src, size_t src_len)
{
    if (!dst || !src)
    {
        return 0;
    }
    size_t pos = 0;
    for (size_t i = 0; i < src_len; i++)
    {
        char c = src[i];
        if (c == '\\')
        {
            if (i + 1 >= src_len)
            {
                return 0; // dangling escape
            }
            char n = src[++i];
            switch (n)
            {
            case 'r':
                c = '\r';
                break;
            case 'n':
                c = '\n';
                break;
            case 'c':
                c = ':';
                break;
            case '\\':
                c = '\\';
                break;
            default:
                return 0; // invalid escape sequence
            }
        }
        if (pos + 1 > cap)
        {
            return 0; // overflow
        }
        dst[pos++] = c;
    }
    return pos;
}

#endif // PC_ENABLE_STOMP
