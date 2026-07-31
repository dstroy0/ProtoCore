// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/interbus: the summation-frame codec.

#include "services/fieldbus/interbus/interbus.h"
#include <string.h>
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_fcs_check_vector(void)
{
    // CRC-16/CCITT-FALSE check value: CRC of "123456789" = 0x29B1.
    const uint8_t msg[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX16(0x29B1, pc_interbus_fcs(msg, sizeof(msg)));
}

void test_build_and_parse(void)
{
    // Three device slices: 0x1111, 0x2222, 0x3333.
    uint16_t words[3] = {0x1111, 0x2222, 0x3333};
    uint8_t buf[16];
    size_t n = pc_interbus_build(words, 3, buf, sizeof(buf));
    // loopback(2) + 3*2 + FCS(2) = 10.
    TEST_ASSERT_EQUAL_size_t(10, n);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x11, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x11, buf[3]);

    uint16_t out[8];
    size_t count = 0;
    TEST_ASSERT_TRUE(pc_interbus_parse(buf, n, out, 8, &count));
    TEST_ASSERT_EQUAL_size_t(3, count);
    TEST_ASSERT_EQUAL_HEX16(0x1111, out[0]);
    TEST_ASSERT_EQUAL_HEX16(0x2222, out[1]);
    TEST_ASSERT_EQUAL_HEX16(0x3333, out[2]);
}

void test_empty_frame(void)
{
    uint8_t buf[8];
    size_t n = pc_interbus_build(nullptr, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(4, n); // loopback + FCS
    uint16_t out[2];
    size_t count = 99;
    TEST_ASSERT_TRUE(pc_interbus_parse(buf, n, out, 2, &count));
    TEST_ASSERT_EQUAL_size_t(0, count);
}

void test_parse_rejects(void)
{
    uint16_t words[2] = {0xABCD, 0x1234};
    uint8_t buf[16];
    size_t n = pc_interbus_build(words, 2, buf, sizeof(buf));
    uint16_t out[4];
    size_t count;
    // Corrupt FCS.
    buf[n - 1] ^= 0xFF;
    TEST_ASSERT_FALSE(pc_interbus_parse(buf, n, out, 4, &count));
    // Bad loopback word.
    n = pc_interbus_build(words, 2, buf, sizeof(buf));
    buf[0] = 0x00;
    TEST_ASSERT_FALSE(pc_interbus_parse(buf, n, out, 4, &count));
    // Too many words for the buffer.
    n = pc_interbus_build(words, 2, buf, sizeof(buf));
    TEST_ASSERT_FALSE(pc_interbus_parse(buf, n, out, 1, &count));
}

void test_build_parse_guards()
{
    uint8_t out[64];
    uint16_t words[2] = {0x1234, 0x5678};
    TEST_ASSERT_EQUAL_size_t(0, pc_interbus_build(nullptr, 2, out, sizeof(out))); // null words
    TEST_ASSERT_EQUAL_size_t(0, pc_interbus_build(words, 2, out, 2));             // cap too small
    TEST_ASSERT_EQUAL_size_t(0, pc_interbus_build(words, 2, nullptr, 64));        // null out buffer
    uint16_t ow[4];
    size_t oc = 0;
    TEST_ASSERT_FALSE(pc_interbus_parse(nullptr, 10, ow, 4, &oc)); // null frame
    uint8_t tiny[2] = {0, 0};
    TEST_ASSERT_FALSE(pc_interbus_parse(tiny, sizeof(tiny), ow, 4, &oc));      // too short
    TEST_ASSERT_FALSE(pc_interbus_parse(tiny, sizeof(tiny), nullptr, 4, &oc)); // null out_words
    TEST_ASSERT_FALSE(pc_interbus_parse(tiny, sizeof(tiny), ow, 4, nullptr));  // null out_count
}

void test_parse_rejects_odd_word_region(void)
{
    // Loopback word valid, but the region between loopback and FCS is an odd
    // number of bytes, so it cannot be split into whole 16-bit words. This
    // shape can't arise from pc_interbus_build() (which always emits an
    // even-length frame), so it's hand-crafted here.
    uint8_t oddframe[5] = {0xFF, 0xFF, 0x12, 0x34, 0x56};
    uint16_t out[4];
    size_t count = 0;
    TEST_ASSERT_FALSE(pc_interbus_parse(oddframe, sizeof(oddframe), out, 4, &count));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fcs_check_vector);
    RUN_TEST(test_build_and_parse);
    RUN_TEST(test_empty_frame);
    RUN_TEST(test_parse_rejects);
    RUN_TEST(test_build_parse_guards);
    RUN_TEST(test_parse_rejects_odd_word_region);
    return UNITY_END();
}
