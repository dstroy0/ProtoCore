// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cbor.h
 * @brief Layer 6 (Presentation) - zero-heap CBOR (RFC 8949) encoder.
 *
 * A streaming encoder that writes directly into a caller-provided buffer (no
 * heap), the binary counterpart to the JSON writer. Emit definite-length arrays
 * and maps by writing the header (pc_cbor_array / pc_cbor_map with the item count) then
 * that many items (twice that for a map: key, value, key, value, ...).
 *
 * Overflow is tracked, not crashed on: writes past the buffer set the overflow
 * flag and stop, while pc_cbor_len() keeps counting the bytes the full payload would
 * need, so a caller can size the buffer and check pc_cbor_ok().
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CBOR_H
#define PROTOCORE_CBOR_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_NEED_CBOR

/** @brief CBOR encoder state over a caller-provided buffer. */
struct CborWriter
{
    uint8_t *buf;  ///< destination buffer.
    size_t cap;    ///< buffer capacity in bytes.
    size_t pos;    ///< bytes the payload needs so far (may exceed cap on overflow).
    bool overflow; ///< true once a write did not fit.
};

/** @brief Bind a writer to @p buf (capacity @p cap) and reset it. */
void pc_cbor_init(CborWriter *w, uint8_t *buf, size_t cap);

/** @brief Encoded length in bytes (what the full payload needs; compare to cap). */
size_t pc_cbor_len(const CborWriter *w);

/** @brief True if every value fit in the buffer. */
bool pc_cbor_ok(const CborWriter *w);

void pc_cbor_uint(CborWriter *w, uint64_t v);                       ///< unsigned integer
void pc_cbor_int(CborWriter *w, int64_t v);                         ///< signed integer
void pc_cbor_bytes(CborWriter *w, const uint8_t *data, size_t len); ///< byte string
void pc_cbor_text(CborWriter *w, const char *s);                    ///< text string (null-terminated)
void pc_cbor_text_n(CborWriter *w, const char *s, size_t len);      ///< text string (explicit length)
void pc_cbor_bool(CborWriter *w, bool b);                           ///< true / false
void pc_cbor_null(CborWriter *w);                                   ///< null
void pc_cbor_float(CborWriter *w, float f);                         ///< IEEE-754 single (major 7, 0xfa)
void pc_cbor_array(CborWriter *w, size_t count);                    ///< definite-length array header
void pc_cbor_map(CborWriter *w, size_t count);                      ///< definite-length map header

// ---------------------------------------------------------------------------
// Decoder (cursor over a CBOR byte buffer)
// ---------------------------------------------------------------------------

/** @brief Major type of the next item (returned by pc_cbor_peek). */
enum class CborType : uint8_t
{
    CBOR_TYPE_UINT = 0,
    CBOR_TYPE_INT,
    CBOR_TYPE_BYTES,
    CBOR_TYPE_TEXT,
    CBOR_TYPE_ARRAY,
    CBOR_TYPE_MAP,
    CBOR_TYPE_BOOL,
    CBOR_TYPE_NULL,
    CBOR_TYPE_FLOAT,
    CBOR_TYPE_INVALID ///< end of buffer, a prior error, or an unsupported item
};

/** @brief CBOR decoder cursor over a read-only buffer. */
struct CborReader
{
    const uint8_t *buf; ///< source bytes.
    size_t len;         ///< buffer length.
    size_t pos;         ///< current read offset.
    bool err;           ///< sticky: set on any malformed / out-of-bounds read.
};

/** @brief Bind a reader to @p buf (length @p len) at offset 0. */
void pc_cbor_reader_init(CborReader *r, const uint8_t *buf, size_t len);

/** @brief Type of the next item without consuming it. */
CborType pc_cbor_peek(CborReader *r);

/** @brief True while no malformed read has occurred. */
bool pc_cbor_reader_ok(const CborReader *r);

bool pc_cbor_read_uint(CborReader *r, uint64_t *out);                     ///< unsigned integer
bool pc_cbor_read_int(CborReader *r, int64_t *out);                       ///< signed integer (also accepts unsigned)
bool pc_cbor_read_bool(CborReader *r, bool *out);                         ///< true / false
bool pc_cbor_read_null(CborReader *r);                                    ///< null
bool pc_cbor_read_float(CborReader *r, float *out);                       ///< float32 (0xfa) or double (0xfb)
bool pc_cbor_read_text(CborReader *r, const char **out, size_t *len);     ///< text string (points into the buffer)
bool pc_cbor_read_bytes(CborReader *r, const uint8_t **out, size_t *len); ///< byte string (points into the buffer)
bool pc_cbor_read_array(CborReader *r, size_t *count);                    ///< definite-length array header
bool pc_cbor_read_map(CborReader *r, size_t *count);                      ///< definite-length map header

#endif // PC_NEED_CBOR
#endif // PROTOCORE_CBOR_H
