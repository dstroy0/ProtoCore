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
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_WS_DEFLATE

#include "mmgr/bitio.h"
#include "network_drivers/presentation/codec/deflate/rfc1951.h" // RFC1951: the sec 3.2.5 tables

#define PROTOCORE_MIN_MATCH 3   // shortest LZ77 back-reference
#define PROTOCORE_MAX_MATCH 258 // longest (RFC 1951 length code 285)
#define PROTOCORE_HASH_BITS 10  // hash table size = 1<<PROTOCORE_HASH_BITS buckets
#define PROTOCORE_HASH_SIZE (1 << PROTOCORE_HASH_BITS)
#define PROTOCORE_HASH_MASK (PROTOCORE_HASH_SIZE - 1)
#define PROTOCORE_WINDOW 512 // max back-reference distance (>= WS_FRAME_SIZE)
#define PROTOCORE_WIN_MASK (PROTOCORE_WINDOW - 1)
#define PROTOCORE_MAX_CHAIN 64 // bounded hash-chain walk per position
#define PROTOCORE_NONE 0xFFFF  // empty hash slot / chain terminator

// All working memory deflate_raw() needs, laid over the caller's scratch.
typedef struct
{
    uint16_t head[PROTOCORE_HASH_SIZE]; // most-recent position for each 3-byte hash
    uint16_t prev[PROTOCORE_WINDOW];    // previous position with the same hash (chain)
    uint16_t ll_code[288];              // fixed lit/length Huffman codes (bit-reversed)
    uint8_t ll_len[288];                // their lengths in bits
    uint16_t d_code[30];                // fixed distance Huffman codes (bit-reversed)
    uint8_t d_len[30];                  // their lengths in bits (all 5)
} Tables;
static_assert(sizeof(Tables) <= DEFLATE_SCRATCH_SIZE, "bump DEFLATE_SCRATCH_SIZE");

// 3-byte rolling hash into a PROTOCORE_HASH_SIZE bucket.
static inline int hash3(const uint8_t *p)
{
    return (int)(((uint32_t)p[0] << 8 ^ (uint32_t)p[1] << 4 ^ (uint32_t)p[2]) & PROTOCORE_HASH_MASK);
}

static DeflateResult deflate_raw(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap, size_t *out_len,
                                 void *scratch, size_t scratch_len)
{
    if (scratch_len < DEFLATE_SCRATCH_SIZE)
    {
        return DEFLATE_ERR_SCRATCH;
    }

    Tables *t = (Tables *)scratch;
    protocore_rfc1951_build_fixed(t->ll_code, t->ll_len, t->d_code, t->d_len);
    for (int i = 0; i < PROTOCORE_HASH_SIZE; i++)
    {
        t->head[i] = PROTOCORE_NONE;
    }

    protocore_bit_writer w;
    w.out = dst;
    w.cap = dst_cap;
    w.cnt = 0;
    w.acc = 0;
    w.nbits = 0;
    w.overflow = PROTO_FALSE;

    // One fixed-Huffman block, not final (permessage-deflate streams never set
    // BFINAL): BFINAL=0 (1 bit), BTYPE=01 (2 bits, value 1).
    bitw.put(&w, 0, 1);
    bitw.put(&w, 1, 2);

    size_t i = 0;
    while (i < src_len)
    {
        int best_len = 0;
        int best_dist = 0;

        // Only positions with PROTOCORE_MIN_MATCH lookahead bytes can start a match.
        if (i + PROTOCORE_MIN_MATCH <= src_len)
        {
            int h = hash3(src + i);
            uint16_t cand = t->head[h];
            int chain = PROTOCORE_MAX_CHAIN;
            size_t max_len = src_len - i;
            if (max_len > (size_t)PROTOCORE_MAX_MATCH)
            {
                max_len = PROTOCORE_MAX_MATCH;
            }
            while (cand != PROTOCORE_NONE && chain > 0)
            {
                chain--; // bound the hash-chain walk; decrement here, not in the && (no side effect in the condition)
                size_t dist = i - cand;
                if (dist > (size_t)PROTOCORE_WINDOW)
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
                cand = t->prev[cand & PROTOCORE_WIN_MASK];
            }
        }

        size_t advance;
        if (best_len >= PROTOCORE_MIN_MATCH)
        {
            protocore_rfc1951_emit_match(&w, t->ll_code, t->ll_len, t->d_code, t->d_len, best_len, best_dist);
            advance = (size_t)best_len;
        }
        else
        {
            protocore_rfc1951_emit_literal(&w, t->ll_code, t->ll_len, src[i]);
            advance = 1;
        }

        // Step over the consumed bytes, inserting each into the hash chains so
        // later positions can reference them (only where PROTOCORE_MIN_MATCH bytes remain).
        // The byte comparison above always validates a candidate before use, so a
        // stale insert can only cost ratio, never correctness.
        size_t end = i + advance;
        while (i < end)
        {
            if (i + PROTOCORE_MIN_MATCH <= src_len)
            {
                int h = hash3(src + i);
                t->prev[i & PROTOCORE_WIN_MASK] = t->head[h];
                t->head[h] = (uint16_t)i;
            }
            i++;
        }
    }

    // End-of-block, then a sync flush: byte-align via an empty stored block and
    // drop its 0x00 0x00 0xff 0xff tail (RFC 7692 sec 7.2.1), leaving a ready
    // permessage-deflate payload.
    bitw.put(&w, t->ll_code[256], t->ll_len[256]); // end-of-block symbol
    bitw.put(&w, 0, 1);                            // BFINAL=0 (empty stored block)
    bitw.put(&w, 0, 2);                            // BTYPE=00 (stored)
    bitw.align(&w);
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

#endif // PROTOCORE_ENABLE_WS_DEFLATE
