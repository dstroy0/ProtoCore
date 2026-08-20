// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file stomp.c
 * @brief The STOMP 1.2 frame codec: the sec 9 grammar over caller buffers. See stomp.h.
 *
 * STOMP 1.2 is a community specification published at stomp.github.io, not an IETF document.
 *
 * build() writes `command EOL *( header EOL ) EOL *OCTET NULL` (sec 9), escaping every
 * header-name and header-value per sec 4.1. parse() walks the same grammar in place and slices the
 * command, the header entries and the body out of the caller's buffer. Pure: nothing is held
 * between calls, so there is no storage member.
 */

#include "services/iot/stomp/stomp.h"

#if PROTOCORE_ENABLE_STOMP

#include "mmgr/protomem/protomem.h" // mem.cpy / mem.cmp: the spans a frame is assembled from and matched over
#include "mmgr/protostr/protostr.h" // str.len: the bounded length of a header-name needle

/** @brief Octets a lookup reads from its header-name needle. Sec 4.5 leaves every size limit open. */
#define PROTOCORE_STOMP_HEADER_NAME_MAX 128

/** @brief The header-name whose value is the body octet count (sec 4.3.1). */
#define PROTOCORE_STOMP_CONTENT_LENGTH "content-length"

/** @brief Its octet count. */
#define PROTOCORE_STOMP_CONTENT_LENGTH_LEN 14

// Append one octet's sec 4.1 form to buf[pos..cap) and advance pos. False on overflow.
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
    default: // one raw octet
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

// Append the sec 4.1 form of a NUL-terminated string. False on overflow.
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

// Take an unsigned base-10 count from [s, s+len). False on empty, on a non-digit, or when the next
// digit would carry the accumulator past SIZE_MAX.
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
        if (v > (SIZE_MAX - 9) / 10)
        {
            return PROTO_FALSE;
        }
        v = v * 10 + (size_t)(s[i] - '0');
    }
    *out = v;
    return PROTO_TRUE;
}

// Octets of the line starting at start and ending at the LF at index nl, a trailing CR trimmed
// (sec 9: EOL = [CR] LF).
static size_t line_len(const char *buf, size_t start, size_t nl)
{
    size_t end = nl;
    if (end > start && buf[end - 1] == '\r')
    {
        end--;
    }
    return end - start;
}

// Write one frame (sec 9) into ns->buf.out and report its octet count in ns->n.
void protocore_stomp_build(uint8_t *restrict work)
{
    (void)work;
    char *out = StompV.buf.out;
    const size_t cap = StompV.buf.cap;
    const StompBuildArgs *a = &StompV.build_args;

    StompV.ok = PROTO_FALSE;
    StompV.n = 0;
    if (!out || cap == 0 || !a->command || (a->header_count && (!a->header_names || !a->header_values)))
    {
        return;
    }

    size_t pos = 0;

    // command EOL, the EOL a bare LF.
    if (!emit_escaped_str(out, cap, &pos, a->command))
    {
        return;
    }
    if (pos + 1 > cap)
    {
        return;
    }
    out[pos++] = '\n';

    // *( header EOL ): header-name ":" header-value, both escaped (sec 4.1).
    for (size_t i = 0; i < a->header_count; i++)
    {
        if (!a->header_names[i] || !a->header_values[i])
        {
            return;
        }
        if (!emit_escaped_str(out, cap, &pos, a->header_names[i]))
        {
            return;
        }
        if (pos + 1 > cap)
        {
            return;
        }
        out[pos++] = ':';
        if (!emit_escaped_str(out, cap, &pos, a->header_values[i]))
        {
            return;
        }
        if (pos + 1 > cap)
        {
            return;
        }
        out[pos++] = '\n';
    }

    // EOL *OCTET NULL: the blank line, the raw body, the terminating NULL.
    if (pos + 1 > cap)
    {
        return;
    }
    out[pos++] = '\n';
    if (a->body_len)
    {
        // pos <= cap here, so cap - pos is the room left and never wraps.
        if (!a->body || a->body_len > cap - pos)
        {
            return;
        }
        mem.cpy(out + pos, a->body, a->body_len);
        pos += a->body_len;
    }
    if (pos + 1 > cap)
    {
        return;
    }
    out[pos++] = '\0';
    StompV.n = pos;
    StompV.ok = PROTO_TRUE;
}

// Take one frame from the head of ns->buf.in into *ns->frame (sec 9), reporting the octets it
// occupied in ns->consumed.
void protocore_stomp_parse(uint8_t *restrict work)
{
    (void)work;
    const char *buf = StompV.buf.in;
    const size_t len = StompV.buf.len;
    StompFrame *f = StompV.frame;

    StompV.ok = PROTO_FALSE;
    StompV.consumed = 0;
    if (!buf || !f)
    {
        return;
    }

    // Step over the EOL a heart-beat sends (sec 5.4) and the *( EOL ) a frame trails (sec 9).
    size_t i = 0;
    while (i < len && (buf[i] == '\r' || buf[i] == '\n'))
    {
        i++;
    }
    if (i >= len)
    {
        return;
    }

    f->command = NULL;
    f->command_len = 0;
    f->header_count = 0;
    f->body = NULL;
    f->body_len = 0;

    // command EOL.
    size_t nl = i;
    while (nl < len && buf[nl] != '\n')
    {
        nl++;
    }
    if (nl >= len)
    {
        return; // command line incomplete
    }
    f->command = buf + i;
    f->command_len = line_len(buf, i, nl);
    if (f->command_len == 0)
    {
        return;
    }
    size_t cur = nl + 1;

    // *( header EOL ) up to the blank line that ends them.
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
            return; // header entry incomplete
        }
        size_t ll = line_len(buf, cur, nl);
        if (ll == 0)
        {
            cur = nl + 1; // the blank line: the body starts here
            break;
        }
        // header = header-name ":" header-value, split at the first colon (sec 9).
        size_t colon = cur;
        size_t line_end = cur + ll;
        while (colon < line_end && buf[colon] != ':')
        {
            colon++;
        }
        if (colon >= line_end)
        {
            return; // header entry with no colon
        }
        if (f->header_count < PROTOCORE_STOMP_MAX_HEADERS)
        {
            StompHeader *h = &f->headers[f->header_count++];
            h->name = buf + cur;
            h->name_len = colon - cur;
            h->value = buf + colon + 1;
            h->value_len = line_end - (colon + 1);
            // The first content-length entry sizes the body (sec 4.3.1, sec 4.4). A value that is
            // not a count of digits, or one wider than size_t, fails the frame.
            if (!have_content_length && h->name_len == PROTOCORE_STOMP_CONTENT_LENGTH_LEN &&
                mem.cmp(h->name, PROTOCORE_STOMP_CONTENT_LENGTH, PROTOCORE_STOMP_CONTENT_LENGTH_LEN) == 0)
            {
                if (!parse_len(h->value, h->value_len, &content_length))
                {
                    return;
                }
                have_content_length = PROTO_TRUE;
            }
        }
        cur = nl + 1;
    }

    // *OCTET NULL.
    if (have_content_length)
    {
        // cur <= len here, so len - cur is the room left and never wraps. Sec 4.3.1 reads exactly
        // content_length octets, then the NULL that ends the frame.
        if (content_length >= len - cur)
        {
            return;
        }
        if (buf[cur + content_length] != '\0')
        {
            return; // the stated count does not land on the NULL
        }
        f->body = buf + cur;
        f->body_len = content_length;
        StompV.consumed = cur + content_length + 1;
        StompV.ok = PROTO_TRUE;
        return;
    }
    // No content-length: the body runs to the first NULL.
    size_t b = cur;
    while (b < len && buf[b] != '\0')
    {
        b++;
    }
    if (b >= len)
    {
        return; // the NULL is not buffered yet
    }
    f->body = buf + cur;
    f->body_len = b - cur;
    StompV.consumed = b + 1;
    StompV.ok = PROTO_TRUE;
}

// Find ns->lookup.name among the frame's header entries and report its raw header-value. The walk
// runs in wire order and stops at the first match (sec 4.4).
void protocore_stomp_header(uint8_t *restrict work)
{
    (void)work;
    const StompFrame *f = StompV.frame;
    const char *name = StompV.lookup.name;

    StompV.ok = PROTO_FALSE;
    StompV.value = NULL;
    StompV.value_len = 0;
    if (!f || !name)
    {
        return;
    }
    const size_t nlen = str.len(name, PROTOCORE_STOMP_HEADER_NAME_MAX);
    for (size_t i = 0; i < f->header_count; i++)
    {
        if (f->headers[i].name_len == nlen && mem.cmp(f->headers[i].name, name, nlen) == 0)
        {
            StompV.value = f->headers[i].value;
            StompV.value_len = f->headers[i].value_len;
            StompV.ok = PROTO_TRUE;
            return;
        }
    }
}

// Decode the sec 4.1 escapes in ns->buf.in into ns->buf.out and report the decoded octet count in
// ns->n.
void protocore_stomp_unescape(uint8_t *restrict work)
{
    (void)work;
    char *dst = StompV.buf.out;
    const size_t cap = StompV.buf.cap;
    const char *src = StompV.buf.in;
    const size_t src_len = StompV.buf.len;

    StompV.ok = PROTO_FALSE;
    StompV.n = 0;
    if (!dst || !src)
    {
        return;
    }
    size_t pos = 0;
    for (size_t i = 0; i < src_len; i++)
    {
        char c = src[i];
        if (c == '\\')
        {
            if (i + 1 >= src_len)
            {
                return; // an escape with no second octet
            }
            char e = src[++i];
            switch (e)
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
                return; // sec 4.1: an undefined escape sequence is a fatal protocol error
            }
        }
        if (pos + 1 > cap)
        {
            return;
        }
        dst[pos++] = c;
    }
    StompV.n = pos;
    StompV.ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
StompVars StompV;

#endif // PROTOCORE_ENABLE_STOMP
