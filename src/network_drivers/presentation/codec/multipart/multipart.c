// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file multipart.c
 * @brief In-place multipart/form-data parser implementation.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_MULTIPART

#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h" // str.find: a quoted parameter key, and the boundary in a Content-Type
#include "multipart.h"

// Longest parameter key the header scan will match ("name=", "filename=").
#define MULTIPART_KEY_MAX 32

// Length-bounded, binary-safe forward search for needle[0..nlen) within hay[0..hlen).
// Unlike strstr, it does not stop at a NUL, so a body containing NUL bytes scans correctly.
static char *mem_find(char *hay, size_t hlen, const char *needle, size_t nlen)
{
    if (nlen == 0 || nlen > hlen)
    {
        return NULL; // a fixed literal (2) or a length derived from an already-checked nonzero blen
    }
    for (size_t i = 0; i + nlen <= hlen; i++)
    {
        if (mem.cmp(hay + i, needle, nlen) == 0)
        {
            return hay + i;
        }
    }
    return NULL;
}

// Extract parameter value: search for `key="<value>"` inside `src`.
// If found, null-terminates in-place and returns pointer to the value.
// Returns NULL if not found.
static char *extract_quoted_param(char *src, const char *key)
{
    char *p = (char *)str.find(src, str.len(src, 0xFFFF) + 1u, key, str.len(key, MULTIPART_KEY_MAX) + 1u, PROTO_FALSE);
    if (!p)
    {
        return NULL;
    }
    p += str.len(key, MULTIPART_KEY_MAX);
    if (*p != '"')
    {
        return NULL;
    }
    p++; // skip opening quote
    char *end = (char *)str.find(p, str.len(p, 0xFFFF) + 1u, "\"", sizeof("\""), PROTO_FALSE);
    if (!end)
    {
        return NULL;
    }
    *end = '\0';
    return p;
}

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_multipart_parse(uint8_t *restrict work)
{
    (void)work;
    HttpReq *req = MultipartV.parse_args.req;
    MultipartBody *mp = MultipartV.parse_args.mp;

    mp->part_count = 0;

    HttpParserV.get_header_args.req = req;
    HttpParserV.get_header_args.key = "Content-Type";
    HttpParserV.get_header(protocore_http_parser_span());
    const char *ct = HttpParserV.text;
    if (!ct)
    {
        MultipartV.ok = PROTO_FALSE;
        return;
    }

    // Extract boundary value (may be quoted or unquoted)
    const char *bsearch = str.find(ct, MAX_VAL_LEN, "boundary=", sizeof("boundary="), PROTO_FALSE);
    if (!bsearch)
    {
        MultipartV.ok = PROTO_FALSE;
        return;
    }
    bsearch += 9;
    if (*bsearch == '"')
    {
        bsearch++;
    }

    char bval[MAX_BOUNDARY_LEN + 1];
    size_t blen = 0;
    // blen reaching MAX_BOUNDARY_LEN (72) never terminates this loop in practice: bsearch points into
    // the stored Content-Type value, which http_parser caps at MAX_VAL_LEN-1 (47) bytes total, and
    // "boundary=" alone consumes 9+ of those - so the NUL (or a '"'/';'/' ' delimiter) is always hit first.
    while (*bsearch && *bsearch != '"' && *bsearch != ';' && *bsearch != ' ' && blen < MAX_BOUNDARY_LEN)
    {
        bval[blen++] = *bsearch++;
    }
    bval[blen] = '\0';

    if (blen == 0)
    {
        MultipartV.ok = PROTO_FALSE;
        return;
    }

    // Delimiter is "--" + boundary
    char delim[MAX_BOUNDARY_LEN + 3];
    delim[0] = '-';
    delim[1] = '-';
    mem.cpy(delim + 2, bval, blen + 1); // includes null
    size_t dlen = blen + 2;

    char *body = (char *)req->body;
    char *end = body + req->body_len; // length-bounded scanning: NUL bytes in a binary part are fine

    // A part's data ends at the full "\r\n--boundary" delimiter (RFC 2046): matching only the
    // "--boundary" bytes would false-truncate a binary part that happens to contain them.
    char ddelim[MAX_BOUNDARY_LEN + 5];
    ddelim[0] = '\r';
    ddelim[1] = '\n';
    mem.cpy(ddelim + 2, delim, dlen); // "--boundary" (dlen bytes, no NUL)
    size_t ddlen = dlen + 2;

    // Find the first delimiter ("--boundary"; a leading CRLF / preamble is optional here).
    char *pos = mem_find(body, (size_t)(end - body), delim, dlen);
    if (!pos)
    {
        MultipartV.ok = PROTO_FALSE;
        return;
    }
    pos += dlen;
    if (pos + 2 <= end && pos[0] == '\r' && pos[1] == '\n')
    {
        pos += 2;
    }

    while (mp->part_count < MAX_MULTIPART_PARTS)
    {
        // "--" immediately after the delimiter marks the terminating boundary.
        if (pos + 2 <= end && pos[0] == '-' && pos[1] == '-')
        {
            break;
        }

        MultipartPart *part = &mp->parts[mp->part_count];
        part->name = NULL;
        part->filename = NULL;
        part->type = NULL;
        part->data = NULL;
        part->data_len = 0;

        // Parse the per-part headers (text) until the blank line.
        for (;;)
        {
            if (pos + 2 <= end && pos[0] == '\r' && pos[1] == '\n')
            {
                pos += 2; // blank line → start of data
                break;
            }

            char *line_end = mem_find(pos, (size_t)(end - pos), "\r\n", 2);
            if (!line_end)
            {
                MultipartV.ok = PROTO_FALSE;
                return;
            }

            *line_end = '\0'; // null-terminate header line

            const size_t hlen = (size_t)(line_end - pos);

            if (hlen >= 20 && str.starts(pos, "Content-Disposition:", 20, PROTO_TRUE))
            {
                char *v = pos + 20;
                while (*v == ' ')
                {
                    v++;
                }
                // Extract filename before name: filename= appears after name= in the
                // header, so extracting it first avoids corrupting name='s search
                // when extract_quoted_param null-terminates the value in-place.
                part->filename = extract_quoted_param(v, "filename=");
                part->name = extract_quoted_param(v, "name=");
            }
            else if (hlen >= 13 && str.starts(pos, "Content-Type:", 13, PROTO_TRUE))
            {
                char *v = pos + 13;
                while (*v == ' ')
                {
                    v++;
                }
                part->type = v;
            }

            pos = line_end + 2; // next line (skip '\0' + '\n')
        }

        // Data runs from pos until the next "\r\n--boundary" (binary-safe, length-bounded).
        char *next = mem_find(pos, (size_t)(end - pos), ddelim, ddlen);
        if (!next)
        {
            MultipartV.ok = PROTO_FALSE;
            return;
        }

        part->data = pos;
        part->data_len = (size_t)(next - pos);
        *next = '\0'; // terminate at the CRLF so a text part is still usable as a C-string

        mp->part_count++;

        pos = next + ddlen; // past "\r\n--boundary"
        if (pos + 2 <= end && pos[0] == '\r' && pos[1] == '\n')
        {
            pos += 2;
        }
    }

    MultipartV.ok = mp->part_count > 0;
}

void protocore_multipart_get_field(uint8_t *restrict work)
{
    (void)work;
    const MultipartBody *mp = MultipartV.get_field_args.mp;
    const char *field = MultipartV.get_field_args.field;

    for (int i = 0; i < mp->part_count; i++)
    {
        if (mp->parts[i].name &&
            str.eq(mp->parts[i].name, field, str.len(mp->parts[i].name, str.len(field, 0xFFFF)) + 1u, PROTO_FALSE))
        {
            MultipartV.text = mp->parts[i].data;
            return;
        }
    }
    MultipartV.text = NULL;
}
/** @brief The operands and the outcome. */
MultipartVars MultipartV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MULTIPART
