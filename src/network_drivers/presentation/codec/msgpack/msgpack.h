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
 * (MsgPack.put_array / MsgPack.put_map with the item count) then that many items (twice
 * that for a map: key, value, key, value, ...).
 *
 * Overflow is tracked, not crashed on: writes past the buffer set the span's overflow
 * flag and stop, while pc_span_len() keeps counting the bytes the full payload
 * would need, so a caller can size the buffer and check pc_span_ok().
 *
 * The decoder is a cursor: MsgPack.peek() reports the next object's type and the
 * MsgPack.get_* calls consume it (strings and binary point into the source buffer,
 * no copy). Any malformed or out-of-bounds read sets a sticky error - check
 * pc_cspan_ok(). ext and the unused 0xc1 byte are reported as INVALID.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MSGPACK_H
#define PROTOCORE_MSGPACK_H

#include "mmgr/span.h"                                // pc_span / pc_cspan - the region, bound with pc_span_from()
#include "network_drivers/presentation/codec/codec.h" // pc_codec_type - one item vocabulary
#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_MSGPACK

// The encoder writes into a pc_span and the decoder reads from a pc_cspan. Bind with
// pc_span_from(buf, cap), check with pc_span_ok(), and take the encoded length from pc_span_len().

/**
 * @brief MessagePack as an instance of the codec interface.
 *
 * The operations, their order and their signatures are pc_codec's; this is the
 * format that supplies them. The one symbol this module exports.
 */
extern const pc_codec MsgPack;

#endif // PC_ENABLE_MSGPACK

PROTO_END_DECLS

#endif // PROTOCORE_MSGPACK_H
