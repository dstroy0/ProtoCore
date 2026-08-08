// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/ldc1614: the inductance-to-digital codec (data combine, error flags,
// frequency scale, config sequence). The Wire binding is ESP32-only and not exercised here.

#include "services/peripherals/ldc1614/ldc1614.h"
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_data_combine(void)
{
    uint32_t d = pc_ldc1614_data(0xF123, 0x4567); // error nibble 0xF, data MSB 0x123
    TEST_ASSERT_EQUAL_HEX32(0x01234567, d);
    TEST_ASSERT_EQUAL_UINT8(0xF, pc_ldc1614_error(0xF123));
    TEST_ASSERT_EQUAL_UINT8(0x0, pc_ldc1614_error(0x0123));
}

void test_freq_scale(void)
{
    TEST_ASSERT_EQUAL_UINT64(20000000ULL, pc_ldc1614_sensor_freq_hz(1u << 27, 40000000u));
    TEST_ASSERT_EQUAL_UINT64(0ULL, pc_ldc1614_sensor_freq_hz(0, 40000000u));
}

void test_build_config(void)
{
    uint8_t buf[LDC1614_CONFIG_MAX];
    size_t n = pc_ldc1614_build_config(buf, sizeof(buf), 0xFFFF, 0x0400);
    TEST_ASSERT_EQUAL_size_t(21, n);
    TEST_ASSERT_EQUAL_HEX8(LDC1614_REG_RCOUNT_CH0, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[2]);
    // CONFIG last (starts conversion).
    TEST_ASSERT_EQUAL_HEX8(LDC1614_REG_CONFIG, buf[18]);
    TEST_ASSERT_EQUAL_HEX8(0x16, buf[19]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[20]);
}

void test_build_config_too_small(void)
{
    uint8_t small[10];
    TEST_ASSERT_EQUAL_size_t(0, pc_ldc1614_build_config(small, sizeof(small), 0xFFFF, 0x0400));
}

void test_build_config_null_buf(void)
{
    TEST_ASSERT_EQUAL_size_t(0, pc_ldc1614_build_config(NULL, LDC1614_CONFIG_MAX, 0xFFFF, 0x0400));
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
