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
    TEST_ASSERT_EQUAL_HEX8(0x00u, protocore_cclink_sum(WRAPS, sizeof(WRAPS)));

    static const uint8_t PAIR[2] = {0xFF, 0x01};
    TEST_ASSERT_EQUAL_HEX8(0x00u, protocore_cclink_sum(PAIR, sizeof(PAIR)));

    static const uint8_t TWO_MAX[2] = {0xFF, 0xFF};
    TEST_ASSERT_EQUAL_HEX8(0xFEu, protocore_cclink_sum(TWO_MAX, sizeof(TWO_MAX)));

    TEST_ASSERT_EQUAL_HEX8(0x00u, protocore_cclink_sum(WRAPS, 0));

    static const uint8_t BODY[8] = {0x05, 0x5A, 0xA5, 0x00, 0x34, 0x12, 0x78, 0x56};
    TEST_ASSERT_EQUAL_HEX8(0x18u, protocore_cclink_sum(BODY, sizeof(BODY)));
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

    size_t n = protocore_cclink_build(5, CMD_A, BITS, sizeof(BITS), WORDS, sizeof(WORDS), buf, sizeof(buf));
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
    size_t n = protocore_cclink_build(63, CMD_B, BITS, sizeof(BITS), WORDS, sizeof(WORDS), buf, sizeof(buf));

    CcLinkFrame f;
    TEST_ASSERT_TRUE(protocore_cclink_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_UINT8(63u, f.station);
    TEST_ASSERT_EQUAL_HEX8(CMD_B, f.command);
    TEST_ASSERT_EQUAL_size_t(6u, f.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(BITS, f.payload, 2);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WORDS, f.payload + 2, 4);

    TEST_ASSERT_TRUE(protocore_cclink_get_bit(f.payload, 2, 0));
    TEST_ASSERT_EQUAL_UINT16(0x1234u, protocore_cclink_get_word(f.payload + 2, 4, 0));
    TEST_ASSERT_EQUAL_UINT16(0x5678u, protocore_cclink_get_word(f.payload + 2, 4, 1));
}

// The command octet is carried, not interpreted: every one of the 256 values survives the trip,
// so no value is special-cased and no unpublished constant is needed to state the property.
void test_every_command_octet_survives_the_round_trip(void)
{
    uint8_t buf[8];
    CcLinkFrame f;
    for (unsigned cmd = 0; cmd < 256u; cmd++)
    {
        size_t n = protocore_cclink_build(1, (uint8_t)cmd, NULL, 0, NULL, 0, buf, sizeof(buf));
        TEST_ASSERT_EQUAL_size_t(3u, n);
        TEST_ASSERT_TRUE(protocore_cclink_parse(buf, n, &f));
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
    size_t n = protocore_cclink_build(7, CMD_A, BITS, sizeof(BITS), NULL, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(6u, n);

    CcLinkFrame f;
    TEST_ASSERT_TRUE(protocore_cclink_parse(buf, n, &f));

    for (size_t i = 0; i < n; i++)
    {
        uint8_t bad[16];
        memcpy(bad, buf, n);
        bad[i] ^= 0x01;
        TEST_ASSERT_FALSE_MESSAGE(protocore_cclink_parse(bad, n, &f), "flipped octet accepted");
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
        TEST_ASSERT_EQUAL_INT_MESSAGE(WANT[i], protocore_cclink_get_bit(bits, 2, i) ? 1 : 0, "bit order");
    }

    protocore_cclink_set_bit(bits, 2, 8, PROTO_TRUE);
    TEST_ASSERT_EQUAL_HEX8(0x01u, bits[1]);
    TEST_ASSERT_TRUE(protocore_cclink_get_bit(bits, 2, 8));

    protocore_cclink_set_bit(bits, 2, 15, PROTO_TRUE);
    TEST_ASSERT_EQUAL_HEX8(0x81u, bits[1]);

    protocore_cclink_set_bit(bits, 2, 8, PROTO_FALSE);
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
        protocore_cclink_set_bit(bits, sizeof(bits), i, (i % 3) == 0 ? PROTO_TRUE : PROTO_FALSE);
    }
    for (size_t i = 0; i < 32; i++)
    {
        proto_bool want = ((i % 3) == 0) ? PROTO_TRUE : PROTO_FALSE;
        TEST_ASSERT_EQUAL_INT(want ? 1 : 0, protocore_cclink_get_bit(bits, sizeof(bits), i) ? 1 : 0);
    }

    for (size_t i = 0; i < 32; i++)
    {
        protocore_cclink_set_bit(bits, sizeof(bits), i, PROTO_FALSE);
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
    TEST_ASSERT_EQUAL_HEX16(0x1234u, protocore_cclink_get_word(WORDS, sizeof(WORDS), 0));
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, protocore_cclink_get_word(WORDS, sizeof(WORDS), 1));
    TEST_ASSERT_EQUAL_HEX16(0x8000u, protocore_cclink_get_word(WORDS, sizeof(WORDS), 2));
}

// Bounds: the last in-range bit of a 2-byte block is 15, the last whole word of a 4-byte block is 1,
// and a word that would straddle the end of a 3-byte block is out of range.
void test_accessors_refuse_out_of_range(void)
{
    uint8_t bits[2] = {0xFF, 0xFF};
    TEST_ASSERT_TRUE(protocore_cclink_get_bit(bits, 2, 15));
    TEST_ASSERT_FALSE(protocore_cclink_get_bit(bits, 2, 16));
    TEST_ASSERT_FALSE(protocore_cclink_get_bit(bits, 2, 999));
    TEST_ASSERT_FALSE(protocore_cclink_get_bit(NULL, 2, 0));

    protocore_cclink_set_bit(bits, 2, 16, PROTO_FALSE);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, bits[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, bits[1]);
    protocore_cclink_set_bit(NULL, 2, 0, PROTO_TRUE);

    static const uint8_t WORDS[4] = {0x11, 0x22, 0x33, 0x44};
    TEST_ASSERT_EQUAL_HEX16(0x4433u, protocore_cclink_get_word(WORDS, 4, 1));
    TEST_ASSERT_EQUAL_HEX16(0u, protocore_cclink_get_word(WORDS, 4, 2));
    TEST_ASSERT_EQUAL_HEX16(0u, protocore_cclink_get_word(WORDS, 3, 1));
    TEST_ASSERT_EQUAL_HEX16(0u, protocore_cclink_get_word(NULL, 4, 0));
}

// cclink.h bounds the station at 0..63 and returns 0 rather than writing past @p cap. The smallest
// frame is 2 + 0 + 0 + 1 = 3 octets; the 4+4 payload frame is 2 + 4 + 4 + 1 = 11, so cap 10 refuses
// and cap 11 fits exactly.
void test_build_refusals(void)
{
    static const uint8_t DATA[4] = {1, 2, 3, 4};
    uint8_t buf[16];

    TEST_ASSERT_EQUAL_size_t(0u, protocore_cclink_build(64, CMD_A, NULL, 0, NULL, 0, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cclink_build(255, CMD_A, NULL, 0, NULL, 0, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(3u, protocore_cclink_build(63, CMD_A, NULL, 0, NULL, 0, buf, sizeof(buf)));

    TEST_ASSERT_EQUAL_size_t(0u, protocore_cclink_build(1, CMD_A, NULL, 0, NULL, 0, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cclink_build(1, CMD_A, NULL, 3, NULL, 0, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cclink_build(1, CMD_A, NULL, 0, NULL, 3, buf, sizeof(buf)));

    TEST_ASSERT_EQUAL_size_t(0u, protocore_cclink_build(1, CMD_A, DATA, 4, DATA, 4, buf, 10));
    TEST_ASSERT_EQUAL_size_t(11u, protocore_cclink_build(1, CMD_A, DATA, 4, DATA, 4, buf, 11));
}

// A frame shorter than station + command + checksum cannot be one, and a payload-free frame reports
// a length of 0 with no payload pointer to dereference.
void test_parse_refusals_and_the_empty_payload(void)
{
    uint8_t buf[8];
    size_t n = protocore_cclink_build(3, CMD_B, NULL, 0, NULL, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(3u, n);

    CcLinkFrame f;
    TEST_ASSERT_TRUE(protocore_cclink_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_UINT8(3u, f.station);
    TEST_ASSERT_EQUAL_HEX8(CMD_B, f.command);
    TEST_ASSERT_EQUAL_size_t(0u, f.payload_len);
    TEST_ASSERT_NULL(f.payload);

    TEST_ASSERT_FALSE(protocore_cclink_parse(buf, 2, &f));
    TEST_ASSERT_FALSE(protocore_cclink_parse(buf, 0, &f));
    TEST_ASSERT_FALSE(protocore_cclink_parse(NULL, n, &f));
    TEST_ASSERT_FALSE(protocore_cclink_parse(buf, n, NULL));
}

// Either half of the process image may be absent, and the length follows the same
// 2 + bit_len + word_len + 1 arithmetic in both cases.
void test_bit_only_and_word_only_exchanges(void)
{
    static const uint8_t WORDS[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t buf[16];

    size_t n = protocore_cclink_build(2, CMD_A, NULL, 0, WORDS, sizeof(WORDS), buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(2u + 0u + 4u + 1u, n);
    CcLinkFrame f;
    TEST_ASSERT_TRUE(protocore_cclink_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_size_t(4u, f.payload_len);
    TEST_ASSERT_EQUAL_HEX16(0x0201u, protocore_cclink_get_word(f.payload, f.payload_len, 0));

    static const uint8_t BITS[1] = {0x81};
    n = protocore_cclink_build(2, CMD_A, BITS, sizeof(BITS), NULL, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(2u + 1u + 0u + 1u, n);
    TEST_ASSERT_TRUE(protocore_cclink_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_size_t(1u, f.payload_len);
    TEST_ASSERT_TRUE(protocore_cclink_get_bit(f.payload, f.payload_len, 0));
    TEST_ASSERT_TRUE(protocore_cclink_get_bit(f.payload, f.payload_len, 7));
    TEST_ASSERT_FALSE(protocore_cclink_get_bit(f.payload, f.payload_len, 1));
}
