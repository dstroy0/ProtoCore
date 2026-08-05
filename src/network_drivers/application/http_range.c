// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_range.c
 * @brief Shared single-range `Range: bytes=...` parser. See http_range.h.
 */

#include "network_drivers/application/http_range.h"

#if PC_ENABLE_RANGE

// strncasecmp, strchr

int http_parse_byte_range(const char *hdr, size_t size, size_t *out_start, size_t *out_end)
{
    if (!hdr)
    {
        return 0;
    }
    // Require the "bytes=" unit (case-insensitive).
    if (strncasecmp(hdr, "bytes=", 6) != 0)
    {
        return 0;
    }
    const char *p = hdr + 6;
    while (*p == ' ')
    {
        p++;
    }
    if (strchr(p, ',')) // multi-range not supported -> fall back to full 200
    {
        return 0;
    }

    proto_bool have_start = PROTO_FALSE;
    proto_bool have_end = PROTO_FALSE;
    size_t start = 0;
    size_t end = 0;
    const size_t SZMAX = (size_t)-1;
    if (*p >= '0' && *p <= '9')
    {
        have_start = PROTO_TRUE;
        while (*p >= '0' && *p <= '9')
        {
            size_t d = (size_t)(*p++ - '0');
            // Saturate on overflow: a start past SIZE_MAX is past EOF -> 416, never wraps.
            start = (start > (SZMAX - d) / 10) ? SZMAX : start * 10 + d;
        }
    }
    if (*p != '-')
    {
        return 0; // malformed
    }
    p++;
    if (*p >= '0' && *p <= '9')
    {
        have_end = PROTO_TRUE;
        end = 0;
        while (*p >= '0' && *p <= '9')
        {
            size_t d = (size_t)(*p++ - '0');
            end = (end > (SZMAX - d) / 10) ? SZMAX : end * 10 + d; // saturate -> clamps to last byte
        }
    }
    while (*p == ' ')
    {
        p++;
    }
    if (*p != '\0')
    {
        return 0; // trailing garbage -> ignore the header
    }

    if (!have_start)
    {
        // Suffix form "bytes=-N": the last N bytes.
        if (!have_end || end == 0)
        {
            return -1; // "-" alone, or "-0" -> unsatisfiable
        }
        if (size == 0)
        {
            return -1;
        }
        start = (end >= size) ? 0 : (size - end);
        end = size - 1;
    }
    else
    {
        if (start >= size)
        {
            return -1; // start past EOF -> unsatisfiable
        }
        if (!have_end || end >= size)
        {
            end = size - 1; // open-ended or clamped to last byte
        }
        if (start > end)
        {
            return -1;
        }
    }
    *out_start = start;
    *out_end = end;
    return 1;
}

#endif // PC_ENABLE_RANGE
