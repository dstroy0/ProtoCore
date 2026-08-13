// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Per-file coverage for codec/deflate/rfc1951.{h,c}: the one definition of the RFC 1951 sec 3.2.5
// length and distance tables, and the sec 3.2.6 fixed-Huffman construction over them.
//
// Every expected value below is transcribed from RFC 1951 itself (rfc-editor.org), not from the
// implementation, so the tables are grounded in the standard rather than in themselves.

#include "network_drivers/presentation/codec/deflate/rfc1951.h"
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// RFC 1951 sec 3.2.5, length codes 257..285: "Code Bits Length(s)".
static const short RFC_LEN_BASE[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                       31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short RFC_LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                        2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

// RFC 1951 sec 3.2.5, distance codes 0..29: "Code Bits Dist".
static const short RFC_DIST_BASE[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                        33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                        1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const short RFC_DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                         6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static void test_length_table_matches_rfc(void)
{
    const Rfc1951Ns *r = RFC1951;
    for (int i = 0; i < 29; i++)
    {
        TEST_ASSERT_EQUAL_INT16(RFC_LEN_BASE[i], r->len_base[i]);
        TEST_ASSERT_EQUAL_INT16(RFC_LEN_EXTRA[i], r->len_extra[i]);
    }
}

static void test_distance_table_matches_rfc(void)
{
    const Rfc1951Ns *r = RFC1951;
    for (int i = 0; i < 30; i++)
    {
        TEST_ASSERT_EQUAL_INT16(RFC_DIST_BASE[i], r->dist_base[i]);
        TEST_ASSERT_EQUAL_INT16(RFC_DIST_EXTRA[i], r->dist_extra[i]);
    }
}

// The spans the tables describe have to be contiguous: each code's base is the previous base plus
// 2^(previous extra bits), which is what makes a (base, extra) pair decode a whole range.
static void test_length_spans_are_contiguous(void)
{
    const Rfc1951Ns *r = RFC1951;
    for (int i = 0; i < 27; i++) // 285 is the literal 258 and breaks the run by design
    {
        const int span = 1 << r->len_extra[i];
        TEST_ASSERT_EQUAL_INT16(r->len_base[i] + span, r->len_base[i + 1]);
    }
    TEST_ASSERT_EQUAL_INT16(258, r->len_base[28]);
}

static void test_distance_spans_are_contiguous(void)
{
    const Rfc1951Ns *r = RFC1951;
    for (int i = 0; i < 29; i++)
    {
        const int span = 1 << r->dist_extra[i];
        TEST_ASSERT_EQUAL_INT16(r->dist_base[i] + span, r->dist_base[i + 1]);
    }
}

// The one instance is the same object every call, which is the point of the accessor.
static void test_namespace_is_one_instance(void)
{
    TEST_ASSERT_EQUAL_PTR(protocore_rfc1951(), protocore_rfc1951());
    TEST_ASSERT_EQUAL_PTR(RFC1951->len_base, protocore_rfc1951()->len_base);
}

// RFC 1951 sec 3.2.6: lit 0-143 are 8 bits, 144-255 are 9, 256-279 are 7, 280-287 are 8, and every
// distance code is 5.
static void test_build_fixed_lengths_match_rfc(void)
{
    uint16_t ll_code[288];
    uint8_t ll_len[288];
    uint16_t d_code[30];
    uint8_t d_len[30];
    protocore_rfc1951_build_fixed(ll_code, ll_len, d_code, d_len);

    for (int s = 0; s < 144; s++)
    {
        TEST_ASSERT_EQUAL_UINT8(8, ll_len[s]);
    }
    for (int s = 144; s < 256; s++)
    {
        TEST_ASSERT_EQUAL_UINT8(9, ll_len[s]);
    }
    for (int s = 256; s < 280; s++)
    {
        TEST_ASSERT_EQUAL_UINT8(7, ll_len[s]);
    }
    for (int s = 280; s < 288; s++)
    {
        TEST_ASSERT_EQUAL_UINT8(8, ll_len[s]);
    }
    for (int s = 0; s < 30; s++)
    {
        TEST_ASSERT_EQUAL_UINT8(5, d_len[s]);
    }
}

// sec 3.2.6 gives the codes themselves: lit 0 is 00110000, lit 144 is 110010000, lit 256 is 0000000.
// They are stored bit-reversed because sec 3.1.1 puts Huffman codes on the wire MSB-first while the
// bit writer emits LSB-first.
static void test_build_fixed_codes_are_the_rfc_codes_reversed(void)
{
    uint16_t ll_code[288];
    uint8_t ll_len[288];
    uint16_t d_code[30];
    uint8_t d_len[30];
    protocore_rfc1951_build_fixed(ll_code, ll_len, d_code, d_len);

    TEST_ASSERT_EQUAL_UINT16(protocore_rfc1951_reverse_bits(0x30, 8), ll_code[0]);    // 00110000
    TEST_ASSERT_EQUAL_UINT16(protocore_rfc1951_reverse_bits(0xBF, 8), ll_code[143]);  // 10111111
    TEST_ASSERT_EQUAL_UINT16(protocore_rfc1951_reverse_bits(0x190, 9), ll_code[144]); // 110010000
    TEST_ASSERT_EQUAL_UINT16(protocore_rfc1951_reverse_bits(0x1FF, 9), ll_code[255]); // 111111111
    TEST_ASSERT_EQUAL_UINT16(protocore_rfc1951_reverse_bits(0x00, 7), ll_code[256]);  // 0000000
    TEST_ASSERT_EQUAL_UINT16(protocore_rfc1951_reverse_bits(0x17, 7), ll_code[279]);  // 0010111
    TEST_ASSERT_EQUAL_UINT16(protocore_rfc1951_reverse_bits(0xC0, 8), ll_code[280]);  // 11000000
    TEST_ASSERT_EQUAL_UINT16(protocore_rfc1951_reverse_bits(0xC7, 8), ll_code[287]);  // 11000111

    // Distance codes are 0..29 in order, five bits each (sec 3.2.6).
    for (int s = 0; s < 30; s++)
    {
        TEST_ASSERT_EQUAL_UINT16(protocore_rfc1951_reverse_bits((uint16_t)s, 5), d_code[s]);
    }
}

static void test_reverse_bits_is_its_own_inverse(void)
{
    TEST_ASSERT_EQUAL_UINT16(0x0C, protocore_rfc1951_reverse_bits(0x30, 8)); // 00110000 -> 00001100
    TEST_ASSERT_EQUAL_UINT16(0x30, protocore_rfc1951_reverse_bits(0x0C, 8));
    TEST_ASSERT_EQUAL_UINT16(0x01, protocore_rfc1951_reverse_bits(0x10, 5));
    TEST_ASSERT_EQUAL_UINT16(0x00, protocore_rfc1951_reverse_bits(0x00, 7));
    for (uint16_t v = 0; v < 256; v++)
    {
        TEST_ASSERT_EQUAL_UINT16(v, protocore_rfc1951_reverse_bits(protocore_rfc1951_reverse_bits(v, 8), 8));
    }
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_length_table_matches_rfc);
    RUN_TEST(test_distance_table_matches_rfc);
    RUN_TEST(test_length_spans_are_contiguous);
    RUN_TEST(test_distance_spans_are_contiguous);
    RUN_TEST(test_namespace_is_one_instance);
    RUN_TEST(test_build_fixed_lengths_match_rfc);
    RUN_TEST(test_build_fixed_codes_are_the_rfc_codes_reversed);
    RUN_TEST(test_reverse_bits_is_its_own_inverse);
    return UNITY_END();
}
