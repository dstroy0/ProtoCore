// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file inflate.c
 * @brief Bounded RFC 1951 DEFLATE decompressor - implementation.
 *
 * A compact canonical-Huffman INFLATE (the classic count[]/symbol[] decode, as
 * in Mark Adler's "puff" reference) written from RFC 1951. Decoding is bit by
 * bit - small and deterministic rather than fast, which suits the small messages
 * this serves. All state is on the stack plus a caller-supplied table scratch;
 * the output buffer doubles as the LZ77 window (see inflate.h).
 */

#include "inflate.h"

#if PC_ENABLE_WS_DEFLATE

#include <string.h>

#define PC_MAXBITS 15    // max bits in a Huffman code
#define PC_MAXLCODES 288 // max literal/length codes
#define PC_MAXDCODES 32  // max distance codes (30 used; 32 for safety)

// Huffman decoding table: count[len] = #codes of that length, symbol[] = symbols
// in canonical order. Both point into the caller's table scratch.
typedef struct
{
    short *count;
    short *symbol;
} Huffman;

// All the table memory inflate_raw() needs, laid over the caller's scratch.
typedef struct
{
    short lcount[PC_MAXBITS + 1];
    short lsym[PC_MAXLCODES];
    short dcount[PC_MAXBITS + 1];
    short dsym[PC_MAXDCODES];
    short lengths[PC_MAXLCODES + PC_MAXDCODES]; // code lengths during construction
} Tables;
static_assert(sizeof(Tables) <= INFLATE_SCRATCH_SIZE, "bump INFLATE_SCRATCH_SIZE");

// Decoder state.
typedef struct
{
    uint8_t *out;      // output buffer (also the back-reference window)
    size_t outcap;     // capacity
    size_t outcnt;     // bytes written
    const uint8_t *in; // input
    size_t inlen;
    size_t incnt;   // bytes consumed
    int bitbuf;     // bit accumulator (LSB first)
    int bitcnt;     // bits available in bitbuf
    proto_bool err; // ran out of input mid-element
} State;

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

// Pull @p need bits (LSB first). On end-of-input sets s->err and returns 0.
static int bits(State *s, int need)
{
    long val = s->bitbuf;
    while (s->bitcnt < need)
    {
        if (s->incnt >= s->inlen)
        {
            s->err = PROTO_TRUE;
            return 0;
        }
        val |= (long)(s->in[s->incnt++]) << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = (int)(val >> need);
    s->bitcnt -= need;
    return (int)(val & ((1L << need) - 1));
}

// Decode one symbol using the canonical Huffman table. Returns the symbol, or -1
// on end-of-input / an invalid code.
static int decode(State *s, const Huffman *h)
{
    int code = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len <= PC_MAXBITS; len++)
    {
        code |= bits(s, 1);
        if (s->err)
        {
            return -1;
        }
        int count = h->count[len];
        if (code - count < first) // length len codes start at 'first'
        {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1; // ran past PC_MAXBITS without a match
}

// Build a Huffman table from code lengths. Returns 0 if complete, >0 if
// incomplete (left-over codes), <0 if over-subscribed.
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
        return 0; // no codes at all -> complete (empty)
    }

    int left = 1;
    for (int len = 1; len <= PC_MAXBITS; len++)
    {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
        {
            return left; // over-subscribed
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

    return left; // 0 = complete, >0 = incomplete
}

// Decode literal/length + distance codes into the output. Returns 0 on the
// end-of-block symbol, INFLATE_ERR_MALFORMED, or INFLATE_ERR_OVERFLOW.
static InflateResult codes(State *s, const Huffman *lencode, const Huffman *distcode)
{
    int symbol;
    do
    {
        symbol = decode(s, lencode);
        if (symbol < 0)
        {
            return INFLATE_ERR_MALFORMED;
        }
        if (symbol < 256)
        {
            if (s->outcnt >= s->outcap)
            {
                return INFLATE_ERR_OVERFLOW;
            }
            s->out[s->outcnt++] = (uint8_t)symbol;
        }
        else if (symbol > 256)
        {
            symbol -= 257;
            if (symbol >= 29)
            {
                return INFLATE_ERR_MALFORMED; // invalid length code (286/287)
            }
            int len = LEN_BASE[symbol] + bits(s, LEN_EXTRA[symbol]);
            if (s->err)
            {
                return INFLATE_ERR_MALFORMED;
            }

            symbol = decode(s, distcode);
            if (symbol < 0 || symbol >= 30)
            {
                return INFLATE_ERR_MALFORMED;
            }
            size_t dist = (size_t)(DIST_BASE[symbol] + bits(s, DIST_EXTRA[symbol]));
            if (s->err)
            {
                return INFLATE_ERR_MALFORMED;
            }
            if (dist > s->outcnt)
            {
                return INFLATE_ERR_MALFORMED; // reference before start of output
            }
            if (len > (int)(s->outcap - s->outcnt))
            {
                return INFLATE_ERR_OVERFLOW;
            }
            for (int k = 0; k < len; k++)
            {
                s->out[s->outcnt] = s->out[s->outcnt - dist];
                s->outcnt++;
            }
        }
    } while (symbol != 256); // 256 = end of block
    return INFLATE_OK;
}

// Uncompressed (stored) block: byte-align, read LEN/NLEN, copy LEN bytes.
static InflateResult stored(State *s)
{
    s->bitbuf = 0;
    s->bitcnt = 0; // discard bits to the next byte boundary
    if (s->incnt + 4 > s->inlen)
    {
        return INFLATE_ERR_MALFORMED;
    }
    int len = s->in[s->incnt] | (s->in[s->incnt + 1] << 8);
    int nlen = s->in[s->incnt + 2] | (s->in[s->incnt + 3] << 8);
    s->incnt += 4;
    if ((len ^ nlen) != 0xFFFF)
    {
        return INFLATE_ERR_MALFORMED; // NLEN must be ones-complement of LEN
    }
    if (s->incnt + (size_t)len > s->inlen)
    {
        return INFLATE_ERR_MALFORMED;
    }
    if ((size_t)len > s->outcap - s->outcnt)
    {
        return INFLATE_ERR_OVERFLOW;
    }
    memcpy(s->out + s->outcnt, s->in + s->incnt, (size_t)len);
    s->incnt += (size_t)len;
    s->outcnt += (size_t)len;
    return INFLATE_OK;
}

// Fixed-Huffman block (RFC 1951 sec 3.2.6).
static InflateResult fixed(State *s, Huffman *lencode, Huffman *distcode, short *lengths)
{
    int sym = 0;
    for (; sym < 144; sym++)
    {
        lengths[sym] = 8;
    }
    for (; sym < 256; sym++)
    {
        lengths[sym] = 9;
    }
    for (; sym < 280; sym++)
    {
        lengths[sym] = 7;
    }
    for (; sym < 288; sym++)
    {
        lengths[sym] = 8;
    }
    construct(lencode, lengths, 288);
    for (sym = 0; sym < 30; sym++)
    {
        lengths[sym] = 5;
    }
    construct(distcode, lengths, 30);
    return codes(s, lencode, distcode);
}

// Dynamic-Huffman block (RFC 1951 sec 3.2.7).
static InflateResult dynamic(State *s, Huffman *lencode, Huffman *distcode, short *lengths)
{
    static const short ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

    int nlen = bits(s, 5) + 257;
    int ndist = bits(s, 5) + 1;
    int ncode = bits(s, 4) + 4;
    if (s->err)
    {
        return INFLATE_ERR_MALFORMED;
    }
    // GCOVR_EXCL_START  unreachable: HLIT/HDIST are 5-bit fields, so nlen = bits(s,5)+257 <= 288 ==
    // PC_MAXLCODES and ndist = bits(s,5)+1 <= 32 == PC_MAXDCODES always; neither comparison can ever be true.
    // Kept as the bound that would otherwise let a future encoding change overrun the tables.
    if (nlen > PC_MAXLCODES || ndist > PC_MAXDCODES)
    {
        return INFLATE_ERR_MALFORMED;
    }
    // GCOVR_EXCL_STOP

    // Code-length code lengths, in the permuted order.
    int index;
    for (index = 0; index < ncode; index++)
    {
        lengths[ORDER[index]] = (short)bits(s, 3);
    }
    if (s->err)
    {
        return INFLATE_ERR_MALFORMED;
    }
    for (; index < 19; index++)
    {
        lengths[ORDER[index]] = 0;
    }

    // Build the code-length code (reuse lencode temporarily); it must be complete.
    if (construct(lencode, lengths, 19) != 0)
    {
        return INFLATE_ERR_MALFORMED;
    }

    // Read the literal/length and distance code lengths.
    index = 0;
    while (index < nlen + ndist)
    {
        int symbol = decode(s, lencode);
        if (symbol < 0)
        {
            return INFLATE_ERR_MALFORMED;
        }
        if (symbol < 16)
        {
            lengths[index++] = (short)symbol;
            continue;
        }
        int repeat_len = 0;
        int repeat;
        if (symbol == 16)
        {
            if (index == 0)
            {
                return INFLATE_ERR_MALFORMED; // no previous length to repeat
            }
            repeat_len = lengths[index - 1];
            repeat = 3 + bits(s, 2);
        }
        else if (symbol == 17)
        {
            repeat = 3 + bits(s, 3);
        }
        else // symbol == 18
        {
            repeat = 11 + bits(s, 7);
        }
        if (s->err)
        {
            return INFLATE_ERR_MALFORMED;
        }
        if (index + repeat > nlen + ndist)
        {
            return INFLATE_ERR_MALFORMED; // repeat past the end
        }
        while (repeat--)
        {
            lengths[index++] = (short)repeat_len;
        }
    }

    if (lengths[256] == 0)
    {
        return INFLATE_ERR_MALFORMED; // no end-of-block code
    }

    // Build the literal/length and distance tables.
    int err = construct(lencode, lengths, nlen);
    if (err && (err < 0 || nlen != lencode->count[0] + lencode->count[1]))
    {
        return INFLATE_ERR_MALFORMED;
    }
    err = construct(distcode, lengths + nlen, ndist);
    if (err && (err < 0 || ndist != distcode->count[0] + distcode->count[1]))
    {
        return INFLATE_ERR_MALFORMED; // incomplete distance code (ok only for 0/1 codes)
    }

    return codes(s, lencode, distcode);
}

InflateResult inflate_raw(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap, size_t *out_len,
                          void *scratch, size_t scratch_len)
{
    if (scratch_len < INFLATE_SCRATCH_SIZE)
    {
        return INFLATE_ERR_SCRATCH;
    }

    Tables *t = (Tables *)scratch;
    Huffman lencode = {t->lcount, t->lsym};
    Huffman distcode = {t->dcount, t->dsym};

    State s;
    s.out = dst;
    s.outcap = dst_cap;
    s.outcnt = 0;
    s.in = src;
    s.inlen = src_len;
    s.incnt = 0;
    s.bitbuf = 0;
    s.bitcnt = 0;
    s.err = PROTO_FALSE;

    int last = 0;
    do
    {
        // Clean end-of-input at a block boundary: permessage-deflate streams have
        // no final block, so this (not BFINAL) is the normal termination.
        if (s.incnt >= s.inlen && s.bitcnt == 0)
        {
            break;
        }

        last = bits(&s, 1);
        int type = bits(&s, 2);
        if (s.err)
        {
            return INFLATE_ERR_MALFORMED;
        }

        InflateResult rc;
        if (type == 0)
        {
            rc = stored(&s);
        }
        else if (type == 1)
        {
            rc = fixed(&s, &lencode, &distcode, t->lengths);
        }
        else if (type == 2)
        {
            rc = dynamic(&s, &lencode, &distcode, t->lengths);
        }
        else
        {
            return INFLATE_ERR_MALFORMED; // type 3 is reserved
        }

        if (rc != INFLATE_OK)
        {
            return rc;
        }
    } while (!last);

    *out_len = s.outcnt;
    return INFLATE_OK;
}

#endif // PC_ENABLE_WS_DEFLATE
