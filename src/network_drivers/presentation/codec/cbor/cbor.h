// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
 * flag and stop, while pc_span_len() keeps counting the bytes the full payload would
 * need, so a caller can size the buffer and check pc_span_ok().
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CBOR_H
#define PROTOCORE_CBOR_H

#include "mmgr/span.h"                                // pc_span / pc_cspan - the region, bound with pc_span_from()
#include "network_drivers/presentation/codec/codec.h" // pc_codec_type - one item vocabulary
#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_NEED_CBOR

// The encoder writes into a pc_span and the decoder reads from a pc_cspan. There is no CBOR-specific
// cursor type: this codec declared one field-identical to pc_span, MessagePack declared another, and
// the byte verbs were templated only to bind them by field name. Bind with pc_span_from(buf, cap),
// check with pc_span_ok(), and take the encoded length from pc_span_len().

/**
 * @brief CBOR (RFC 8949) as an instance of the codec interface.
 *
 * The operations, their order and their signatures are pc_codec's; this is the
 * format that supplies them. The one symbol this module exports.
 */
extern const pc_codec Cbor;

#endif // PC_NEED_CBOR

PROTO_END_DECLS

#endif // PROTOCORE_CBOR_H
