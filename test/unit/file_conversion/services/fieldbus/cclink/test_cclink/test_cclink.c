// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the CC-Link cyclic frame codec (services/fieldbus/cclink/cclink.h).
//
// NO GOVERNING DOCUMENT COULD BE OBTAINED. The CC-Link protocol specification is published by the
// CLPA to members only, no copy is in docs/learn, and no public statement of the classic CC-Link
// frame layout was found. Every case below is therefore a property (category 3), not a published
// value. Nothing here asserts a command octet, a field offset, or a checksum rule as a fact about
// the wire: the previous file's TEST_ASSERT_EQUAL_HEX8(0x01u, CCLINK_CMD_REFRESH) and its two
// companions compared cclink.h's own macros against copies of themselves and are gone. Every
// command byte in this file is a literal argument chosen not to collide with any of the macros, so
// changing a macro cannot change a result here.
//
// The properties asserted:
//   - the checksum is added up in the comment that carries it, from the arithmetic cclink.h
//     documents (low byte of the sum), so an XOR or two's-complement checksum fails these;
//   - build then parse recovers the station, the command and the payload octets unchanged;
//   - flipping any single octet of a built frame makes parse refuse it;
//   - the bit accessors agree with the index arithmetic they document (byte index/8, bit index%8)
//     across a whole block and at both ends of it, and set/get are inverse;
//   - the word accessor composes its two octets by the arithmetic the header documents;
//   - out-of-range indices, short frames and null pointers are refused;
//   - the three command macros are mutually distinct, which must hold whatever the published
//     octets are, since a codec that mapped two commands onto one octet could not tell them apart.
//
// The field ORDER the layout case asserts is cclink.h's own documented contract, not a published
// CC-Link layout. It locks the API against silent drift; it cannot show the wire form is right, and
// no test in this file can until the specification is obtained. One trade-press article
// (control.com, "Understanding the Industrial Network Protocol CC-Link") describes classic CC-Link
// data frames as HDLC, which has flags, bit stuffing and an FCS rather than a station/command/sum
// layout; that is a secondary source with no field table, so nothing here asserts it either.

#include "services/fieldbus/cclink/cclink.h"
#include <string.h>

#include <unity.h>

static uint8_t cclink_work[16]; // the borrow an entry takes; Cclink never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// Command octets used as literals so no case depends on a macro's value.
#define CMD_A 0x5Au
#define CMD_B 0x3Cu

// cclink.h: the checksum is the low byte of the arithmetic sum of the framed bytes. Each expected
// value is that sum, added up here:
//   0x01 + 0x01 + 0xFE = 1 + 1 + 254 = 256 -> 256 - 256          = 0x00
//   0xFF + 0x01        = 255 + 1     = 256 -> 256 - 256          = 0x00
//   0xFF + 0xFF        = 255 + 255   = 510 -> 510 - 256 = 254    = 0xFE
//   empty run          = 0                                       = 0x00
//   0x05 + 0x5A + 0xA5 + 0x00 + 0x34 + 0x12 + 0x78 + 0x56
//     = 5 + 90 + 165 + 0 + 52 + 18 + 120 + 86 = 536 -> 536 - 512 = 0x18
void test_checksum_is_the_low_byte_of_the_sum(void)
{
    static const uint8_t WRAPS[3] = {0x01, 0x01, 0xFE};
    CclinkV.sum_args.bytes = WRAPS;
    CclinkV.sum_args.len = sizeof(WRAPS);
    Cclink.sum(cclink_work);
    TEST_ASSERT_EQUAL_HEX8(0x00u, CclinkV.value);

    static const uint8_t PAIR[2] = {0xFF, 0x01};
    CclinkV.sum_args.bytes = PAIR;
    CclinkV.sum_args.len = sizeof(PAIR);
    Cclink.sum(cclink_work);
    TEST_ASSERT_EQUAL_HEX8(0x00u, CclinkV.value);

    static const uint8_t TWO_MAX[2] = {0xFF, 0xFF};
    CclinkV.sum_args.bytes = TWO_MAX;
    CclinkV.sum_args.len = sizeof(TWO_MAX);
    Cclink.sum(cclink_work);
    TEST_ASSERT_EQUAL_HEX8(0xFEu, CclinkV.value);

    CclinkV.sum_args.bytes = WRAPS;
    CclinkV.sum_args.len = 0;
    Cclink.sum(cclink_work);
    TEST_ASSERT_EQUAL_HEX8(0x00u, CclinkV.value);

    static const uint8_t BODY[8] = {0x05, 0x5A, 0xA5, 0x00, 0x34, 0x12, 0x78, 0x56};
    CclinkV.sum_args.bytes = BODY;
    CclinkV.sum_args.len = sizeof(BODY);
    Cclink.sum(cclink_work);
    TEST_ASSERT_EQUAL_HEX8(0x18u, CclinkV.value);
}

// The frame is the arguments in cclink.h's documented order followed by the checksum computed
// above, and the octet past the frame is untouched. The length is 2 + bit_len + word_len + 1, the
// return value the header defines.
void test_frame_is_the_arguments_in_order_plus_the_checksum(void)
{
    static const uint8_t BITS[2] = {0xA5, 0x00};
    static const uint8_t WORDS[4] = {0x34, 0x12, 0x78, 0x56};
    uint8_t buf[16];
    memset(buf, 0xEE, sizeof(buf));

    CclinkV.build_args.station = 5;
    CclinkV.build_args.command = CMD_A;
    CclinkV.build_args.bits = BITS;
    CclinkV.build_args.bit_len = sizeof(BITS);
    CclinkV.build_args.words = WORDS;
    CclinkV.build_args.word_len = sizeof(WORDS);
    CclinkV.build_args.out = buf;
    CclinkV.build_args.cap = sizeof(buf);
    Cclink.build(cclink_work);
    size_t n = CclinkV.n;
    TEST_ASSERT_EQUAL_size_t(2u + 2u + 4u + 1u, n);

    static const uint8_t WANT[9] = {0x05, 0x5A, 0xA5, 0x00, 0x34, 0x12, 0x78, 0x56, 0x18};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 9);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, buf[9]);
}

// Round-trip identity: whatever goes in comes back out.
void test_build_parse_round_trip(void)
{
    static const uint8_t BITS[2] = {0xA5, 0x5A};
    static const uint8_t WORDS[4] = {0x34, 0x12, 0x78, 0x56};
    uint8_t buf[16];
    CclinkV.build_args.station = 63;
    CclinkV.build_args.command = CMD_B;
    CclinkV.build_args.bits = BITS;
    CclinkV.build_args.bit_len = sizeof(BITS);
    CclinkV.build_args.words = WORDS;
    CclinkV.build_args.word_len = sizeof(WORDS);
    CclinkV.build_args.out = buf;
    CclinkV.build_args.cap = sizeof(buf);
    Cclink.build(cclink_work);
    size_t n = CclinkV.n;

    CcLinkFrame f;
    CclinkV.parse_args.frame = buf;
    CclinkV.parse_args.len = n;
    CclinkV.parse_args.out = &f;
    Cclink.parse(cclink_work);
    TEST_ASSERT_TRUE(CclinkV.ok);
    TEST_ASSERT_EQUAL_UINT8(63u, f.station);
    TEST_ASSERT_EQUAL_HEX8(CMD_B, f.command);
    TEST_ASSERT_EQUAL_size_t(6u, f.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(BITS, f.payload, 2);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WORDS, f.payload + 2, 4);

    CclinkV.get_bit_args.bits = f.payload;
    CclinkV.get_bit_args.bit_len = 2;
    CclinkV.get_bit_args.index = 0;
    Cclink.get_bit(cclink_work);
    TEST_ASSERT_TRUE(CclinkV.ok);
    CclinkV.get_word_args.words = f.payload + 2;
    CclinkV.get_word_args.word_len = 4;
    CclinkV.get_word_args.index = 0;
    Cclink.get_word(cclink_work);
    TEST_ASSERT_EQUAL_UINT16(0x1234u, CclinkV.u16);
    CclinkV.get_word_args.words = f.payload + 2;
    CclinkV.get_word_args.word_len = 4;
    CclinkV.get_word_args.index = 1;
    Cclink.get_word(cclink_work);
    TEST_ASSERT_EQUAL_UINT16(0x5678u, CclinkV.u16);
}

// The command octet is carried, not interpreted: every one of the 256 values survives the trip,
// so no value is special-cased and no unpublished constant is needed to state the property.
void test_every_command_octet_survives_the_round_trip(void)
{
    uint8_t buf[8];
    CcLinkFrame f;
    for (unsigned cmd = 0; cmd < 256u; cmd++)
    {
        CclinkV.build_args.station = 1;
        CclinkV.build_args.command = (uint8_t)cmd;
        CclinkV.build_args.bits = NULL;
        CclinkV.build_args.bit_len = 0;
        CclinkV.build_args.words = NULL;
        CclinkV.build_args.word_len = 0;
        CclinkV.build_args.out = buf;
        CclinkV.build_args.cap = sizeof(buf);
        Cclink.build(cclink_work);
        size_t n = CclinkV.n;
        TEST_ASSERT_EQUAL_size_t(3u, n);
        CclinkV.parse_args.frame = buf;
        CclinkV.parse_args.len = n;
        CclinkV.parse_args.out = &f;
        Cclink.parse(cclink_work);
        TEST_ASSERT_TRUE(CclinkV.ok);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)cmd, f.command);
    }
}

// A codec that mapped two commands onto one octet could not tell a refresh from a poll from a line
// test. Distinctness holds whatever the CLPA specification assigns; the octets themselves are not
// asserted, because no document that publishes them could be obtained.
void test_command_macros_are_mutually_distinct(void)
{
    TEST_ASSERT_NOT_EQUAL(CCLINK_CMD_REFRESH, CCLINK_CMD_POLL);
    TEST_ASSERT_NOT_EQUAL(CCLINK_CMD_REFRESH, CCLINK_CMD_TEST);
    TEST_ASSERT_NOT_EQUAL(CCLINK_CMD_POLL, CCLINK_CMD_TEST);
}

// A sum checksum detects any change of +-1 in one octet: the sum moves by +-1 mod 256, which is
// never 0, and a change to the checksum octet itself makes it disagree with the unchanged body.
void test_any_single_octet_change_fails_verification(void)
{
    static const uint8_t BITS[3] = {0x11, 0x22, 0x33};
    uint8_t buf[16];
    CclinkV.build_args.station = 7;
    CclinkV.build_args.command = CMD_A;
    CclinkV.build_args.bits = BITS;
    CclinkV.build_args.bit_len = sizeof(BITS);
    CclinkV.build_args.words = NULL;
    CclinkV.build_args.word_len = 0;
    CclinkV.build_args.out = buf;
    CclinkV.build_args.cap = sizeof(buf);
    Cclink.build(cclink_work);
    size_t n = CclinkV.n;
    TEST_ASSERT_EQUAL_size_t(6u, n);

    CcLinkFrame f;
    CclinkV.parse_args.frame = buf;
    CclinkV.parse_args.len = n;
    CclinkV.parse_args.out = &f;
    Cclink.parse(cclink_work);
    TEST_ASSERT_TRUE(CclinkV.ok);

    for (size_t i = 0; i < n; i++)
    {
        uint8_t bad[16];
        memcpy(bad, buf, n);
        bad[i] ^= 0x01;
        CclinkV.parse_args.frame = bad;
        CclinkV.parse_args.len = n;
        CclinkV.parse_args.out = &f;
        Cclink.parse(cclink_work);
        TEST_ASSERT_FALSE_MESSAGE(CclinkV.ok, "flipped octet accepted");
    }
}

// cclink.h places bit @p index at byte index/8, bit index%8. 0xA5 = 1010 0101, so reading index 0
// upward with that arithmetic gives 1,0,1,0,0,1,0,1; index 8 is bit 0 of byte 1 (0x01) and index 15
// is bit 7 of byte 1 (0x80). Clearing index 8 leaves 0x80 and byte 0 untouched.
void test_bit_addressing_matches_the_documented_index_arithmetic(void)
{
    uint8_t bits[2] = {0xA5, 0x00};
    static const proto_bool WANT[8] = {PROTO_TRUE,  PROTO_FALSE, PROTO_TRUE,  PROTO_FALSE,
                                       PROTO_FALSE, PROTO_TRUE,  PROTO_FALSE, PROTO_TRUE};
    for (size_t i = 0; i < 8; i++)
    {
        CclinkV.get_bit_args.bits = bits;
        CclinkV.get_bit_args.bit_len = 2;
        CclinkV.get_bit_args.index = i;
        Cclink.get_bit(cclink_work);
        TEST_ASSERT_EQUAL_INT_MESSAGE(WANT[i], CclinkV.ok ? 1 : 0, "bit order");
    }

    CclinkV.set_bit_args.bits = bits;
    CclinkV.set_bit_args.bit_len = 2;
    CclinkV.set_bit_args.index = 8;
    CclinkV.set_bit_args.value = PROTO_TRUE;
    Cclink.set_bit(cclink_work);
    TEST_ASSERT_EQUAL_HEX8(0x01u, bits[1]);
    CclinkV.get_bit_args.bits = bits;
    CclinkV.get_bit_args.bit_len = 2;
    CclinkV.get_bit_args.index = 8;
    Cclink.get_bit(cclink_work);
    TEST_ASSERT_TRUE(CclinkV.ok);

    CclinkV.set_bit_args.bits = bits;
    CclinkV.set_bit_args.bit_len = 2;
    CclinkV.set_bit_args.index = 15;
    CclinkV.set_bit_args.value = PROTO_TRUE;
    Cclink.set_bit(cclink_work);
    TEST_ASSERT_EQUAL_HEX8(0x81u, bits[1]);

    CclinkV.set_bit_args.bits = bits;
    CclinkV.set_bit_args.bit_len = 2;
    CclinkV.set_bit_args.index = 8;
    CclinkV.set_bit_args.value = PROTO_FALSE;
    Cclink.set_bit(cclink_work);
    TEST_ASSERT_EQUAL_HEX8(0x80u, bits[1]);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, bits[0]);
}

// set then get is the identity over the whole block, and clearing every bit reaches all-zero, so no
// index aliases another.
void test_bit_accessors_round_trip_over_a_block(void)
{
    uint8_t bits[4];
    memset(bits, 0, sizeof(bits));
    for (size_t i = 0; i < 32; i++)
    {
        CclinkV.set_bit_args.bits = bits;
        CclinkV.set_bit_args.bit_len = sizeof(bits);
        CclinkV.set_bit_args.index = i;
        CclinkV.set_bit_args.value = (i % 3) == 0 ? PROTO_TRUE : PROTO_FALSE;
        Cclink.set_bit(cclink_work);
    }
    for (size_t i = 0; i < 32; i++)
    {
        proto_bool want = ((i % 3) == 0) ? PROTO_TRUE : PROTO_FALSE;
        CclinkV.get_bit_args.bits = bits;
        CclinkV.get_bit_args.bit_len = sizeof(bits);
        CclinkV.get_bit_args.index = i;
        Cclink.get_bit(cclink_work);
        TEST_ASSERT_EQUAL_INT(want ? 1 : 0, CclinkV.ok ? 1 : 0);
    }

    for (size_t i = 0; i < 32; i++)
    {
        CclinkV.set_bit_args.bits = bits;
        CclinkV.set_bit_args.bit_len = sizeof(bits);
        CclinkV.set_bit_args.index = i;
        CclinkV.set_bit_args.value = PROTO_FALSE;
        Cclink.set_bit(cclink_work);
    }
    static const uint8_t ZERO[4] = {0, 0, 0, 0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ZERO, bits, 4);
}

// cclink.h: word @p index is little-endian, i.e. words[2i] | (words[2i+1] << 8):
//   word 0: 0x34 | (0x12 << 8) = 0x1234
//   word 1: 0xFF | (0xFF << 8) = 0xFFFF
//   word 2: 0x00 | (0x80 << 8) = 0x8000
// The three differ under the opposite byte order (0x3412, 0xFFFF, 0x0080), so word 1 alone would
// not distinguish them and word 2 pins which octet carries the high bit.
void test_word_accessor_is_little_endian(void)
{
    static const uint8_t WORDS[6] = {0x34, 0x12, 0xFF, 0xFF, 0x00, 0x80};
    CclinkV.get_word_args.words = WORDS;
    CclinkV.get_word_args.word_len = sizeof(WORDS);
    CclinkV.get_word_args.index = 0;
    Cclink.get_word(cclink_work);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, CclinkV.u16);
    CclinkV.get_word_args.words = WORDS;
    CclinkV.get_word_args.word_len = sizeof(WORDS);
    CclinkV.get_word_args.index = 1;
    Cclink.get_word(cclink_work);
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, CclinkV.u16);
    CclinkV.get_word_args.words = WORDS;
    CclinkV.get_word_args.word_len = sizeof(WORDS);
    CclinkV.get_word_args.index = 2;
    Cclink.get_word(cclink_work);
    TEST_ASSERT_EQUAL_HEX16(0x8000u, CclinkV.u16);
}

// Bounds: the last in-range bit of a 2-byte block is 15, the last whole word of a 4-byte block is 1,
// and a word that would straddle the end of a 3-byte block is out of range.
void test_accessors_refuse_out_of_range(void)
{
    uint8_t bits[2] = {0xFF, 0xFF};
    CclinkV.get_bit_args.bits = bits;
    CclinkV.get_bit_args.bit_len = 2;
    CclinkV.get_bit_args.index = 15;
    Cclink.get_bit(cclink_work);
    TEST_ASSERT_TRUE(CclinkV.ok);
    CclinkV.get_bit_args.bits = bits;
    CclinkV.get_bit_args.bit_len = 2;
    CclinkV.get_bit_args.index = 16;
    Cclink.get_bit(cclink_work);
    TEST_ASSERT_FALSE(CclinkV.ok);
    CclinkV.get_bit_args.bits = bits;
    CclinkV.get_bit_args.bit_len = 2;
    CclinkV.get_bit_args.index = 999;
    Cclink.get_bit(cclink_work);
    TEST_ASSERT_FALSE(CclinkV.ok);
    CclinkV.get_bit_args.bits = NULL;
    CclinkV.get_bit_args.bit_len = 2;
    CclinkV.get_bit_args.index = 0;
    Cclink.get_bit(cclink_work);
    TEST_ASSERT_FALSE(CclinkV.ok);

    CclinkV.set_bit_args.bits = bits;
    CclinkV.set_bit_args.bit_len = 2;
    CclinkV.set_bit_args.index = 16;
    CclinkV.set_bit_args.value = PROTO_FALSE;
    Cclink.set_bit(cclink_work);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, bits[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, bits[1]);
    CclinkV.set_bit_args.bits = NULL;
    CclinkV.set_bit_args.bit_len = 2;
    CclinkV.set_bit_args.index = 0;
    CclinkV.set_bit_args.value = PROTO_TRUE;
    Cclink.set_bit(cclink_work);

    static const uint8_t WORDS[4] = {0x11, 0x22, 0x33, 0x44};
    CclinkV.get_word_args.words = WORDS;
    CclinkV.get_word_args.word_len = 4;
    CclinkV.get_word_args.index = 1;
    Cclink.get_word(cclink_work);
    TEST_ASSERT_EQUAL_HEX16(0x4433u, CclinkV.u16);
    CclinkV.get_word_args.words = WORDS;
    CclinkV.get_word_args.word_len = 4;
    CclinkV.get_word_args.index = 2;
    Cclink.get_word(cclink_work);
    TEST_ASSERT_EQUAL_HEX16(0u, CclinkV.u16);
    CclinkV.get_word_args.words = WORDS;
    CclinkV.get_word_args.word_len = 3;
    CclinkV.get_word_args.index = 1;
    Cclink.get_word(cclink_work);
    TEST_ASSERT_EQUAL_HEX16(0u, CclinkV.u16);
    CclinkV.get_word_args.words = NULL;
    CclinkV.get_word_args.word_len = 4;
    CclinkV.get_word_args.index = 0;
    Cclink.get_word(cclink_work);
    TEST_ASSERT_EQUAL_HEX16(0u, CclinkV.u16);
}

// cclink.h bounds the station at 0..63 and returns 0 rather than writing past @p cap. The smallest
// frame is 2 + 0 + 0 + 1 = 3 octets; the 4+4 payload frame is 2 + 4 + 4 + 1 = 11, so cap 10 refuses
// and cap 11 fits exactly.
void test_build_refusals(void)
{
    static const uint8_t DATA[4] = {1, 2, 3, 4};
    uint8_t buf[16];

    CclinkV.build_args.station = 64;
    CclinkV.build_args.command = CMD_A;
    CclinkV.build_args.bits = NULL;
    CclinkV.build_args.bit_len = 0;
    CclinkV.build_args.words = NULL;
    CclinkV.build_args.word_len = 0;
    CclinkV.build_args.out = buf;
    CclinkV.build_args.cap = sizeof(buf);
    Cclink.build(cclink_work);
    TEST_ASSERT_EQUAL_size_t(0u, CclinkV.n);
    CclinkV.build_args.station = 255;
    CclinkV.build_args.command = CMD_A;
    CclinkV.build_args.bits = NULL;
    CclinkV.build_args.bit_len = 0;
    CclinkV.build_args.words = NULL;
    CclinkV.build_args.word_len = 0;
    CclinkV.build_args.out = buf;
    CclinkV.build_args.cap = sizeof(buf);
    Cclink.build(cclink_work);
    TEST_ASSERT_EQUAL_size_t(0u, CclinkV.n);
    CclinkV.build_args.station = 63;
    CclinkV.build_args.command = CMD_A;
    CclinkV.build_args.bits = NULL;
    CclinkV.build_args.bit_len = 0;
    CclinkV.build_args.words = NULL;
    CclinkV.build_args.word_len = 0;
    CclinkV.build_args.out = buf;
    CclinkV.build_args.cap = sizeof(buf);
    Cclink.build(cclink_work);
    TEST_ASSERT_EQUAL_size_t(3u, CclinkV.n);

    CclinkV.build_args.station = 1;
    CclinkV.build_args.command = CMD_A;
    CclinkV.build_args.bits = NULL;
    CclinkV.build_args.bit_len = 0;
    CclinkV.build_args.words = NULL;
    CclinkV.build_args.word_len = 0;
    CclinkV.build_args.out = NULL;
    CclinkV.build_args.cap = sizeof(buf);
    Cclink.build(cclink_work);
    TEST_ASSERT_EQUAL_size_t(0u, CclinkV.n);
    CclinkV.build_args.station = 1;
    CclinkV.build_args.command = CMD_A;
    CclinkV.build_args.bits = NULL;
    CclinkV.build_args.bit_len = 3;
    CclinkV.build_args.words = NULL;
    CclinkV.build_args.word_len = 0;
    CclinkV.build_args.out = buf;
    CclinkV.build_args.cap = sizeof(buf);
    Cclink.build(cclink_work);
    TEST_ASSERT_EQUAL_size_t(0u, CclinkV.n);
    CclinkV.build_args.station = 1;
    CclinkV.build_args.command = CMD_A;
    CclinkV.build_args.bits = NULL;
    CclinkV.build_args.bit_len = 0;
    CclinkV.build_args.words = NULL;
    CclinkV.build_args.word_len = 3;
    CclinkV.build_args.out = buf;
    CclinkV.build_args.cap = sizeof(buf);
    Cclink.build(cclink_work);
    TEST_ASSERT_EQUAL_size_t(0u, CclinkV.n);

    CclinkV.build_args.station = 1;
    CclinkV.build_args.command = CMD_A;
    CclinkV.build_args.bits = DATA;
    CclinkV.build_args.bit_len = 4;
    CclinkV.build_args.words = DATA;
    CclinkV.build_args.word_len = 4;
    CclinkV.build_args.out = buf;
    CclinkV.build_args.cap = 10;
    Cclink.build(cclink_work);
    TEST_ASSERT_EQUAL_size_t(0u, CclinkV.n);
    CclinkV.build_args.station = 1;
    CclinkV.build_args.command = CMD_A;
    CclinkV.build_args.bits = DATA;
    CclinkV.build_args.bit_len = 4;
    CclinkV.build_args.words = DATA;
    CclinkV.build_args.word_len = 4;
    CclinkV.build_args.out = buf;
    CclinkV.build_args.cap = 11;
    Cclink.build(cclink_work);
    TEST_ASSERT_EQUAL_size_t(11u, CclinkV.n);
}

// A frame shorter than station + command + checksum cannot be one, and a payload-free frame reports
// a length of 0 with no payload pointer to dereference.
void test_parse_refusals_and_the_empty_payload(void)
{
    uint8_t buf[8];
    CclinkV.build_args.station = 3;
    CclinkV.build_args.command = CMD_B;
    CclinkV.build_args.bits = NULL;
    CclinkV.build_args.bit_len = 0;
    CclinkV.build_args.words = NULL;
    CclinkV.build_args.word_len = 0;
    CclinkV.build_args.out = buf;
    CclinkV.build_args.cap = sizeof(buf);
    Cclink.build(cclink_work);
    size_t n = CclinkV.n;
    TEST_ASSERT_EQUAL_size_t(3u, n);

    CcLinkFrame f;
    CclinkV.parse_args.frame = buf;
    CclinkV.parse_args.len = n;
    CclinkV.parse_args.out = &f;
    Cclink.parse(cclink_work);
    TEST_ASSERT_TRUE(CclinkV.ok);
    TEST_ASSERT_EQUAL_UINT8(3u, f.station);
    TEST_ASSERT_EQUAL_HEX8(CMD_B, f.command);
    TEST_ASSERT_EQUAL_size_t(0u, f.payload_len);
    TEST_ASSERT_NULL(f.payload);

    CclinkV.parse_args.frame = buf;
    CclinkV.parse_args.len = 2;
    CclinkV.parse_args.out = &f;
    Cclink.parse(cclink_work);
    TEST_ASSERT_FALSE(CclinkV.ok);
    CclinkV.parse_args.frame = buf;
    CclinkV.parse_args.len = 0;
    CclinkV.parse_args.out = &f;
    Cclink.parse(cclink_work);
    TEST_ASSERT_FALSE(CclinkV.ok);
    CclinkV.parse_args.frame = NULL;
    CclinkV.parse_args.len = n;
    CclinkV.parse_args.out = &f;
    Cclink.parse(cclink_work);
    TEST_ASSERT_FALSE(CclinkV.ok);
    CclinkV.parse_args.frame = buf;
    CclinkV.parse_args.len = n;
    CclinkV.parse_args.out = NULL;
    Cclink.parse(cclink_work);
    TEST_ASSERT_FALSE(CclinkV.ok);
}

// Either half of the process image may be absent, and the length follows the same
// 2 + bit_len + word_len + 1 arithmetic in both cases.
void test_bit_only_and_word_only_exchanges(void)
{
    static const uint8_t WORDS[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t buf[16];

    CclinkV.build_args.station = 2;
    CclinkV.build_args.command = CMD_A;
    CclinkV.build_args.bits = NULL;
    CclinkV.build_args.bit_len = 0;
    CclinkV.build_args.words = WORDS;
    CclinkV.build_args.word_len = sizeof(WORDS);
    CclinkV.build_args.out = buf;
    CclinkV.build_args.cap = sizeof(buf);
    Cclink.build(cclink_work);
    size_t n = CclinkV.n;
    TEST_ASSERT_EQUAL_size_t(2u + 0u + 4u + 1u, n);
    CcLinkFrame f;
    CclinkV.parse_args.frame = buf;
    CclinkV.parse_args.len = n;
    CclinkV.parse_args.out = &f;
    Cclink.parse(cclink_work);
    TEST_ASSERT_TRUE(CclinkV.ok);
    TEST_ASSERT_EQUAL_size_t(4u, f.payload_len);
    CclinkV.get_word_args.words = f.payload;
    CclinkV.get_word_args.word_len = f.payload_len;
    CclinkV.get_word_args.index = 0;
    Cclink.get_word(cclink_work);
    TEST_ASSERT_EQUAL_HEX16(0x0201u, CclinkV.u16);

    static const uint8_t BITS[1] = {0x81};
    CclinkV.build_args.station = 2;
    CclinkV.build_args.command = CMD_A;
    CclinkV.build_args.bits = BITS;
    CclinkV.build_args.bit_len = sizeof(BITS);
    CclinkV.build_args.words = NULL;
    CclinkV.build_args.word_len = 0;
    CclinkV.build_args.out = buf;
    CclinkV.build_args.cap = sizeof(buf);
    Cclink.build(cclink_work);
    n = CclinkV.n;
    TEST_ASSERT_EQUAL_size_t(2u + 1u + 0u + 1u, n);
    CclinkV.parse_args.frame = buf;
    CclinkV.parse_args.len = n;
    CclinkV.parse_args.out = &f;
    Cclink.parse(cclink_work);
    TEST_ASSERT_TRUE(CclinkV.ok);
    TEST_ASSERT_EQUAL_size_t(1u, f.payload_len);
    CclinkV.get_bit_args.bits = f.payload;
    CclinkV.get_bit_args.bit_len = f.payload_len;
    CclinkV.get_bit_args.index = 0;
    Cclink.get_bit(cclink_work);
    TEST_ASSERT_TRUE(CclinkV.ok);
    CclinkV.get_bit_args.bits = f.payload;
    CclinkV.get_bit_args.bit_len = f.payload_len;
    CclinkV.get_bit_args.index = 7;
    Cclink.get_bit(cclink_work);
    TEST_ASSERT_TRUE(CclinkV.ok);
    CclinkV.get_bit_args.bits = f.payload;
    CclinkV.get_bit_args.bit_len = f.payload_len;
    CclinkV.get_bit_args.index = 1;
    Cclink.get_bit(cclink_work);
    TEST_ASSERT_FALSE(CclinkV.ok);
}
