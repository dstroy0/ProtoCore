// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the INA219 current/power codec (services/peripherals/ina219): decoding the bus-voltage
// register (value in bits [15:3], LSB 4 mV, low status bits ignored) and the shunt-voltage
// register (signed, LSB 10 uV), computing the calibration register from the current LSB and the
// shunt resistance, and scaling the raw current / power registers. The I2C transfer is
// ESP32-only.

#include "services/peripherals/ina219/ina219.h"
#include <stdint.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_bus_mv()
{
    // 3300 mV -> value 825 (0x339) in bits [15:3] -> register 825<<3 = 0x19C8.
    TEST_ASSERT_EQUAL_INT32(3300, pc_ina219_bus_mv(0x19C8));
    // The low status bits (CNVR bit1, OVF bit0) must not affect the value.
    TEST_ASSERT_EQUAL_INT32(3300, pc_ina219_bus_mv(0x19CB));
    TEST_ASSERT_EQUAL_INT32(0, pc_ina219_bus_mv(0));
}

void test_shunt_uv()
{
    TEST_ASSERT_EQUAL_INT32(3200, pc_ina219_shunt_uv(320));   // 320 * 10 uV
    TEST_ASSERT_EQUAL_INT32(-1000, pc_ina219_shunt_uv(-100)); // signed
    TEST_ASSERT_EQUAL_INT32(0, pc_ina219_shunt_uv(0));
}

void test_calibration()
{
    TEST_ASSERT_EQUAL_UINT16(4096, pc_ina219_calibration(100, 100)); // 100 uA LSB, 0.1 ohm shunt
    TEST_ASSERT_EQUAL_UINT16(8192, pc_ina219_calibration(50, 100));  // finer LSB -> larger cal
    TEST_ASSERT_EQUAL_UINT16(0, pc_ina219_calibration(0, 100));      // guard: zero denominator
}

void test_current_and_power()
{
    // current = raw * current_LSB (uA); power = raw * 20 * current_LSB (uW).
    TEST_ASSERT_EQUAL_INT32(100000, pc_ina219_current_ua(1000, 100)); // 100 mA
    TEST_ASSERT_EQUAL_INT32(-50000, pc_ina219_current_ua(-500, 100)); // signed
    TEST_ASSERT_EQUAL_INT32(1000000, pc_ina219_power_uw(500, 100));   // 1 W
}

void test_host_i2c_stubs_fail_closed()
{
    // On a host build there is no I2C: begin and every read fail closed (return false), so a caller
    // never mistakes an unavailable sensor for a zero reading.
    TEST_ASSERT_FALSE(pc_ina219_begin(0x40, 100, 100));
    int32_t v = 123;
    TEST_ASSERT_FALSE(pc_ina219_read_bus_mv(&v));
    TEST_ASSERT_FALSE(pc_ina219_read_shunt_uv(&v));
    TEST_ASSERT_FALSE(pc_ina219_read_current_ua(&v));
    TEST_ASSERT_FALSE(pc_ina219_read_power_uw(&v));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_bus_mv);
    RUN_TEST(test_shunt_uv);
    RUN_TEST(test_calibration);
    RUN_TEST(test_current_and_power);
    RUN_TEST(test_host_i2c_stubs_fail_closed);
    return UNITY_END();
}
