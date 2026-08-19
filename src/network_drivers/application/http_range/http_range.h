// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_range.h
 * @brief Shared single-range `Range: bytes=...` parser (RFC 7233), used by static file serving and the
 *        edge cache (PROTOCORE_ENABLE_RANGE).
 *
 * One owner for the range math, shared by the filesystem file server and the CDN edge cache. Pure
 * and size/string-driven - no PC or fs:: dependency.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HTTP_RANGE_H
#define PROTOCORE_HTTP_RANGE_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_RANGE

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief What http_parse_byte_range takes: hdr, size, out_start, ... */
typedef struct
{
    const char *hdr;
    size_t size;
    size_t *out_start;
    size_t *out_end;
} HttpRangeHttpParseByteRangeArgs;

/**
 * @brief Shared single-range `Range: bytes=...` parser (RFC 7233), used by static file serving and the edge cache
 * (PROTOCORE_ENABLE_RANGE).
 *
 * A caller sets the members a call takes, invokes it through ::HttpRange with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   HttpRange.http_parse_byte_range_args.hdr = ...;
 *   HttpRange.http_parse_byte_range_args.size = ...;
 *   HttpRange.http_parse_byte_range_args.out_start = ...;
 *   HttpRange.http_parse_byte_range_args.out_end = ...;
 *   HttpRange.http_parse_byte_range(work);
 *   // HttpRange.n is what the call reports
 *
 * @var HttpRangeNs::http_parse_byte_range_args  what http_parse_byte_range takes: hdr, size, out_start,
 * @var HttpRangeNs::ok  a call's true/false outcome
 * @var HttpRangeNs::n  the count a call reports
 * @var HttpRangeNs::http_parse_byte_range  parse a single-range `Range: bytes=...` header value against a ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    HttpRangeHttpParseByteRangeArgs http_parse_byte_range_args;

    proto_bool ok;
    int n;

    void (*const http_parse_byte_range)(uint8_t *restrict work);
} HttpRangeNs;

/** @brief The one symbol this module exports. */
extern HttpRangeNs HttpRange;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_RANGE

#endif // PROTOCORE_HTTP_RANGE_H
