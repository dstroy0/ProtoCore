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

#include "network_drivers/presentation/codec/codec.h" // pc_codec_type - one item vocabulary
#include "protocore_config.h"
#include "shared_primitives/span.h" // pc_span / pc_cspan - the region, bound with pc_span_from()

PROTO_BEGIN_DECLS

#if PC_NEED_CBOR

// The encoder writes into a pc_span and the decoder reads from a pc_cspan. There is no CBOR-specific
// cursor type: this codec declared one field-identical to pc_span, MessagePack declared another, and
// the byte verbs were templated only to bind them by field name. Bind with pc_span_from(buf, cap),
// check with pc_span_ok(), and take the encoded length from pc_span_len().

void pc_cbor_uint(pc_span *w, uint64_t v);                       ///< unsigned integer
void pc_cbor_int(pc_span *w, int64_t v);                         ///< signed integer
void pc_cbor_bytes(pc_span *w, const uint8_t *data, size_t len); ///< byte string
void pc_cbor_str(pc_span *w, const char *s);                     ///< text string (null-terminated)
void pc_cbor_str_n(pc_span *w, const char *s, size_t len);       ///< text string (explicit length)
void pc_cbor_bool(pc_span *w, proto_bool b);                     ///< true / false
void pc_cbor_null(pc_span *w);                                   ///< null
void pc_cbor_float(pc_span *w, float f);                         ///< IEEE-754 single (major 7, 0xfa)
void pc_cbor_array(pc_span *w, size_t count);                    ///< definite-length array header
void pc_cbor_map(pc_span *w, size_t count);                      ///< definite-length map header
void pc_cbor_label(pc_span *w, const char *name, int64_t num);   ///< map key: the integer label form

// ---------------------------------------------------------------------------
// Decoder (cursor over a CBOR byte buffer)
// ---------------------------------------------------------------------------

// The decoder reads from a pc_cspan. Bind with pc_cspan_from(buf, len) and check pc_cspan_ok().
// Major 6 (tags) and the indefinite-length forms report INVALID.

/** @brief Type of the next item without consuming it. */
pc_codec_type pc_cbor_peek(pc_cspan *r);

proto_bool pc_cbor_read_uint(pc_cspan *r, uint64_t *out);                ///< unsigned integer
proto_bool pc_cbor_read_int(pc_cspan *r, int64_t *out);                  ///< signed integer (also accepts unsigned)
proto_bool pc_cbor_read_bool(pc_cspan *r, proto_bool *out);              ///< true / false
proto_bool pc_cbor_read_null(pc_cspan *r);                               ///< null
proto_bool pc_cbor_read_float(pc_cspan *r, float *out);                  ///< float32 (0xfa) or double (0xfb)
proto_bool pc_cbor_read_str(pc_cspan *r, const char **out, size_t *len); ///< text string (points into the buffer)
proto_bool pc_cbor_read_bytes(pc_cspan *r, const uint8_t **out, size_t *len); ///< byte string (points into the buffer)
proto_bool pc_cbor_read_array(pc_cspan *r, size_t *count);                    ///< definite-length array header
proto_bool pc_cbor_read_map(pc_cspan *r, size_t *count);                      ///< definite-length map header

#endif // PC_NEED_CBOR

PROTO_END_DECLS

#endif // PROTOCORE_CBOR_H
