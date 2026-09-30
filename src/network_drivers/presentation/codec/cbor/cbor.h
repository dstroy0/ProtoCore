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
 * flag and stop, while the span's pos keeps counting the bytes the full payload would
 * need, so a caller can size the buffer and check spat.ok().
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CBOR_H
#define PROTOCORE_CBOR_H

#include "spatium/spatium.h" // mmgr_span / mmgr_cspan - the region, bound with spat.from()
#include "network_drivers/presentation/codec/codec.h" // protocore_codec_type - one item vocabulary

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_CBOR

// The encoder writes into a mmgr_span and the decoder reads from a mmgr_cspan. There is no CBOR-specific
// cursor type: this codec declared one field-identical to mmgr_span, MessagePack declared another, and
// the byte verbs were templated only to bind them by field name. Bind with spat.from(),
// check with spat.ok(), and take the encoded length from the span's pos.

/**
 * @brief CBOR (RFC 8949) as an instance of the codec interface.
 *
 * The operations, their order and their signatures are protocore_codec's; this is the
 * format that supplies them. The one symbol this module exports.
 */
extern const protocore_codec Cbor;

#endif // PROTOCORE_ENABLE_CBOR

PROTOCORE_END_DECLS

#endif // PROTOCORE_CBOR_H
