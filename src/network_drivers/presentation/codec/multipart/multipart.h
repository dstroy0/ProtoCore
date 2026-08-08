// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

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
 *   other parameters are ignored.
 * - Boundary value must be ≤ `MAX_BOUNDARY_LEN` bytes (RFC 2046 cap: 70).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MULTIPART_H
#define PROTOCORE_MULTIPART_H

#include "network_drivers/presentation/presentation.h" // for HttpReq, http_get_header
#include "protocore_config.h"

PROTO_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Data types
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
 * @brief The in-place form-data parser.
 *
 * @var MultipartNs::parse      scan @p req's body as multipart/form-data, reading the boundary from
 *                              Content-Type and null-terminating each part's headers and data in
 *                              place. True when at least one part was found. @p req must be
 *                              PARSE_COMPLETE
 * @var MultipartNs::get_field  the data pointer of the first part whose name matches @p field, or
 *                              NULL. The simple-form case, where each field name appears once
 */
typedef struct
{
    proto_bool (*parse)(HttpReq *req, MultipartBody *mp);
    const char *(*get_field)(const MultipartBody *mp, const char *field);
} MultipartNs;

/** @brief The one symbol this module exports. */
extern const MultipartNs Multipart;

PROTO_END_DECLS

#endif
