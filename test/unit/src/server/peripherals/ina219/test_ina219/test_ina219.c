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

// The bus cases at the bottom drive a datasheet model of the part (test/core_setup/hal/host/devices/
// ina219_device.h) rather than a primed byte queue: a suite applies a shunt drop and a bus
// voltage, the driver programs its own calibration and composes its own transfers, and the part
// computes current and power from them by the datasheet's own equations.

#include "server/peripherals/ina219/ina219.h"

#include "devices/ina219_device.h"

#include <unity.h>

static protocore_ina219_dev s_part;

void setUp(void)
{
    protocore_bus_host_reset();
    protocore_bus_host_detach_all();
    protocore_ina219_dev_place(&s_part, (uint16_t)PROTOCORE_INA219_I2C_ADDR);
}
void tearDown(void)
{
    protocore_bus_host_detach_all();
}

// SBOS448: the bus voltage sits in bits 15:3, one LSB is 4 mV, and bits 2:1 are CNVR and OVF.
// Each expectation is (raw >> 3) * 4 written out from that layout.
void test_sbos448_bus_voltage_register(void)
{
    Ina219V.bus_mv_args.raw = 0x0000u;
    Ina219.bus_mv(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(0, Ina219V.value);
    // one LSB: bit 3 set -> 4 mV
    Ina219V.bus_mv_args.raw = 0x0008u;
    Ina219.bus_mv(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(4, Ina219V.value);
    // 12.000 V = 3000 LSBs -> 3000 << 3 = 0x5DC0
    Ina219V.bus_mv_args.raw = 0x5DC0u;
    Ina219.bus_mv(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(12000, Ina219V.value);
    // 32.000 V = 8000 LSBs -> 8000 << 3 = 0xFA00, the top of the 32 V range
    Ina219V.bus_mv_args.raw = 0xFA00u;
    Ina219.bus_mv(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(32000, Ina219V.value);
    // the full 13-bit field: 8191 LSBs * 4 mV = 32764 mV
    Ina219V.bus_mv_args.raw = 0xFFF8u;
    Ina219.bus_mv(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(32764, Ina219V.value);
}

// The CNVR and OVF status bits share the register below the voltage field and must not reach the
// reading: the same voltage with every low bit set decodes identically.
void test_sbos448_bus_status_bits_do_not_reach_the_voltage(void)
{
    for (uint32_t lsbs = 0u; lsbs <= 8191u; lsbs += 97u)
    {
        uint16_t clean = (uint16_t)(lsbs << 3);
        int32_t want = (int32_t)(lsbs * 4u);
        Ina219V.bus_mv_args.raw = clean;
        Ina219.bus_mv(protocore_ina219_span());
        TEST_ASSERT_EQUAL_INT32(want, Ina219V.value);
        Ina219V.bus_mv_args.raw = (uint16_t)(clean | 0x0007u);
        Ina219.bus_mv(protocore_ina219_span());
        TEST_ASSERT_EQUAL_INT32(want, Ina219V.value);
    }
}

// SBOS448: the shunt voltage is signed with a 10 uV LSB, so the register is a two's-complement
// count of 10 uV steps.
void test_sbos448_shunt_voltage_register(void)
{
    Ina219V.shunt_uv_args.raw = 0;
    Ina219.shunt_uv(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(0, Ina219V.value);
    Ina219V.shunt_uv_args.raw = 1;
    Ina219.shunt_uv(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(10, Ina219V.value);
    Ina219V.shunt_uv_args.raw = -1;
    Ina219.shunt_uv(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(-10, Ina219V.value);
    // 320 mV, the full-scale of the /8 PGA range: 320000 uV / 10 = 32000 counts
    Ina219V.shunt_uv_args.raw = 32000;
    Ina219.shunt_uv(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(320000, Ina219V.value);
    Ina219V.shunt_uv_args.raw = -32000;
    Ina219.shunt_uv(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(-320000, Ina219V.value);
    // 40 mV, the /1 PGA range: 4000 counts
    Ina219V.shunt_uv_args.raw = 4000;
    Ina219.shunt_uv(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(40000, Ina219V.value);
    // the register's own extremes
    Ina219V.shunt_uv_args.raw = 32767;
    Ina219.shunt_uv(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(327670, Ina219V.value);
    Ina219V.shunt_uv_args.raw = (int16_t)-32768;
    Ina219.shunt_uv(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(-327680, Ina219V.value);
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
    Ina219V.calibration_args.current_lsb_ua = 100u;
    Ina219V.calibration_args.shunt_mohm = 100u;
    Ina219.calibration(protocore_ina219_span());
    TEST_ASSERT_EQUAL_HEX16(4096u, Ina219V.cal);
    Ina219V.calibration_args.current_lsb_ua = 50u;
    Ina219V.calibration_args.shunt_mohm = 100u;
    Ina219.calibration(protocore_ina219_span());
    TEST_ASSERT_EQUAL_HEX16(8192u, Ina219V.cal);
    Ina219V.calibration_args.current_lsb_ua = 10u;
    Ina219V.calibration_args.shunt_mohm = 100u;
    Ina219.calibration(protocore_ina219_span());
    TEST_ASSERT_EQUAL_HEX16(40960u, Ina219V.cal);
    Ina219V.calibration_args.current_lsb_ua = 100u;
    Ina219V.calibration_args.shunt_mohm = 10u;
    Ina219.calibration(protocore_ina219_span());
    TEST_ASSERT_EQUAL_HEX16(40960u, Ina219V.cal);
    Ina219V.calibration_args.current_lsb_ua = 400u;
    Ina219V.calibration_args.shunt_mohm = 100u;
    Ina219.calibration(protocore_ina219_span());
    TEST_ASSERT_EQUAL_HEX16(1024u, Ina219V.cal);
    Ina219V.calibration_args.current_lsb_ua = 1000u;
    Ina219V.calibration_args.shunt_mohm = 2u;
    Ina219.calibration(protocore_ina219_span());
    TEST_ASSERT_EQUAL_HEX16(20480u, Ina219V.cal);
}

// The equation truncates: 40960000 / (3 * 100) = 136533.33 clamps at the register width, while
// 40960000 / (7 * 100) = 58514.28 truncates to 58514.
void test_calibration_truncates_and_clamps_to_sixteen_bits(void)
{
    Ina219V.calibration_args.current_lsb_ua = 7u;
    Ina219V.calibration_args.shunt_mohm = 100u;
    Ina219.calibration(protocore_ina219_span());
    TEST_ASSERT_EQUAL_HEX16(58514u, Ina219V.cal);
    // anything at or beyond 65535 saturates rather than wrapping into a small calibration
    Ina219V.calibration_args.current_lsb_ua = 3u;
    Ina219V.calibration_args.shunt_mohm = 100u;
    Ina219.calibration(protocore_ina219_span());
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, Ina219V.cal);
    Ina219V.calibration_args.current_lsb_ua = 1u;
    Ina219V.calibration_args.shunt_mohm = 1u;
    Ina219.calibration(protocore_ina219_span());
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, Ina219V.cal);
    // 40960000 / 625 = 65536, one past the register, so it saturates too
    Ina219V.calibration_args.current_lsb_ua = 25u;
    Ina219V.calibration_args.shunt_mohm = 25u;
    Ina219.calibration(protocore_ina219_span());
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, Ina219V.cal);
    // 40960000 / 626 = 65431.0 -> 65431, the first value that fits
    Ina219V.calibration_args.current_lsb_ua = 626u;
    Ina219V.calibration_args.shunt_mohm = 1u;
    Ina219.calibration(protocore_ina219_span());
    TEST_ASSERT_EQUAL_HEX16(65431u, Ina219V.cal);
}

// A zero denominator has no calibration, so it reports 0 rather than dividing by zero.
void test_calibration_zero_denominator(void)
{
    Ina219V.calibration_args.current_lsb_ua = 0u;
    Ina219V.calibration_args.shunt_mohm = 100u;
    Ina219.calibration(protocore_ina219_span());
    TEST_ASSERT_EQUAL_HEX16(0u, Ina219V.cal);
    Ina219V.calibration_args.current_lsb_ua = 100u;
    Ina219V.calibration_args.shunt_mohm = 0u;
    Ina219.calibration(protocore_ina219_span());
    TEST_ASSERT_EQUAL_HEX16(0u, Ina219V.cal);
    Ina219V.calibration_args.current_lsb_ua = 0u;
    Ina219V.calibration_args.shunt_mohm = 0u;
    Ina219.calibration(protocore_ina219_span());
    TEST_ASSERT_EQUAL_HEX16(0u, Ina219V.cal);
}

// A coarser current LSB or a smaller shunt both need a smaller calibration value, since the
// equation divides by their product.
void test_calibration_falls_as_the_denominator_grows(void)
{
    uint16_t prev = 0xFFFFu;
    for (uint32_t lsb = 100u; lsb <= 2000u; lsb += 25u)
    {
        Ina219V.calibration_args.current_lsb_ua = lsb;
        Ina219V.calibration_args.shunt_mohm = 100u;
        Ina219.calibration(protocore_ina219_span());
        uint16_t cal = Ina219V.cal;
        TEST_ASSERT_TRUE(cal <= prev);
        prev = cal;
    }
    // The smaller shunt is captured before the larger one runs: both report through the one
    // namespace, so comparing them in a single expression would compare the second with itself.
    Ina219V.calibration_args.current_lsb_ua = 100u;
    Ina219V.calibration_args.shunt_mohm = 10u;
    Ina219.calibration(protocore_ina219_span());
    const uint16_t small_shunt = Ina219V.cal;
    Ina219V.calibration_args.current_lsb_ua = 100u;
    Ina219V.calibration_args.shunt_mohm = 100u;
    Ina219.calibration(protocore_ina219_span());
    TEST_ASSERT_TRUE(small_shunt > Ina219V.cal);
}

// SBOS448: the current register is a count of Current_LSB, so microamps is that count times the
// LSB in microamps.
void test_sbos448_current_scaling(void)
{
    Ina219V.current_ua_args.raw = 0;
    Ina219V.current_ua_args.current_lsb_ua = 100u;
    Ina219.current_ua(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(0, Ina219V.value);
    Ina219V.current_ua_args.raw = 1;
    Ina219V.current_ua_args.current_lsb_ua = 100u;
    Ina219.current_ua(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(100, Ina219V.value);
    // 1 A at 100 uA/bit is 10000 counts
    Ina219V.current_ua_args.raw = 10000;
    Ina219V.current_ua_args.current_lsb_ua = 100u;
    Ina219.current_ua(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(1000000, Ina219V.value);
    // current flowing the other way is negative
    Ina219V.current_ua_args.raw = -10000;
    Ina219V.current_ua_args.current_lsb_ua = 100u;
    Ina219.current_ua(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(-1000000, Ina219V.value);
    // the register's positive extreme at 100 uA/bit: 32767 * 100 = 3.2767 A
    Ina219V.current_ua_args.raw = 32767;
    Ina219V.current_ua_args.current_lsb_ua = 100u;
    Ina219.current_ua(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(3276700, Ina219V.value);
    Ina219V.current_ua_args.raw = (int16_t)-32768;
    Ina219V.current_ua_args.current_lsb_ua = 100u;
    Ina219.current_ua(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(-3276800, Ina219V.value);
}

// SBOS448: "Power_LSB = 20 x Current_LSB", so a power count is worth twenty times what the same
// current count is.
void test_sbos448_power_lsb_is_twenty_times_the_current_lsb(void)
{
    Ina219V.power_uw_args.raw = 0;
    Ina219V.power_uw_args.current_lsb_ua = 100u;
    Ina219.power_uw(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(0, Ina219V.value);
    Ina219V.power_uw_args.raw = 1;
    Ina219V.power_uw_args.current_lsb_ua = 100u;
    Ina219.power_uw(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(2000, Ina219V.value);
    // 1 W at a 100 uA current LSB (2 mW power LSB) is 500 counts
    Ina219V.power_uw_args.raw = 500;
    Ina219V.power_uw_args.current_lsb_ua = 100u;
    Ina219.power_uw(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(1000000, Ina219V.value);
    for (int32_t raw = -30000; raw < 30000; raw += 3701)
    {
        // The current is captured before the power runs: both report through the one namespace.
        Ina219V.current_ua_args.raw = (int16_t)raw;
        Ina219V.current_ua_args.current_lsb_ua = 100u;
        Ina219.current_ua(protocore_ina219_span());
        const int32_t current = Ina219V.value;
        Ina219V.power_uw_args.raw = (int16_t)raw;
        Ina219V.power_uw_args.current_lsb_ua = 100u;
        Ina219.power_uw(protocore_ina219_span());
        TEST_ASSERT_EQUAL_INT32(20 * current, Ina219V.value);
    }
}

// Both scalings are linear through zero, so they are odd about it.
void test_current_and_power_are_odd_about_zero(void)
{
    for (int32_t raw = 1; raw <= 32767; raw += 991)
    {
        // Each positive reading is captured before its negative runs: both report through the one
        // namespace, so testing them in a single expression would test the second one twice.
        Ina219V.current_ua_args.raw = (int16_t)raw;
        Ina219V.current_ua_args.current_lsb_ua = 250u;
        Ina219.current_ua(protocore_ina219_span());
        const int32_t current_pos = Ina219V.value;
        Ina219V.current_ua_args.raw = (int16_t)(-raw);
        Ina219V.current_ua_args.current_lsb_ua = 250u;
        Ina219.current_ua(protocore_ina219_span());
        TEST_ASSERT_EQUAL_INT32(-current_pos, Ina219V.value);
        Ina219V.power_uw_args.raw = (int16_t)raw;
        Ina219V.power_uw_args.current_lsb_ua = 250u;
        Ina219.power_uw(protocore_ina219_span());
        const int32_t power_pos = Ina219V.value;
        Ina219V.power_uw_args.raw = (int16_t)(-raw);
        Ina219V.power_uw_args.current_lsb_ua = 250u;
        Ina219.power_uw(protocore_ina219_span());
        TEST_ASSERT_EQUAL_INT32(-power_pos, Ina219V.value);
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
        Ina219V.bus_mv_args.raw = (uint16_t)(lsbs << 3);
        Ina219.bus_mv(protocore_ina219_span());
        int32_t mv = Ina219V.value;
        TEST_ASSERT_TRUE(mv > prev);
        prev = mv;
    }
    TEST_ASSERT_EQUAL_INT32(32764, prev);
}

// ---------------------------------------------------------------------------
// Over the bus, against the device model
// ---------------------------------------------------------------------------

// SBOS448G 8.6.2.1 publishes the config reset as 399Fh, and 8.6.3.3 / 8.6.3.4 / 8.6.4.1 publish
// the power, current and calibration resets as 0. The address pointer persists until a write
// moves it (8.5.5.3). Asserted through the platform seam: every case below reads a register the
// pointer selected, and a model that lost it would answer plausibly wrong.
void test_sbos448g_model_reset_values_and_a_persistent_address_pointer(void)
{
    uint8_t reg = 0x00u;
    uint8_t r[2] = {0u, 0u};
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_INA219_I2C_ADDR, &reg, 1u, r, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX16(0x399Fu, (uint16_t)(((uint16_t)r[0] << 8) | r[1]));
    // no pointer this time: the part still answers from the register it was left on
    TEST_ASSERT_TRUE(protocore_platform_i2c_read(0u, (uint16_t)PROTOCORE_INA219_I2C_ADDR, r, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX16(0x399Fu, (uint16_t)(((uint16_t)r[0] << 8) | r[1]));
    reg = 0x05u; // calibration, reset 0
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_INA219_I2C_ADDR, &reg, 1u, r, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX16(0x0000u, (uint16_t)(((uint16_t)r[0] << 8) | r[1]));
    // 8.5.3: "The Current register and Power register are only available if the Calibration
    // register contains a programmed value" - until begin() writes one, both read zero.
    reg = 0x04u;
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_INA219_I2C_ADDR, &reg, 1u, r, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX16(0x0000u, (uint16_t)(((uint16_t)r[0] << 8) | r[1]));
    reg = 0x03u;
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_INA219_I2C_ADDR, &reg, 1u, r, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX16(0x0000u, (uint16_t)(((uint16_t)r[0] << 8) | r[1]));
}

// SBOS448G 8.5.2 Eq 1: the calibration is trunc(0.04096 / (Current_LSB * R_shunt)), which at
// 100 uA/bit and 100 mohm is 4096. begin() programs that, then the 32 V / 320 mV / 12-bit
// continuous config word - two writes, in that order, to the device address.
void test_sbos448g_begin_programs_the_calibration_then_the_config(void)
{
    Ina219V.begin_args.addr = (uint8_t)PROTOCORE_INA219_I2C_ADDR;
    Ina219V.begin_args.current_lsb_ua = 100u;
    Ina219V.begin_args.shunt_mohm = 100u;
    Ina219.begin(protocore_ina219_span());
    TEST_ASSERT_TRUE(Ina219V.ok);

    uint32_t len = 0u;
    const uint8_t *tx = protocore_bus_host_written(&len);
    TEST_ASSERT_EQUAL_UINT32(2u, protocore_bus_host_log_len);
    // the calibration write: pointer 05h, then 4096 big-endian
    TEST_ASSERT_EQUAL_HEX16(PROTOCORE_INA219_I2C_ADDR, protocore_bus_host_log[0].target);
    TEST_ASSERT_EQUAL_UINT32(3u, protocore_bus_host_log[0].wlen);
    TEST_ASSERT_EQUAL_HEX8(0x05u, tx[protocore_bus_host_log[0].woff]);
    TEST_ASSERT_EQUAL_HEX8(0x10u, tx[protocore_bus_host_log[0].woff + 1u]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, tx[protocore_bus_host_log[0].woff + 2u]);
    // the config write: pointer 00h, then 399Fh
    TEST_ASSERT_EQUAL_UINT32(3u, protocore_bus_host_log[1].wlen);
    TEST_ASSERT_EQUAL_HEX8(0x00u, tx[protocore_bus_host_log[1].woff]);
    TEST_ASSERT_EQUAL_HEX8(0x39u, tx[protocore_bus_host_log[1].woff + 1u]);
    TEST_ASSERT_EQUAL_HEX8(0x9Fu, tx[protocore_bus_host_log[1].woff + 2u]);
    TEST_ASSERT_EQUAL_UINT32(6u, len);
}

// SBOS448G 8.6.3.2: the bus reading is a count of 4 mV in bits 15:3. 12.000 V applied is count
// 3000, and the driver shifts and scales it back to 12000 mV.
void test_sbos448g_the_bus_reading_is_the_applied_voltage(void)
{
    s_part.bus_uv = 12000000;
    int32_t mv = 0;
    Ina219V.read_bus_mv_args.millivolts = &mv;
    Ina219.read_bus_mv(protocore_ina219_span());
    TEST_ASSERT_TRUE(Ina219V.ok);
    TEST_ASSERT_EQUAL_INT32(12000, mv);
}

// SBOS448G 8.6.3.1: one shunt count is 10 uV, signed. A 100 mV drop is register 10000, and the
// driver scales it back to 100000 uV. Current the other way is negative.
void test_sbos448g_the_shunt_reading_is_the_applied_drop(void)
{
    s_part.shunt_uv = 100000;
    int32_t uv = 0;
    Ina219V.read_shunt_uv_args.microvolts = &uv;
    Ina219.read_shunt_uv(protocore_ina219_span());
    TEST_ASSERT_TRUE(Ina219V.ok);
    TEST_ASSERT_EQUAL_INT32(100000, uv);
    s_part.shunt_uv = -100000;
    Ina219V.read_shunt_uv_args.microvolts = &uv;
    Ina219.read_shunt_uv(protocore_ina219_span());
    TEST_ASSERT_TRUE(Ina219V.ok);
    TEST_ASSERT_EQUAL_INT32(-100000, uv);
}

// SBOS448G 8.6.2.1 Table 4: the PGA defaults to /8, a +/-320 mV range, and 8.6.3.1's table clips
// the register there. A 400 mV drop reads as 320 mV, which is the range and not the input.
void test_sbos448g_a_drop_past_the_pga_range_clips(void)
{
    s_part.shunt_uv = 400000;
    int32_t uv = 0;
    Ina219V.read_shunt_uv_args.microvolts = &uv;
    Ina219.read_shunt_uv(protocore_ina219_span());
    TEST_ASSERT_TRUE(Ina219V.ok);
    TEST_ASSERT_EQUAL_INT32(320000, uv);
    s_part.shunt_uv = -400000;
    Ina219V.read_shunt_uv_args.microvolts = &uv;
    Ina219.read_shunt_uv(protocore_ina219_span());
    TEST_ASSERT_TRUE(Ina219V.ok);
    TEST_ASSERT_EQUAL_INT32(-320000, uv);
}

// The whole chain, end to end. SBOS448G Eq 1 sets the calibration from the shunt and the current
// LSB, Eq 4 turns the shunt register into the current register, and the driver scales that by the
// LSB: 1.000 A through a 100 mohm shunt is a 100 mV drop, and at 100 uA/bit it reads 1000000 uA.
void test_sbos448g_one_amp_through_the_shunt_reads_one_amp(void)
{
    Ina219V.begin_args.addr = (uint8_t)PROTOCORE_INA219_I2C_ADDR;
    Ina219V.begin_args.current_lsb_ua = 100u;
    Ina219V.begin_args.shunt_mohm = 100u;
    Ina219.begin(protocore_ina219_span());
    TEST_ASSERT_TRUE(Ina219V.ok);

    s_part.shunt_uv = 100000; // 1.000 A across 100 mohm
    int32_t ua = 0;
    Ina219V.read_current_ua_args.microamps = &ua;
    Ina219.read_current_ua(protocore_ina219_span());
    TEST_ASSERT_TRUE(Ina219V.ok);
    TEST_ASSERT_EQUAL_INT32(1000000, ua);

    s_part.shunt_uv = -100000; // and the other way
    Ina219V.read_current_ua_args.microamps = &ua;
    Ina219.read_current_ua(protocore_ina219_span());
    TEST_ASSERT_TRUE(Ina219V.ok);
    TEST_ASSERT_EQUAL_INT32(-1000000, ua);
}

// SBOS448G Eq 5 with Eq 3: the power register times 20 * Current_LSB is watts, and watts is volts
// times amps. 12.000 V at 1.000 A is 12 W, reached through the part's own two divisions.
void test_sbos448g_power_is_the_bus_voltage_times_the_current(void)
{
    Ina219V.begin_args.addr = (uint8_t)PROTOCORE_INA219_I2C_ADDR;
    Ina219V.begin_args.current_lsb_ua = 100u;
    Ina219V.begin_args.shunt_mohm = 100u;
    Ina219.begin(protocore_ina219_span());

    s_part.bus_uv = 12000000; // 12.000 V
    s_part.shunt_uv = 100000; // 1.000 A across 100 mohm
    int32_t uw = 0;
    Ina219V.read_power_uw_args.microwatts = &uw;
    Ina219.read_power_uw(protocore_ina219_span());
    TEST_ASSERT_TRUE(Ina219V.ok);
    TEST_ASSERT_EQUAL_INT32(12000000, uw);
}

// SBOS448G 8.5.3: without a programmed calibration the current and power registers are not
// available, so a reading taken before begin() is zero however much current is flowing.
void test_sbos448g_current_reads_zero_until_the_calibration_is_programmed(void)
{
    s_part.shunt_uv = 100000;
    s_part.bus_uv = 12000000;
    int32_t ua = -1;
    Ina219V.read_current_ua_args.microamps = &ua;
    Ina219.read_current_ua(protocore_ina219_span());
    TEST_ASSERT_TRUE(Ina219V.ok);
    TEST_ASSERT_EQUAL_INT32(0, ua);
    int32_t uw = -1;
    Ina219V.read_power_uw_args.microwatts = &uw;
    Ina219.read_power_uw(protocore_ina219_span());
    TEST_ASSERT_TRUE(Ina219V.ok);
    TEST_ASSERT_EQUAL_INT32(0, uw);
}

// A finer current LSB is a larger calibration, and the two cancel: the same current through the
// same shunt reads the same amperage whichever scaling was programmed.
void test_the_reading_is_independent_of_the_programmed_current_lsb(void)
{
    static const uint32_t LSB_UA[3] = {50u, 100u, 200u};
    for (uint32_t i = 0; i < 3u; i++)
    {
        Ina219V.begin_args.addr = (uint8_t)PROTOCORE_INA219_I2C_ADDR;
        Ina219V.begin_args.current_lsb_ua = LSB_UA[i];
        Ina219V.begin_args.shunt_mohm = 100u;
        Ina219.begin(protocore_ina219_span());
        s_part.shunt_uv = 100000; // 1.000 A across 100 mohm
        int32_t ua = 0;
        Ina219V.read_current_ua_args.microamps = &ua;
        Ina219.read_current_ua(protocore_ina219_span());
        TEST_ASSERT_TRUE(Ina219V.ok);
        TEST_ASSERT_EQUAL_INT32(1000000, ua);
    }
}

// begin() sends later transfers to the address it was given, and back to the strapped default
// when handed zero - so the address is state and not a constant. 8.5.5.1 Table 1 gives 1000000b
// through 1001111b as the sixteen A1/A0 straps; 41h is A1 = GND, A0 = VS+.
void test_begin_sends_later_transfers_to_the_address_it_was_given(void)
{
    protocore_bus_host_detach_all();
    protocore_ina219_dev_place(&s_part, 0x41u);
    Ina219V.begin_args.addr = 0x41u;
    Ina219V.begin_args.current_lsb_ua = 100u;
    Ina219V.begin_args.shunt_mohm = 100u;
    Ina219.begin(protocore_ina219_span());
    TEST_ASSERT_TRUE(Ina219V.ok);
    s_part.shunt_uv = 100000;
    int32_t ua = 0;
    Ina219V.read_current_ua_args.microamps = &ua;
    Ina219.read_current_ua(protocore_ina219_span());
    TEST_ASSERT_EQUAL_INT32(1000000, ua);
    TEST_ASSERT_EQUAL_HEX16(0x41u, protocore_bus_host_log[protocore_bus_host_log_len - 1u].target);
    Ina219V.begin_args.addr = 0u;
    Ina219V.begin_args.current_lsb_ua = 100u;
    Ina219V.begin_args.shunt_mohm = 100u;
    Ina219.begin(protocore_ina219_span());
    TEST_ASSERT_EQUAL_HEX16(PROTOCORE_INA219_I2C_ADDR, protocore_bus_host_log[protocore_bus_host_log_len - 1u].target);
}

// A part at another address does not answer, so the reading returns nothing rather than someone
// else's bytes.
void test_a_part_at_another_address_is_not_read(void)
{
    protocore_bus_host_detach_all();
    protocore_ina219_dev_place(&s_part, (uint16_t)(PROTOCORE_INA219_I2C_ADDR + 1));
    s_part.bus_uv = 12000000;
    int32_t mv = -1;
    Ina219V.read_bus_mv_args.millivolts = &mv;
    Ina219.read_bus_mv(protocore_ina219_span());
    TEST_ASSERT_TRUE(Ina219V.ok); // the transfer completed; nothing answered it
    TEST_ASSERT_EQUAL_INT32(0, mv);
}

// A refused transfer is reported rather than handed back as a reading.
void test_a_refused_transfer_fails_the_reading(void)
{
    s_part.bus_uv = 12000000;
    protocore_bus_host_fail = 1u;
    int32_t mv = 12345;
    Ina219V.read_bus_mv_args.millivolts = &mv;
    Ina219.read_bus_mv(protocore_ina219_span());
    TEST_ASSERT_FALSE(Ina219V.ok);
    TEST_ASSERT_EQUAL_INT32(12345, mv);
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_bus_host_log_len);
}

// begin() reports a bus that would not take the calibration write.
void test_begin_reports_a_refused_write(void)
{
    protocore_bus_host_fail = 1u;
    Ina219V.begin_args.addr = (uint8_t)PROTOCORE_INA219_I2C_ADDR;
    Ina219V.begin_args.current_lsb_ua = 100u;
    Ina219V.begin_args.shunt_mohm = 100u;
    Ina219.begin(protocore_ina219_span());
    TEST_ASSERT_FALSE(Ina219V.ok);
}
