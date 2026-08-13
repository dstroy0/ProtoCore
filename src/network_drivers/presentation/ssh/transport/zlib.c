// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file zlib.c
 * @brief RFC 1951 deflate: fixed-Huffman blocks and the SSH partial flush.
 */

#include "network_drivers/presentation/ssh/transport/zlib.h"
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_SSH_ZLIB

#include "mmgr/bitio.h"
#include "network_drivers/presentation/codec/deflate/rfc1951.h" // the sec 3.2.5 tables

#define PROTOCORE_MIN_MATCH 3   // shortest LZ77 back-reference
#define PROTOCORE_MAX_MATCH 258 // longest (RFC 1951 length code 285)
#define PROTOCORE_HASH_MASK (SSH_ZLIB_HASH_SIZE - 1)
#define PROTOCORE_WINDOW PROTOCORE_SSH_ZLIB_WINDOW // max back-reference distance (power of two)
#define PROTOCORE_MAX_CHAIN 128                    // bounded hash-chain walk per position
#define PROTOCORE_NONE 0xFFFF                      // empty hash slot / chain terminator

// Emit one raw byte (only valid on a byte boundary: the zlib header and sync marker).
static void put_byte(protocore_bit_writer *w, uint8_t b)
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
    return (int)(((uint32_t)p[0] << 10 ^ (uint32_t)p[1] << 5 ^ (uint32_t)p[2]) & PROTOCORE_HASH_MASK);
}

// Walk the hash chain from cand (newest-first) for the longest match of buf[i..] within PROTOCORE_WINDOW.
// Writes the best match length/distance found (both 0 if none).
static void zlib_chain_match(const SshDeflate *z, const uint8_t *buf, size_t i, uint16_t cand, int chain,
                             size_t max_len, int *best_len, int *best_dist)
{
    *best_len = 0;
    *best_dist = 0;
    while (cand != PROTOCORE_NONE && chain > 0)
    {
        chain--;
        size_t dist = i - cand;
        if (dist > (size_t)PROTOCORE_WINDOW)
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

void ssh_deflate_init(SshDeflate *z, uint8_t *work, uint16_t *head, uint16_t *prev, uint16_t *ll_code, uint8_t *ll_len,
                      uint16_t *d_code, uint8_t *d_len)
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
    protocore_rfc1951_build_fixed(z->ll_code, z->ll_len, z->d_code, z->d_len);
}

int ssh_deflate_packet(SshDeflate *z, const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap, size_t *out_len)
{
    if (src_len > (size_t)PROTOCORE_SSH_ZLIB_MAX_IN)
    {
        return -1;
    }

    // Lay [history || input] out contiguously; matches for the input may reach back into history.
    size_t hist = z->hist;
    if (hist + src_len > SSH_ZLIB_WORK_SIZE)
    {
        return -1; // sizing invariant (hist <= PROTOCORE_WINDOW, src_len <= MAX_IN) should prevent this
    }
    mem.cpy(z->work + hist, src, src_len);
    size_t total = hist + src_len;
    const uint8_t *buf = z->work;

    // Rebuild the hash over the history (search-only) so input matches can reference it.
    for (int b = 0; b < SSH_ZLIB_HASH_SIZE; b++)
    {
        z->head[b] = PROTOCORE_NONE;
    }
    for (size_t p = 0; p + PROTOCORE_MIN_MATCH <= total && p < hist; p++)
    {
        int h = hash3(buf + p);
        z->prev[p] = z->head[h];
        z->head[h] = (uint16_t)p;
    }

    protocore_bit_writer w;
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
    protocore_bitw_put(&w, 0, 1);
    protocore_bitw_put(&w, 1, 2);

    size_t i = hist; // emit tokens only for the new input
    while (i < total)
    {
        int best_len = 0;
        int best_dist = 0;

        if (i + PROTOCORE_MIN_MATCH <= total)
        {
            int h = hash3(buf + i);
            uint16_t cand = z->head[h];
            size_t max_len = total - i;
            if (max_len > (size_t)PROTOCORE_MAX_MATCH)
            {
                max_len = PROTOCORE_MAX_MATCH;
            }
            zlib_chain_match(z, buf, i, cand, PROTOCORE_MAX_CHAIN, max_len, &best_len, &best_dist);
        }

        size_t advance;
        if (best_len >= PROTOCORE_MIN_MATCH)
        {
            protocore_rfc1951_emit_match(&w, z->ll_code, z->ll_len, z->d_code, z->d_len, best_len, best_dist);
            advance = (size_t)best_len;
        }
        else
        {
            protocore_rfc1951_emit_literal(&w, z->ll_code, z->ll_len, buf[i]);
            advance = 1;
        }

        // Insert each consumed position into the hash chains for later matches. The byte comparison
        // above validates every candidate, so a stale insert can only cost ratio, never correctness.
        size_t end = i + advance;
        while (i < end)
        {
            if (i + PROTOCORE_MIN_MATCH <= total)
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
    protocore_bitw_put(&w, z->ll_code[256], z->ll_len[256]); // end-of-block symbol
    protocore_bitw_put(&w, 0, 1);                            // BFINAL=0 (empty stored block)
    protocore_bitw_put(&w, 0, 2);                            // BTYPE=00 (stored)
    protocore_bitw_align(&w);
    put_byte(&w, 0x00);
    put_byte(&w, 0x00);
    put_byte(&w, 0xff);
    put_byte(&w, 0xff);

    if (w.overflow)
    {
        return -1;
    }

    // Slide the window: keep the last PROTOCORE_WINDOW bytes of [history || input] as history for next time.
    size_t keep = total;
    if (keep > (size_t)PROTOCORE_WINDOW)
    {
        keep = PROTOCORE_WINDOW;
    }
    if (keep < total)
    {
        mem.move(z->work, z->work + total - keep, keep);
    }
    z->hist = keep;

    *out_len = w.cnt;
    return 0;
}

#endif // PROTOCORE_ENABLE_SSH_ZLIB
