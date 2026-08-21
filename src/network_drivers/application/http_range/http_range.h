// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_HTTP_RANGE_H
#define PROTOCORE_HTTP_RANGE_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file http_range.h
 * @brief Shared single-range `Range: bytes=...` parser (RFC 7233), used by static file serving and the
edge cache (PROTOCORE_ENABLE_RANGE).
 *
 * One owner for the range math, shared by the filesystem file server and the CDN edge cache. Pure
 * and size/string-driven - no PC or fs:: dependency.
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_HTTP_RANGE_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    int (*http_parse_byte_range)(uint8_t *, const char *, size_t, size_t *, size_t *);
} HttpRangeNs;
PROTOCORE_NS_LAYOUT(HttpRangeNs, http_parse_byte_range);

/**
 * @brief Parse a single-range `Range: bytes=...` header value against a .
 * @param work PROTOCORE_HTTP_RANGE_BORROW bytes the caller took. Not held past the call.
 * @param hdr Hdr
 * @param size Size
 * @param out_start Out start
 * @param out_end Out end
 * @return The int.
 */
int protocore_http_range_http_parse_byte_range(uint8_t *work, const char *hdr, size_t size, size_t *out_start,
                                               size_t *out_end);

/** @brief Module namespace. */
PROTOCORE_NS HttpRangeNs HttpRange PROTOCORE_UNUSED = {.http_parse_byte_range =
                                                           protocore_http_range_http_parse_byte_range};

PROTOCORE_END_DECLS

#endif // PROTOCORE_HTTP_RANGE_H
