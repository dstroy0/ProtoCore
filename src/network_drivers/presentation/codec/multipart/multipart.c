// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file multipart.c
 * @brief In-place multipart/form-data parser implementation.
 */

#include "multipart.h"
#include <string.h>

// Length-bounded, binary-safe forward search for needle[0..nlen) within hay[0..hlen).
// Unlike strstr, it does not stop at a NUL, so a body containing NUL bytes scans correctly.
static char *mem_find(char *hay, size_t hlen, const char *needle, size_t nlen)
{
    if (nlen == 0 || nlen > hlen) // GCOVR_EXCL_BR_LINE  nlen==0 never true: every caller in this file passes
    {
        return NULL; // a fixed literal (2) or a length derived from an already-checked nonzero blen
    }
    for (size_t i = 0; i + nlen <= hlen; i++)
    {
        if (memcmp(hay + i, needle, nlen) == 0)
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
    char *p = strstr(src, key);
    if (!p)
    {
        return NULL;
    }
#define PC_key_max 32 // param keys are short literals ("name=", "filename=")
    p += strnlen(key, PC_key_max);
    if (*p != '"')
    {
        return NULL;
    }
    p++; // skip opening quote
    char *end = strchr(p, '"');
    if (!end)
    {
        return NULL;
    }
    *end = '\0';
    return p;
}

proto_bool pc_multipart_parse(HttpReq *req, Multipart *mp)
{
    mp->part_count = 0;

    const char *ct = http_get_header(req, "Content-Type");
    if (!ct)
    {
        return PROTO_FALSE;
    }

    // Extract boundary value (may be quoted or unquoted)
    const char *bsearch = strstr(ct, "boundary=");
    if (!bsearch)
    {
        return PROTO_FALSE;
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
    while (*bsearch && *bsearch != '"' && *bsearch != ';' && *bsearch != ' ' && // GCOVR_EXCL_BR_LINE
           blen < MAX_BOUNDARY_LEN)
    {
        bval[blen++] = *bsearch++;
    }
    bval[blen] = '\0';

    if (blen == 0)
    {
        return PROTO_FALSE;
    }

    // Delimiter is "--" + boundary
    char delim[MAX_BOUNDARY_LEN + 3];
    delim[0] = '-';
    delim[1] = '-';
    memcpy(delim + 2, bval, blen + 1); // includes null
    size_t dlen = blen + 2;

    char *body = (char *)req->body;
    char *end = body + req->body_len; // length-bounded scanning: NUL bytes in a binary part are fine

    // A part's data ends at the full "\r\n--boundary" delimiter (RFC 2046): matching only the
    // "--boundary" bytes would false-truncate a binary part that happens to contain them.
    char ddelim[MAX_BOUNDARY_LEN + 5];
    ddelim[0] = '\r';
    ddelim[1] = '\n';
    memcpy(ddelim + 2, delim, dlen); // "--boundary" (dlen bytes, no NUL)
    size_t ddlen = dlen + 2;

    // Find the first delimiter ("--boundary"; a leading CRLF / preamble is optional here).
    char *pos = mem_find(body, (size_t)(end - body), delim, dlen);
    if (!pos)
    {
        return PROTO_FALSE;
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
                return PROTO_FALSE;
            }

            *line_end = '\0'; // null-terminate header line

            if (strncasecmp(pos, "Content-Disposition:", 20) == 0)
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
            else if (strncasecmp(pos, "Content-Type:", 13) == 0)
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
            return PROTO_FALSE;
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

    return mp->part_count > 0;
}

const char *pc_multipart_get_field(const Multipart *mp, const char *field)
{
    for (int i = 0; i < mp->part_count; i++)
    {
        if (mp->parts[i].name && strcmp(mp->parts[i].name, field) == 0)
        {
            return mp->parts[i].data;
        }
    }
    return NULL;
}
