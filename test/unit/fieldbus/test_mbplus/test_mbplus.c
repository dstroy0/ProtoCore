// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/mbplus: the Modbus Plus HDLC token-bus frame codec.

#include "services/fieldbus/mbplus/mbplus.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_crc_check_vector(void)
{
    // CRC-16/X-25 check value: CRC of "123456789" = 0x906E.
    const uint8_t msg[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX16(0x906E, pc_mbplus_crc(msg, sizeof(msg)));
}

void test_build_and_parse(void)
{
    uint8_t payload[3] = {0x10, 0x03, 0x00}; // routing + a Modbus read function
    uint8_t buf[16];
    size_t n = pc_mbplus_build(5, MBPLUS_CTRL_DATA, payload, 3, buf, sizeof(buf));
    // 7E 05 00 10 03 00 CRClo CRChi 7E = 9 bytes.
    TEST_ASSERT_EQUAL_size_t(9, n);
    TEST_ASSERT_EQUAL_HEX8(MBPLUS_FLAG, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(MBPLUS_FLAG, buf[n - 1]);
    TEST_ASSERT_EQUAL_HEX8(5, buf[1]);

    MbPlusFrame f;
    TEST_ASSERT_TRUE(pc_mbplus_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_HEX8(5, f.address);
    TEST_ASSERT_EQUAL_HEX8(MBPLUS_CTRL_DATA, f.control);
    TEST_ASSERT_EQUAL_size_t(3, f.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, f.payload, 3);
}

void test_token_frame_no_payload(void)
{
    uint8_t buf[8];
    size_t n = pc_mbplus_build(3, MBPLUS_CTRL_TOKEN, NULL, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(6, n);
    MbPlusFrame f;
    TEST_ASSERT_TRUE(pc_mbplus_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_HEX8(MBPLUS_CTRL_TOKEN, f.control);
    TEST_ASSERT_EQUAL_size_t(0, f.payload_len);
    TEST_ASSERT_NULL(f.payload);
}

void test_next_token_ring(void)
{
    TEST_ASSERT_EQUAL_UINT8(2, pc_mbplus_next_token(1, 8));
    TEST_ASSERT_EQUAL_UINT8(1, pc_mbplus_next_token(8, 8));  // wrap
    TEST_ASSERT_EQUAL_UINT8(1, pc_mbplus_next_token(20, 8)); // beyond max also wraps
}

void test_parse_rejects(void)
{
    uint8_t payload[2] = {0xAA, 0xBB};
    uint8_t buf[16];
    size_t n = pc_mbplus_build(4, MBPLUS_CTRL_DATA, payload, 2, buf, sizeof(buf));
    MbPlusFrame f;
    buf[n - 2] ^= 0xFF; // corrupt CRC hi
    TEST_ASSERT_FALSE(pc_mbplus_parse(buf, n, &f));
    // Missing trailing flag.
    n = pc_mbplus_build(4, MBPLUS_CTRL_DATA, payload, 2, buf, sizeof(buf));
    buf[n - 1] = 0x00;
    TEST_ASSERT_FALSE(pc_mbplus_parse(buf, n, &f));
    // Bad station at build.
    TEST_ASSERT_EQUAL_size_t(0, pc_mbplus_build(0, MBPLUS_CTRL_DATA, NULL, 0, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0, pc_mbplus_build(65, MBPLUS_CTRL_DATA, NULL, 0, buf, sizeof(buf)));
}

void test_build_parse_and_token_wrap()
{
    uint8_t out[64];
    uint8_t payload[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_size_t(0, pc_mbplus_build(1, 0, payload, sizeof(payload), out, 2)); // cap too small
    MbPlusFrame fr;
    uint8_t tiny[2] = {0, 0};
    TEST_ASSERT_FALSE(pc_mbplus_parse(tiny, sizeof(tiny), &fr)); // too short
    TEST_ASSERT_EQUAL_UINT8(1, pc_mbplus_next_token(8, 8));      // current == max -> wrap to 1
}

void test_mbplus_null_and_flag_edges(void)
{
    uint8_t payload[2] = {0x01, 0x02};
    uint8_t buf[16];

    // Null output buffer at build.
    TEST_ASSERT_EQUAL_size_t(0, pc_mbplus_build(5, MBPLUS_CTRL_DATA, payload, 2, NULL, 16));
    // Non-zero payload_len with a null payload pointer.
    TEST_ASSERT_EQUAL_size_t(0, pc_mbplus_build(5, MBPLUS_CTRL_DATA, NULL, 2, buf, sizeof(buf)));

    // Null frame / null out pointers at parse.
    MbPlusFrame f;
    uint8_t frame[8] = {0};
    TEST_ASSERT_FALSE(pc_mbplus_parse(NULL, 8, &f));
    TEST_ASSERT_FALSE(pc_mbplus_parse(frame, 8, NULL));

    // Corrupted leading flag (trailing-flag corruption is covered by test_parse_rejects).
    size_t n = pc_mbplus_build(4, MBPLUS_CTRL_DATA, payload, 2, buf, sizeof(buf));
    buf[0] = 0x00;
    TEST_ASSERT_FALSE(pc_mbplus_parse(buf, n, &f));

    // max_station == 0 is degenerate input; the guard rail still returns a valid station.
    TEST_ASSERT_EQUAL_UINT8(1, pc_mbplus_next_token(1, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc_check_vector);
    RUN_TEST(test_build_and_parse);
    RUN_TEST(test_token_frame_no_payload);
    RUN_TEST(test_next_token_ring);
    RUN_TEST(test_parse_rejects);
    RUN_TEST(test_build_parse_and_token_wrap);
    RUN_TEST(test_mbplus_null_and_flag_edges);
    return UNITY_END();
}
