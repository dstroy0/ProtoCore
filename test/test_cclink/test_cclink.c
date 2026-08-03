// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/cclink: the CC-Link cyclic frame codec + process-image accessors.

#include "services/fieldbus/cclink/cclink.h"
#include <string.h>
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_sum(void)
{
    const uint8_t b[] = {0x01, 0x01, 0xFE};
    TEST_ASSERT_EQUAL_HEX8(0x00, pc_cclink_sum(b, sizeof(b))); // wraps to 0
}

void test_build_and_parse(void)
{
    uint8_t bits[2] = {0xA5, 0x00};
    uint8_t words[4] = {0x34, 0x12, 0x78, 0x56}; // 0x1234, 0x5678
    uint8_t buf[16];
    size_t n = pc_cclink_build(5, CCLINK_CMD_REFRESH, bits, 2, words, 4, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(2 + 2 + 4 + 1, n);
    TEST_ASSERT_EQUAL_HEX8(5, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(CCLINK_CMD_REFRESH, buf[1]);

    CcLinkFrame f;
    TEST_ASSERT_TRUE(pc_cclink_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_HEX8(5, f.station);
    TEST_ASSERT_EQUAL_HEX8(CCLINK_CMD_REFRESH, f.command);
    TEST_ASSERT_EQUAL_size_t(6, f.payload_len);
    // payload = bits(2) + words(4)
    TEST_ASSERT_EQUAL_HEX8(0xA5, f.payload[0]);
    TEST_ASSERT_EQUAL_UINT16(0x1234, pc_cclink_get_word(f.payload + 2, 4, 0));
    TEST_ASSERT_EQUAL_UINT16(0x5678, pc_cclink_get_word(f.payload + 2, 4, 1));
}

void test_bit_accessors(void)
{
    uint8_t bits[2] = {0xA5, 0x00}; // 1010_0101
    TEST_ASSERT_TRUE(pc_cclink_get_bit(bits, 2, 0));
    TEST_ASSERT_FALSE(pc_cclink_get_bit(bits, 2, 1));
    TEST_ASSERT_TRUE(pc_cclink_get_bit(bits, 2, 7));
    TEST_ASSERT_FALSE(pc_cclink_get_bit(bits, 2, 99)); // out of range
    pc_cclink_set_bit(bits, 2, 8, true);
    TEST_ASSERT_TRUE(pc_cclink_get_bit(bits, 2, 8));
    TEST_ASSERT_EQUAL_HEX8(0x01, bits[1]);
    pc_cclink_set_bit(bits, 2, 0, false);
    TEST_ASSERT_FALSE(pc_cclink_get_bit(bits, 2, 0));
}

void test_parse_rejects(void)
{
    uint8_t bits[1] = {0x11};
    uint8_t buf[8];
    size_t n = pc_cclink_build(1, CCLINK_CMD_POLL, bits, 1, NULL, 0, buf, sizeof(buf));
    CcLinkFrame f;
    buf[n - 1] ^= 0xFF; // bad checksum
    TEST_ASSERT_FALSE(pc_cclink_parse(buf, n, &f));
    TEST_ASSERT_FALSE(pc_cclink_parse(buf, 2, &f)); // too short
    // station > 63 rejected at build.
    TEST_ASSERT_EQUAL_size_t(0,
                             pc_cclink_build(64, CCLINK_CMD_POLL, NULL, 0, NULL, 0, buf, sizeof(buf)));
}

void test_build_and_accessor_guards()
{
    uint8_t out[64];
    uint8_t bits[2] = {0xFF, 0x00};
    uint8_t words[4] = {0x12, 0x34, 0x56, 0x78};
    TEST_ASSERT_EQUAL_size_t(0, pc_cclink_build(1, 0, bits, 16, words, 2, out, 2)); // cap too small
    pc_cclink_set_bit(bits, 16, 999, true);                                         // out of range -> no-op
    TEST_ASSERT_FALSE(pc_cclink_get_bit(bits, 16, 999));                            // out of range -> false
    TEST_ASSERT_EQUAL_UINT16(0, pc_cclink_get_word(words, 2, 999));                 // out of range -> 0
}

void test_build_null_args(void)
{
    uint8_t buf[16];
    // out == NULL -> rejected before any other check.
    TEST_ASSERT_EQUAL_size_t(
        0, pc_cclink_build(1, CCLINK_CMD_POLL, NULL, 0, NULL, 0, NULL, sizeof(buf)));
    // bit_len > 0 but bits == NULL -> rejected.
    TEST_ASSERT_EQUAL_size_t(0,
                             pc_cclink_build(1, CCLINK_CMD_POLL, NULL, 3, NULL, 0, buf, sizeof(buf)));
    // word_len > 0 but words == NULL -> rejected.
    TEST_ASSERT_EQUAL_size_t(0,
                             pc_cclink_build(1, CCLINK_CMD_POLL, NULL, 0, NULL, 3, buf, sizeof(buf)));
}

void test_build_zero_bit_len(void)
{
    // bit_len == 0 (with non-empty word data) on a successful build path.
    uint8_t words[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t buf[16];
    size_t n = pc_cclink_build(2, CCLINK_CMD_POLL, NULL, 0, words, 4, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(2 + 0 + 4 + 1, n);
    CcLinkFrame f;
    TEST_ASSERT_TRUE(pc_cclink_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_size_t(4, f.payload_len);
}

void test_parse_null_args(void)
{
    uint8_t buf[3] = {0x01, 0x02, 0x03};
    CcLinkFrame f;
    TEST_ASSERT_FALSE(pc_cclink_parse(NULL, sizeof(buf), &f));
    TEST_ASSERT_FALSE(pc_cclink_parse(buf, sizeof(buf), NULL));
}

void test_parse_no_payload(void)
{
    // station + command + checksum only -> body <= 2 -> payload == NULL.
    uint8_t buf[8];
    size_t n = pc_cclink_build(3, CCLINK_CMD_TEST, NULL, 0, NULL, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(3, n);
    CcLinkFrame f;
    TEST_ASSERT_TRUE(pc_cclink_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_size_t(0, f.payload_len);
    TEST_ASSERT_NULL(f.payload);
}

void test_accessor_null_ptrs(void)
{
    TEST_ASSERT_FALSE(pc_cclink_get_bit(NULL, 2, 0));
    pc_cclink_set_bit(NULL, 2, 0, true); // out-of-range on null bits -> no-op, must not crash
    TEST_ASSERT_EQUAL_UINT16(0, pc_cclink_get_word(NULL, 2, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sum);
    RUN_TEST(test_build_and_parse);
    RUN_TEST(test_bit_accessors);
    RUN_TEST(test_parse_rejects);
    RUN_TEST(test_build_and_accessor_guards);
    RUN_TEST(test_build_null_args);
    RUN_TEST(test_build_zero_bit_len);
    RUN_TEST(test_parse_null_args);
    RUN_TEST(test_parse_no_payload);
    RUN_TEST(test_accessor_null_ptrs);
    return UNITY_END();
}
