// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the RFC 1951 code tables (network_drivers/presentation/codec/deflate/rfc1951.h).
//
// test_length_table_matches_rfc and test_distance_table_matches_rfc are the load-bearing cases:
// RFC 1951 sec 3.2.5 prints the base length and extra-bit count for every length code 257..285 and
// every distance code 0..29, and the arrays below are transcribed from that printed table rather
// than from the implementation, so the tables are grounded in the standard and not in themselves.
// One wrong entry silently shifts every back-reference a compressor emits.
//
// The fixed-Huffman construction is checked against the sec 3.2.6 code lengths and against the four
// boundary codes that section prints in full.

#include "network_drivers/presentation/codec/deflate/rfc1951.h"
#include <string.h>

#include <unity.h>

// RFC 1951 sec 3.2.5, "Code Bits Length(s)" for codes 257..285. Each entry is the first length of
// the printed range, and 285 is the literal 258.
static const short RFC_LEN_BASE[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                       31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short RFC_LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                        2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

// RFC 1951 sec 3.2.5, "Code Bits Dist" for codes 0..29.
static const short RFC_DIST_BASE[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                        33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                        1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const short RFC_DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                         6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void setUp(void)
{
}

void tearDown(void)
{
}

void test_length_table_matches_rfc(void)
{
    const Rfc1951Ns *r = RFC1951;
    for (int i = 0; i < 29; i++)
    {
        TEST_ASSERT_EQUAL_INT16(RFC_LEN_BASE[i], r->len_base[i]);
        TEST_ASSERT_EQUAL_INT16(RFC_LEN_EXTRA[i], r->len_extra[i]);
    }
}

void test_distance_table_matches_rfc(void)
{
    const Rfc1951Ns *r = RFC1951;
    for (int i = 0; i < 30; i++)
    {
        TEST_ASSERT_EQUAL_INT16(RFC_DIST_BASE[i], r->dist_base[i]);
        TEST_ASSERT_EQUAL_INT16(RFC_DIST_EXTRA[i], r->dist_extra[i]);
    }
}

// The printed ranges tile the number line without a gap or an overlap: each code's base is the
// previous base plus 2^(previous extra bits), which is what makes a (base, extra) pair name a whole
// span. Code 285 breaks the run by design, being the single length 258.
void test_length_spans_are_contiguous(void)
{
    const Rfc1951Ns *r = RFC1951;
    for (int i = 0; i < 27; i++)
    {
        const int span = 1 << r->len_extra[i];
        TEST_ASSERT_EQUAL_INT16(r->len_base[i] + span, r->len_base[i + 1]);
    }
    TEST_ASSERT_EQUAL_INT16(258, r->len_base[28]);
}

// The distance ranges tile all the way to 32768, sec 3.2.5's largest distance.
void test_distance_spans_are_contiguous(void)
{
    const Rfc1951Ns *r = RFC1951;
    for (int i = 0; i < 29; i++)
    {
        const int span = 1 << r->dist_extra[i];
        TEST_ASSERT_EQUAL_INT16(r->dist_base[i] + span, r->dist_base[i + 1]);
    }
    // Code 29's printed range is 24577-32768, so its last value is base + 2^extra - 1.
    TEST_ASSERT_EQUAL_INT32(32768, r->dist_base[29] + (1 << r->dist_extra[29]) - 1);
}

// The accessor hands out one instance, which is what lets the encoder and the decoder read the same
// table rather than two copies that can drift.
void test_namespace_is_one_instance(void)
{
    TEST_ASSERT_EQUAL_PTR(protocore_rfc1951(), protocore_rfc1951());
    TEST_ASSERT_EQUAL_PTR(RFC1951->len_base, protocore_rfc1951()->len_base);
}

// RFC 1951 sec 3.2.6: literal/length values 0-143 take 8 bits, 144-255 take 9, 256-279 take 7 and
// 280-287 take 8; every distance code is a fixed 5 bits.
void test_build_fixed_lengths_match_rfc(void)
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

// sec 3.2.6 prints the codes at each range boundary: 0 is 00110000, 143 is 10111111, 144 is
// 110010000, 255 is 111111111, 256 is 0000000, 279 is 0010111, 280 is 11000000 and 287 is 11000111.
// They are stored bit-reversed because sec 3.1.1 puts a Huffman code on the wire starting from its
// most significant bit while the bit writer packs LSB-first.
void test_build_fixed_codes_are_the_rfc_codes_reversed(void)
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

    // The whole 0-143 run is consecutive from 00110000, which is what "canonical" means here.
    for (int s = 0; s < 144; s++)
    {
        TEST_ASSERT_EQUAL_UINT16(protocore_rfc1951_reverse_bits((uint16_t)(0x30 + s), 8), ll_code[s]);
    }
    // Distance codes are 0..29 in order, five bits each.
    for (int s = 0; s < 30; s++)
    {
        TEST_ASSERT_EQUAL_UINT16(protocore_rfc1951_reverse_bits((uint16_t)s, 5), d_code[s]);
    }
}

// Reversing the same width twice is the identity, so a code written out and read back is the code.
void test_reverse_bits_is_its_own_inverse(void)
{
    TEST_ASSERT_EQUAL_UINT16(0x0C, protocore_rfc1951_reverse_bits(0x30, 8)); // 00110000 -> 00001100
    TEST_ASSERT_EQUAL_UINT16(0x30, protocore_rfc1951_reverse_bits(0x0C, 8));
    TEST_ASSERT_EQUAL_UINT16(0x01, protocore_rfc1951_reverse_bits(0x10, 5)); // 10000 -> 00001
    TEST_ASSERT_EQUAL_UINT16(0x00, protocore_rfc1951_reverse_bits(0x00, 7));
    for (uint16_t v = 0; v < 256; v++)
    {
        TEST_ASSERT_EQUAL_UINT16(v, protocore_rfc1951_reverse_bits(protocore_rfc1951_reverse_bits(v, 8), 8));
    }
}

// One literal through the fixed code, straight onto the wire. 'A' is 65, so sec 3.2.6 gives it the
// 8-bit code 00110000 + 65 = 01110001, and sec 3.1.1 puts those bits out MSB-first while the writer
// packs LSB-first: bit0 = 0, bit1 = 1, bit2 = 1, bit3 = 1, bit4 = 0, bit5 = 0, bit6 = 0, bit7 = 1,
// which is the octet 10001110 = 0x8E.
void test_emit_literal_puts_the_code_on_the_wire(void)
{
    uint16_t ll_code[288];
    uint8_t ll_len[288];
    uint16_t d_code[30];
    uint8_t d_len[30];
    protocore_rfc1951_build_fixed(ll_code, ll_len, d_code, d_len);

    uint8_t out[4];
    memset(out, 0, sizeof(out));
    protocore_bit_writer w = {out, sizeof(out), 0, 0, 0, PROTO_FALSE};

    protocore_rfc1951_emit_literal(&w, ll_code, ll_len, 'A');
    bitw.align(&w);

    TEST_ASSERT_FALSE(w.overflow);
    TEST_ASSERT_EQUAL_size_t(1, w.cnt);
    TEST_ASSERT_EQUAL_HEX8(0x8E, out[0]);
}

// One back-reference through the fixed codes. Length 3 is sec 3.2.5's code 257 with no extra bits,
// which sec 3.2.6 gives the 7-bit code 0000001; distance 1 is code 0 with no extra bits, a 5-bit
// 00000. Packed LSB-first that is bits 0..6 = 0,0,0,0,0,0,1 then bits 7..11 = 0, so octet 0 is
// 0x40 (bit 6 set) and octet 1 is 0x00 after the align.
void test_emit_match_selects_the_code_for_the_span(void)
{
    uint16_t ll_code[288];
    uint8_t ll_len[288];
    uint16_t d_code[30];
    uint8_t d_len[30];
    protocore_rfc1951_build_fixed(ll_code, ll_len, d_code, d_len);

    uint8_t out[4];
    memset(out, 0, sizeof(out));
    protocore_bit_writer w = {out, sizeof(out), 0, 0, 0, PROTO_FALSE};

    protocore_rfc1951_emit_match(&w, ll_code, ll_len, d_code, d_len, 3, 1);
    bitw.align(&w);

    TEST_ASSERT_FALSE(w.overflow);
    TEST_ASSERT_EQUAL_size_t(2, w.cnt);
    TEST_ASSERT_EQUAL_HEX8(0x40, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[1]);
}

// The longest match, sec 3.2.5's code 285: length 258 carries no extra bits, so a length-258 match
// costs exactly the 8-bit code 11000001 plus the distance code and nothing more. Written LSB-first
// that is 10000011 = 0x83 for the first octet.
void test_emit_match_uses_the_single_length_code_for_258(void)
{
    uint16_t ll_code[288];
    uint8_t ll_len[288];
    uint16_t d_code[30];
    uint8_t d_len[30];
    protocore_rfc1951_build_fixed(ll_code, ll_len, d_code, d_len);

    uint8_t out[8];
    memset(out, 0, sizeof(out));
    protocore_bit_writer w = {out, sizeof(out), 0, 0, 0, PROTO_FALSE};

    protocore_rfc1951_emit_match(&w, ll_code, ll_len, d_code, d_len, 258, 1);
    bitw.align(&w);

    TEST_ASSERT_FALSE(w.overflow);
    // Symbol 285 is 280 + 5, so its 8-bit code is 11000000 + 5 = 11000101; then the 5-bit distance
    // code 00000. Thirteen bits, so two octets.
    TEST_ASSERT_EQUAL_size_t(2, w.cnt);
    TEST_ASSERT_EQUAL_UINT16(protocore_rfc1951_reverse_bits(0xC5, 8), ll_code[285]);
}

// A length that falls inside a printed range carries its offset in the extra bits: length 12 sits in
// code 265's "11,12" span, so the code is 265 and the single extra bit is 12 - 11 = 1. Distance 5
// sits in code 4's "5,6" span, so the extra bit is 5 - 5 = 0.
void test_emit_match_writes_the_offset_in_the_extra_bits(void)
{
    const Rfc1951Ns *r = RFC1951;

    // The table itself says which code a span belongs to, which is what the emitter walks.
    TEST_ASSERT_EQUAL_INT16(11, r->len_base[8]); // code 257 + 8 = 265
    TEST_ASSERT_EQUAL_INT16(1, r->len_extra[8]);
    TEST_ASSERT_EQUAL_INT16(5, r->dist_base[4]); // distance code 4
    TEST_ASSERT_EQUAL_INT16(1, r->dist_extra[4]);

    uint16_t ll_code[288];
    uint8_t ll_len[288];
    uint16_t d_code[30];
    uint8_t d_len[30];
    protocore_rfc1951_build_fixed(ll_code, ll_len, d_code, d_len);

    uint8_t out[8];
    memset(out, 0, sizeof(out));
    protocore_bit_writer w = {out, sizeof(out), 0, 0, 0, PROTO_FALSE};

    protocore_rfc1951_emit_match(&w, ll_code, ll_len, d_code, d_len, 12, 5);
    bitw.align(&w);

    TEST_ASSERT_FALSE(w.overflow);
    // 7 bits of code 265, 1 extra bit, 5 bits of distance code 4, 1 extra bit = 14 bits, two octets.
    TEST_ASSERT_EQUAL_size_t(2, w.cnt);
}

// A writer whose buffer is already full latches overflow instead of writing past it.
void test_emit_past_the_buffer_latches_overflow(void)
{
    uint16_t ll_code[288];
    uint8_t ll_len[288];
    uint16_t d_code[30];
    uint8_t d_len[30];
    protocore_rfc1951_build_fixed(ll_code, ll_len, d_code, d_len);

    uint8_t out[1];
    out[0] = 0;
    protocore_bit_writer w = {out, 0, 0, 0, 0, PROTO_FALSE};

    protocore_rfc1951_emit_literal(&w, ll_code, ll_len, 'A');
    bitw.align(&w);

    TEST_ASSERT_TRUE(w.overflow);
    TEST_ASSERT_EQUAL_size_t(0, w.cnt);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[0]);
}
