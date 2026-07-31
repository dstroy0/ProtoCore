// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/vl53l0x: the ToF ranging codec (range combine, data-ready, range status).
// The Wire ranging binding is ESP32-only and not exercised here.

#include "services/peripherals/vl53l0x/vl53l0x.h"
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_range_mm(void)
{
    TEST_ASSERT_EQUAL_UINT16(1234, pc_vl53l0x_range_mm(0x04, 0xD2)); // 0x04D2 = 1234 mm
    TEST_ASSERT_EQUAL_UINT16(0, pc_vl53l0x_range_mm(0x00, 0x00));
    TEST_ASSERT_EQUAL_UINT16(65535, pc_vl53l0x_range_mm(0xFF, 0xFF));
}

void test_data_ready(void)
{
    TEST_ASSERT_FALSE(pc_vl53l0x_data_ready(0x00));
    TEST_ASSERT_TRUE(pc_vl53l0x_data_ready(0x01));
    TEST_ASSERT_TRUE(pc_vl53l0x_data_ready(0x04));
    TEST_ASSERT_FALSE(pc_vl53l0x_data_ready(0x08)); // bit 3 is not a data-ready bit
}

void test_range_status(void)
{
    // DeviceRangeStatus = 11 (valid) in bits 6:3 -> register value 11<<3 = 0x58.
    TEST_ASSERT_EQUAL_UINT8(11, pc_vl53l0x_range_status(0x58));
    TEST_ASSERT_TRUE(pc_vl53l0x_range_valid(0x58));
    // Status 4 (phase fail) -> not valid.
    TEST_ASSERT_EQUAL_UINT8(4, pc_vl53l0x_range_status(4 << 3));
    TEST_ASSERT_FALSE(pc_vl53l0x_range_valid(4 << 3));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_range_mm);
    RUN_TEST(test_data_ready);
    RUN_TEST(test_range_status);
    return UNITY_END();
}
