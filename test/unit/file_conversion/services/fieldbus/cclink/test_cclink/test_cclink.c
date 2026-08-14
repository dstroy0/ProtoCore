// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/fieldbus/cclink/cclink.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_checksum_is_the_low_byte_of_the_sum(void)
{
    static const uint8_t WRAPS[3] = {0x01, 0x01, 0xFE};
    TEST_ASSERT_EQUAL_HEX8(0x00u, protocore_cclink_sum(WRAPS, sizeof(WRAPS)));

    static const uint8_t PAIR[2] = {0xFF, 0x01};
    TEST_ASSERT_EQUAL_HEX8(0x00u, protocore_cclink_sum(PAIR, sizeof(PAIR)));

    static const uint8_t TWO_MAX[2] = {0xFF, 0xFF};
    TEST_ASSERT_EQUAL_HEX8(0xFEu, protocore_cclink_sum(TWO_MAX, sizeof(TWO_MAX)));

    TEST_ASSERT_EQUAL_HEX8(0x00u, protocore_cclink_sum(WRAPS, 0));

    static const uint8_t BODY[8] = {0x05, 0x01, 0xA5, 0x00, 0x34, 0x12, 0x78, 0x56};
    TEST_ASSERT_EQUAL_HEX8(0xBFu, protocore_cclink_sum(BODY, sizeof(BODY)));
}

void test_frame_layout_and_length(void)
{
    static const uint8_t BITS[2] = {0xA5, 0x00};
    static const uint8_t WORDS[4] = {0x34, 0x12, 0x78, 0x56};
    uint8_t buf[16];
    memset(buf, 0xEE, sizeof(buf));

    size_t n = protocore_cclink_build(5, CCLINK_CMD_REFRESH, BITS, sizeof(BITS), WORDS, sizeof(WORDS), buf,
                                      sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(2u + 2u + 4u + 1u, n);

    static const uint8_t WANT[9] = {0x05, 0x01, 0xA5, 0x00, 0x34, 0x12, 0x78, 0x56, 0xBF};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 9);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, buf[9]);

    TEST_ASSERT_EQUAL_HEX8(0x01u, CCLINK_CMD_REFRESH);
    TEST_ASSERT_EQUAL_HEX8(0x02u, CCLINK_CMD_POLL);
    TEST_ASSERT_EQUAL_HEX8(0x0Fu, CCLINK_CMD_TEST);
}

void test_build_parse_round_trip(void)
{
    static const uint8_t BITS[2] = {0xA5, 0x5A};
    static const uint8_t WORDS[4] = {0x34, 0x12, 0x78, 0x56};
    uint8_t buf[16];
    size_t n = protocore_cclink_build(63, CCLINK_CMD_POLL, BITS, sizeof(BITS), WORDS, sizeof(WORDS), buf, sizeof(buf));

    CcLinkFrame f;
    TEST_ASSERT_TRUE(protocore_cclink_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_UINT8(63u, f.station);
    TEST_ASSERT_EQUAL_HEX8(CCLINK_CMD_POLL, f.command);
    TEST_ASSERT_EQUAL_size_t(6u, f.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(BITS, f.payload, 2);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WORDS, f.payload + 2, 4);

    TEST_ASSERT_TRUE(protocore_cclink_get_bit(f.payload, 2, 0));
    TEST_ASSERT_EQUAL_UINT16(0x1234u, protocore_cclink_get_word(f.payload + 2, 4, 0));
    TEST_ASSERT_EQUAL_UINT16(0x5678u, protocore_cclink_get_word(f.payload + 2, 4, 1));
}

void test_any_single_octet_change_fails_verification(void)
{
    static const uint8_t BITS[3] = {0x11, 0x22, 0x33};
    uint8_t buf[16];
    size_t n = protocore_cclink_build(7, CCLINK_CMD_REFRESH, BITS, sizeof(BITS), NULL, 0, buf, sizeof(buf));
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

void test_bit_addressing_is_lsb_first(void)
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

void test_word_accessor_is_little_endian(void)
{
    static const uint8_t WORDS[6] = {0x34, 0x12, 0xFF, 0xFF, 0x00, 0x80};
    TEST_ASSERT_EQUAL_HEX16(0x1234u, protocore_cclink_get_word(WORDS, sizeof(WORDS), 0));
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, protocore_cclink_get_word(WORDS, sizeof(WORDS), 1));
    TEST_ASSERT_EQUAL_HEX16(0x8000u, protocore_cclink_get_word(WORDS, sizeof(WORDS), 2));
}

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

void test_build_refusals(void)
{
    static const uint8_t DATA[4] = {1, 2, 3, 4};
    uint8_t buf[16];

    TEST_ASSERT_EQUAL_size_t(0u, protocore_cclink_build(64, CCLINK_CMD_POLL, NULL, 0, NULL, 0, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cclink_build(255, CCLINK_CMD_POLL, NULL, 0, NULL, 0, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(3u, protocore_cclink_build(63, CCLINK_CMD_POLL, NULL, 0, NULL, 0, buf, sizeof(buf)));

    TEST_ASSERT_EQUAL_size_t(0u, protocore_cclink_build(1, CCLINK_CMD_POLL, NULL, 0, NULL, 0, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cclink_build(1, CCLINK_CMD_POLL, NULL, 3, NULL, 0, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cclink_build(1, CCLINK_CMD_POLL, NULL, 0, NULL, 3, buf, sizeof(buf)));

    TEST_ASSERT_EQUAL_size_t(0u, protocore_cclink_build(1, CCLINK_CMD_POLL, DATA, 4, DATA, 4, buf, 10));
    TEST_ASSERT_EQUAL_size_t(11u, protocore_cclink_build(1, CCLINK_CMD_POLL, DATA, 4, DATA, 4, buf, 11));
}

void test_parse_refusals_and_the_empty_payload(void)
{
    uint8_t buf[8];
    size_t n = protocore_cclink_build(3, CCLINK_CMD_TEST, NULL, 0, NULL, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(3u, n);

    CcLinkFrame f;
    TEST_ASSERT_TRUE(protocore_cclink_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_UINT8(3u, f.station);
    TEST_ASSERT_EQUAL_HEX8(CCLINK_CMD_TEST, f.command);
    TEST_ASSERT_EQUAL_size_t(0u, f.payload_len);
    TEST_ASSERT_NULL(f.payload);

    TEST_ASSERT_FALSE(protocore_cclink_parse(buf, 2, &f));
    TEST_ASSERT_FALSE(protocore_cclink_parse(buf, 0, &f));
    TEST_ASSERT_FALSE(protocore_cclink_parse(NULL, n, &f));
    TEST_ASSERT_FALSE(protocore_cclink_parse(buf, n, NULL));
}

void test_bit_only_and_word_only_exchanges(void)
{
    static const uint8_t WORDS[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t buf[16];

    size_t n = protocore_cclink_build(2, CCLINK_CMD_REFRESH, NULL, 0, WORDS, sizeof(WORDS), buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(2u + 0u + 4u + 1u, n);
    CcLinkFrame f;
    TEST_ASSERT_TRUE(protocore_cclink_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_size_t(4u, f.payload_len);
    TEST_ASSERT_EQUAL_HEX16(0x0201u, protocore_cclink_get_word(f.payload, f.payload_len, 0));

    static const uint8_t BITS[1] = {0x81};
    n = protocore_cclink_build(2, CCLINK_CMD_REFRESH, BITS, sizeof(BITS), NULL, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(2u + 1u + 0u + 1u, n);
    TEST_ASSERT_TRUE(protocore_cclink_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_size_t(1u, f.payload_len);
    TEST_ASSERT_TRUE(protocore_cclink_get_bit(f.payload, f.payload_len, 0));
    TEST_ASSERT_TRUE(protocore_cclink_get_bit(f.payload, f.payload_len, 7));
    TEST_ASSERT_FALSE(protocore_cclink_get_bit(f.payload, f.payload_len, 1));
}
