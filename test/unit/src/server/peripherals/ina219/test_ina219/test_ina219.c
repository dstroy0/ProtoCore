// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the TI INA219 current/power codec (server/peripherals/ina219/ina219.h).
//
// The governing document is the INA219 data sheet SBOS448. Its Bus Voltage register (02h) carries
// the reading in bits 15:3 with a 4 mV LSB and the CNVR / OVF status bits below it; the Shunt
// Voltage register (01h) is signed with a 10 uV LSB; the calibration register is
//     Cal = trunc(0.04096 / (Current_LSB * R_SHUNT))
// and the Power register LSB is 20 times the Current LSB.
//
// test_sbos448_calibration_equation is the load-bearing case: the equation is printed in the data
// sheet and the check value follows from it directly - 0.04096 / (100 uA * 0.1 ohm) = 4096 - so a
// wrong constant or a wrong unit scaling cannot land on it.

#include "server/peripherals/ina219/ina219.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// SBOS448: the bus voltage sits in bits 15:3, one LSB is 4 mV, and bits 2:1 are CNVR and OVF.
// Each expectation is (raw >> 3) * 4 written out from that layout.
void test_sbos448_bus_voltage_register(void)
{
    TEST_ASSERT_EQUAL_INT32(0, protocore_ina219_bus_mv(0x0000u));
    // one LSB: bit 3 set -> 4 mV
    TEST_ASSERT_EQUAL_INT32(4, protocore_ina219_bus_mv(0x0008u));
    // 12.000 V = 3000 LSBs -> 3000 << 3 = 0x5DC0
    TEST_ASSERT_EQUAL_INT32(12000, protocore_ina219_bus_mv(0x5DC0u));
    // 32.000 V = 8000 LSBs -> 8000 << 3 = 0xFA00, the top of the 32 V range
    TEST_ASSERT_EQUAL_INT32(32000, protocore_ina219_bus_mv(0xFA00u));
    // the full 13-bit field: 8191 LSBs * 4 mV = 32764 mV
    TEST_ASSERT_EQUAL_INT32(32764, protocore_ina219_bus_mv(0xFFF8u));
}

// The CNVR and OVF status bits share the register below the voltage field and must not reach the
// reading: the same voltage with every low bit set decodes identically.
void test_sbos448_bus_status_bits_do_not_reach_the_voltage(void)
{
    for (uint32_t lsbs = 0u; lsbs <= 8191u; lsbs += 97u)
    {
        uint16_t clean = (uint16_t)(lsbs << 3);
        int32_t want = (int32_t)(lsbs * 4u);
        TEST_ASSERT_EQUAL_INT32(want, protocore_ina219_bus_mv(clean));
        TEST_ASSERT_EQUAL_INT32(want, protocore_ina219_bus_mv((uint16_t)(clean | 0x0007u)));
    }
}

// SBOS448: the shunt voltage is signed with a 10 uV LSB, so the register is a two's-complement
// count of 10 uV steps.
void test_sbos448_shunt_voltage_register(void)
{
    TEST_ASSERT_EQUAL_INT32(0, protocore_ina219_shunt_uv(0));
    TEST_ASSERT_EQUAL_INT32(10, protocore_ina219_shunt_uv(1));
    TEST_ASSERT_EQUAL_INT32(-10, protocore_ina219_shunt_uv(-1));
    // 320 mV, the full-scale of the /8 PGA range: 320000 uV / 10 = 32000 counts
    TEST_ASSERT_EQUAL_INT32(320000, protocore_ina219_shunt_uv(32000));
    TEST_ASSERT_EQUAL_INT32(-320000, protocore_ina219_shunt_uv(-32000));
    // 40 mV, the /1 PGA range: 4000 counts
    TEST_ASSERT_EQUAL_INT32(40000, protocore_ina219_shunt_uv(4000));
    // the register's own extremes
    TEST_ASSERT_EQUAL_INT32(327670, protocore_ina219_shunt_uv(32767));
    TEST_ASSERT_EQUAL_INT32(-327680, protocore_ina219_shunt_uv((int16_t)-32768));
}

// SBOS448: "Cal = trunc(0.04096 / (Current_LSB x R_SHUNT))", with Current_LSB in amps and R_SHUNT
// in ohms. In the units this codec takes that is 40960000 / (lsb_uA * shunt_mOhm):
//   100 uA/bit over 100 mohm  -> 0.04096 / (0.0001 * 0.1)  = 4096
//    50 uA/bit over 100 mohm  -> 0.04096 / (0.00005 * 0.1) = 8192
//    10 uA/bit over 100 mohm  -> 0.04096 / (0.00001 * 0.1) = 40960
//   100 uA/bit over  10 mohm  -> 0.04096 / (0.0001 * 0.01) = 40960
//   400 uA/bit over 100 mohm  -> 0.04096 / (0.0004 * 0.1)  = 1024
//  1000 uA/bit over   2 mohm  -> 0.04096 / (0.001 * 0.002) = 20480
void test_sbos448_calibration_equation(void)
{
    TEST_ASSERT_EQUAL_HEX16(4096u, protocore_ina219_calibration(100u, 100u));
    TEST_ASSERT_EQUAL_HEX16(8192u, protocore_ina219_calibration(50u, 100u));
    TEST_ASSERT_EQUAL_HEX16(40960u, protocore_ina219_calibration(10u, 100u));
    TEST_ASSERT_EQUAL_HEX16(40960u, protocore_ina219_calibration(100u, 10u));
    TEST_ASSERT_EQUAL_HEX16(1024u, protocore_ina219_calibration(400u, 100u));
    TEST_ASSERT_EQUAL_HEX16(20480u, protocore_ina219_calibration(1000u, 2u));
}

// The equation truncates: 40960000 / (3 * 100) = 136533.33 clamps at the register width, while
// 40960000 / (7 * 100) = 58514.28 truncates to 58514.
void test_calibration_truncates_and_clamps_to_sixteen_bits(void)
{
    TEST_ASSERT_EQUAL_HEX16(58514u, protocore_ina219_calibration(7u, 100u));
    // anything at or beyond 65535 saturates rather than wrapping into a small calibration
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, protocore_ina219_calibration(3u, 100u));
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, protocore_ina219_calibration(1u, 1u));
    // 40960000 / 625 = 65536, one past the register, so it saturates too
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, protocore_ina219_calibration(25u, 25u));
    // 40960000 / 626 = 65431.0 -> 65431, the first value that fits
    TEST_ASSERT_EQUAL_HEX16(65431u, protocore_ina219_calibration(626u, 1u));
}

// A zero denominator has no calibration, so it reports 0 rather than dividing by zero.
void test_calibration_zero_denominator(void)
{
    TEST_ASSERT_EQUAL_HEX16(0u, protocore_ina219_calibration(0u, 100u));
    TEST_ASSERT_EQUAL_HEX16(0u, protocore_ina219_calibration(100u, 0u));
    TEST_ASSERT_EQUAL_HEX16(0u, protocore_ina219_calibration(0u, 0u));
}

// A coarser current LSB or a smaller shunt both need a smaller calibration value, since the
// equation divides by their product.
void test_calibration_falls_as_the_denominator_grows(void)
{
    uint16_t prev = 0xFFFFu;
    for (uint32_t lsb = 100u; lsb <= 2000u; lsb += 25u)
    {
        uint16_t cal = protocore_ina219_calibration(lsb, 100u);
        TEST_ASSERT_TRUE(cal <= prev);
        prev = cal;
    }
    TEST_ASSERT_TRUE(protocore_ina219_calibration(100u, 10u) > protocore_ina219_calibration(100u, 100u));
}

// SBOS448: the current register is a count of Current_LSB, so microamps is that count times the
// LSB in microamps.
void test_sbos448_current_scaling(void)
{
    TEST_ASSERT_EQUAL_INT32(0, protocore_ina219_current_ua(0, 100u));
    TEST_ASSERT_EQUAL_INT32(100, protocore_ina219_current_ua(1, 100u));
    // 1 A at 100 uA/bit is 10000 counts
    TEST_ASSERT_EQUAL_INT32(1000000, protocore_ina219_current_ua(10000, 100u));
    // current flowing the other way is negative
    TEST_ASSERT_EQUAL_INT32(-1000000, protocore_ina219_current_ua(-10000, 100u));
    // the register's positive extreme at 100 uA/bit: 32767 * 100 = 3.2767 A
    TEST_ASSERT_EQUAL_INT32(3276700, protocore_ina219_current_ua(32767, 100u));
    TEST_ASSERT_EQUAL_INT32(-3276800, protocore_ina219_current_ua((int16_t)-32768, 100u));
}

// SBOS448: "Power_LSB = 20 x Current_LSB", so a power count is worth twenty times what the same
// current count is.
void test_sbos448_power_lsb_is_twenty_times_the_current_lsb(void)
{
    TEST_ASSERT_EQUAL_INT32(0, protocore_ina219_power_uw(0, 100u));
    TEST_ASSERT_EQUAL_INT32(2000, protocore_ina219_power_uw(1, 100u));
    // 1 W at a 100 uA current LSB (2 mW power LSB) is 500 counts
    TEST_ASSERT_EQUAL_INT32(1000000, protocore_ina219_power_uw(500, 100u));
    for (int32_t raw = -30000; raw < 30000; raw += 3701)
    {
        TEST_ASSERT_EQUAL_INT32(20 * protocore_ina219_current_ua((int16_t)raw, 100u),
                                protocore_ina219_power_uw((int16_t)raw, 100u));
    }
}

// Both scalings are linear through zero, so they are odd about it.
void test_current_and_power_are_odd_about_zero(void)
{
    for (int32_t raw = 1; raw <= 32767; raw += 991)
    {
        TEST_ASSERT_EQUAL_INT32(-protocore_ina219_current_ua((int16_t)raw, 250u),
                                protocore_ina219_current_ua((int16_t)(-raw), 250u));
        TEST_ASSERT_EQUAL_INT32(-protocore_ina219_power_uw((int16_t)raw, 250u),
                                protocore_ina219_power_uw((int16_t)(-raw), 250u));
    }
}

// SBOS448 register map: config 00h, shunt 01h, bus 02h, power 03h, current 04h, calibration 05h.
void test_sbos448_register_addresses(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00u, INA219_REG_CONFIG);
    TEST_ASSERT_EQUAL_HEX8(0x01u, INA219_REG_SHUNT);
    TEST_ASSERT_EQUAL_HEX8(0x02u, INA219_REG_BUS);
    TEST_ASSERT_EQUAL_HEX8(0x03u, INA219_REG_POWER);
    TEST_ASSERT_EQUAL_HEX8(0x04u, INA219_REG_CURRENT);
    TEST_ASSERT_EQUAL_HEX8(0x05u, INA219_REG_CALIBRATION);
}

// The bus decode is monotone across the whole 13-bit field: a higher register reading is never a
// lower voltage.
void test_bus_voltage_is_monotone(void)
{
    int32_t prev = -1;
    for (uint32_t lsbs = 0u; lsbs <= 8191u; lsbs++)
    {
        int32_t mv = protocore_ina219_bus_mv((uint16_t)(lsbs << 3));
        TEST_ASSERT_TRUE(mv > prev);
        prev = mv;
    }
    TEST_ASSERT_EQUAL_INT32(32764, prev);
}
