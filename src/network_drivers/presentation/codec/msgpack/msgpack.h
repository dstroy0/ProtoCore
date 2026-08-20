// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
 * flag and stop, while span.len() keeps counting the bytes the full payload
 * would need, so a caller can size the buffer and check span.ok().
 *
 * The decoder is a cursor: MsgPack.peek() reports the next object's type and the
 * MsgPack.get_* calls consume it (strings and binary point into the source buffer,
 * no copy). Any malformed or out-of-bounds read sets a sticky error - check
 * span.cok(). ext and the unused 0xc1 byte are reported as INVALID.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MSGPACK_H
#define PROTOCORE_MSGPACK_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_MSGPACK

#include "mmgr/span/span.h" // protocore_span / protocore_cspan - the region, bound with span.from()
#include "network_drivers/presentation/codec/codec.h" // protocore_codec_type - one item vocabulary

PROTOCORE_BEGIN_DECLS

// The encoder writes into a protocore_span and the decoder reads from a protocore_cspan. Bind with
// span.from(buf, cap), check with span.ok(), and take the encoded length from span.len().

/**
 * @brief MessagePack as an instance of the codec interface.
 *
 * The operations, their order and their signatures are protocore_codec's; this is the
 * format that supplies them. The one symbol this module exports.
 */
extern const protocore_codec MsgPack;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MSGPACK

#endif // PROTOCORE_MSGPACK_H
