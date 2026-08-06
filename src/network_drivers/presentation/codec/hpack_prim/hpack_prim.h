// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_hpack_prim.h
 * @brief Low-level field-coding primitives shared by HPACK and QPACK.
 *
 * RFC 7541 defines two primitives that RFC 9204 (QPACK) reuses verbatim: the prefix-integer
 * coding (RFC 7541 sec 5.1) and the canonical Huffman code (RFC 7541 Appendix B, referenced by
 * RFC 9204 sec 5). This module is the single copy of both, so HTTP/2's HPACK and HTTP/3's QPACK
 * share one implementation and one Huffman table instead of duplicating ~1 KB of tables.
 *
 * Pure and host-tested (via the HPACK and QPACK codec tests). Zero heap.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HPACK_PRIM_H
#define PROTOCORE_HPACK_PRIM_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_HTTP2 || PC_ENABLE_HTTP3

/**
 * @brief The two RFC 7541 primitives, in both directions, as HPACK and QPACK both call them.
 *
 * @var HpackPrimNs::encode_int   a prefix-@p prefix_bits integer with the high @p flags bits set in byte 0
 * @var HpackPrimNs::decode_int   the same integer back; sets @p consumed and @p value, false if malformed
 * @var HpackPrimNs::huff_encode  Huffman-encode @p n bytes of @p s (RFC 7541 Appendix B); bytes written,
 *                                or 0 on overflow
 * @var HpackPrimNs::huff_len     the Huffman byte length of @p s without encoding it, which is what
 *                                decides Huffman against raw
 * @var HpackPrimNs::huff_decode  Huffman-decode @p n bytes into @p out; sets @p out_len, false on a bad code
 * @var HpackPrimNs::decode_str   a length-prefixed string literal (H bit at 0x80 + a 7-bit length prefix,
 *                                RFC 7541 sec 5.2, reused verbatim by RFC 9204) at @p block[*pos]; advances
 *                                @p *pos and sets @p out_len, false if truncated, over @p cap, or a bad code
 * @var HpackPrimNs::encode_str   the same literal out, Huffman-coded when that is the shorter of the two;
 *                                bytes written, or 0 on overflow
 */
typedef struct
{
    size_t (*encode_int)(uint8_t *out, size_t cap, uint8_t prefix_bits, uint8_t flags, uint32_t value);
    proto_bool (*decode_int)(const uint8_t *in, size_t len, uint8_t prefix_bits, size_t *consumed, uint32_t *value);
    size_t (*huff_encode)(uint8_t *out, size_t cap, const char *s, size_t n);
    size_t (*huff_len)(const char *s, size_t n);
    proto_bool (*huff_decode)(const uint8_t *in, size_t n, char *out, size_t cap, size_t *out_len);
    proto_bool (*decode_str)(const uint8_t *block, size_t len, size_t *pos, char *out, size_t cap, size_t *out_len);
    size_t (*encode_str)(uint8_t *out, size_t cap, const char *s, size_t n);
} HpackPrimNs;

/** @brief The one symbol this module exports. */
extern const HpackPrimNs HpackPrim;

#endif // PC_ENABLE_HTTP2 || PC_ENABLE_HTTP3

PROTO_END_DECLS

#endif // PROTOCORE_HPACK_PRIM_H
