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

// begin() writes the calibration register big-endian, and each read addresses its own register.
void test_begin_and_read_drive_the_bus()
{
    pc_bus_host_reset();
    TEST_ASSERT_TRUE(pc_ina219_begin(0x40, 100, 100));

    uint32_t n = 0;
    const uint8_t *w = pc_bus_host_txn_bytes(0, &n);
    TEST_ASSERT_EQUAL_UINT32(3, n);
    TEST_ASSERT_EQUAL_HEX8(INA219_REG_CALIBRATION, w[0]);
    uint16_t cal = (uint16_t)(((uint16_t)w[1] << 8) | w[2]);
    TEST_ASSERT_EQUAL_UINT16(pc_ina219_calibration(100, 100), cal);

    pc_bus_host_reset();
    const uint8_t reply[2] = {0x1F, 0xA0}; // bus voltage register, LSB 4 mV after the 3-bit shift
    pc_bus_host_preload(reply, sizeof(reply));
    int32_t v = 0;
    TEST_ASSERT_TRUE(pc_ina219_read_bus_mv(&v));
    w = pc_bus_host_txn_bytes(0, &n);
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_HEX8(INA219_REG_BUS_VOLTAGE, w[0]);
}

// A sensor that does not acknowledge reports failure, so a caller never mistakes an unavailable
// part for a zero reading.
void test_reads_fail_closed_when_silent()
{
    pc_bus_host_reset();
    int32_t v = 123;
    pc_bus_host_fail_next(4);
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
    RUN_TEST(test_begin_and_read_drive_the_bus);
    RUN_TEST(test_reads_fail_closed_when_silent);
    return UNITY_END();
}
