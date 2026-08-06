// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/radio_sniff: the int->float32 RSSI encode and the 802.15.4 TAP pcap record.

#include "services/radio/radio_sniff/radio_sniff.h"
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

void test_i2f32(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x00000000, pc_radiosniff_i2f32(0));
    TEST_ASSERT_EQUAL_HEX32(0xC2200000, pc_radiosniff_i2f32(-40));  // -40.0f
    TEST_ASSERT_EQUAL_HEX32(0x42200000, pc_radiosniff_i2f32(40));   // 40.0f
    TEST_ASSERT_EQUAL_HEX32(0xC2FE0000, pc_radiosniff_i2f32(-127)); // -127.0f
    TEST_ASSERT_EQUAL_HEX32(0x3F800000, pc_radiosniff_i2f32(1));    // 1.0f
}

void test_i2f32_wide_magnitude(void)
{
    // |dbm| >= 2^23 takes the "highest bit at/above the mantissa width" leg of the mantissa
    // ternary (right-shift instead of left-shift): pc_radiosniff_i2f32 is a generic
    // int->float32 bit encoder (exposed for testing), so exercise it outside the usual dBm
    // range to reach that leg.
    TEST_ASSERT_EQUAL_HEX32(0x4B000000, pc_radiosniff_i2f32(8388608));  // 2^23 -> 8388608.0f
    TEST_ASSERT_EQUAL_HEX32(0xCB000000, pc_radiosniff_i2f32(-8388608)); // -8388608.0f
}

void test_global_header(void)
{
    uint8_t buf[32];
    size_t n = pc_radiosniff_global(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(24, n);
    TEST_ASSERT_EQUAL_HEX32(0xa1b2c3d4, rd32le(buf)); // pcap magic
    TEST_ASSERT_EQUAL_UINT32(283, rd32le(buf + 20));  // DLT_IEEE802_15_4_TAP
}

void test_tap_record(void)
{
    const uint8_t frame[5] = {0x41, 0x88, 0x00, 0xAB, 0xCD}; // a tiny fake 802.15.4 MAC frame
    uint8_t buf[64];
    size_t n = pc_radiosniff_tap_record(buf, sizeof(buf), frame, 5, -55, 11, 0x1234, 0x5678);
    // record(16) + tap(20) + frame(5) = 41.
    TEST_ASSERT_EQUAL_size_t(41, n);
    // Record header: ts + caplen/origlen = tap+frame = 25.
    TEST_ASSERT_EQUAL_UINT32(0x1234, rd32le(buf + 0)); // ts_sec
    TEST_ASSERT_EQUAL_UINT32(0x5678, rd32le(buf + 4)); // ts_usec
    TEST_ASSERT_EQUAL_UINT32(25, rd32le(buf + 8));     // caplen
    TEST_ASSERT_EQUAL_UINT32(25, rd32le(buf + 12));    // origlen
    // TAP header.
    const uint8_t *tap = buf + 16;
    TEST_ASSERT_EQUAL_UINT8(0, tap[0]);            // version
    TEST_ASSERT_EQUAL_UINT16(20, rd16le(tap + 2)); // TAP length
    // RSSI TLV.
    TEST_ASSERT_EQUAL_UINT16(1, rd16le(tap + 4)); // type = RSS
    TEST_ASSERT_EQUAL_UINT16(4, rd16le(tap + 6)); // len
    TEST_ASSERT_EQUAL_HEX32(pc_radiosniff_i2f32(-55), rd32le(tap + 8));
    // Channel TLV.
    TEST_ASSERT_EQUAL_UINT16(3, rd16le(tap + 12));  // type = channel
    TEST_ASSERT_EQUAL_UINT16(11, rd16le(tap + 16)); // channel number
    // Frame follows the TAP block.
    TEST_ASSERT_EQUAL_HEX8(0x41, buf[36]);
    TEST_ASSERT_EQUAL_HEX8(0xCD, buf[40]);
}

void test_tap_record_overflow(void)
{
    const uint8_t frame[5] = {1, 2, 3, 4, 5};
    uint8_t small[20];
    TEST_ASSERT_EQUAL_size_t(0, pc_radiosniff_tap_record(small, sizeof(small), frame, 5, -55, 11, 0, 0));
}

void test_tap_record_bad_args(void)
{
    const uint8_t frame[5] = {1, 2, 3, 4, 5};
    uint8_t buf[64];
    // out == NULL.
    TEST_ASSERT_EQUAL_size_t(0, pc_radiosniff_tap_record(NULL, sizeof(buf), frame, 5, -55, 11, 0, 0));
    // frame == NULL.
    TEST_ASSERT_EQUAL_size_t(0, pc_radiosniff_tap_record(buf, sizeof(buf), NULL, 5, -55, 11, 0, 0));
    // flen == 0.
    TEST_ASSERT_EQUAL_size_t(0, pc_radiosniff_tap_record(buf, sizeof(buf), frame, 0, -55, 11, 0, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_i2f32);
    RUN_TEST(test_i2f32_wide_magnitude);
    RUN_TEST(test_global_header);
    RUN_TEST(test_tap_record);
    RUN_TEST(test_tap_record_overflow);
    RUN_TEST(test_tap_record_bad_args);
    return UNITY_END();
}
