// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/fdc2214: the capacitance-to-digital codec (data combine, error flags,
// frequency scale, config sequence). The Wire binding is ESP32-only and not exercised here.

#include "services/peripherals/fdc2214/fdc2214.h"
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_data_combine(void)
{
    // MSB register: error flags 0x3 in top nibble, data MSB 0xABC; LSB register 0x1234.
    uint32_t d = pc_fdc2214_data(0x3ABC, 0x1234);
    TEST_ASSERT_EQUAL_HEX32(0x0ABC1234, d); // error nibble masked out of the 28-bit result
    TEST_ASSERT_EQUAL_UINT8(0x3, pc_fdc2214_error(0x3ABC));
    TEST_ASSERT_EQUAL_UINT8(0x0, pc_fdc2214_error(0x0ABC));
}

void test_freq_scale(void)
{
    // data = 2^27 (half scale), fref = 40 MHz -> f_sensor = 20 MHz.
    TEST_ASSERT_EQUAL_UINT64(20000000ULL, pc_fdc2214_sensor_freq_hz(1u << 27, 40000000u));
    // data = 2^28-... full scale approx fref.
    TEST_ASSERT_EQUAL_UINT64(0ULL, pc_fdc2214_sensor_freq_hz(0, 40000000u));
}

void test_build_config(void)
{
    uint8_t buf[FDC2214_CONFIG_MAX];
    size_t n = pc_fdc2214_build_config(buf, sizeof(buf), 0xFFFF, 0x0400);
    TEST_ASSERT_EQUAL_size_t(21, n); // 7 triples
    // First triple: RCOUNT_CH0 = 0xFFFF.
    TEST_ASSERT_EQUAL_HEX8(FDC2214_REG_RCOUNT_CH0, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[2]);
    // Second: SETTLECOUNT_CH0 = 0x0400.
    TEST_ASSERT_EQUAL_HEX8(FDC2214_REG_SETTLECOUNT_CH0, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x04, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[5]);
    // Last triple must be CONFIG (starts conversion).
    TEST_ASSERT_EQUAL_HEX8(FDC2214_REG_CONFIG, buf[18]);
    TEST_ASSERT_EQUAL_HEX8(0x1E, buf[19]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[20]);
}

void test_build_config_too_small(void)
{
    uint8_t small[10];
    TEST_ASSERT_EQUAL_size_t(0, pc_fdc2214_build_config(small, sizeof(small), 0xFFFF, 0x0400));
}

void test_build_config_null_buf(void)
{
    // buf == NULL must be rejected before the capacity check is even reached.
    TEST_ASSERT_EQUAL_size_t(0, pc_fdc2214_build_config(NULL, FDC2214_CONFIG_MAX, 0xFFFF, 0x0400));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_data_combine);
    RUN_TEST(test_freq_scale);
    RUN_TEST(test_build_config);
    RUN_TEST(test_build_config_too_small);
    RUN_TEST(test_build_config_null_buf);
    return UNITY_END();
}
