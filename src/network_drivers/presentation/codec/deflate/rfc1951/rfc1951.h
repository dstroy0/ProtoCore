// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rfc1951.h
 * @brief The RFC 1951 sec 3.2.5 length and distance tables, and the fixed-Huffman coder over them.
 *
 * The four tables are defined once in rfc1951.c and read off this namespace. The DEFLATE encoder and
 * decoder (codec/deflate, codec/inflate) and the SSH zlib@openssh.com stream codecs
 * (ssh/transport/ssh_zlib, ssh/transport/ssh_inflate) read them.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_RFC1951_H
#define PROTOCORE_RFC1951_H

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_DEFLATE_RFC1951

#include "mmgr/bitio/bitio.h" // protocore_bit_writer - what the emitters write through

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief What reverse_bits takes: a code, and how many of its low bits to reverse. */
typedef struct
{
    uint16_t code; ///< the code whose low @c len bits are reversed
    int len;       ///< how many low bits to reverse
} Rfc1951ReverseBitsArgs;

/** @brief What build_fixed takes: the four tables it fills. */
typedef struct
{
    uint16_t *ll_code; ///< 288 entries: the lit/length code, bit-reversed
    uint8_t *ll_len;   ///< 288 entries: its bit length
    uint16_t *d_code;  ///< 30 entries: the distance code, bit-reversed
    uint8_t *d_len;    ///< 30 entries: its bit length
} Rfc1951BuildFixedArgs;

/** @brief What emit_literal takes: the writer, the lit/length code tables, and the byte. */
typedef struct
{
    protocore_bit_writer *w; ///< the bit writer the code goes out through
    const uint16_t *ll_code; ///< 288 entries, from build_fixed
    const uint8_t *ll_len;   ///< 288 entries, from build_fixed
    uint8_t b;               ///< the literal byte
} Rfc1951EmitLiteralArgs;

/** @brief What emit_match takes: the writer, all four code tables, and the back-reference. */
typedef struct
{
    protocore_bit_writer *w; ///< the bit writer the codes go out through
    const uint16_t *ll_code; ///< 288 entries, from build_fixed
    const uint8_t *ll_len;   ///< 288 entries, from build_fixed
    const uint16_t *d_code;  ///< 30 entries, from build_fixed
    const uint8_t *d_len;    ///< 30 entries, from build_fixed
    int len;                 ///< match length, 3..258
    int dist;                ///< match distance, 1..32768
} Rfc1951EmitMatchArgs;

/**
 * @brief The RFC 1951 sec 3.2.5 tables, and the fixed Huffman coder of sec 3.2.6 over them.
 *
 * A caller sets the members a call takes, invokes it through ::Rfc1951 with the bytes it runs out
 * of, and reads the outcome off the same handle. The four tables are read straight off it.
 *
 *   Rfc1951.emit_literal_args.w = &w;
 *   Rfc1951.emit_literal_args.ll_code = ll_code;
 *   Rfc1951.emit_literal_args.ll_len = ll_len;
 *   Rfc1951.emit_literal_args.b = src[i];
 *   Rfc1951.emit_literal(work);
 *
 * @var Rfc1951Ns::len_base           29 entries: base length for codes 257..285
 * @var Rfc1951Ns::len_extra          29 entries: extra bits read after each of those codes
 * @var Rfc1951Ns::dist_base          30 entries: base distance for codes 0..29
 * @var Rfc1951Ns::dist_extra         30 entries: extra bits read after each of those codes
 * @var Rfc1951Ns::reverse_bits_args  what reverse_bits takes: a code, and how many of its low bits to reverse
 * @var Rfc1951Ns::build_fixed_args   what build_fixed takes: the four tables it fills
 * @var Rfc1951Ns::emit_literal_args  what emit_literal takes: the writer, the lit/length code tables, and the byte
 * @var Rfc1951Ns::emit_match_args    what emit_match takes: the writer, all four code tables, and the back-reference
 * @var Rfc1951Ns::u16                the reversed code the last reverse_bits produced
 * @var Rfc1951Ns::reverse_bits       reverse the low @c len bits of @c code, MSB-first on the wire
 * @var Rfc1951Ns::build_fixed        fill the fixed Huffman code/length tables (sec 3.2.6), each code bit-reversed
 * @var Rfc1951Ns::emit_literal       emit one literal byte through the fixed lit/length code
 * @var Rfc1951Ns::emit_match         emit a (length, distance) back-reference through the fixed code tables
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing between
 * calls, so there is no state to keep and nothing to wipe. The parameter is there so a caller
 * drives every namespace the same way.
 */
typedef struct
{
    const short *len_base;
    const short *len_extra;
    const short *dist_base;
    const short *dist_extra;
    Rfc1951ReverseBitsArgs reverse_bits_args;
    Rfc1951BuildFixedArgs build_fixed_args;
    Rfc1951EmitLiteralArgs emit_literal_args;
    Rfc1951EmitMatchArgs emit_match_args;
    uint16_t u16;
} Rfc1951Vars;

/** @brief The operands and the outcome. */
extern Rfc1951Vars Rfc1951V;

/** @brief The entries. */
typedef struct
{
    void (*const reverse_bits)(uint8_t *restrict work);
    void (*const build_fixed)(uint8_t *restrict work);
    void (*const emit_literal)(uint8_t *restrict work);
    void (*const emit_match)(uint8_t *restrict work);
} Rfc1951Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Rfc1951V or a region of the borrow at a fixed offset.
void protocore_rfc1951_reverse_bits(uint8_t *restrict work);
void protocore_rfc1951_build_fixed(uint8_t *restrict work);
void protocore_rfc1951_emit_literal(uint8_t *restrict work);
void protocore_rfc1951_emit_match(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Rfc1951.reverse_bits(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Rfc1951Ns Rfc1951 __attribute__((unused)) = {
    .reverse_bits = protocore_rfc1951_reverse_bits,
    .build_fixed = protocore_rfc1951_build_fixed,
    .emit_literal = protocore_rfc1951_emit_literal,
    .emit_match = protocore_rfc1951_emit_match,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DEFLATE_RFC1951

#endif // PROTOCORE_RFC1951_H
