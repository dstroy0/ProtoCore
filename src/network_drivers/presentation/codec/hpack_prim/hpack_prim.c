// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_hpack_prim.c
 * @brief Shared HPACK/QPACK field-coding primitives - implementation. See pc_hpack_prim.h.
 *
 * The Huffman code (Appendix B) and the canonical Huffman decode tables are generated verbatim
 * from RFC 7541. RFC 9204 (QPACK) references the same integer coding and Huffman table.
 */

#include "network_drivers/presentation/codec/hpack_prim/hpack_prim.h"

#if PC_ENABLE_HTTP2 || PC_ENABLE_HTTP3

// --- Huffman tables generated from RFC 7541 Appendix B ---------------------------------------

// MSB-aligned code and bit length per symbol 0..255, plus EOS(256).
static const uint32_t HUFF_CODE[257] = {
    0x00001ff8, 0x007fffd8, 0x0fffffe2, 0x0fffffe3, 0x0fffffe4, 0x0fffffe5, 0x0fffffe6, 0x0fffffe7, 0x0fffffe8,
    0x00ffffea, 0x3ffffffc, 0x0fffffe9, 0x0fffffea, 0x3ffffffd, 0x0fffffeb, 0x0fffffec, 0x0fffffed, 0x0fffffee,
    0x0fffffef, 0x0ffffff0, 0x0ffffff1, 0x0ffffff2, 0x3ffffffe, 0x0ffffff3, 0x0ffffff4, 0x0ffffff5, 0x0ffffff6,
    0x0ffffff7, 0x0ffffff8, 0x0ffffff9, 0x0ffffffa, 0x0ffffffb, 0x00000014, 0x000003f8, 0x000003f9, 0x00000ffa,
    0x00001ff9, 0x00000015, 0x000000f8, 0x000007fa, 0x000003fa, 0x000003fb, 0x000000f9, 0x000007fb, 0x000000fa,
    0x00000016, 0x00000017, 0x00000018, 0x00000000, 0x00000001, 0x00000002, 0x00000019, 0x0000001a, 0x0000001b,
    0x0000001c, 0x0000001d, 0x0000001e, 0x0000001f, 0x0000005c, 0x000000fb, 0x00007ffc, 0x00000020, 0x00000ffb,
    0x000003fc, 0x00001ffa, 0x00000021, 0x0000005d, 0x0000005e, 0x0000005f, 0x00000060, 0x00000061, 0x00000062,
    0x00000063, 0x00000064, 0x00000065, 0x00000066, 0x00000067, 0x00000068, 0x00000069, 0x0000006a, 0x0000006b,
    0x0000006c, 0x0000006d, 0x0000006e, 0x0000006f, 0x00000070, 0x00000071, 0x00000072, 0x000000fc, 0x00000073,
    0x000000fd, 0x00001ffb, 0x0007fff0, 0x00001ffc, 0x00003ffc, 0x00000022, 0x00007ffd, 0x00000003, 0x00000023,
    0x00000004, 0x00000024, 0x00000005, 0x00000025, 0x00000026, 0x00000027, 0x00000006, 0x00000074, 0x00000075,
    0x00000028, 0x00000029, 0x0000002a, 0x00000007, 0x0000002b, 0x00000076, 0x0000002c, 0x00000008, 0x00000009,
    0x0000002d, 0x00000077, 0x00000078, 0x00000079, 0x0000007a, 0x0000007b, 0x00007ffe, 0x000007fc, 0x00003ffd,
    0x00001ffd, 0x0ffffffc, 0x000fffe6, 0x003fffd2, 0x000fffe7, 0x000fffe8, 0x003fffd3, 0x003fffd4, 0x003fffd5,
    0x007fffd9, 0x003fffd6, 0x007fffda, 0x007fffdb, 0x007fffdc, 0x007fffdd, 0x007fffde, 0x00ffffeb, 0x007fffdf,
    0x00ffffec, 0x00ffffed, 0x003fffd7, 0x007fffe0, 0x00ffffee, 0x007fffe1, 0x007fffe2, 0x007fffe3, 0x007fffe4,
    0x001fffdc, 0x003fffd8, 0x007fffe5, 0x003fffd9, 0x007fffe6, 0x007fffe7, 0x00ffffef, 0x003fffda, 0x001fffdd,
    0x000fffe9, 0x003fffdb, 0x003fffdc, 0x007fffe8, 0x007fffe9, 0x001fffde, 0x007fffea, 0x003fffdd, 0x003fffde,
    0x00fffff0, 0x001fffdf, 0x003fffdf, 0x007fffeb, 0x007fffec, 0x001fffe0, 0x001fffe1, 0x003fffe0, 0x001fffe2,
    0x007fffed, 0x003fffe1, 0x007fffee, 0x007fffef, 0x000fffea, 0x003fffe2, 0x003fffe3, 0x003fffe4, 0x007ffff0,
    0x003fffe5, 0x003fffe6, 0x007ffff1, 0x03ffffe0, 0x03ffffe1, 0x000fffeb, 0x0007fff1, 0x003fffe7, 0x007ffff2,
    0x003fffe8, 0x01ffffec, 0x03ffffe2, 0x03ffffe3, 0x03ffffe4, 0x07ffffde, 0x07ffffdf, 0x03ffffe5, 0x00fffff1,
    0x01ffffed, 0x0007fff2, 0x001fffe3, 0x03ffffe6, 0x07ffffe0, 0x07ffffe1, 0x03ffffe7, 0x07ffffe2, 0x00fffff2,
    0x001fffe4, 0x001fffe5, 0x03ffffe8, 0x03ffffe9, 0x0ffffffd, 0x07ffffe3, 0x07ffffe4, 0x07ffffe5, 0x000fffec,
    0x00fffff3, 0x000fffed, 0x001fffe6, 0x003fffe9, 0x001fffe7, 0x001fffe8, 0x007ffff3, 0x003fffea, 0x003fffeb,
    0x01ffffee, 0x01ffffef, 0x00fffff4, 0x00fffff5, 0x03ffffea, 0x007ffff4, 0x03ffffeb, 0x07ffffe6, 0x03ffffec,
    0x03ffffed, 0x07ffffe7, 0x07ffffe8, 0x07ffffe9, 0x07ffffea, 0x07ffffeb, 0x0ffffffe, 0x07ffffec, 0x07ffffed,
    0x07ffffee, 0x07ffffef, 0x07fffff0, 0x03ffffee, 0x3fffffff,
};
static const uint8_t HUFF_LEN[257] = {
    13, 23, 28, 28, 28, 28, 28, 28, 28, 24, 30, 28, 28, 30, 28, 28, 28, 28, 28, 28, 28, 28, 30, 28, 28, 28, 28, 28, 28,
    28, 28, 28, 6,  10, 10, 12, 13, 6,  8,  11, 10, 10, 8,  11, 8,  6,  6,  6,  5,  5,  5,  6,  6,  6,  6,  6,  6,  6,
    7,  8,  15, 6,  12, 10, 13, 6,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
    7,  8,  7,  8,  13, 19, 13, 14, 6,  15, 5,  6,  5,  6,  5,  6,  6,  6,  5,  7,  7,  6,  6,  6,  5,  6,  7,  6,  5,
    5,  6,  7,  7,  7,  7,  7,  15, 11, 14, 13, 28, 20, 22, 20, 20, 22, 22, 22, 23, 22, 23, 23, 23, 23, 23, 24, 23, 24,
    24, 22, 23, 24, 23, 23, 23, 23, 21, 22, 23, 22, 23, 23, 24, 22, 21, 20, 22, 22, 23, 23, 21, 23, 22, 22, 24, 21, 22,
    23, 23, 21, 21, 22, 21, 23, 22, 23, 23, 20, 22, 22, 22, 23, 22, 22, 23, 26, 26, 20, 19, 22, 23, 22, 25, 26, 26, 26,
    27, 27, 26, 24, 25, 19, 21, 26, 27, 27, 26, 27, 24, 21, 21, 26, 26, 28, 27, 27, 27, 20, 24, 20, 21, 22, 21, 21, 23,
    22, 22, 25, 25, 24, 24, 26, 23, 26, 27, 26, 26, 27, 27, 27, 27, 27, 28, 27, 27, 27, 27, 27, 26, 30,
};

// Canonical Huffman decode tables (index by code length 1..30).
static const uint16_t DEC_COUNT[31] = {0, 0, 0, 0, 0, 10, 26, 32, 6,  0, 5,  3,  2,  6, 2, 3,
                                       0, 0, 0, 3, 8, 13, 26, 29, 12, 4, 15, 19, 29, 0, 4};
static const uint32_t DEC_FIRSTCODE[31] = {
    0,       0,       0,        0,        0,        0,         20,        92,        248,       508,     1016,
    2042,    4090,    8184,     16380,    32764,    65534,     131068,    262136,    524272,    1048550, 2097116,
    4194258, 8388568, 16777194, 33554412, 67108832, 134217694, 268435426, 536870910, 1073741820};
static const uint16_t DEC_FIRSTSYM[31] = {0,  0,  0,  0,  0,  0,   10,  36,  68,  74,  74,  79,  82,  84,  90, 92,
                                          95, 95, 95, 95, 98, 106, 119, 145, 174, 186, 190, 205, 224, 253, 253};
static const uint16_t DEC_SYM[257] = {
    48,  49,  50,  97,  99,  101, 105, 111, 115, 116, 32,  37,  45,  46,  47,  51,  52,  53,  54,  55,  56,  57,
    61,  65,  95,  98,  100, 102, 103, 104, 108, 109, 110, 112, 114, 117, 58,  66,  67,  68,  69,  70,  71,  72,
    73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,  87,  89,  106, 107, 113, 118, 119, 120,
    121, 122, 38,  42,  44,  59,  88,  90,  33,  34,  40,  41,  63,  39,  43,  124, 35,  62,  0,   36,  64,  91,
    93,  126, 94,  125, 60,  96,  123, 92,  195, 208, 128, 130, 131, 162, 184, 194, 224, 226, 153, 161, 167, 172,
    176, 177, 179, 209, 216, 217, 227, 229, 230, 129, 132, 133, 134, 136, 146, 154, 156, 160, 163, 164, 169, 170,
    173, 178, 181, 185, 186, 187, 189, 190, 196, 198, 228, 232, 233, 1,   135, 137, 138, 139, 140, 141, 143, 147,
    149, 150, 151, 152, 155, 157, 158, 165, 166, 168, 174, 175, 180, 182, 183, 188, 191, 197, 231, 239, 9,   142,
    144, 145, 148, 159, 171, 206, 215, 225, 236, 237, 199, 207, 234, 235, 192, 193, 200, 201, 202, 205, 210, 213,
    218, 219, 238, 240, 242, 243, 255, 203, 204, 211, 212, 214, 221, 222, 223, 241, 244, 245, 246, 247, 248, 250,
    251, 252, 253, 254, 2,   3,   4,   5,   6,   7,   8,   11,  12,  14,  15,  16,  17,  18,  19,  20,  21,  23,
    24,  25,  26,  27,  28,  29,  30,  31,  127, 220, 249, 10,  13,  22,  256};

size_t pc_hpack_encode_int(uint8_t *out, size_t cap, uint8_t prefix_bits, uint8_t flags, uint32_t value)
{
    uint8_t max = (uint8_t)((1u << prefix_bits) - 1);
    if (cap < 1)
    {
        return 0;
    }
    if (value < max)
    {
        out[0] = (uint8_t)(flags | value);
        return 1;
    }
    out[0] = (uint8_t)(flags | max);
    value -= max;
    size_t i = 1;
    while (value >= 128)
    {
        if (i >= cap)
        {
            return 0;
        }
        out[i++] = (uint8_t)((value & 0x7f) | 0x80);
        value >>= 7;
    }
    if (i >= cap)
    {
        return 0;
    }
    out[i++] = (uint8_t)value;
    return i;
}

proto_bool pc_hpack_decode_int(const uint8_t *in, size_t len, uint8_t prefix_bits, size_t *consumed, uint32_t *value)
{
    if (len < 1)
    {
        return PROTO_FALSE;
    }
    uint8_t max = (uint8_t)((1u << prefix_bits) - 1);
    uint32_t v = in[0] & max;
    if (v < max)
    {
        *consumed = 1;
        *value = v;
        return PROTO_TRUE;
    }
    size_t i = 1;
    uint32_t m = 0;
    uint8_t b;
    do
    {
        if (i >= len || m > 28) // bound the continuation to a 32-bit result
        {
            return PROTO_FALSE;
        }
        b = in[i++];
        v += (uint32_t)(b & 0x7f) << m;
        m += 7;
    } while (b & 0x80);
    *consumed = i;
    *value = v;
    return PROTO_TRUE;
}

size_t pc_hpack_huff_len(const char *s, size_t n)
{
    size_t bits = 0;
    for (size_t i = 0; i < n; i++)
    {
        bits += HUFF_LEN[(uint8_t)s[i]];
    }
    return (bits + 7) / 8;
}

size_t pc_hpack_huff_encode(uint8_t *out, size_t cap, const char *s, size_t n)
{
    uint64_t acc = 0;
    int nbits = 0;
    size_t o = 0;
    for (size_t i = 0; i < n; i++)
    {
        uint8_t sym = (uint8_t)s[i];
        acc = (acc << HUFF_LEN[sym]) | HUFF_CODE[sym];
        nbits += HUFF_LEN[sym];
        while (nbits >= 8)
        {
            nbits -= 8;
            if (o >= cap)
            {
                return 0;
            }
            out[o++] = (uint8_t)(acc >> nbits);
        }
        acc &= (nbits ? ((uint64_t)1 << nbits) - 1 : 0); // drop already-emitted high bits
    }
    if (nbits > 0)
    {
        if (o >= cap)
        {
            return 0;
        }
        out[o++] = (uint8_t)((acc << (8 - nbits)) | (((uint32_t)1 << (8 - nbits)) - 1));
    }
    return o;
}

proto_bool pc_hpack_huff_decode(const uint8_t *in, size_t n, char *out, size_t cap, size_t *out_len)
{
    uint32_t code = 0;
    int len = 0;
    size_t o = 0;
    for (size_t i = 0; i < n; i++)
    {
        for (int bit = 7; bit >= 0; bit--)
        {
            code = (code << 1) | ((in[i] >> bit) & 1);
            len++;
            // RFC 7541 Huffman is a complete prefix code (Kraft sum 1, max length 30), so every bit
            // path matches a symbol by length 30 and len can never reach 31.
            if (len > 30)
            {
                return PROTO_FALSE;
            }
            uint16_t cnt = DEC_COUNT[len];
            if (!cnt)
            {
                continue;
            }
            uint32_t first = DEC_FIRSTCODE[len];
            // DEC_FIRSTCODE is canonical (first[L] == (first[Lprev]+count[Lprev]) << (L-Lprev) for the
            // next nonzero-count length), and code resets to 0/len to 0 after every match, so by
            // induction code is always >= first[len] at this point; "code < first" cannot fire for any
            // input and is kept only as a defensive bound.
            if (code < first || code - first >= cnt)
            {
                continue;
            }
            uint16_t sym = DEC_SYM[DEC_FIRSTSYM[len] + (code - first)];
            if (sym == 256) // EOS symbol must never be decoded
            {
                return PROTO_FALSE;
            }
            if (o >= cap)
            {
                return PROTO_FALSE;
            }
            out[o++] = (char)sym;
            code = 0;
            len = 0;
        }
    }
    if (len >= 8) // padding longer than a byte is malformed
    {
        return PROTO_FALSE;
    }
    if (len > 0)
    {
        uint32_t pad = ((uint32_t)1 << len) - 1;
        if ((code & pad) != pad) // padding must be the EOS prefix (all 1s)
        {
            return PROTO_FALSE;
        }
    }
    *out_len = o;
    return PROTO_TRUE;
}

// --- string literal (RFC 7541 sec 5.2; RFC 9204 reuses it verbatim) -------------------------------

proto_bool pc_hpack_decode_str(const uint8_t *block, size_t len, size_t *pos, char *out, size_t cap, size_t *out_len)
{
    if (*pos >= len)
    {
        return PROTO_FALSE;
    }
    proto_bool huff = (block[*pos] & 0x80) != 0;
    size_t c = 0;
    uint32_t slen = 0;
    if (!pc_hpack_decode_int(block + *pos, len - *pos, 7, &c, &slen))
    {
        return PROTO_FALSE;
    }
    *pos += c;
    if (*pos + slen > len)
    {
        return PROTO_FALSE;
    }
    if (huff)
    {
        if (!pc_hpack_huff_decode(block + *pos, slen, out, cap, out_len))
        {
            return PROTO_FALSE;
        }
    }
    else
    {
        if (slen > cap)
        {
            return PROTO_FALSE;
        }
        memcpy(out, block + *pos, slen);
        *out_len = slen;
    }
    *pos += slen;
    return PROTO_TRUE;
}

size_t pc_hpack_encode_str(uint8_t *out, size_t cap, const char *s, size_t n)
{
    size_t hl = pc_hpack_huff_len(s, n);
    if (hl < n)
    {
        size_t hdr = pc_hpack_encode_int(out, cap, 7, 0x80, (uint32_t)hl);
        if (!hdr)
        {
            return 0;
        }
        size_t body = pc_hpack_huff_encode(out + hdr, cap - hdr, s, n);
        if (body != hl)
        {
            return 0;
        }
        return hdr + body;
    }
    size_t hdr = pc_hpack_encode_int(out, cap, 7, 0x00, (uint32_t)n);
    if (!hdr || hdr + n > cap)
    {
        return 0;
    }
    memcpy(out + hdr, s, n);
    return hdr + n;
}

#endif // PC_ENABLE_HTTP2 || PC_ENABLE_HTTP3
