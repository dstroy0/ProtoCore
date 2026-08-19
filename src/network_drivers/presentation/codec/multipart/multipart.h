// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_MULTIPART

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

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

/** @brief What parse takes: req, mp. */
typedef struct
{
    HttpReq *req;
    MultipartBody *mp;
} MultipartParseArgs;

/** @brief What get_field takes: mp, field. */
typedef struct
{
    const MultipartBody *mp;
    const char *field;
} MultipartGetFieldArgs;

/**
 * @brief In-place multipart/form-data parser (RFC 7578).
 *
 * A caller sets the members a call takes, invokes it through ::Multipart with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Multipart.parse_args.req = ...;
 *   Multipart.parse_args.mp = ...;
 *   Multipart.parse(work);
 *   // Multipart.ok is what the call reports
 *
 * @var MultipartNs::parse_args  what parse takes: req, mp
 * @var MultipartNs::get_field_args  what get_field takes: mp, field
 * @var MultipartNs::ok  a call's true/false outcome
 * @var MultipartNs::text  the string a call reports
 * @var MultipartNs::parse  scan req's body as multipart/form-data, reading the boundary from
 * @var MultipartNs::get_field  the data pointer of the first part whose name matches field, or
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    MultipartParseArgs parse_args;
    MultipartGetFieldArgs get_field_args;

    proto_bool ok;
    const char *text;

    void (*const parse)(uint8_t *restrict work);
    void (*const get_field)(uint8_t *restrict work);
} MultipartNs;

/** @brief The one symbol this module exports. */
extern MultipartNs Multipart;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MULTIPART

#endif // PROTOCORE_MULTIPART_H
