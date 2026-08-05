// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the PCA9685 PWM/servo codec (services/peripherals/pca9685): the PRESCALE computation from a
// PWM frequency (with clamping), the per-channel register address, the servo pulse-width ->
// 12-bit count conversion (with clamping), and the 5-byte channel PWM write encoder (including
// the full-on/off flag). The I2C writes are ESP32-only.

#include "services/peripherals/pca9685/pca9685.h"
#include <stdint.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_prescale()
{
    TEST_ASSERT_EQUAL_UINT8(121, pc_pca9685_prescale(50)); // classic servo frequency
    TEST_ASSERT_EQUAL_UINT8(30, pc_pca9685_prescale(200)); //
    TEST_ASSERT_EQUAL_UINT8(5, pc_pca9685_prescale(1000)); //
    TEST_ASSERT_EQUAL_UINT8(253, pc_pca9685_prescale(24)); // near the low-frequency end
    TEST_ASSERT_EQUAL_UINT8(3, pc_pca9685_prescale(1526)); // clamps at PRESCALE_MIN
    TEST_ASSERT_EQUAL_UINT8(3, pc_pca9685_prescale(4000)); // above range -> min
    TEST_ASSERT_EQUAL_UINT8(255, pc_pca9685_prescale(0));  // guard: no divide by zero
    // Low enough frequency that the raw (pre - 1) value overflows PRESCALE_MAX, exercising the
    // high-side clamp (unlike freq=24/1526/4000 above, which land at or below 255 already).
    TEST_ASSERT_EQUAL_UINT8(255, pc_pca9685_prescale(20));
    // High enough frequency that round(25e6 / (4096*freq)) truncates to 0, exercising the
    // `pre ? pre - 1 : 0` false branch before the low-side clamp brings it back to PRESCALE_MIN.
    TEST_ASSERT_EQUAL_UINT8(3, pc_pca9685_prescale(20000));
}

void test_channel_reg()
{
    TEST_ASSERT_EQUAL_HEX8(0x06, pc_pca9685_channel_reg(0));
    TEST_ASSERT_EQUAL_HEX8(0x0A, pc_pca9685_channel_reg(1));
    TEST_ASSERT_EQUAL_HEX8(0x42, pc_pca9685_channel_reg(15));
    TEST_ASSERT_EQUAL_HEX8(0x00, pc_pca9685_channel_reg(16)); // out of range
}

void test_us_to_count()
{
    TEST_ASSERT_EQUAL_UINT16(307, pc_pca9685_us_to_count(1500, 50));   // servo mid (1.5 ms of 20 ms)
    TEST_ASSERT_EQUAL_UINT16(102, pc_pca9685_us_to_count(500, 50));    // servo min
    TEST_ASSERT_EQUAL_UINT16(512, pc_pca9685_us_to_count(2500, 50));   // servo max
    TEST_ASSERT_EQUAL_UINT16(4095, pc_pca9685_us_to_count(20000, 50)); // a full period clamps to max
}

void test_set_pwm_bytes()
{
    uint8_t b[5];
    // channel 0, on=0, off=307 (0x133) -> reg 0x06, off_l 0x33, off_h 0x01.
    TEST_ASSERT_EQUAL_INT(5, (int)pc_pca9685_set_pwm_bytes(b, sizeof(b), 0, 0, 307));
    const uint8_t want0[5] = {0x06, 0x00, 0x00, 0x33, 0x01};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want0, b, 5);

    // channel 15, off=4095 (0xFFF) -> reg 0x42, off_l 0xFF, off_h 0x0F.
    TEST_ASSERT_EQUAL_INT(5, (int)pc_pca9685_set_pwm_bytes(b, sizeof(b), 15, 0, 4095));
    const uint8_t want15[5] = {0x42, 0x00, 0x00, 0xFF, 0x0F};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want15, b, 5);

    // full-on flag (bit 12) must survive into ON_H bit 4.
    TEST_ASSERT_EQUAL_INT(5, (int)pc_pca9685_set_pwm_bytes(b, sizeof(b), 0, PCA9685_FULL_ON, 0));
    const uint8_t want_full[5] = {0x06, 0x00, 0x10, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want_full, b, 5);

    // guards
    TEST_ASSERT_EQUAL_INT(0, (int)pc_pca9685_set_pwm_bytes(b, 4, 0, 0, 0));    // buffer too small
    TEST_ASSERT_EQUAL_INT(0, (int)pc_pca9685_set_pwm_bytes(b, 5, 16, 0, 0));   // channel out of range
    TEST_ASSERT_EQUAL_INT(0, (int)pc_pca9685_set_pwm_bytes(NULL, 5, 0, 0, 0)); // null buffer
}

void test_prescale_zero()
{
    // Zero frequency takes the max-prescale early return.
    TEST_ASSERT_TRUE(pc_pca9685_prescale(0) > 0);
}

// begin() must sleep the oscillator before PRESCALE can be written, wake it, let it settle, then
// RESTART. The order and the settle are both on the wire.
void test_begin_sequence_on_the_wire()
{
    pc_bus_host_reset();
    TEST_ASSERT_TRUE(pc_pca9685_begin(0x40, 50));

    TEST_ASSERT_EQUAL_UINT32(5, pc_bus_host_count());
    uint32_t n = 0;
    const uint8_t *w = pc_bus_host_txn_bytes(0, &n);
    TEST_ASSERT_EQUAL_HEX8(PCA9685_REG_MODE1, w[0]);
    TEST_ASSERT_EQUAL_HEX8(0x10, w[1]); // SLEEP, required before PRESCALE

    w = pc_bus_host_txn_bytes(1, &n);
    TEST_ASSERT_EQUAL_HEX8(PCA9685_REG_PRESCALE, w[0]);
    TEST_ASSERT_EQUAL_HEX8(pc_pca9685_prescale(50), w[1]);

    // The oscillator needs real time between the wake and the RESTART.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(500, pc_bus_host_gap_us(2, 3));
}

// A channel write is the channel's base register then on/off little-endian.
void test_set_pwm_on_the_wire()
{
    pc_bus_host_reset();
    TEST_ASSERT_TRUE(pc_pca9685_set_pwm(0, 0, 2048));
    const uint8_t want[5] = {PCA9685_REG_LED0_ON_L, 0x00, 0x00, (uint8_t)(2048 & 0xFF),
                             (uint8_t)((2048 >> 8) & 0x1F)};
    uint32_t n = 0;
    const uint8_t *tx = pc_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(sizeof(want), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, tx, sizeof(want));

    pc_bus_host_reset();
    TEST_ASSERT_TRUE(pc_pca9685_set_servo_us(0, 1500));
    uint16_t off = pc_pca9685_us_to_count(1500, PC_PCA9685_FREQ);
    tx = pc_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(5, n);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(off & 0xFF), tx[3]);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_prescale);
    RUN_TEST(test_channel_reg);
    RUN_TEST(test_us_to_count);
    RUN_TEST(test_set_pwm_bytes);
    RUN_TEST(test_prescale_zero);
    RUN_TEST(test_begin_sequence_on_the_wire);
    RUN_TEST(test_set_pwm_on_the_wire);
    return UNITY_END();
}
