// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_WS_DEFLATE

#include "inflate.h"
#include "mmgr/protomem/protomem.h"
#include "network_drivers/presentation/codec/deflate/rfc1951/rfc1951.h" // RFC1951: the sec 3.2.5 tables

PROTOCORE_BEGIN_DECLS

#define PROTOCORE_MAXBITS 15    // max bits in a Huffman code
#define PROTOCORE_MAXLCODES 288 // max literal/length codes
#define PROTOCORE_MAXDCODES 32  // max distance codes (30 used; 32 for safety)

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
    short lcount[PROTOCORE_MAXBITS + 1];
    short lsym[PROTOCORE_MAXLCODES];
    short dcount[PROTOCORE_MAXBITS + 1];
    short dsym[PROTOCORE_MAXDCODES];
    short lengths[PROTOCORE_MAXLCODES + PROTOCORE_MAXDCODES]; // code lengths during construction
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
    for (int len = 1; len <= PROTOCORE_MAXBITS; len++)
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
    return -1; // ran past PROTOCORE_MAXBITS without a match
}

// Build a Huffman table from code lengths. Returns 0 if complete, >0 if
// incomplete (left-over codes), <0 if over-subscribed.
static int construct(Huffman *h, const short *lengths, int n)
{
    for (int len = 0; len <= PROTOCORE_MAXBITS; len++)
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
    for (int len = 1; len <= PROTOCORE_MAXBITS; len++)
    {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
        {
            return left; // over-subscribed
        }
    }

    short offs[PROTOCORE_MAXBITS + 1];
    offs[1] = 0;
    for (int len = 1; len < PROTOCORE_MAXBITS; len++)
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
            int len = Rfc1951V.len_base[symbol] + bits(s, Rfc1951V.len_extra[symbol]);
            if (s->err)
            {
                return INFLATE_ERR_MALFORMED;
            }

            symbol = decode(s, distcode);
            if (symbol < 0 || symbol >= 30)
            {
                return INFLATE_ERR_MALFORMED;
            }
            size_t dist = (size_t)(Rfc1951V.dist_base[symbol] + bits(s, Rfc1951V.dist_extra[symbol]));
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
    mem.cpy(s->out + s->outcnt, s->in + s->incnt, (size_t)len);
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
    if (nlen > PROTOCORE_MAXLCODES || ndist > PROTOCORE_MAXDCODES)
    {
        return INFLATE_ERR_MALFORMED;
    }

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

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void inflate_raw(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *src = Inflate.raw_args.src;
    size_t src_len = Inflate.raw_args.src_len;
    uint8_t *dst = Inflate.raw_args.dst;
    size_t dst_cap = Inflate.raw_args.dst_cap;
    size_t *out_len = Inflate.raw_args.out_len;
    void *scratch = Inflate.raw_args.scratch;
    size_t scratch_len = Inflate.raw_args.scratch_len;

    if (scratch_len < INFLATE_SCRATCH_SIZE)
    {
        Inflate.value = INFLATE_ERR_SCRATCH;
        return;
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
            Inflate.value = INFLATE_ERR_MALFORMED;
            return;
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
            Inflate.value = INFLATE_ERR_MALFORMED; // type 3 is reserved
            return;
        }

        if (rc != INFLATE_OK)
        {
            Inflate.value = rc;
            return;
        }
    } while (!last);

    *out_len = s.outcnt;
    Inflate.value = INFLATE_OK;
}

InflateNs Inflate = {
    .raw = inflate_raw,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WS_DEFLATE
