// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_inflate.c
 * @brief SSH client-to-server resumable, context-takeover INFLATE - implementation.
 *
 * A canonical-Huffman DEFLATE decoder (RFC 1951), like the one-shot presentation/inflate but built to
 * resume across packets: it reads the logical stream (carried tail bytes ++ this packet) bit by bit,
 * decodes only *complete* blocks, and on running out of input rolls the in-progress block back to its
 * boundary - so no mid-block state persists. Output goes to the caller buffer and into a persistent
 * 32 KB circular window that satisfies back-references reaching into earlier packets.
 */

#include "network_drivers/presentation/ssh/transport/ssh_inflate.h"

#if PC_ENABLE_SSH_ZLIB

#define PC_MAXBITS 15
#define PC_MAXLCODES 288
#define PC_MAXDCODES 32

// Block decode outcomes.
#define PC_BLK_OK 0   // a whole block decoded
#define PC_BLK_NEED 1 // ran out of input mid-block (roll back to the boundary, wait for more)
#define PC_BLK_ERR 2  // malformed stream or output overflow

// Length / distance base + extra-bit tables (RFC 1951 sec 3.2.5), codes 257..285 and 0..29.
static const short LEN_BASE[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const short DIST_BASE[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                    33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const short DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                     6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// Huffman table scratch (stack-resident per call; the decoder rebuilds it per block since no block
// state persists across packets).
typedef struct
{
    short lcount[PC_MAXBITS + 1];
    short lsym[PC_MAXLCODES];
    short dcount[PC_MAXBITS + 1];
    short dsym[PC_MAXDCODES];
    short lengths[PC_MAXLCODES + PC_MAXDCODES];
} Tables;

typedef struct
{
    short *count;
    short *symbol;
} Huffman;

// Bit reader over the two-segment logical stream (carried tail ++ this packet), LSB first.
typedef struct
{
    const uint8_t *seg0;
    size_t n0;
    const uint8_t *seg1;
    size_t n1;
    size_t bitpos; // current bit offset into the logical stream
    size_t nbits;  // total bits available
    proto_bool underflow;
} BitIn;

static inline uint8_t logical_byte(const uint8_t *seg0, size_t n0, const uint8_t *seg1, size_t i)
{
    return i < n0 ? seg0[i] : seg1[i - n0];
}

// Pull @p need bits (LSB first). On end-of-input sets underflow and returns what it has.
static int getbits(BitIn *b, int need)
{
    int v = 0;
    for (int i = 0; i < need; i++)
    {
        if (b->bitpos >= b->nbits)
        {
            b->underflow = PROTO_TRUE;
            return v;
        }
        size_t byte = b->bitpos >> 3;
        int bit = (int)(b->bitpos & 7u);
        v |= ((logical_byte(b->seg0, b->n0, b->seg1, byte) >> bit) & 1) << i;
        b->bitpos++;
    }
    return v;
}

// Decode one canonical-Huffman symbol; -1 on an invalid code, underflow flagged on end-of-input.
static int hdecode(BitIn *b, const Huffman *h)
{
    int code = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len <= PC_MAXBITS; len++)
    {
        code |= getbits(b, 1);
        if (b->underflow)
        {
            return -1;
        }
        int count = h->count[len];
        if (code - count < first)
        {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

// Build a canonical-Huffman table from code lengths. 0 = complete, >0 = incomplete, <0 = over-subscribed.
static int construct(Huffman *h, const short *lengths, int n)
{
    for (int len = 0; len <= PC_MAXBITS; len++)
    {
        h->count[len] = 0;
    }
    for (int sym = 0; sym < n; sym++)
    {
        h->count[lengths[sym]]++;
    }
    if (h->count[0] == n)
    {
        return 0;
    }
    int left = 1;
    for (int len = 1; len <= PC_MAXBITS; len++)
    {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
        {
            return left;
        }
    }
    short offs[PC_MAXBITS + 1];
    offs[1] = 0;
    for (int len = 1; len < PC_MAXBITS; len++)
    {
        offs[len + 1] = offs[len] + h->count[len];
    }
    for (int sym = 0; sym < n; sym++)
    {
        if (lengths[sym] != 0)
        {
            h->symbol[offs[lengths[sym]]++] = (short)sym;
        }
    }
    return left;
}

// Output one byte to the caller buffer and the circular window; flags overflow when dst is full.
typedef struct
{
    uint8_t *dst;
    size_t cap;
    size_t cnt;
    SshInflate *z;
    proto_bool overflow;
} OutCtx;

static void put_byte(OutCtx *o, uint8_t byte)
{
    if (o->cnt >= o->cap)
    {
        o->overflow = PROTO_TRUE;
        return;
    }
    o->dst[o->cnt++] = byte;
    o->z->window[o->z->wpos] = byte;
    o->z->wpos = (o->z->wpos + 1u) & (SSH_INFLATE_WINDOW - 1u);
    if (o->z->whist < SSH_INFLATE_WINDOW)
    {
        o->z->whist++;
    }
}

// Literal/length + distance decode into the output/window. PC_BLK_OK on end-of-block.
static int do_codes(BitIn *b, OutCtx *o, const Huffman *lc, const Huffman *dc)
{
    for (;;)
    {
        int sym = hdecode(b, lc);
        if (b->underflow)
        {
            return PC_BLK_NEED;
        }
        if (sym < 0)
        {
            return PC_BLK_ERR;
        }
        if (sym == 256)
        {
            return PC_BLK_OK; // end of block
        }
        if (sym < 256)
        {
            put_byte(o, (uint8_t)sym);
            if (o->overflow)
            {
                return PC_BLK_ERR;
            }
            continue;
        }
        sym -= 257;
        if (sym >= 29)
        {
            return PC_BLK_ERR;
        }
        int len = LEN_BASE[sym] + getbits(b, LEN_EXTRA[sym]);
        if (b->underflow)
        {
            return PC_BLK_NEED;
        }
        int dsym = hdecode(b, dc);
        if (b->underflow)
        {
            return PC_BLK_NEED;
        }
        if (dsym < 0 || dsym >= 30)
        {
            return PC_BLK_ERR;
        }
        size_t dist = (size_t)(DIST_BASE[dsym] + getbits(b, DIST_EXTRA[dsym]));
        if (b->underflow)
        {
            return PC_BLK_NEED;
        }
        if (dist == 0 || dist > o->z->whist)
        {
            return PC_BLK_ERR; // back-reference before the start of the window history
        }
        for (int k = 0; k < len; k++)
        {
            uint32_t idx = (o->z->wpos - (uint32_t)dist) & (SSH_INFLATE_WINDOW - 1u);
            put_byte(o, o->z->window[idx]);
            if (o->overflow)
            {
                return PC_BLK_ERR;
            }
        }
    }
}

// Uncompressed (stored) block: align to the next byte, read LEN/NLEN, copy LEN literal bytes.
static int do_stored(BitIn *b, OutCtx *o)
{
    if (b->bitpos & 7u)
    {
        b->bitpos = (b->bitpos + 7u) & ~(size_t)7u; // discard bits to the byte boundary
    }
    if (b->bitpos + 32u > b->nbits)
    {
        return PC_BLK_NEED;
    }
    int len = getbits(b, 16);
    int nlen = getbits(b, 16);
    if ((len ^ nlen) != 0xFFFF)
    {
        return PC_BLK_ERR;
    }
    if (b->bitpos + (size_t)len * 8u > b->nbits)
    {
        return PC_BLK_NEED;
    }
    for (int k = 0; k < len; k++)
    {
        put_byte(o, (uint8_t)getbits(b, 8));
        if (o->overflow)
        {
            return PC_BLK_ERR;
        }
    }
    return PC_BLK_OK;
}

// Fixed-Huffman block (RFC 1951 sec 3.2.6).
static int do_fixed(BitIn *b, OutCtx *o, Tables *t)
{
    Huffman lc = {t->lcount, t->lsym};
    Huffman dc = {t->dcount, t->dsym};
    int sym = 0;
    for (; sym < 144; sym++)
    {
        t->lengths[sym] = 8;
    }
    for (; sym < 256; sym++)
    {
        t->lengths[sym] = 9;
    }
    for (; sym < 280; sym++)
    {
        t->lengths[sym] = 7;
    }
    for (; sym < 288; sym++)
    {
        t->lengths[sym] = 8;
    }
    construct(&lc, t->lengths, 288);
    for (sym = 0; sym < 30; sym++)
    {
        t->lengths[sym] = 5;
    }
    construct(&dc, t->lengths, 30);
    return do_codes(b, o, &lc, &dc);
}

// Dynamic-Huffman block (RFC 1951 sec 3.2.7). Because a NEED rolls the whole block back and re-decodes
// it next packet, the tables are rebuilt from scratch here every attempt - no partial state is kept.
static int do_dynamic(BitIn *b, OutCtx *o, Tables *t)
{
    static const short ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    Huffman lc = {t->lcount, t->lsym};
    Huffman dc = {t->dcount, t->dsym};

    int nlen = getbits(b, 5) + 257;
    int ndist = getbits(b, 5) + 1;
    int ncode = getbits(b, 4) + 4;
    if (b->underflow)
    {
        return PC_BLK_NEED;
    }
    if (nlen > PC_MAXLCODES || ndist > PC_MAXDCODES)
    {
        return PC_BLK_ERR;
    }

    int index;
    for (index = 0; index < ncode; index++)
    {
        t->lengths[ORDER[index]] = (short)getbits(b, 3);
    }
    if (b->underflow)
    {
        return PC_BLK_NEED;
    }
    for (; index < 19; index++)
    {
        t->lengths[ORDER[index]] = 0;
    }
    if (construct(&lc, t->lengths, 19) != 0)
    {
        return PC_BLK_ERR; // the code-length code must be complete
    }

    index = 0;
    while (index < nlen + ndist)
    {
        int symbol = hdecode(b, &lc);
        if (b->underflow)
        {
            return PC_BLK_NEED;
        }
        if (symbol < 0)
        {
            return PC_BLK_ERR;
        }
        if (symbol < 16)
        {
            t->lengths[index++] = (short)symbol;
            continue;
        }
        int repeat_len = 0;
        int repeat;
        if (symbol == 16)
        {
            if (index == 0)
            {
                return PC_BLK_ERR;
            }
            repeat_len = t->lengths[index - 1];
            repeat = 3 + getbits(b, 2);
        }
        else if (symbol == 17)
        {
            repeat = 3 + getbits(b, 3);
        }
        else
        {
            repeat = 11 + getbits(b, 7);
        }
        if (b->underflow)
        {
            return PC_BLK_NEED;
        }
        if (index + repeat > nlen + ndist)
        {
            return PC_BLK_ERR;
        }
        while (repeat--)
        {
            t->lengths[index++] = (short)repeat_len;
        }
    }
    if (t->lengths[256] == 0)
    {
        return PC_BLK_ERR; // no end-of-block code
    }

    int err = construct(&lc, t->lengths, nlen);
    if (err && (err < 0 || nlen != lc.count[0] + lc.count[1]))
    {
        return PC_BLK_ERR;
    }
    err = construct(&dc, t->lengths + nlen, ndist);
    if (err && (err < 0 || ndist != dc.count[0] + dc.count[1]))
    {
        return PC_BLK_ERR;
    }
    return do_codes(b, o, &lc, &dc);
}

// Decode one whole block (header + body). PC_BLK_NEED restores nothing itself; the caller rolls back.
static int do_block(BitIn *b, OutCtx *o, Tables *t)
{
    getbits(b, 1); // BFINAL: SSH streams flush rather than finish, so it is not acted on
    int type = getbits(b, 2);
    if (b->underflow)
    {
        return PC_BLK_NEED;
    }
    if (type == 0)
    {
        return do_stored(b, o);
    }
    if (type == 1)
    {
        return do_fixed(b, o, t);
    }
    if (type == 2)
    {
        return do_dynamic(b, o, t);
    }
    return PC_BLK_ERR; // type 3 is reserved
}

void ssh_inflate_init(SshInflate *z, uint8_t *window)
{
    z->window = window;
    z->wpos = 0;
    z->whist = 0;
    z->carry_len = 0;
    z->bit_off = 0;
    z->header_seen = PROTO_FALSE;
}

int ssh_inflate_packet(SshInflate *z, const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap, size_t *out_len)
{
    if (!z || (src_len && !src) || !out_len)
    {
        return -1;
    }

    BitIn b;
    b.seg0 = z->carry;
    b.n0 = z->carry_len;
    b.seg1 = src;
    b.n1 = src_len;
    b.nbits = ((size_t)z->carry_len + src_len) * 8u;
    b.bitpos = z->bit_off;
    b.underflow = PROTO_FALSE;

    OutCtx o;
    o.dst = dst;
    o.cap = dst_cap;
    o.cnt = 0;
    o.z = z;
    o.overflow = PROTO_FALSE;

    // RFC 1950 zlib header, once per stream: CM=8 (deflate) + the mod-31 check. CINFO (the peer's
    // window size) is not enforced - we always carry the full 32 KB window, which covers any CINFO.
    if (!z->header_seen)
    {
        if (b.nbits - b.bitpos < 16u)
        {
            // Fewer than the 2 header bytes arrived; carry them whole and wait for more.
            size_t rem = (size_t)z->carry_len + src_len;
            if (rem > SSH_INFLATE_CARRY)
            {
                return -1;
            }
            uint8_t tmp[SSH_INFLATE_CARRY];
            for (size_t i = 0; i < rem; i++)
            {
                tmp[i] = logical_byte(z->carry, z->carry_len, src, i);
            }
            memcpy(z->carry, tmp, rem);
            z->carry_len = (uint8_t)rem;
            z->bit_off = 0;
            *out_len = 0;
            return 0;
        }
        int cmf = getbits(&b, 8);
        int flg = getbits(&b, 8);
        if ((cmf & 0x0F) != 8)
        {
            return -1; // compression method must be DEFLATE
        }
        if ((((unsigned)cmf << 8) | (unsigned)flg) % 31u != 0u)
        {
            return -1; // header checksum
        }
        if (flg & 0x20)
        {
            return -1; // a preset dictionary (FDICT) is not used by SSH
        }
        z->header_seen = PROTO_TRUE;
    }

    Tables tbl;
    size_t boundary = b.bitpos; // bit position of the last decoded block boundary

    for (;;)
    {
        if (b.bitpos >= b.nbits)
        {
            break; // input exhausted exactly at a boundary
        }
        size_t cp_bit = b.bitpos;
        size_t cp_cnt = o.cnt;
        uint32_t cp_wpos = z->wpos;
        uint32_t cp_whist = z->whist;

        int st = do_block(&b, &o, &tbl);
        if (st == PC_BLK_OK)
        {
            boundary = b.bitpos;
            continue;
        }
        if (st == PC_BLK_NEED)
        {
            // Roll the incomplete block back to its boundary and stop; it re-decodes next packet.
            b.bitpos = cp_bit;
            o.cnt = cp_cnt;
            z->wpos = cp_wpos;
            z->whist = cp_whist;
            boundary = cp_bit;
            break;
        }
        return -1; // PC_BLK_ERR
    }

    // Carry the bytes from the last boundary onward (the incomplete flush block).
    size_t bstart_byte = boundary >> 3;
    size_t total_bytes = (size_t)z->carry_len + src_len;
    size_t rem = total_bytes - bstart_byte;
    if (rem > SSH_INFLATE_CARRY)
    {
        return -1; // the peer did not flush at a block boundary within the tail bound
    }
    uint8_t tmp[SSH_INFLATE_CARRY];
    for (size_t i = 0; i < rem; i++)
    {
        tmp[i] = logical_byte(z->carry, z->carry_len, src, bstart_byte + i);
    }
    memcpy(z->carry, tmp, rem);
    z->carry_len = (uint8_t)rem;
    z->bit_off = (uint8_t)(boundary & 7u);

    *out_len = o.cnt;
    return 0;
}

#endif // PC_ENABLE_SSH_ZLIB
