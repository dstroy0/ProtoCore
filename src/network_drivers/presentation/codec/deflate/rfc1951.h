// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rfc1951.h
 * @brief The RFC 1951 sec 3.2.5 length and distance tables, as one namespace.
 *
 * The four tables are defined once in rfc1951.c and reached through a pointer to that instance. The
 * DEFLATE encoder and decoder (codec/deflate, codec/inflate) and the SSH zlib@openssh.com stream
 * codecs (ssh/transport/ssh_zlib, ssh/transport/ssh_inflate) read them.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_RFC1951_H
#define PROTOCORE_RFC1951_H

#include "mmgr/bitio.h"    // protocore_bit_writer - what the emitters write through
#include "mmgr/protomem.h" // mem.set - the byte mover

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

/**
 * @brief The length and distance tables of RFC 1951 sec 3.2.5.
 *
 * @var Rfc1951Ns::len_base   29 entries: base length for codes 257..285
 * @var Rfc1951Ns::len_extra  29 entries: extra bits read after each of those codes
 * @var Rfc1951Ns::dist_base  30 entries: base distance for codes 0..29
 * @var Rfc1951Ns::dist_extra 30 entries: extra bits read after each of those codes
 */
typedef struct
{
    const short *len_base;
    const short *len_extra;
    const short *dist_base;
    const short *dist_extra;
} Rfc1951Ns;

/** @brief The one instance. */
const Rfc1951Ns *protocore_rfc1951(void);

/** @brief Reader shorthand: RFC1951->len_base[code]. */
#define RFC1951 (protocore_rfc1951())

/** @brief Reverse the low @p len bits of @p code (a Huffman code goes on the wire MSB-first). */
PROTOCORE_INLINE uint16_t protocore_rfc1951_reverse_bits(uint16_t code, int len)
{
    uint16_t r = 0;
    for (int k = 0; k < len; k++)
    {
        r = (uint16_t)((r << 1) | (code & 1));
        code >>= 1;
    }
    return r;
}

/**
 * @brief Build the fixed Huffman code/length tables (RFC 1951 sec 3.2.6), each code bit-reversed.
 *
 * @p ll_code / @p ll_len hold 288 entries, @p d_code / @p d_len hold 30.
 */
PROTOCORE_INLINE void protocore_rfc1951_build_fixed(uint16_t *ll_code, uint8_t *ll_len, uint16_t *d_code,
                                                    uint8_t *d_len)
{
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
        ll_code[sym] = protocore_rfc1951_reverse_bits(next_code[len], len);
        next_code[len]++;
    }

    // Distance alphabet: 30 codes all of length 5 -> codes 0..29 in order.
    for (sym = 0; sym < 30; sym++)
    {
        d_code[sym] = protocore_rfc1951_reverse_bits((uint16_t)sym, 5);
    }
}

/** @brief Emit one literal byte through the fixed lit/length code. */
PROTOCORE_INLINE void protocore_rfc1951_emit_literal(protocore_bit_writer *w, const uint16_t *ll_code,
                                                     const uint8_t *ll_len, uint8_t b)
{
    bitw.put(w, ll_code[b], ll_len[b]);
}

/** @brief Emit a (@p len, @p dist) back-reference through the fixed code tables (RFC 1951 sec 3.2.5). */
PROTOCORE_INLINE void protocore_rfc1951_emit_match(protocore_bit_writer *w, const uint16_t *ll_code,
                                                   const uint8_t *ll_len, const uint16_t *d_code, const uint8_t *d_len,
                                                   int len, int dist)
{
    const Rfc1951Ns *r = protocore_rfc1951();
    int li = 0;
    while (li < 28 && len >= r->len_base[li + 1])
    {
        li++;
    }
    int lsym = 257 + li;
    bitw.put(w, ll_code[lsym], ll_len[lsym]);
    if (r->len_extra[li])
    {
        bitw.put(w, (uint32_t)(len - r->len_base[li]), r->len_extra[li]);
    }

    int di = 0;
    while (di < 29 && dist >= r->dist_base[di + 1])
    {
        di++;
    }
    bitw.put(w, d_code[di], d_len[di]);
    if (r->dist_extra[di])
    {
        bitw.put(w, (uint32_t)(dist - r->dist_base[di]), r->dist_extra[di]);
    }
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_RFC1951_H
