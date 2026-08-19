// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rfc1951.c
 * @brief The one definition of the RFC 1951 sec 3.2.5 tables, and the coder over them.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_DEFLATE_RFC1951

#include "mmgr/protomem/protomem.h" // mem.set: build_fixed zeroes its bit-length counts
#include "network_drivers/presentation/codec/deflate/rfc1951/rfc1951.h"

PROTOCORE_BEGIN_DECLS

// Length code base values and extra bits (RFC 1951 sec 3.2.5), codes 257..285.
static const short len_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

// Distance code base values and extra bits, codes 0..29.
static const short dist_base[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                    33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const short dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                     6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

// r = the low len bits of code, in the opposite order (a Huffman code goes on the wire MSB-first).
static void rfc1951_reverse_bits(uint8_t *restrict work)
{
    (void)work;
    uint16_t code = Rfc1951.reverse_bits_args.code;
    int len = Rfc1951.reverse_bits_args.len;

    uint16_t r = 0;
    for (int k = 0; k < len; k++)
    {
        r = (uint16_t)((r << 1) | (code & 1));
        code >>= 1;
    }
    Rfc1951.u16 = r;
}

// The fixed Huffman code/length tables (RFC 1951 sec 3.2.6), each code bit-reversed. ll_code /
// ll_len hold 288 entries, d_code / d_len hold 30.
static void rfc1951_build_fixed(uint8_t *restrict work)
{
    uint16_t *ll_code = Rfc1951.build_fixed_args.ll_code;
    uint8_t *ll_len = Rfc1951.build_fixed_args.ll_len;
    uint16_t *d_code = Rfc1951.build_fixed_args.d_code;
    uint8_t *d_len = Rfc1951.build_fixed_args.d_len;

    int sym = 0;
    for (; sym < 144; sym++)
    {
        ll_len[sym] = 8;
    }
    for (; sym < 256; sym++)
    {
        ll_len[sym] = 9;
    }
    for (; sym < 280; sym++)
    {
        ll_len[sym] = 7;
    }
    for (; sym < 288; sym++)
    {
        ll_len[sym] = 8;
    }
    for (sym = 0; sym < 30; sym++)
    {
        d_len[sym] = 5;
    }

    // Canonical code assignment (RFC 1951 sec 3.2.2) for the lit/length alphabet.
    uint16_t bl_count[16];
    mem.set(bl_count, 0, sizeof(bl_count));
    for (sym = 0; sym < 288; sym++)
    {
        bl_count[ll_len[sym]]++;
    }
    uint16_t next_code[16];
    next_code[0] = 0;
    uint16_t code = 0;
    for (int bits = 1; bits <= 15; bits++)
    {
        code = (uint16_t)((code + bl_count[bits - 1]) << 1);
        next_code[bits] = code;
    }
    for (sym = 0; sym < 288; sym++)
    {
        int len = ll_len[sym];
        Rfc1951.reverse_bits_args.code = next_code[len];
        Rfc1951.reverse_bits_args.len = len;
        rfc1951_reverse_bits(work);
        ll_code[sym] = Rfc1951.u16;
        next_code[len]++;
    }

    // Distance alphabet: 30 codes all of length 5 -> codes 0..29 in order.
    for (sym = 0; sym < 30; sym++)
    {
        Rfc1951.reverse_bits_args.code = (uint16_t)sym;
        Rfc1951.reverse_bits_args.len = 5;
        rfc1951_reverse_bits(work);
        d_code[sym] = Rfc1951.u16;
    }
}

// One literal byte through the fixed lit/length code.
static void rfc1951_emit_literal(uint8_t *restrict work)
{
    (void)work;
    protocore_bit_writer *w = Rfc1951.emit_literal_args.w;
    const uint16_t *ll_code = Rfc1951.emit_literal_args.ll_code;
    const uint8_t *ll_len = Rfc1951.emit_literal_args.ll_len;
    uint8_t b = Rfc1951.emit_literal_args.b;

    bitw.put(w, ll_code[b], ll_len[b]);
}

// A (len, dist) back-reference through the fixed code tables (RFC 1951 sec 3.2.5).
static void rfc1951_emit_match(uint8_t *restrict work)
{
    (void)work;
    protocore_bit_writer *w = Rfc1951.emit_match_args.w;
    const uint16_t *ll_code = Rfc1951.emit_match_args.ll_code;
    const uint8_t *ll_len = Rfc1951.emit_match_args.ll_len;
    const uint16_t *d_code = Rfc1951.emit_match_args.d_code;
    const uint8_t *d_len = Rfc1951.emit_match_args.d_len;
    int len = Rfc1951.emit_match_args.len;
    int dist = Rfc1951.emit_match_args.dist;

    int li = 0;
    while (li < 28 && len >= len_base[li + 1])
    {
        li++;
    }
    int lsym = 257 + li;
    bitw.put(w, ll_code[lsym], ll_len[lsym]);
    if (len_extra[li])
    {
        bitw.put(w, (uint32_t)(len - len_base[li]), len_extra[li]);
    }

    int di = 0;
    while (di < 29 && dist >= dist_base[di + 1])
    {
        di++;
    }
    bitw.put(w, d_code[di], d_len[di]);
    if (dist_extra[di])
    {
        bitw.put(w, (uint32_t)(dist - dist_base[di]), dist_extra[di]);
    }
}

Rfc1951Ns Rfc1951 = {
    .len_base = len_base,
    .len_extra = len_extra,
    .dist_base = dist_base,
    .dist_extra = dist_extra,
    .reverse_bits = rfc1951_reverse_bits,
    .build_fixed = rfc1951_build_fixed,
    .emit_literal = rfc1951_emit_literal,
    .emit_match = rfc1951_emit_match,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DEFLATE_RFC1951
