// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file deflate.c
 * @brief Bounded RFC 1951 DEFLATE compressor - implementation.
 *
 * Fixed-Huffman (BTYPE=01) encoding with greedy LZ77 matching over a bounded
 * window. Hash chains (head[]/prev[]) locate candidate matches; the chain depth
 * and the window are both capped so the per-byte cost is bounded - deterministic
 * rather than maximal compression, which suits the small messages this serves.
 * The static code tables are generated from the RFC 1951 sec 3.2.6 code lengths
 * (the same lengths inflate's fixed() builds) so the two stay in lock-step.
 *
 * Bits are packed LSB-first into the byte stream; Huffman codes are stored
 * bit-reversed so writing them LSB-first puts them on the wire MSB-first as
 * RFC 1951 sec 3.1.1 requires. All state is the caller's scratch plus the stack.
 */

#include "deflate.h"

#if PC_ENABLE_WS_DEFLATE

#include "mmgr/bitio.h"

#define PC_MIN_MATCH 3   // shortest LZ77 back-reference
#define PC_MAX_MATCH 258 // longest (RFC 1951 length code 285)
#define PC_HASH_BITS 10  // hash table size = 1<<PC_HASH_BITS buckets
#define PC_HASH_SIZE (1 << PC_HASH_BITS)
#define PC_HASH_MASK (PC_HASH_SIZE - 1)
#define PC_WINDOW 512 // max back-reference distance (>= WS_FRAME_SIZE)
#define PC_WIN_MASK (PC_WINDOW - 1)
#define PC_MAX_CHAIN 64 // bounded hash-chain walk per position
#define PC_NONE 0xFFFF  // empty hash slot / chain terminator

// Length code base values and extra bits (RFC 1951 sec 3.2.5), codes 257..285.
static const short LEN_BASE[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

// Distance code base values and extra bits, codes 0..29.
static const short DIST_BASE[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                    33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const short DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                     6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// All working memory deflate_raw() needs, laid over the caller's scratch.
typedef struct
{
    uint16_t head[PC_HASH_SIZE]; // most-recent position for each 3-byte hash
    uint16_t prev[PC_WINDOW];    // previous position with the same hash (chain)
    uint16_t ll_code[288];       // fixed lit/length Huffman codes (bit-reversed)
    uint8_t ll_len[288];         // their lengths in bits
    uint16_t d_code[30];         // fixed distance Huffman codes (bit-reversed)
    uint8_t d_len[30];           // their lengths in bits (all 5)
} Tables;
static_assert(sizeof(Tables) <= DEFLATE_SCRATCH_SIZE, "bump DEFLATE_SCRATCH_SIZE");

// Reverse the low @p len bits of @p code (Huffman codes go on the wire MSB-first).
static uint16_t reverse_bits(uint16_t code, int len)
{
    uint16_t r = 0;
    for (int k = 0; k < len; k++)
    {
        r = (uint16_t)((r << 1) | (code & 1));
        code >>= 1;
    }
    return r;
}

// Build the fixed Huffman code/length tables (RFC 1951 sec 3.2.6) into @p t,
// storing each code bit-reversed so it can be emitted LSB-first.
static void build_fixed(Tables *t)
{
    int sym = 0;
    for (; sym < 144; sym++)
    {
        t->ll_len[sym] = 8;
    }
    for (; sym < 256; sym++)
    {
        t->ll_len[sym] = 9;
    }
    for (; sym < 280; sym++)
    {
        t->ll_len[sym] = 7;
    }
    for (; sym < 288; sym++)
    {
        t->ll_len[sym] = 8;
    }
    for (sym = 0; sym < 30; sym++)
    {
        t->d_len[sym] = 5;
    }

    // Canonical code assignment (RFC 1951 sec 3.2.2) for the lit/length alphabet.
    uint16_t bl_count[16];
    memset(bl_count, 0, sizeof(bl_count));
    for (sym = 0; sym < 288; sym++)
    {
        bl_count[t->ll_len[sym]]++;
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
        int len = t->ll_len[sym];
        t->ll_code[sym] = reverse_bits(next_code[len]++, len);
    }

    // Distance alphabet: 30 codes all of length 5 -> codes 0..29 in order.
    for (sym = 0; sym < 30; sym++)
    {
        t->d_code[sym] = reverse_bits((uint16_t)sym, 5);
    }
}

// 3-byte rolling hash into a PC_HASH_SIZE bucket.
static inline int hash3(const uint8_t *p)
{
    return (int)(((uint32_t)p[0] << 8 ^ (uint32_t)p[1] << 4 ^ (uint32_t)p[2]) & PC_HASH_MASK);
}

// Emit one literal byte via the fixed lit/length code.
static void emit_literal(pc_bit_writer *w, const Tables *t, uint8_t b)
{
    pc_bitw_put(w, t->ll_code[b], t->ll_len[b]);
}

// Emit a (length, distance) back-reference via the fixed code tables.
static void emit_match(pc_bit_writer *w, const Tables *t, int len, int dist)
{
    int li = 0;
    while (li < 28 && len >= LEN_BASE[li + 1])
    {
        li++;
    }
    int lsym = 257 + li;
    pc_bitw_put(w, t->ll_code[lsym], t->ll_len[lsym]);
    if (LEN_EXTRA[li])
    {
        pc_bitw_put(w, (uint32_t)(len - LEN_BASE[li]), LEN_EXTRA[li]);
    }

    int di = 0;
    // di == 29 (loop exit via the left operand) is unreachable: dist is always <=
    // PC_WINDOW (512) here - the chain walk in deflate_raw() discards any candidate
    // farther away - and DIST_BASE[18] is 513, so di never advances past 17.
    while (di < 29 && dist >= DIST_BASE[di + 1])
    {
        di++;
    }
    pc_bitw_put(w, t->d_code[di], t->d_len[di]);
    if (DIST_EXTRA[di])
    {
        pc_bitw_put(w, (uint32_t)(dist - DIST_BASE[di]), DIST_EXTRA[di]);
    }
}

static DeflateResult deflate_raw(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap, size_t *out_len,
                                 void *scratch, size_t scratch_len)
{
    if (scratch_len < DEFLATE_SCRATCH_SIZE)
    {
        return DEFLATE_ERR_SCRATCH;
    }

    Tables *t = (Tables *)scratch;
    build_fixed(t);
    for (int i = 0; i < PC_HASH_SIZE; i++)
    {
        t->head[i] = PC_NONE;
    }

    pc_bit_writer w;
    w.out = dst;
    w.cap = dst_cap;
    w.cnt = 0;
    w.acc = 0;
    w.nbits = 0;
    w.overflow = PROTO_FALSE;

    // One fixed-Huffman block, not final (permessage-deflate streams never set
    // BFINAL): BFINAL=0 (1 bit), BTYPE=01 (2 bits, value 1).
    pc_bitw_put(&w, 0, 1);
    pc_bitw_put(&w, 1, 2);

    size_t i = 0;
    while (i < src_len)
    {
        int best_len = 0;
        int best_dist = 0;

        // Only positions with PC_MIN_MATCH lookahead bytes can start a match.
        if (i + PC_MIN_MATCH <= src_len)
        {
            int h = hash3(src + i);
            uint16_t cand = t->head[h];
            int chain = PC_MAX_CHAIN;
            size_t max_len = src_len - i;
            if (max_len > (size_t)PC_MAX_MATCH)
            {
                max_len = PC_MAX_MATCH;
            }
            while (cand != PC_NONE && chain > 0)
            {
                chain--; // bound the hash-chain walk; decrement here, not in the && (no side effect in the condition)
                size_t dist = i - cand;
                if (dist > (size_t)PC_WINDOW)
                {
                    break; // chain is newest-first; everything past here is farther
                }
                size_t l = 0;
                while (l < max_len && src[cand + l] == src[i + l])
                {
                    l++;
                }
                if ((int)l > best_len)
                {
                    best_len = (int)l;
                    best_dist = (int)dist;
                    if (l >= max_len)
                    {
                        break; // can't beat the lookahead limit
                    }
                }
                cand = t->prev[cand & PC_WIN_MASK];
            }
        }

        size_t advance;
        if (best_len >= PC_MIN_MATCH)
        {
            emit_match(&w, t, best_len, best_dist);
            advance = (size_t)best_len;
        }
        else
        {
            emit_literal(&w, t, src[i]);
            advance = 1;
        }

        // Step over the consumed bytes, inserting each into the hash chains so
        // later positions can reference them (only where PC_MIN_MATCH bytes remain).
        // The byte comparison above always validates a candidate before use, so a
        // stale insert can only cost ratio, never correctness.
        size_t end = i + advance;
        while (i < end)
        {
            if (i + PC_MIN_MATCH <= src_len)
            {
                int h = hash3(src + i);
                t->prev[i & PC_WIN_MASK] = t->head[h];
                t->head[h] = (uint16_t)i;
            }
            i++;
        }
    }

    // End-of-block, then a sync flush: byte-align via an empty stored block and
    // drop its 0x00 0x00 0xff 0xff tail (RFC 7692 sec 7.2.1), leaving a ready
    // permessage-deflate payload.
    pc_bitw_put(&w, t->ll_code[256], t->ll_len[256]); // end-of-block symbol
    pc_bitw_put(&w, 0, 1);                            // BFINAL=0 (empty stored block)
    pc_bitw_put(&w, 0, 2);                            // BTYPE=00 (stored)
    pc_bitw_align(&w);
    static const uint8_t marker[4] = {0x00, 0x00, 0xff, 0xff};
    for (int k = 0; k < 4; k++)
    {
        if (w.cnt >= w.cap)
        {
            w.overflow = PROTO_TRUE;
            break;
        }
        w.out[w.cnt++] = marker[k];
    }

    if (w.overflow)
    {
        return DEFLATE_ERR_OVERFLOW;
    }
    *out_len = w.cnt - 4; // strip the marker for the on-wire payload
    return DEFLATE_OK;
}

const DeflateNs Deflate = {deflate_raw};

#endif // PC_ENABLE_WS_DEFLATE
