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
 * Overflow is tracked, not crashed on: writes past the buffer set the span's overflow
 * flag and stop, while pc_span_len() keeps counting the bytes the full payload
 * would need, so a caller can size the buffer and check pc_span_ok().
 *
 * The decoder is a cursor: pc_msgpack_peek() reports the next object's type and the
 * pc_msgpack_read_* calls consume it (strings and binary point into the source buffer,
 * no copy). Any malformed or out-of-bounds read sets a sticky error - check
 * pc_cspan_ok(). ext and the unused 0xc1 byte are reported as INVALID.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MSGPACK_H
#define PROTOCORE_MSGPACK_H

#include "network_drivers/presentation/codec/codec.h" // pc_codec_type - one item vocabulary
#include "protocore_config.h"
#include "server/mmgr/span.h" // pc_span / pc_cspan - the region, bound with pc_span_from()
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_MSGPACK

// The encoder writes into a pc_span and the decoder reads from a pc_cspan. Bind with
// pc_span_from(buf, cap), check with pc_span_ok(), and take the encoded length from pc_span_len().

void pc_msgpack_uint(pc_span *w, uint64_t v);                       ///< unsigned integer
void pc_msgpack_int(pc_span *w, int64_t v);                         ///< signed integer
void pc_msgpack_bytes(pc_span *w, const uint8_t *data, size_t len); ///< binary (bin family)
void pc_msgpack_str(pc_span *w, const char *s);                     ///< UTF-8 string (null-terminated)
void pc_msgpack_str_n(pc_span *w, const char *s, size_t len);       ///< UTF-8 string (explicit length)
void pc_msgpack_bool(pc_span *w, bool b);                           ///< true / false
void pc_msgpack_null(pc_span *w);                                   ///< nil
void pc_msgpack_float(pc_span *w, float f);                         ///< IEEE-754 single (float32, 0xca)
void pc_msgpack_array(pc_span *w, size_t count);                    ///< array header
void pc_msgpack_map(pc_span *w, size_t count);                      ///< map header
void pc_msgpack_label(pc_span *w, const char *name, int64_t num);   ///< map key: the integer label form

// ---------------------------------------------------------------------------
// Decoder (cursor over a MessagePack byte buffer)
// ---------------------------------------------------------------------------

/** @brief Type of the next object without consuming it. ext and the unused 0xc1 report INVALID. */
pc_codec_type pc_msgpack_peek(pc_cspan *r);

bool pc_msgpack_read_uint(pc_cspan *r, uint64_t *out);                ///< unsigned integer (fixint / uint8..64)
bool pc_msgpack_read_int(pc_cspan *r, int64_t *out);                  ///< signed integer (also accepts unsigned)
bool pc_msgpack_read_bool(pc_cspan *r, bool *out);                    ///< true / false
bool pc_msgpack_read_null(pc_cspan *r);                               ///< nil
bool pc_msgpack_read_float(pc_cspan *r, float *out);                  ///< float32 (0xca) or float64 (0xcb)
bool pc_msgpack_read_str(pc_cspan *r, const char **out, size_t *len); ///< str family (points into the buffer)
bool pc_msgpack_read_bytes(pc_cspan *r, const uint8_t **out,
                           size_t *len);                ///< bin family (points into the buffer)
bool pc_msgpack_read_array(pc_cspan *r, size_t *count); ///< array header (object count)
bool pc_msgpack_read_map(pc_cspan *r, size_t *count);   ///< map header (key/value pair count)

#endif // PC_ENABLE_MSGPACK
#endif // PROTOCORE_MSGPACK_H
