// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_hpack_prim.h
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HPACK_PRIM

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief What encode_int takes: out, cap, prefix_bits, flags, value. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    uint8_t prefix_bits;
    uint8_t flags;
    uint32_t value;
} HpackPrimEncodeIntArgs;
/** @brief What decode_int takes: in, len, prefix_bits, consumed, value. */
typedef struct
{
    const uint8_t *in;
    size_t len;
    uint8_t prefix_bits;
    size_t *consumed;
    uint32_t *value;
} HpackPrimDecodeIntArgs;
/** @brief What huff_encode takes: out, cap, s, n. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    const char *s;
    size_t n;
} HpackPrimHuffEncodeArgs;
/** @brief What huff_len takes: s, n. */
typedef struct
{
    const char *s;
    size_t n;
} HpackPrimHuffLenArgs;
/** @brief What huff_decode takes: in, n, out, cap, out_len. */
typedef struct
{
    const uint8_t *in;
    size_t n;
    char *out;
    size_t cap;
    size_t *out_len;
} HpackPrimHuffDecodeArgs;
/** @brief What decode_str takes: block, len, pos, out, cap, out_len. */
typedef struct
{
    const uint8_t *block;
    size_t len;
    size_t *pos;
    char *out;
    size_t cap;
    size_t *out_len;
} HpackPrimDecodeStrArgs;
/** @brief What encode_str takes: out, cap, s, n. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    const char *s;
    size_t n;
} HpackPrimEncodeStrArgs;
/**
 * @brief Low-level field-coding primitives shared by HPACK and QPACK.
 *
 * A caller sets the members a call takes, invokes it through ::HpackPrim with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   HpackPrim.encode_int_args.out = ...;
 *   HpackPrim.encode_int_args.cap = ...;
 *   HpackPrim.encode_int_args.prefix_bits = ...;
 *   HpackPrim.encode_int_args.flags = ...;
 *   HpackPrim.encode_int_args.value = ...;
 *   HpackPrim.encode_int(work);
 *   // HpackPrim.n is what the call reports
 *
 * @var HpackPrimNs::encode_int_args  what encode_int takes: out, cap, prefix_bits, flags, value
 * @var HpackPrimNs::decode_int_args  what decode_int takes: in, len, prefix_bits, consumed, value
 * @var HpackPrimNs::huff_encode_args  what huff_encode takes: out, cap, s, n
 * @var HpackPrimNs::huff_len_args  what huff_len takes: s, n
 * @var HpackPrimNs::huff_decode_args  what huff_decode takes: in, n, out, cap, out_len
 * @var HpackPrimNs::decode_str_args  what decode_str takes: block, len, pos, out, cap, out_len
 * @var HpackPrimNs::encode_str_args  what encode_str takes: out, cap, s, n
 * @var HpackPrimNs::ok  a call's true/false outcome
 * @var HpackPrimNs::n  the count a call reports
 * @var HpackPrimNs::encode_int  a prefix-prefix_bits integer with the high flags bits set in byte 0
 * @var HpackPrimNs::decode_int  the same integer back; sets consumed and value, false if malformed
 * @var HpackPrimNs::huff_encode  huffman-encode n bytes of s (RFC 7541 Appendix B); bytes written,
 * @var HpackPrimNs::huff_len  the Huffman byte length of s without encoding it, which is what
 * @var HpackPrimNs::huff_decode  huffman-decode n bytes into out; sets out_len, false on a bad code
 * @var HpackPrimNs::decode_str  a length-prefixed string literal (H bit at 0x80 + a 7-bit length ...
 * @var HpackPrimNs::encode_str  the same literal out, Huffman-coded when that is the shorter of the ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    HpackPrimEncodeIntArgs encode_int_args;
    HpackPrimDecodeIntArgs decode_int_args;
    HpackPrimHuffEncodeArgs huff_encode_args;
    HpackPrimHuffLenArgs huff_len_args;
    HpackPrimHuffDecodeArgs huff_decode_args;
    HpackPrimDecodeStrArgs decode_str_args;
    HpackPrimEncodeStrArgs encode_str_args;
    proto_bool ok;
    size_t n;
} HpackPrimVars;

/** @brief The operands and the outcome. */
extern HpackPrimVars HpackPrimV;

/** @brief The entries. */
typedef struct
{
    void (*const encode_int)(uint8_t *restrict work);
    void (*const decode_int)(uint8_t *restrict work);
    void (*const huff_encode)(uint8_t *restrict work);
    void (*const huff_len)(uint8_t *restrict work);
    void (*const huff_decode)(uint8_t *restrict work);
    void (*const decode_str)(uint8_t *restrict work);
    void (*const encode_str)(uint8_t *restrict work);
} HpackPrimNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in HpackPrimV or a region of the borrow at a fixed offset.
void protocore_hpack_prim_encode_int(uint8_t *restrict work);
void protocore_hpack_prim_decode_int(uint8_t *restrict work);
void protocore_hpack_prim_huff_encode(uint8_t *restrict work);
void protocore_hpack_prim_huff_len(uint8_t *restrict work);
void protocore_hpack_prim_huff_decode(uint8_t *restrict work);
void protocore_hpack_prim_decode_str(uint8_t *restrict work);
void protocore_hpack_prim_encode_str(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `HpackPrim.encode_int(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const HpackPrimNs HpackPrim __attribute__((unused)) = {
    .encode_int = protocore_hpack_prim_encode_int,
    .decode_int = protocore_hpack_prim_decode_int,
    .huff_encode = protocore_hpack_prim_huff_encode,
    .huff_len = protocore_hpack_prim_huff_len,
    .huff_decode = protocore_hpack_prim_huff_decode,
    .decode_str = protocore_hpack_prim_decode_str,
    .encode_str = protocore_hpack_prim_encode_str,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HPACK_PRIM

#endif // PROTOCORE_HPACK_PRIM_H
