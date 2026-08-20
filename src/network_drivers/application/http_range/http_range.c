// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_range.c
 * @brief Shared single-range `Range: bytes=...` parser. See http_range.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_RANGE

#include "mmgr/protostr/protostr.h" // str.starts / str.find: the unit prefix and the multi-range comma
#include "network_drivers/application/http_range/http_range.h"

PROTOCORE_BEGIN_DECLS

// strncasecmp, strchr

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void http_range_http_parse_byte_range(uint8_t *restrict work)
{
    (void)work;
    const char *hdr = HttpRange.http_parse_byte_range_args.hdr;
    size_t size = HttpRange.http_parse_byte_range_args.size;
    size_t *out_start = HttpRange.http_parse_byte_range_args.out_start;
    size_t *out_end = HttpRange.http_parse_byte_range_args.out_end;

    if (!hdr)
    {
        HttpRange.n = 0;
        return;
    }
    // Require the "bytes=" unit (case-insensitive).
    if (!str.starts(hdr, "bytes=", 6, PROTO_TRUE))
    {
        HttpRange.n = 0;
        return;
    }
    const char *p = hdr + 6;
    while (*p == ' ')
    {
        p++;
    }
    if (str.find(p, MAX_VAL_LEN, ",", sizeof(","), PROTO_FALSE)) // multi-range not supported -> fall back to full 200
    {
        HttpRange.n = 0;
        return;
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
        HttpRange.n = 0; // malformed
        return;
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
        HttpRange.n = 0; // trailing garbage -> ignore the header
        return;
    }

    if (!have_start)
    {
        // Suffix form "bytes=-N": the last N bytes.
        if (!have_end || end == 0)
        {
            HttpRange.n = -1; // "-" alone, or "-0" -> unsatisfiable
            return;
        }
        if (size == 0)
        {
            // RFC 9110 sec 14.1.2: "When a selected representation has zero length, the only
            // satisfiable form of range-spec in a GET request is a suffix-range with a non-zero
            // suffix-length." So this is satisfiable and 416 would be wrong. It covers zero bytes,
            // which an inclusive [start, end] cannot express, so the caller is told there is no
            // usable range and serves the whole (empty) representation with 200 - which is what
            // sec 14.2 permits a server to do with any Range it does not act on.
            HttpRange.n = 0;
            return;
        }
        start = (end >= size) ? 0 : (size - end);
        end = size - 1;
    }
    else
    {
        if (start >= size)
        {
            HttpRange.n = -1; // start past EOF -> unsatisfiable
            return;
        }
        if (!have_end || end >= size)
        {
            end = size - 1; // open-ended or clamped to last byte
        }
        if (start > end)
        {
            HttpRange.n = -1;
            return;
        }
    }
    *out_start = start;
    *out_end = end;
    HttpRange.n = 1;
}

HttpRangeNs HttpRange = {
    .http_parse_byte_range = http_range_http_parse_byte_range,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_RANGE
