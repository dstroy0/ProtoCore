// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_MULTIPART_H
#define PROTOCORE_MULTIPART_H

#include "network_drivers/presentation/http/http_parser/http_parser.h" // the complete type a public struct below holds by value
#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file multipart.h
 * @brief In-place multipart/form-data parser (RFC 7578).
 *
 * Parses the body already stored in `HttpReq::body[]`.  The parser modifies
 * the body buffer in-place by inserting null terminators, so `part->data`
 * pointers are valid only while the `HttpReq` lives (before `http_reset()`).
 *
 * The scan is length-bounded over `HttpReq::body_len` and matches the full
 * `\r\n--boundary` delimiter (RFC 2046), so a **binary** part is safe: embedded
 * NUL bytes and even the raw boundary string inside the payload do not truncate it
 * (only the true `CRLF--boundary` delimiter ends a part). Read a binary part via
 * `part->data` + `part->data_len` (the in-place NUL terminator is a convenience for
 * text parts, not a length).
 *
 * **Limitations**
 * - Maximum parts: `MAX_MULTIPART_PARTS` (default 4).
 * - Maximum total body size: `BODY_BUF_SIZE` bytes.
 * - Only `name` and `filename` are extracted from Content-Disposition;
 * other parameters are ignored.
 * - Boundary value must be ≤ `MAX_BOUNDARY_LEN` bytes (RFC 2046 cap: 70).
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_MULTIPART_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/**
 * @brief One parsed part from a multipart body.
 *
 * All char* fields point into the (modified) `HttpReq::body[]` buffer.
 * They are null-terminated and valid until `http_reset()` is called.
 */
typedef struct
{
    const char *name;     ///< Form field name from Content-Disposition, or nullptr.
    const char *filename; ///< Upload filename from Content-Disposition, or nullptr.
    const char *type;     ///< Content-Type of this part, or nullptr.
    const char *data;     ///< Part body (null-terminated in-place).
    size_t data_len;      ///< Part body length in bytes (not counting the null).
} MultipartPart;

/**
 * @brief Container for all parsed parts of a multipart body.
 */
typedef struct
{
    MultipartPart parts[MAX_MULTIPART_PARTS]; ///< Parsed parts.
    int part_count;                           ///< Number of valid entries in parts[].
} MultipartBody;

#include "network_drivers/presentation/http/http_parser/http_parser.h" // HttpReq: the type a parameter points at

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*parse)(uint8_t *restrict, HttpReq *, MultipartBody *);
    const char *(*get_field)(uint8_t *restrict, const MultipartBody *, const char *);
} MultipartNs;
PROTOCORE_NS_LAYOUT(MultipartNs, parse, get_field);

/**
 * @brief Scan req's body as multipart/form-data, reading the boundary from.
 * @param work PROTOCORE_MULTIPART_BORROW bytes the caller took. Not held past the call.
 * @param req Req
 * @param mp Mp
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_multipart_parse(uint8_t *restrict work, HttpReq *req, MultipartBody *mp);
/**
 * @brief The data pointer of the first part whose name matches field, or.
 * @param work PROTOCORE_MULTIPART_BORROW bytes the caller took. Not held past the call.
 * @param mp Mp
 * @param field Field
 * @return The const char *.
 */
const char *protocore_multipart_get_field(uint8_t *restrict work, const MultipartBody *mp, const char *field);

/** @brief Module namespace. */
PROTOCORE_NS MultipartNs Multipart PROTOCORE_UNUSED = {.parse = protocore_multipart_parse,
                                                       .get_field = protocore_multipart_get_field};

PROTOCORE_END_DECLS

#endif // PROTOCORE_MULTIPART_H
