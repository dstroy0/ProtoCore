// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cbor.h
 * @brief Layer 6 (Presentation) - zero-heap CBOR (RFC 8949) encoder.
 *
 * A streaming encoder that writes directly into a caller-provided buffer (no
 * heap), the binary counterpart to the JSON writer. Emit definite-length arrays
 * and maps by writing the header (Cbor.put_array / Cbor.put_map with the item count) then
 * that many items (twice that for a map: key, value, key, value, ...).
 *
 * Overflow is tracked, not crashed on: writes past the buffer set the overflow
 * flag and stop, while span.len() keeps counting the bytes the full payload would
 * need, so a caller can size the buffer and check span.ok().
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CBOR_H
#define PROTOCORE_CBOR_H

#include "mmgr/span/span.h" // protocore_span / protocore_cspan - the region, bound with span.from()
#include "network_drivers/presentation/codec/codec.h" // protocore_codec_type - one item vocabulary

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_NEED_CBOR

// The encoder writes into a protocore_span and the decoder reads from a protocore_cspan. There is no CBOR-specific
// cursor type: this codec declared one field-identical to protocore_span, MessagePack declared another, and
// the byte verbs were templated only to bind them by field name. Bind with span.from(buf, cap),
// check with span.ok(), and take the encoded length from span.len().

/**
 * @brief CBOR (RFC 8949) as an instance of the codec interface.
 *
 * The operations, their order and their signatures are protocore_codec's; this is the
 * format that supplies them. The one symbol this module exports.
 */
extern const protocore_codec Cbor;

#endif // PROTOCORE_NEED_CBOR

PROTOCORE_END_DECLS

#endif // PROTOCORE_CBOR_H
