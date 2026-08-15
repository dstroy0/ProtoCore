// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the shared HPACK/QPACK field-coding primitives
// (network_drivers/presentation/codec/hpack_prim/hpack_prim.h).
//
// test_appendix_b_huffman_table is the load-bearing case: RFC 7541 Appendix B publishes a code and a
// bit length for all 256 octets, and the two arrays below are transcribed from that printed table.
// Every symbol is encoded and decoded against the RFC's own bits, so the two directions are checked
// against the standard and never against each other. RFC 9204 sec 5 hands QPACK the same table, so a
// single wrong row breaks HTTP/2 and HTTP/3 together.
//
// Around it: Appendix C.1's three worked prefix-integer examples, the Huffman-coded string literals
// printed in Appendix C.4 and C.6, and the sec 5.1 rule that an integer past the implementation's
// limit MUST be treated as a decoding error.

#include "network_drivers/presentation/codec/hpack_prim/hpack_prim.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}

void tearDown(void)
{
}

// RFC 7541 Appendix C.1.1: "The value 10 is to be encoded with a 5-bit prefix. 10 is less than 31
// (2^5 - 1) and is represented using the 5-bit prefix" -> 0 1 0 1 0 in the low five bits, 0x0a.
//
// C.1.2: 1337 with a 5-bit prefix. The prefix takes its max, 31; I = 1337 - 31 = 1306; 1306 >= 128
// so 1306 % 128 == 26, 26 + 128 == 154 = 0x9a goes out and I becomes 1306 / 128 == 10; 10 < 128 so
// 10 = 0x0a ends it. The RFC prints the three octets as 0x1f 0x9a 0x0a.
//
// C.1.3: 42 starting at an octet boundary, so an 8-bit prefix, and 42 < 255 -> 0x2a.
void test_rfc7541_c1_integer_examples(void)
{
    uint8_t b[8];
    size_t consumed = 0;
    uint32_t v = 0;

    TEST_ASSERT_EQUAL_size_t(1, HpackPrim.encode_int(b, sizeof(b), 5, 0, 10));
    TEST_ASSERT_EQUAL_HEX8(0x0a, b[0]);
    TEST_ASSERT_TRUE(HpackPrim.decode_int(b, 1, 5, &consumed, &v));
    TEST_ASSERT_EQUAL_UINT32(10, v);
    TEST_ASSERT_EQUAL_size_t(1, consumed);

    static const uint8_t C112[3] = {0x1f, 0x9a, 0x0a};
    TEST_ASSERT_EQUAL_size_t(3, HpackPrim.encode_int(b, sizeof(b), 5, 0, 1337));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(C112, b, 3);
    TEST_ASSERT_TRUE(HpackPrim.decode_int(C112, sizeof(C112), 5, &consumed, &v));
    TEST_ASSERT_EQUAL_UINT32(1337, v);
    TEST_ASSERT_EQUAL_size_t(3, consumed);

    TEST_ASSERT_EQUAL_size_t(1, HpackPrim.encode_int(b, sizeof(b), 8, 0, 42));
    TEST_ASSERT_EQUAL_HEX8(0x2a, b[0]);
    TEST_ASSERT_TRUE(HpackPrim.decode_int(b, 1, 8, &consumed, &v));
    TEST_ASSERT_EQUAL_UINT32(42, v);
}

// sec 5.1: the octet's high bits belong to the representation carrying the integer, so they survive
// the encode and the decode masks them off rather than adding them in.
void test_prefix_flags_are_left_alone(void)
{
    uint8_t b[8];
    size_t consumed = 0;
    uint32_t v = 0;

    // A dynamic table size update is 001 then a 5-bit prefix integer (sec 6.3).
    TEST_ASSERT_EQUAL_size_t(1, HpackPrim.encode_int(b, sizeof(b), 5, 0x20, 10));
    TEST_ASSERT_EQUAL_HEX8(0x2a, b[0]);
    TEST_ASSERT_TRUE(HpackPrim.decode_int(b, 1, 5, &consumed, &v));
    TEST_ASSERT_EQUAL_UINT32(10, v);

    // An indexed header field is 1 then a 7-bit prefix (sec 6.1); index 2 gives C.2.4's 0x82.
    TEST_ASSERT_EQUAL_size_t(1, HpackPrim.encode_int(b, sizeof(b), 7, 0x80, 2));
    TEST_ASSERT_EQUAL_HEX8(0x82, b[0]);
    TEST_ASSERT_TRUE(HpackPrim.decode_int(b, 1, 7, &consumed, &v));
    TEST_ASSERT_EQUAL_UINT32(2, v);
}

// Every prefix width sec 5.1 admits, at and either side of 2^N - 1 where the continuation octets
// start. Encode then decode returns the value and consumes exactly what was written.
void test_prefix_int_round_trips_at_every_width(void)
{
    for (uint8_t bits = 1; bits <= 8; bits++)
    {
        const uint32_t max = (1u << bits) - 1u;
        const uint32_t VALUES[] = {0, 1, max - 1u, max, max + 1u, max + 127u, max + 128u, 65535u, 1000000u};
        for (size_t i = 0; i < sizeof(VALUES) / sizeof(VALUES[0]); i++)
        {
            uint8_t b[8];
            size_t n = HpackPrim.encode_int(b, sizeof(b), bits, 0, VALUES[i]);
            TEST_ASSERT_TRUE(n > 0);

            size_t consumed = 0;
            uint32_t got = 0;
            TEST_ASSERT_TRUE(HpackPrim.decode_int(b, n, bits, &consumed, &got));
            TEST_ASSERT_EQUAL_UINT32(VALUES[i], got);
            TEST_ASSERT_EQUAL_size_t(n, consumed);
        }
    }
}

// sec 5.1: "Integer encodings that exceed implementation limits - in value or octet length - MUST be
// treated as decoding errors." The result is a 32-bit integer, so a continuation carrying a bit past
// bit 31 is refused rather than wrapped.
void test_prefix_int_rejects_an_overflowing_encoding(void)
{
    size_t consumed = 0;
    uint32_t v = 0;

    // Four continuations put the fifth octet's payload at bit 28, so its value may not exceed 0x0f.
    static const uint8_t LARGEST[6] = {0x1f, 0x80, 0x80, 0x80, 0x80, 0x0f};
    TEST_ASSERT_TRUE(HpackPrim.decode_int(LARGEST, sizeof(LARGEST), 5, &consumed, &v));
    TEST_ASSERT_EQUAL_UINT32(31u + (0x0fu << 28), v);
    TEST_ASSERT_EQUAL_size_t(6, consumed);

    static const uint8_t JUST_OVER[6] = {0x1f, 0x80, 0x80, 0x80, 0x80, 0x10};
    TEST_ASSERT_FALSE(HpackPrim.decode_int(JUST_OVER, sizeof(JUST_OVER), 5, &consumed, &v));

    static const uint8_t WAY_OVER[6] = {0x1f, 0x80, 0x80, 0x80, 0x80, 0x7f};
    TEST_ASSERT_FALSE(HpackPrim.decode_int(WAY_OVER, sizeof(WAY_OVER), 5, &consumed, &v));

    // The octet-length bound: a sixth continuation is past any shift a 32-bit result can take.
    static const uint8_t TOO_LONG[8] = {0x1f, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00};
    TEST_ASSERT_FALSE(HpackPrim.decode_int(TOO_LONG, sizeof(TOO_LONG), 5, &consumed, &v));

    // A continuation that never terminates inside the octets available.
    static const uint8_t UNTERMINATED[3] = {0x1f, 0x80, 0x80};
    TEST_ASSERT_FALSE(HpackPrim.decode_int(UNTERMINATED, sizeof(UNTERMINATED), 5, &consumed, &v));

    // A prefix at its maximum with no continuation at all.
    static const uint8_t NO_CONTINUATION[1] = {0x1f};
    TEST_ASSERT_FALSE(HpackPrim.decode_int(NO_CONTINUATION, sizeof(NO_CONTINUATION), 5, &consumed, &v));

    // Nothing to read.
    TEST_ASSERT_FALSE(HpackPrim.decode_int(UNTERMINATED, 0, 5, &consumed, &v));
}

// A short output buffer is reported rather than written past: the first octet, and a continuation
// that does not fit.
void test_encode_int_refuses_a_short_buffer(void)
{
    uint8_t b[8];
    TEST_ASSERT_EQUAL_size_t(0, HpackPrim.encode_int(b, 0, 5, 0, 10));
    TEST_ASSERT_EQUAL_size_t(0, HpackPrim.encode_int(b, 1, 5, 0, 1337)); // needs three octets
    TEST_ASSERT_EQUAL_size_t(0, HpackPrim.encode_int(b, 2, 5, 0, 1337));
    TEST_ASSERT_EQUAL_size_t(3, HpackPrim.encode_int(b, 3, 5, 0, 1337));
    TEST_ASSERT_EQUAL_size_t(0, HpackPrim.encode_int(b, 1, 7, 0, 200)); // one continuation, no room
}

// The Huffman-coded string literals RFC 7541 prints in full, each with the octet count its length
// prefix declares (sec 5.2).
void test_appendix_c_huffman_strings(void)
{
    // C.4.1: :authority www.example.com, prefix 0x8c then twelve octets.
    static const uint8_t V_WWW[] = {0xf1, 0xe3, 0xc2, 0xe5, 0xf2, 0x3a, 0x6b, 0xa0, 0xab, 0x90, 0xf4, 0xff};
    // C.4.2: cache-control no-cache, prefix 0x86 then six.
    static const uint8_t V_NOCACHE[] = {0xa8, 0xeb, 0x10, 0x64, 0x9c, 0xbf};
    // C.4.3: custom-key, prefix 0x88 then eight; custom-value, prefix 0x89 then nine.
    static const uint8_t V_CUSTOM_KEY[] = {0x25, 0xa8, 0x49, 0xe9, 0x5b, 0xa9, 0x7d, 0x7f};
    static const uint8_t V_CUSTOM_VALUE[] = {0x25, 0xa8, 0x49, 0xe9, 0x5b, 0xb8, 0xe8, 0xb4, 0xbf};
    // C.6.1: :status 302, cache-control private, date, location.
    static const uint8_t V_302[] = {0x64, 0x02};
    static const uint8_t V_PRIVATE[] = {0xae, 0xc3, 0x77, 0x1a, 0x4b};
    static const uint8_t V_DATE[] = {0xd0, 0x7a, 0xbe, 0x94, 0x10, 0x54, 0xd4, 0x44, 0xa8, 0x20, 0x05,
                                     0x95, 0x04, 0x0b, 0x81, 0x66, 0xe0, 0x82, 0xa6, 0x2d, 0x1b, 0xff};
    static const uint8_t V_LOCATION[] = {0x9d, 0x29, 0xad, 0x17, 0x18, 0x63, 0xc7, 0x8f, 0x0b,
                                         0x97, 0xc8, 0xe9, 0xae, 0x82, 0xae, 0x43, 0xd3};
    // C.6.3: content-encoding gzip.
    static const uint8_t V_GZIP[] = {0x9b, 0xd9, 0xab};

    struct
    {
        const char *text;
        const uint8_t *want;
        size_t n;
    } static const CASES[] = {
        {"www.example.com", V_WWW, sizeof(V_WWW)},
        {"no-cache", V_NOCACHE, sizeof(V_NOCACHE)},
        {"custom-key", V_CUSTOM_KEY, sizeof(V_CUSTOM_KEY)},
        {"custom-value", V_CUSTOM_VALUE, sizeof(V_CUSTOM_VALUE)},
        {"302", V_302, sizeof(V_302)},
        {"private", V_PRIVATE, sizeof(V_PRIVATE)},
        {"Mon, 21 Oct 2013 20:13:21 GMT", V_DATE, sizeof(V_DATE)},
        {"https://www.example.com", V_LOCATION, sizeof(V_LOCATION)},
        {"gzip", V_GZIP, sizeof(V_GZIP)},
    };

    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        const size_t len = strlen(CASES[i].text);
        uint8_t out[64];
        char back[64];
        size_t back_len = 0;

        TEST_ASSERT_EQUAL_size_t(CASES[i].n, HpackPrim.huff_len(CASES[i].text, len));
        TEST_ASSERT_EQUAL_size_t(CASES[i].n, HpackPrim.huff_encode(out, sizeof(out), CASES[i].text, len));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(CASES[i].want, out, CASES[i].n);

        TEST_ASSERT_TRUE(HpackPrim.huff_decode(CASES[i].want, CASES[i].n, back, sizeof(back), &back_len));
        TEST_ASSERT_EQUAL_size_t(len, back_len);
        TEST_ASSERT_EQUAL_MEMORY(CASES[i].text, back, len);
    }
}

// RFC 7541 Appendix B, transcribed: each octet's code as a hexadecimal integer aligned on the least
// significant bit, and the number of bits in it. EOS (symbol 256) is left out because it never
// encodes a symbol.
static const uint32_t RFC_HUFF_CODE[256] = {
    0x00001ff8u, 0x007fffd8u, 0x0fffffe2u, 0x0fffffe3u, 0x0fffffe4u, 0x0fffffe5u, 0x0fffffe6u, 0x0fffffe7u, 0x0fffffe8u,
    0x00ffffeau, 0x3ffffffcu, 0x0fffffe9u, 0x0fffffeau, 0x3ffffffdu, 0x0fffffebu, 0x0fffffecu, 0x0fffffedu, 0x0fffffeeu,
    0x0fffffefu, 0x0ffffff0u, 0x0ffffff1u, 0x0ffffff2u, 0x3ffffffeu, 0x0ffffff3u, 0x0ffffff4u, 0x0ffffff5u, 0x0ffffff6u,
    0x0ffffff7u, 0x0ffffff8u, 0x0ffffff9u, 0x0ffffffau, 0x0ffffffbu, 0x00000014u, 0x000003f8u, 0x000003f9u, 0x00000ffau,
    0x00001ff9u, 0x00000015u, 0x000000f8u, 0x000007fau, 0x000003fau, 0x000003fbu, 0x000000f9u, 0x000007fbu, 0x000000fau,
    0x00000016u, 0x00000017u, 0x00000018u, 0x00000000u, 0x00000001u, 0x00000002u, 0x00000019u, 0x0000001au, 0x0000001bu,
    0x0000001cu, 0x0000001du, 0x0000001eu, 0x0000001fu, 0x0000005cu, 0x000000fbu, 0x00007ffcu, 0x00000020u, 0x00000ffbu,
    0x000003fcu, 0x00001ffau, 0x00000021u, 0x0000005du, 0x0000005eu, 0x0000005fu, 0x00000060u, 0x00000061u, 0x00000062u,
    0x00000063u, 0x00000064u, 0x00000065u, 0x00000066u, 0x00000067u, 0x00000068u, 0x00000069u, 0x0000006au, 0x0000006bu,
    0x0000006cu, 0x0000006du, 0x0000006eu, 0x0000006fu, 0x00000070u, 0x00000071u, 0x00000072u, 0x000000fcu, 0x00000073u,
    0x000000fdu, 0x00001ffbu, 0x0007fff0u, 0x00001ffcu, 0x00003ffcu, 0x00000022u, 0x00007ffdu, 0x00000003u, 0x00000023u,
    0x00000004u, 0x00000024u, 0x00000005u, 0x00000025u, 0x00000026u, 0x00000027u, 0x00000006u, 0x00000074u, 0x00000075u,
    0x00000028u, 0x00000029u, 0x0000002au, 0x00000007u, 0x0000002bu, 0x00000076u, 0x0000002cu, 0x00000008u, 0x00000009u,
    0x0000002du, 0x00000077u, 0x00000078u, 0x00000079u, 0x0000007au, 0x0000007bu, 0x00007ffeu, 0x000007fcu, 0x00003ffdu,
    0x00001ffdu, 0x0ffffffcu, 0x000fffe6u, 0x003fffd2u, 0x000fffe7u, 0x000fffe8u, 0x003fffd3u, 0x003fffd4u, 0x003fffd5u,
    0x007fffd9u, 0x003fffd6u, 0x007fffdau, 0x007fffdbu, 0x007fffdcu, 0x007fffddu, 0x007fffdeu, 0x00ffffebu, 0x007fffdfu,
    0x00ffffecu, 0x00ffffedu, 0x003fffd7u, 0x007fffe0u, 0x00ffffeeu, 0x007fffe1u, 0x007fffe2u, 0x007fffe3u, 0x007fffe4u,
    0x001fffdcu, 0x003fffd8u, 0x007fffe5u, 0x003fffd9u, 0x007fffe6u, 0x007fffe7u, 0x00ffffefu, 0x003fffdau, 0x001fffddu,
    0x000fffe9u, 0x003fffdbu, 0x003fffdcu, 0x007fffe8u, 0x007fffe9u, 0x001fffdeu, 0x007fffeau, 0x003fffddu, 0x003fffdeu,
    0x00fffff0u, 0x001fffdfu, 0x003fffdfu, 0x007fffebu, 0x007fffecu, 0x001fffe0u, 0x001fffe1u, 0x003fffe0u, 0x001fffe2u,
    0x007fffedu, 0x003fffe1u, 0x007fffeeu, 0x007fffefu, 0x000fffeau, 0x003fffe2u, 0x003fffe3u, 0x003fffe4u, 0x007ffff0u,
    0x003fffe5u, 0x003fffe6u, 0x007ffff1u, 0x03ffffe0u, 0x03ffffe1u, 0x000fffebu, 0x0007fff1u, 0x003fffe7u, 0x007ffff2u,
    0x003fffe8u, 0x01ffffecu, 0x03ffffe2u, 0x03ffffe3u, 0x03ffffe4u, 0x07ffffdeu, 0x07ffffdfu, 0x03ffffe5u, 0x00fffff1u,
    0x01ffffedu, 0x0007fff2u, 0x001fffe3u, 0x03ffffe6u, 0x07ffffe0u, 0x07ffffe1u, 0x03ffffe7u, 0x07ffffe2u, 0x00fffff2u,
    0x001fffe4u, 0x001fffe5u, 0x03ffffe8u, 0x03ffffe9u, 0x0ffffffdu, 0x07ffffe3u, 0x07ffffe4u, 0x07ffffe5u, 0x000fffecu,
    0x00fffff3u, 0x000fffedu, 0x001fffe6u, 0x003fffe9u, 0x001fffe7u, 0x001fffe8u, 0x007ffff3u, 0x003fffeau, 0x003fffebu,
    0x01ffffeeu, 0x01ffffefu, 0x00fffff4u, 0x00fffff5u, 0x03ffffeau, 0x007ffff4u, 0x03ffffebu, 0x07ffffe6u, 0x03ffffecu,
    0x03ffffedu, 0x07ffffe7u, 0x07ffffe8u, 0x07ffffe9u, 0x07ffffeau, 0x07ffffebu, 0x0ffffffeu, 0x07ffffecu, 0x07ffffedu,
    0x07ffffeeu, 0x07ffffefu, 0x07fffff0u, 0x03ffffeeu,
};

static const uint8_t RFC_HUFF_LEN[256] = {
    13, 23, 28, 28, 28, 28, 28, 28, 28, 24, 30, 28, 28, 30, 28, 28, 28, 28, 28, 28, 28, 28, 30, 28, 28, 28, 28, 28, 28,
    28, 28, 28, 6,  10, 10, 12, 13, 6,  8,  11, 10, 10, 8,  11, 8,  6,  6,  6,  5,  5,  5,  6,  6,  6,  6,  6,  6,  6,
    7,  8,  15, 6,  12, 10, 13, 6,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
    7,  8,  7,  8,  13, 19, 13, 14, 6,  15, 5,  6,  5,  6,  5,  6,  6,  6,  5,  7,  7,  6,  6,  6,  5,  6,  7,  6,  5,
    5,  6,  7,  7,  7,  7,  7,  15, 11, 14, 13, 28, 20, 22, 20, 20, 22, 22, 22, 23, 22, 23, 23, 23, 23, 23, 24, 23, 24,
    24, 22, 23, 24, 23, 23, 23, 23, 21, 22, 23, 22, 23, 23, 24, 22, 21, 20, 22, 22, 23, 23, 21, 23, 22, 22, 24, 21, 22,
    23, 23, 21, 21, 22, 21, 23, 22, 23, 23, 20, 22, 22, 22, 23, 22, 22, 23, 26, 26, 20, 19, 22, 23, 22, 25, 26, 26, 26,
    27, 27, 26, 24, 25, 19, 21, 26, 27, 27, 26, 27, 24, 21, 21, 26, 26, 28, 27, 27, 27, 20, 24, 20, 21, 22, 21, 21, 23,
    22, 22, 25, 25, 24, 24, 26, 23, 26, 27, 26, 26, 27, 27, 27, 27, 27, 28, 27, 27, 27, 27, 27, 26,
};

// Every octet, both directions, against the RFC's own bits. A one-symbol string is that symbol's
// code left-aligned in the octets it needs, padded with "the most significant bits of the code
// corresponding to the EOS symbol" (sec 5.2), which are all ones.
void test_appendix_b_huffman_table(void)
{
    for (int sym = 0; sym < 256; sym++)
    {
        const char in = (char)sym;
        const unsigned bits = RFC_HUFF_LEN[sym];
        const unsigned octets = (bits + 7u) / 8u;
        const unsigned pad = (octets * 8u) - bits;

        uint8_t want[5];
        const uint64_t padded = ((uint64_t)RFC_HUFF_CODE[sym] << pad) | ((1ull << pad) - 1ull);
        for (unsigned k = 0; k < octets; k++)
        {
            want[k] = (uint8_t)(padded >> (8u * (octets - 1u - k)));
        }

        uint8_t out[8];
        TEST_ASSERT_EQUAL_size_t(octets, HpackPrim.huff_len(&in, 1));
        TEST_ASSERT_EQUAL_size_t(octets, HpackPrim.huff_encode(out, sizeof(out), &in, 1));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, octets);

        char back[8];
        size_t back_len = 0;
        TEST_ASSERT_TRUE(HpackPrim.huff_decode(want, octets, back, sizeof(back), &back_len));
        TEST_ASSERT_EQUAL_size_t(1, back_len);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)sym, (uint8_t)back[0]);
    }
}

// sec 5.2: "A Huffman-encoded string literal containing the EOS symbol MUST be treated as a decoding
// error", padding "strictly longer than 7 bits MUST be treated as a decoding error", and padding
// that does not match the EOS prefix likewise.
void test_huffman_decode_rejects_bad_padding_and_eos(void)
{
    char out[16];
    size_t out_len = 0;

    // EOS is the 30-bit all-ones code, so four octets of 0xff resolve to it.
    static const uint8_t EOS[4] = {0xff, 0xff, 0xff, 0xff};
    TEST_ASSERT_FALSE(HpackPrim.huff_decode(EOS, sizeof(EOS), out, sizeof(out), &out_len));

    // '0' is the 5-bit code 00000, so five of them fill 25 bits and leave 7 of padding: legal when
    // the padding is all ones, a decoding error when it is not.
    static const uint8_t PAD_ONES[4] = {0x00, 0x00, 0x00, 0x7f};
    TEST_ASSERT_TRUE(HpackPrim.huff_decode(PAD_ONES, sizeof(PAD_ONES), out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_size_t(5, out_len);
    TEST_ASSERT_EQUAL_MEMORY("00000", out, 5);

    static const uint8_t PAD_ZEROS[4] = {0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_FALSE(HpackPrim.huff_decode(PAD_ZEROS, sizeof(PAD_ZEROS), out, sizeof(out), &out_len));

    // A whole octet of ones with nothing before it is eight bits of padding, one more than allowed.
    static const uint8_t PAD_OCTET[1] = {0xff};
    TEST_ASSERT_FALSE(HpackPrim.huff_decode(PAD_OCTET, sizeof(PAD_OCTET), out, sizeof(out), &out_len));

    // A destination too small for the symbols decoded is refused rather than overrun.
    static const uint8_t WWW[12] = {0xf1, 0xe3, 0xc2, 0xe5, 0xf2, 0x3a, 0x6b, 0xa0, 0xab, 0x90, 0xf4, 0xff};
    char tiny[4];
    TEST_ASSERT_FALSE(HpackPrim.huff_decode(WWW, sizeof(WWW), tiny, sizeof(tiny), &out_len));
}

// A destination too small for the encoded octets reports 0 rather than writing part of a code.
void test_huff_encode_refuses_a_short_buffer(void)
{
    uint8_t out[16];
    TEST_ASSERT_EQUAL_size_t(0, HpackPrim.huff_encode(out, 0, "www.example.com", 15));
    TEST_ASSERT_EQUAL_size_t(0, HpackPrim.huff_encode(out, 11, "www.example.com", 15)); // needs twelve
    TEST_ASSERT_EQUAL_size_t(12, HpackPrim.huff_encode(out, 12, "www.example.com", 15));
    TEST_ASSERT_EQUAL_size_t(0, HpackPrim.huff_encode(out, 0, "a", 1)); // no room for the last octet
}

// sec 5.2: a string literal is an H bit at 0x80, a 7-bit prefix length, then that many octets. Both
// spellings of the same text decode to the same text, and the cursor lands past the literal.
void test_decode_str_reads_both_forms(void)
{
    char out[64];
    size_t out_len = 0;
    size_t pos = 0;

    // C.3.1's raw form: 0x0f then "www.example.com".
    static const uint8_t RAW[16] = {0x0f, 0x77, 0x77, 0x77, 0x2e, 0x65, 0x78, 0x61,
                                    0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d};
    TEST_ASSERT_TRUE(HpackPrim.decode_str(RAW, sizeof(RAW), &pos, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_size_t(15, out_len);
    TEST_ASSERT_EQUAL_MEMORY("www.example.com", out, 15);
    TEST_ASSERT_EQUAL_size_t(sizeof(RAW), pos);

    // C.4.1's Huffman form: 0x8c then twelve octets.
    static const uint8_t HUFF[13] = {0x8c, 0xf1, 0xe3, 0xc2, 0xe5, 0xf2, 0x3a, 0x6b, 0xa0, 0xab, 0x90, 0xf4, 0xff};
    pos = 0;
    TEST_ASSERT_TRUE(HpackPrim.decode_str(HUFF, sizeof(HUFF), &pos, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_size_t(15, out_len);
    TEST_ASSERT_EQUAL_MEMORY("www.example.com", out, 15);
    TEST_ASSERT_EQUAL_size_t(sizeof(HUFF), pos);

    // Three literals back to back: the cursor is always where the next one starts, and a zero-length
    // literal is a valid empty string.
    static const uint8_t SEQ[6] = {0x02, 'a', 'b', 0x01, 'c', 0x00};
    pos = 0;
    TEST_ASSERT_TRUE(HpackPrim.decode_str(SEQ, sizeof(SEQ), &pos, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_size_t(2, out_len);
    TEST_ASSERT_EQUAL_size_t(3, pos);
    TEST_ASSERT_TRUE(HpackPrim.decode_str(SEQ, sizeof(SEQ), &pos, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_size_t(1, out_len);
    TEST_ASSERT_EQUAL_MEMORY("c", out, 1);
    TEST_ASSERT_EQUAL_size_t(5, pos);
    TEST_ASSERT_TRUE(HpackPrim.decode_str(SEQ, sizeof(SEQ), &pos, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_size_t(0, out_len);
    TEST_ASSERT_EQUAL_size_t(6, pos);
}

// A literal whose declared length runs past the block, or past the caller's destination, is refused
// without copying: the length prefix is a claim, not evidence.
void test_decode_str_fails_closed(void)
{
    char out[64];
    size_t out_len = 0;
    size_t pos = 0;

    static const uint8_t OVER_BLOCK[3] = {0x0f, 'w', 'w'}; // declares fifteen octets, carries two
    TEST_ASSERT_FALSE(HpackPrim.decode_str(OVER_BLOCK, sizeof(OVER_BLOCK), &pos, out, sizeof(out), &out_len));

    pos = 0;
    static const uint8_t TEN[11] = {0x0a, '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    char tiny[4];
    TEST_ASSERT_FALSE(HpackPrim.decode_str(TEN, sizeof(TEN), &pos, tiny, sizeof(tiny), &out_len));

    pos = 0;
    static const uint8_t ONE[1] = {0x00};
    TEST_ASSERT_FALSE(HpackPrim.decode_str(ONE, 0, &pos, out, sizeof(out), &out_len)); // nothing to read

    // A length prefix whose continuation never terminates.
    pos = 0;
    static const uint8_t BAD_LEN[2] = {0xff, 0x80};
    TEST_ASSERT_FALSE(HpackPrim.decode_str(BAD_LEN, sizeof(BAD_LEN), &pos, out, sizeof(out), &out_len));

    // A Huffman literal whose padding is zeros rather than the EOS prefix.
    pos = 0;
    static const uint8_t BAD_PAD[2] = {0x81, 0x00};
    TEST_ASSERT_FALSE(HpackPrim.decode_str(BAD_PAD, sizeof(BAD_PAD), &pos, out, sizeof(out), &out_len));
}

// sec 5.2 leaves the choice of form to the encoder, and this one takes whichever is shorter, so a
// literal is never longer than the text it carries plus its prefix.
void test_encode_str_picks_the_shorter_form(void)
{
    uint8_t out[64];
    char back[64];
    size_t back_len = 0;
    size_t pos = 0;

    // "www.example.com" is fifteen octets raw and twelve Huffman-coded, so the H bit is set and the
    // length is the Huffman length: C.4.1's 0x8c.
    size_t n = HpackPrim.encode_str(out, sizeof(out), "www.example.com", 15);
    TEST_ASSERT_EQUAL_size_t(13, n);
    TEST_ASSERT_EQUAL_HEX8(0x8c, out[0]);
    TEST_ASSERT_TRUE(HpackPrim.decode_str(out, n, &pos, back, sizeof(back), &back_len));
    TEST_ASSERT_EQUAL_size_t(15, back_len);
    TEST_ASSERT_EQUAL_MEMORY("www.example.com", back, 15);

    // Two octets whose Appendix B codes are 28 and 30 bits: Huffman makes them longer, so the raw
    // form wins and the H bit stays clear.
    static const char WIDE[2] = {(char)0x02, (char)0x0a};
    n = HpackPrim.encode_str(out, sizeof(out), WIDE, sizeof(WIDE));
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_HEX8(0x02, out[0]); // H clear, length two
    pos = 0;
    TEST_ASSERT_TRUE(HpackPrim.decode_str(out, n, &pos, back, sizeof(back), &back_len));
    TEST_ASSERT_EQUAL_size_t(2, back_len);
    TEST_ASSERT_EQUAL_MEMORY(WIDE, back, 2);

    // The empty string is one octet either way.
    n = HpackPrim.encode_str(out, sizeof(out), "", 0);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[0]);

    // A destination too small for either form reports 0.
    TEST_ASSERT_EQUAL_size_t(0, HpackPrim.encode_str(out, 4, "www.example.com", 15));
    TEST_ASSERT_EQUAL_size_t(0, HpackPrim.encode_str(out, 0, "x", 1));
}

// Round-trip a literal holding every octet value, which puts codes of every width beside each other
// and crosses the octet boundary at every offset.
void test_encode_str_round_trips_every_octet(void)
{
    char in[256];
    for (int i = 0; i < 256; i++)
    {
        in[i] = (char)i;
    }

    uint8_t block[1024];
    size_t n = HpackPrim.encode_str(block, sizeof(block), in, sizeof(in));
    TEST_ASSERT_TRUE(n > 0);

    char back[256];
    size_t back_len = 0;
    size_t pos = 0;
    TEST_ASSERT_TRUE(HpackPrim.decode_str(block, n, &pos, back, sizeof(back), &back_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(in), back_len);
    TEST_ASSERT_EQUAL_MEMORY(in, back, sizeof(in));
    TEST_ASSERT_EQUAL_size_t(n, pos);
}
