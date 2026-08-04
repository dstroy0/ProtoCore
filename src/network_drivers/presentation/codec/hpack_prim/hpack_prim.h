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

/** @brief Encode a prefix-@p prefix_bits integer with the high @p flags bits set in byte 0. */
size_t pc_hpack_encode_int(uint8_t *out, size_t cap, uint8_t prefix_bits, uint8_t flags, uint32_t value);
/** @brief Decode a prefix-@p prefix_bits integer; sets @p consumed / @p value. @return false if malformed. */
proto_bool pc_hpack_decode_int(const uint8_t *in, size_t len, uint8_t prefix_bits, size_t *consumed, uint32_t *value);
/** @brief Huffman-encode @p n bytes of @p s (RFC 7541 Appendix B). @return bytes written or 0 on overflow. */
size_t pc_hpack_huff_encode(uint8_t *out, size_t cap, const char *s, size_t n);
/** @brief Huffman byte length of @p s without encoding (to choose Huffman vs raw). */
size_t pc_hpack_huff_len(const char *s, size_t n);
/** @brief Huffman-decode @p n bytes into @p out; sets @p out_len. @return false on a bad code. */
proto_bool pc_hpack_huff_decode(const uint8_t *in, size_t n, char *out, size_t cap, size_t *out_len);

/**
 * @brief Decode a length-prefixed string literal (H bit at 0x80 + 7-bit length prefix, RFC 7541 sec 5.2,
 *        reused verbatim by RFC 9204) at @p block[*pos] into @p out; advances @p *pos, sets @p out_len.
 * @return false if truncated, over @p cap, or a bad Huffman code. Shared by HPACK and QPACK string literals.
 */
proto_bool pc_hpack_decode_str(const uint8_t *block, size_t len, size_t *pos, char *out, size_t cap, size_t *out_len);
/** @brief Encode @p n bytes of @p s as a length-prefixed string literal (Huffman when it is shorter).
 * @return bytes written, or 0 on overflow. */
size_t pc_hpack_encode_str(uint8_t *out, size_t cap, const char *s, size_t n);

#endif // PC_ENABLE_HTTP2 || PC_ENABLE_HTTP3

PROTO_END_DECLS

#endif // PROTOCORE_HPACK_PRIM_H
