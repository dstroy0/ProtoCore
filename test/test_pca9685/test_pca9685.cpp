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

void test_prescale_zero_and_host_stubs()
{
    // Zero frequency takes the max-prescale early return.
    TEST_ASSERT_TRUE(pc_pca9685_prescale(0) > 0);
    // Host build: the I2C bind functions all fail closed.
    TEST_ASSERT_FALSE(pc_pca9685_begin(0x40, 50));
    TEST_ASSERT_FALSE(pc_pca9685_set_pwm(0, 0, 2048));
    TEST_ASSERT_FALSE(pc_pca9685_set_servo_us(0, 1500));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_prescale);
    RUN_TEST(test_channel_reg);
    RUN_TEST(test_us_to_count);
    RUN_TEST(test_set_pwm_bytes);
    RUN_TEST(test_prescale_zero_and_host_stubs);
    return UNITY_END();
}
