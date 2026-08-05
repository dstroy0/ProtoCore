// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_range.h
 * @brief Shared single-range `Range: bytes=...` parser (RFC 7233), used by static file serving and the
 *        edge cache (PC_ENABLE_RANGE).
 *
 * One owner for the range math, shared by the filesystem file server and the CDN edge cache. Pure
 * and size/string-driven - no PC or fs:: dependency.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HTTP_RANGE_H
#define PROTOCORE_HTTP_RANGE_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_RANGE

/**
 * @brief Parse a single-range `Range: bytes=...` header value against a resource of @p size bytes.
 *
 * Supported forms: "bytes=A-B", "bytes=A-" (A to end), "bytes=-N" (last N bytes). A start past
 * SIZE_MAX saturates (never wraps) so it resolves as past-EOF. @return:
 *   - 0  no usable Range header (caller sends a full 200) - absent, malformed, trailing garbage, or a
 *        multi-range request (RFC 7233 sec 3.1 permits ignoring it),
 *   - 1  a satisfiable range (writes the inclusive [*out_start, *out_end]),
 *   - -1 a syntactically valid but unsatisfiable range (caller sends 416).
 */
int http_parse_byte_range(const char *hdr, size_t size, size_t *out_start, size_t *out_end);

#endif // PC_ENABLE_RANGE

PROTO_END_DECLS

#endif // PROTOCORE_HTTP_RANGE_H
