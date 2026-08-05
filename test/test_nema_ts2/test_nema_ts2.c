// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/nema_ts2: the TS 2 SDLC frame codec + CRC-16/X-25.

#include "services/transportation/nema_ts2/nema_ts2.h"
#include <string.h>
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_crc_check_vector(void)
{
    // CRC-16/X-25 canonical check value: CRC of "123456789" = 0x906E.
    const uint8_t msg[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX16(0x906E, pc_nema_ts2_crc(msg, sizeof(msg)));
}

void test_build_and_parse(void)
{
    uint8_t data[3] = {0x01, 0x02, 0x03};
    uint8_t buf[16];
    size_t n = pc_nema_ts2_build(0x05, 0x10, NEMA_TS2_FT_CMD_LOADSWITCH, data, 3, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(3 + 3 + 2, n);
    TEST_ASSERT_EQUAL_HEX8(0x05, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x10, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);

    NemaTs2Frame f;
    TEST_ASSERT_TRUE(pc_nema_ts2_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_HEX8(0x05, f.address);
    TEST_ASSERT_EQUAL_HEX8(0x10, f.control);
    TEST_ASSERT_EQUAL_HEX8(NEMA_TS2_FT_CMD_LOADSWITCH, f.frame_type);
    TEST_ASSERT_EQUAL_size_t(3, f.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, f.data, 3);
}

void test_no_data_frame(void)
{
    uint8_t buf[8];
    size_t n = pc_nema_ts2_build(0x01, 0x00, NEMA_TS2_FT_DETECTOR, NULL, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(5, n);
    NemaTs2Frame f;
    TEST_ASSERT_TRUE(pc_nema_ts2_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_size_t(0, f.data_len);
    TEST_ASSERT_NULL(f.data);
}

void test_parse_rejects_bad_crc_and_short(void)
{
    uint8_t data[2] = {0xAA, 0xBB};
    uint8_t buf[8];
    size_t n = pc_nema_ts2_build(0x02, 0x00, NEMA_TS2_FT_CMD_MMU, data, 2, buf, sizeof(buf));
    NemaTs2Frame f;
    buf[n - 1] ^= 0xFF; // corrupt FCS high byte
    TEST_ASSERT_FALSE(pc_nema_ts2_parse(buf, n, &f));
    TEST_ASSERT_FALSE(pc_nema_ts2_parse(buf, 4, &f)); // too short
}

void test_build_rejects_bad_args(void)
{
    uint8_t data[2] = {0xAA, 0xBB};
    uint8_t buf[8];

    // null output buffer.
    TEST_ASSERT_EQUAL_size_t(0, pc_nema_ts2_build(0x01, 0x00, NEMA_TS2_FT_CMD_MMU, data, 2, NULL, sizeof(buf)));

    // non-zero data_len with a null data pointer.
    TEST_ASSERT_EQUAL_size_t(0, pc_nema_ts2_build(0x01, 0x00, NEMA_TS2_FT_CMD_MMU, NULL, 2, buf, sizeof(buf)));
}

void test_build_rejects_undersized_cap(void)
{
    uint8_t data[2] = {0xAA, 0xBB};
    uint8_t buf[8];

    // frame would be 3 + 2 + 2 = 7 bytes; cap of 6 is one byte too small.
    TEST_ASSERT_EQUAL_size_t(0, pc_nema_ts2_build(0x01, 0x00, NEMA_TS2_FT_CMD_MMU, data, 2, buf, 6));
}

void test_parse_rejects_null_args(void)
{
    uint8_t data[2] = {0xAA, 0xBB};
    uint8_t buf[8];
    size_t n = pc_nema_ts2_build(0x02, 0x00, NEMA_TS2_FT_CMD_MMU, data, 2, buf, sizeof(buf));
    NemaTs2Frame f;

    TEST_ASSERT_FALSE(pc_nema_ts2_parse(NULL, n, &f));  // null frame
    TEST_ASSERT_FALSE(pc_nema_ts2_parse(buf, n, NULL)); // null out
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc_check_vector);
    RUN_TEST(test_build_and_parse);
    RUN_TEST(test_no_data_frame);
    RUN_TEST(test_parse_rejects_bad_crc_and_short);
    RUN_TEST(test_build_rejects_bad_args);
    RUN_TEST(test_build_rejects_undersized_cap);
    RUN_TEST(test_parse_rejects_null_args);
    return UNITY_END();
}
