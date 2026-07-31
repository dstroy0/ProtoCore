// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file msgpack.h
 * @brief Layer 6 (Presentation) - zero-heap MessagePack encoder and decoder.
 *
 * A streaming encoder that writes directly into a caller-provided buffer (no
 * heap), the MessagePack-format sibling of the CBOR / JSON writers. Each value is
 * emitted in the shortest MessagePack form (fixint / fixstr / fixarray / fixmap
 * where possible). Emit definite-length arrays and maps by writing the header
 * (pc_msgpack_array / pc_msgpack_map with the item count) then that many items (twice
 * that for a map: key, value, key, value, ...).
 *
 * Overflow is tracked, not crashed on: writes past the buffer set the overflow
 * flag and stop, while pc_msgpack_len() keeps counting the bytes the full payload
 * would need, so a caller can size the buffer and check pc_msgpack_ok().
 *
 * The decoder is a cursor: pc_msgpack_peek() reports the next object's type and the
 * pc_msgpack_read_* calls consume it (strings and binary point into the source buffer,
 * no copy). Any malformed or out-of-bounds read sets a sticky error - check
 * pc_msgpack_reader_ok(). ext and the unused 0xc1 byte are reported as INVALID.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MSGPACK_H
#define PROTOCORE_MSGPACK_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_MSGPACK

/** @brief MessagePack encoder state over a caller-provided buffer. */
struct MsgpackWriter
{
    uint8_t *buf;  ///< destination buffer.
    size_t cap;    ///< buffer capacity in bytes.
    size_t pos;    ///< bytes the payload needs so far (may exceed cap on overflow).
    bool overflow; ///< true once a write did not fit.
};

/** @brief Bind a writer to @p buf (capacity @p cap) and reset it. */
void pc_msgpack_init(MsgpackWriter *w, uint8_t *buf, size_t cap);

/** @brief Encoded length in bytes (what the full payload needs; compare to cap). */
size_t pc_msgpack_len(const MsgpackWriter *w);

/** @brief True if every value fit in the buffer. */
bool pc_msgpack_ok(const MsgpackWriter *w);

void pc_msgpack_uint(MsgpackWriter *w, uint64_t v);                       ///< unsigned integer
void pc_msgpack_int(MsgpackWriter *w, int64_t v);                         ///< signed integer
void pc_msgpack_bytes(MsgpackWriter *w, const uint8_t *data, size_t len); ///< binary (bin family)
void pc_msgpack_str(MsgpackWriter *w, const char *s);                     ///< UTF-8 string (null-terminated)
void pc_msgpack_str_n(MsgpackWriter *w, const char *s, size_t len);       ///< UTF-8 string (explicit length)
void pc_msgpack_bool(MsgpackWriter *w, bool b);                           ///< true / false
void pc_msgpack_nil(MsgpackWriter *w);                                    ///< nil
void pc_msgpack_float(MsgpackWriter *w, float f);                         ///< IEEE-754 single (float32, 0xca)
void pc_msgpack_array(MsgpackWriter *w, size_t count);                    ///< array header
void pc_msgpack_map(MsgpackWriter *w, size_t count);                      ///< map header

// ---------------------------------------------------------------------------
// Decoder (cursor over a MessagePack byte buffer)
// ---------------------------------------------------------------------------

/** @brief Type of the next object (returned by pc_msgpack_peek). */
enum class MsgpackType : uint8_t
{
    MSGPACK_TYPE_UINT = 0,
    MSGPACK_TYPE_INT,
    MSGPACK_TYPE_BIN,
    MSGPACK_TYPE_STR,
    MSGPACK_TYPE_ARRAY,
    MSGPACK_TYPE_MAP,
    MSGPACK_TYPE_BOOL,
    MSGPACK_TYPE_NIL,
    MSGPACK_TYPE_FLOAT,
    MSGPACK_TYPE_INVALID ///< end of buffer, a prior error, or an unsupported object (ext / 0xc1)
};

/** @brief MessagePack decoder cursor over a read-only buffer. */
struct MsgpackReader
{
    const uint8_t *buf; ///< source bytes.
    size_t len;         ///< buffer length.
    size_t pos;         ///< current read offset.
    bool err;           ///< sticky: set on any malformed / out-of-bounds read.
};

/** @brief Bind a reader to @p buf (length @p len) at offset 0. */
void pc_msgpack_reader_init(MsgpackReader *r, const uint8_t *buf, size_t len);

/** @brief Type of the next object without consuming it. */
MsgpackType pc_msgpack_peek(MsgpackReader *r);

/** @brief True while no malformed read has occurred. */
bool pc_msgpack_reader_ok(const MsgpackReader *r);

bool pc_msgpack_read_uint(MsgpackReader *r, uint64_t *out);                ///< unsigned integer (fixint / uint8..64)
bool pc_msgpack_read_int(MsgpackReader *r, int64_t *out);                  ///< signed integer (also accepts unsigned)
bool pc_msgpack_read_bool(MsgpackReader *r, bool *out);                    ///< true / false
bool pc_msgpack_read_nil(MsgpackReader *r);                                ///< nil
bool pc_msgpack_read_float(MsgpackReader *r, float *out);                  ///< float32 (0xca) or float64 (0xcb)
bool pc_msgpack_read_str(MsgpackReader *r, const char **out, size_t *len); ///< str family (points into the buffer)
bool pc_msgpack_read_bytes(MsgpackReader *r, const uint8_t **out,
                           size_t *len);                     ///< bin family (points into the buffer)
bool pc_msgpack_read_array(MsgpackReader *r, size_t *count); ///< array header (object count)
bool pc_msgpack_read_map(MsgpackReader *r, size_t *count);   ///< map header (key/value pair count)

#endif // PC_ENABLE_MSGPACK
#endif // PROTOCORE_MSGPACK_H
