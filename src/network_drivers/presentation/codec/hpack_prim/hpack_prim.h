// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_HPACK_PRIM_H
#define PROTOCORE_HPACK_PRIM_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file hpack_prim.h
 * @brief Low-level field-coding primitives shared by HPACK and QPACK.
 *
 * RFC 7541 defines two primitives that RFC 9204 (QPACK) reuses verbatim: the prefix-integer
 * coding (RFC 7541 sec 5.1) and the canonical Huffman code (RFC 7541 Appendix B, referenced by
 * RFC 9204 sec 5). This module is the single copy of both, so HTTP/2's HPACK and HTTP/3's QPACK
 * share one implementation and one Huffman table instead of duplicating ~1 KB of tables.
 *
 * Pure and host-tested (via the HPACK and QPACK codec tests). Zero heap.
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_HPACK_PRIM_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    size_t (*encode_int)(uint8_t *, uint8_t *, size_t, uint8_t, uint8_t, uint32_t);
    proto_bool (*decode_int)(uint8_t *, const uint8_t *, size_t, uint8_t, size_t *, uint32_t *);
    size_t (*huff_encode)(uint8_t *, uint8_t *, size_t, const char *, size_t);
    size_t (*huff_len)(uint8_t *, const char *, size_t);
    proto_bool (*huff_decode)(uint8_t *, const uint8_t *, size_t, char *, size_t, size_t *);
    proto_bool (*decode_str)(uint8_t *, const uint8_t *, size_t, size_t *, char *, size_t, size_t *);
    size_t (*encode_str)(uint8_t *, uint8_t *, size_t, const char *, size_t);
} HpackPrimNs;
PROTOCORE_NS_LAYOUT(HpackPrimNs, encode_int, decode_int, huff_encode, huff_len, huff_decode, decode_str, encode_str);

/**
 * @brief A prefix-prefix_bits integer with the high flags bits set in byte 0.
 * @param work PROTOCORE_HPACK_PRIM_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param prefix_bits Prefix bits
 * @param flags Flags
 * @param value Value
 * @return The size_t.
 */
size_t protocore_hpack_prim_encode_int(uint8_t *work, uint8_t *out, size_t cap, uint8_t prefix_bits, uint8_t flags,
                                       uint32_t value);
/**
 * @brief The same integer back; sets consumed and value, false if malformed.
 * @param work PROTOCORE_HPACK_PRIM_BORROW bytes the caller took. Not held past the call.
 * @param in In
 * @param len Len
 * @param prefix_bits Prefix bits
 * @param consumed Consumed
 * @param value Value
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_hpack_prim_decode_int(uint8_t *work, const uint8_t *in, size_t len, uint8_t prefix_bits,
                                           size_t *consumed, uint32_t *value);
/**
 * @brief Huffman-encode n bytes of s (RFC 7541 Appendix B); bytes written,.
 * @param work PROTOCORE_HPACK_PRIM_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param s S
 * @param n N
 * @return The size_t.
 */
size_t protocore_hpack_prim_huff_encode(uint8_t *work, uint8_t *out, size_t cap, const char *s, size_t n);
/**
 * @brief The Huffman byte length of s without encoding it, which is what.
 * @param work PROTOCORE_HPACK_PRIM_BORROW bytes the caller took. Not held past the call.
 * @param s S
 * @param n N
 * @return The size_t.
 */
size_t protocore_hpack_prim_huff_len(uint8_t *work, const char *s, size_t n);
/**
 * @brief Huffman-decode n bytes into out; sets out_len, false on a bad code.
 * @param work PROTOCORE_HPACK_PRIM_BORROW bytes the caller took. Not held past the call.
 * @param in In
 * @param n N
 * @param out Out
 * @param cap Cap
 * @param out_len Out len
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_hpack_prim_huff_decode(uint8_t *work, const uint8_t *in, size_t n, char *out, size_t cap,
                                            size_t *out_len);
/**
 * @brief A length-prefixed string literal (H bit at 0x80 + a 7-bit length .
 * @param work PROTOCORE_HPACK_PRIM_BORROW bytes the caller took. Not held past the call.
 * @param block Block
 * @param len Len
 * @param pos Pos
 * @param out Out
 * @param cap Cap
 * @param out_len Out len
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_hpack_prim_decode_str(uint8_t *work, const uint8_t *block, size_t len, size_t *pos, char *out,
                                           size_t cap, size_t *out_len);
/**
 * @brief The same literal out, Huffman-coded when that is the shorter of the .
 * @param work PROTOCORE_HPACK_PRIM_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param s S
 * @param n N
 * @return The size_t.
 */
size_t protocore_hpack_prim_encode_str(uint8_t *work, uint8_t *out, size_t cap, const char *s, size_t n);

/** @brief Module namespace. */
PROTOCORE_NS HpackPrimNs HpackPrim PROTOCORE_UNUSED = {.encode_int = protocore_hpack_prim_encode_int,
                                                       .decode_int = protocore_hpack_prim_decode_int,
                                                       .huff_encode = protocore_hpack_prim_huff_encode,
                                                       .huff_len = protocore_hpack_prim_huff_len,
                                                       .huff_decode = protocore_hpack_prim_huff_decode,
                                                       .decode_str = protocore_hpack_prim_decode_str,
                                                       .encode_str = protocore_hpack_prim_encode_str};

PROTOCORE_END_DECLS

#endif // PROTOCORE_HPACK_PRIM_H
