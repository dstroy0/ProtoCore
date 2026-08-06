// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_zlib.c
 * @brief SSH server-to-client context-takeover DEFLATE - implementation.
 *
 * A fixed-Huffman (BTYPE=01) greedy-LZ77 compressor, like presentation/deflate, but STREAMING: it
 * keeps a persistent sliding window across packets (context takeover) and wraps the output as a zlib
 * stream (RFC 1950 header once) with a Z_SYNC_FLUSH boundary per packet. Because a new packet's
 * matches may reach back into prior packets' bytes, each call runs LZ77 over [history || input] but
 * only EMITS tokens for the input; the history is inserted into the hash chains first (search only,
 * no output). Hash chains are rebuilt each call over the (possibly slid) history, so a window slide
 * needs no chain relocation. Bits are packed LSB-first; Huffman codes are stored bit-reversed so
 * writing them LSB-first puts them on the wire MSB-first (RFC 1951 sec 3.1.1).
 *
 * The one architectural difference from the WebSocket compressor: it KEEPS the `00 00 ff ff` sync
 * marker on the wire (SSH sends it; permessage-deflate strips it) and prepends the 2-byte zlib
 * header on the first packet. (The LZ77+Huffman core is deliberately a sibling of presentation/
 * deflate rather than a shared instance - the stateful vs stateless split is fundamental; unifying
 * the two behind one parameterized streaming codec is a follow-up for the core review.)
 */

#include "network_drivers/presentation/ssh/transport/ssh_zlib.h"

#if PC_ENABLE_SSH_ZLIB

#include "mmgr/bitio.h"

#define PC_MIN_MATCH 3   // shortest LZ77 back-reference
#define PC_MAX_MATCH 258 // longest (RFC 1951 length code 285)
#define PC_HASH_MASK (SSH_ZLIB_HASH_SIZE - 1)
#define PC_WINDOW PC_SSH_ZLIB_WINDOW // max back-reference distance (power of two)
#define PC_MAX_CHAIN 128             // bounded hash-chain walk per position
#define PC_NONE 0xFFFF               // empty hash slot / chain terminator

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

// Build the fixed Huffman code/length tables (RFC 1951 sec 3.2.6), storing each code bit-reversed so
// it can be emitted LSB-first. Identical assignment to inflate/deflate's fixed() so the two agree.
static void build_fixed(SshDeflate *z)
{
    int sym = 0;
    for (; sym < 144; sym++)
    {
        z->ll_len[sym] = 8;
    }
    for (; sym < 256; sym++)
    {
        z->ll_len[sym] = 9;
    }
    for (; sym < 280; sym++)
    {
        z->ll_len[sym] = 7;
    }
    for (; sym < 288; sym++)
    {
        z->ll_len[sym] = 8;
    }
    for (sym = 0; sym < 30; sym++)
    {
        z->d_len[sym] = 5;
    }

    uint16_t bl_count[16];
    memset(bl_count, 0, sizeof(bl_count));
    for (sym = 0; sym < 288; sym++)
    {
        bl_count[z->ll_len[sym]]++;
    }
    uint16_t next_code[16];
    next_code[0] = 0;
    uint16_t code = 0;
    for (int b = 1; b <= 15; b++)
    {
        code = (uint16_t)((code + bl_count[b - 1]) << 1);
        next_code[b] = code;
    }
    for (sym = 0; sym < 288; sym++)
    {
        int len = z->ll_len[sym];
        z->ll_code[sym] = reverse_bits(next_code[len]++, len);
    }
    for (sym = 0; sym < 30; sym++)
    {
        z->d_code[sym] = reverse_bits((uint16_t)sym, 5);
    }
}

// Emit one raw byte (only valid on a byte boundary: the zlib header and sync marker).
static void put_byte(pc_bit_writer *w, uint8_t b)
{
    if (w->cnt >= w->cap)
    {
        w->overflow = PROTO_TRUE;
        return;
    }
    w->out[w->cnt++] = b;
}

// 3-byte rolling hash into a SSH_ZLIB_HASH_SIZE bucket.
static inline int hash3(const uint8_t *p)
{
    return (int)(((uint32_t)p[0] << 10 ^ (uint32_t)p[1] << 5 ^ (uint32_t)p[2]) & PC_HASH_MASK);
}

static void emit_literal(pc_bit_writer *w, const SshDeflate *z, uint8_t b)
{
    pc_bitw_put(w, z->ll_code[b], z->ll_len[b]);
}

static void emit_match(pc_bit_writer *w, const SshDeflate *z, int len, int dist)
{
    int li = 0;
    while (li < 28 && len >= LEN_BASE[li + 1])
    {
        li++;
    }
    int lsym = 257 + li;
    pc_bitw_put(w, z->ll_code[lsym], z->ll_len[lsym]);
    if (LEN_EXTRA[li])
    {
        pc_bitw_put(w, (uint32_t)(len - LEN_BASE[li]), LEN_EXTRA[li]);
    }

    int di = 0;
    while (di < 29 && dist >= DIST_BASE[di + 1])
    {
        di++; // zlib_chain_match caps dist to PC_WINDOW (8192) < DIST_BASE[26] (8193), so di never passes 25
    }
    pc_bitw_put(w, z->d_code[di], z->d_len[di]);
    if (DIST_EXTRA[di])
    {
        pc_bitw_put(w, (uint32_t)(dist - DIST_BASE[di]), DIST_EXTRA[di]);
    }
}

// Walk the hash chain from cand (newest-first) for the longest match of buf[i..] within PC_WINDOW.
// Writes the best match length/distance found (both 0 if none).
static void zlib_chain_match(const SshDeflate *z, const uint8_t *buf, size_t i, uint16_t cand, int chain,
                             size_t max_len, int *best_len, int *best_dist)
{
    *best_len = 0;
    *best_dist = 0;
    while (cand != PC_NONE && chain > 0)
    {
        chain--;
        size_t dist = i - cand;
        if (dist > (size_t)PC_WINDOW)
        {
            break; // chain is newest-first; everything past here is farther
        }
        size_t l = 0;
        while (l < max_len && buf[cand + l] == buf[i + l])
        {
            l++;
        }
        if ((int)l > *best_len)
        {
            *best_len = (int)l;
            *best_dist = (int)dist;
            if (l >= max_len)
            {
                break;
            }
        }
        cand = z->prev[cand];
    }
}

static void ssh_deflate_init(SshDeflate *z, uint8_t *work, uint16_t *head, uint16_t *prev, uint16_t *ll_code,
                             uint8_t *ll_len, uint16_t *d_code, uint8_t *d_len)
{
    z->work = work;
    z->head = head;
    z->prev = prev;
    z->ll_code = ll_code;
    z->ll_len = ll_len;
    z->d_code = d_code;
    z->d_len = d_len;
    z->hist = 0;
    z->header_sent = PROTO_FALSE;
    build_fixed(z);
}

static int ssh_deflate_packet(SshDeflate *z, const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap,
                              size_t *out_len)
{
    if (src_len > (size_t)PC_SSH_ZLIB_MAX_IN)
    {
        return -1;
    }

    // Lay [history || input] out contiguously; matches for the input may reach back into history.
    size_t hist = z->hist;
    if (hist + src_len > SSH_ZLIB_WORK_SIZE)
    {
        return -1; // sizing invariant (hist <= PC_WINDOW, src_len <= MAX_IN) should prevent this
    }
    memcpy(z->work + hist, src, src_len);
    size_t total = hist + src_len;
    const uint8_t *buf = z->work;

    // Rebuild the hash over the history (search-only) so input matches can reference it.
    for (int b = 0; b < SSH_ZLIB_HASH_SIZE; b++)
    {
        z->head[b] = PC_NONE;
    }
    for (size_t p = 0; p + PC_MIN_MATCH <= total && p < hist; p++)
    {
        int h = hash3(buf + p);
        z->prev[p] = z->head[h];
        z->head[h] = (uint16_t)p;
    }

    pc_bit_writer w;
    w.out = dst;
    w.cap = dst_cap;
    w.cnt = 0;
    w.acc = 0;
    w.nbits = 0;
    w.overflow = PROTO_FALSE;

    // RFC 1950 zlib header, once at stream start: CMF=0x78 (deflate, 32 KB window), FLG=0x9C
    // (default level, FCHECK making 0x789C divisible by 31). Byte-aligned, before any deflate bits.
    if (!z->header_sent)
    {
        put_byte(&w, 0x78);
        put_byte(&w, 0x9C);
        z->header_sent = PROTO_TRUE;
    }

    // One fixed-Huffman block, not final: BFINAL=0 (1 bit), BTYPE=01 (2 bits, value 1).
    pc_bitw_put(&w, 0, 1);
    pc_bitw_put(&w, 1, 2);

    size_t i = hist; // emit tokens only for the new input
    while (i < total)
    {
        int best_len = 0;
        int best_dist = 0;

        if (i + PC_MIN_MATCH <= total)
        {
            int h = hash3(buf + i);
            uint16_t cand = z->head[h];
            size_t max_len = total - i;
            if (max_len > (size_t)PC_MAX_MATCH)
            {
                max_len = PC_MAX_MATCH;
            }
            zlib_chain_match(z, buf, i, cand, PC_MAX_CHAIN, max_len, &best_len, &best_dist);
        }

        size_t advance;
        if (best_len >= PC_MIN_MATCH)
        {
            emit_match(&w, z, best_len, best_dist);
            advance = (size_t)best_len;
        }
        else
        {
            emit_literal(&w, z, buf[i]);
            advance = 1;
        }

        // Insert each consumed position into the hash chains for later matches. The byte comparison
        // above validates every candidate, so a stale insert can only cost ratio, never correctness.
        size_t end = i + advance;
        while (i < end)
        {
            if (i + PC_MIN_MATCH <= total)
            {
                int h = hash3(buf + i);
                z->prev[i] = z->head[h];
                z->head[h] = (uint16_t)i;
            }
            i++;
        }
    }

    // End-of-block, then a Z_SYNC_FLUSH: byte-align via an empty stored block and KEEP its
    // 0x00 0x00 0xff 0xff tail on the wire (SSH sends it; a zlib inflate() flushes at the boundary).
    pc_bitw_put(&w, z->ll_code[256], z->ll_len[256]); // end-of-block symbol
    pc_bitw_put(&w, 0, 1);                            // BFINAL=0 (empty stored block)
    pc_bitw_put(&w, 0, 2);                            // BTYPE=00 (stored)
    pc_bitw_align(&w);
    put_byte(&w, 0x00);
    put_byte(&w, 0x00);
    put_byte(&w, 0xff);
    put_byte(&w, 0xff);

    if (w.overflow)
    {
        return -1;
    }

    // Slide the window: keep the last PC_WINDOW bytes of [history || input] as history for next time.
    size_t keep = total;
    if (keep > (size_t)PC_WINDOW)
    {
        keep = PC_WINDOW;
    }
    if (keep < total)
    {
        memmove(z->work, z->work + total - keep, keep);
    }
    z->hist = keep;

    *out_len = w.cnt;
    return 0;
}

const SshDeflateNs SshDeflater = {ssh_deflate_init, ssh_deflate_packet};

#endif // PC_ENABLE_SSH_ZLIB
