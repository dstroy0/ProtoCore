// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the NXP PCA9685 PWM / servo codec (server/peripherals/pca9685/pca9685.h).
//
// Expected values come from the PCA9685 data sheet (rev 02, 16 July 2009): section 7.3.5 equations
// (1) and (2) for the prescaler and its hardware minimum of 3, Table 7 for the PRE_SCALE register's
// own reset value, Table 6 for the LED register addresses, and section 7.3.3 for LEDn_ON_H[4] /
// LEDn_OFF_H[4] and the reserved bits 7:5 above them.
//
// test_datasheet_prescale_example is the load-bearing case: equation (2) works 200 Hz out to 30,
// and Table 7 independently gives PRE_SCALE a reset value of 0001 1110. Two places in the data
// sheet agree on that number, so it pins the rounding of the prescaler without a second
// implementation to be wrong about.

#include "server/peripherals/pca9685/pca9685.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Section 7.3.5, equation (2): prescale value = round(25 MHz / (4096 x 200)) - 1 = 30, and Table 7
// gives PRE_SCALE (address FEh) the reset value 0001 1110 = 0x1E = 30.
void test_datasheet_prescale_example(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x1E, protocore_pca9685_prescale(200));
    TEST_ASSERT_EQUAL_UINT8(30, protocore_pca9685_prescale(200));
}

// Equation (1) at other rates, each derived the same way:
//   50 Hz:  25e6 / (4096 * 50)   = 122.070 -> 122 - 1 = 121 = 0x79
//   60 Hz:  25e6 / (4096 * 60)   = 101.725 -> 102 - 1 = 101 = 0x65
//   1000 Hz:25e6 / (4096 * 1000) =   6.104 ->   6 - 1 =   5
//   1526 Hz:25e6 / (4096 * 1526) =   4.000 ->   4 - 1 =   3, the hardware minimum
void test_equation1_at_other_rates(void)
{
    TEST_ASSERT_EQUAL_UINT8(121, protocore_pca9685_prescale(50));
    TEST_ASSERT_EQUAL_UINT8(101, protocore_pca9685_prescale(60));
    TEST_ASSERT_EQUAL_UINT8(5, protocore_pca9685_prescale(1000));
    TEST_ASSERT_EQUAL_UINT8(3, protocore_pca9685_prescale(1526));
}

// Section 7.3.5: "The hardware forces a minimum value that can be loaded into the PRE_SCALE
// register at 3." Anything the equation puts below that is clamped rather than written through,
// and the register is a byte so the far end clamps at 255.
void test_prescale_is_clamped_to_the_register_range(void)
{
    TEST_ASSERT_EQUAL_UINT8(3, protocore_pca9685_prescale(3000));
    TEST_ASSERT_EQUAL_UINT8(3, protocore_pca9685_prescale(1000000));
    TEST_ASSERT_EQUAL_UINT8(255, protocore_pca9685_prescale(10));
    TEST_ASSERT_EQUAL_UINT8(255, protocore_pca9685_prescale(1));
    // a zero rate has no prescale, so it takes the slowest the register can name.
    TEST_ASSERT_EQUAL_UINT8(255, protocore_pca9685_prescale(0));
}

// Table 6: LED0_ON_L is 06h and each channel is four consecutive registers, so LED1_ON_L is 0Ah and
// LED15_ON_L is 42h.
void test_datasheet_channel_register_addresses(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x06, protocore_pca9685_channel_reg(0));
    TEST_ASSERT_EQUAL_HEX8(0x0A, protocore_pca9685_channel_reg(1));
    TEST_ASSERT_EQUAL_HEX8(0x0E, protocore_pca9685_channel_reg(2));
    TEST_ASSERT_EQUAL_HEX8(0x42, protocore_pca9685_channel_reg(15));
    TEST_ASSERT_EQUAL_HEX8(PCA9685_REG_LED0_ON_L, protocore_pca9685_channel_reg(0));

    // MODE1 / MODE2 / PRE_SCALE sit where Table 5 and Table 7 put them.
    TEST_ASSERT_EQUAL_HEX8(0x00, PCA9685_REG_MODE1);
    TEST_ASSERT_EQUAL_HEX8(0x01, PCA9685_REG_MODE2);
    TEST_ASSERT_EQUAL_HEX8(0xFE, PCA9685_REG_PRESCALE);

    // channel 16 is past the sixteen the part has, so there is no register to name.
    TEST_ASSERT_EQUAL_HEX8(0x00, protocore_pca9685_channel_reg(PCA9685_CHANNELS));
    TEST_ASSERT_EQUAL_HEX8(0x00, protocore_pca9685_channel_reg(255));
    TEST_ASSERT_EQUAL_INT(16, PCA9685_CHANNELS);
}

// A period is 4096 counts whatever the frequency, so a pulse of u microseconds at f Hz is
// count = round(u * 4096 * f / 1e6). At 50 Hz that is u * 0.2048:
//   1000 us -> 204.8  -> 205
//   1500 us -> 307.2  -> 307
//   2000 us -> 409.6  -> 410
// and at 200 Hz (u * 0.8192) 1500 us -> 1228.8 -> 1229.
void test_pulse_width_to_count(void)
{
    TEST_ASSERT_EQUAL_UINT16(205, protocore_pca9685_us_to_count(1000, 50));
    TEST_ASSERT_EQUAL_UINT16(307, protocore_pca9685_us_to_count(1500, 50));
    TEST_ASSERT_EQUAL_UINT16(410, protocore_pca9685_us_to_count(2000, 50));
    TEST_ASSERT_EQUAL_UINT16(1229, protocore_pca9685_us_to_count(1500, 200));
    TEST_ASSERT_EQUAL_UINT16(0, protocore_pca9685_us_to_count(0, 50));
}

// Section 7.3.4: "The LEDn_ON and LEDn_OFF counts can vary from 0 to 4095." A pulse as long as the
// whole 20 ms period at 50 Hz lands on 4096, which is one past the field, so it saturates rather
// than wrapping to 0 and turning the output off.
void test_pulse_width_saturates_at_the_twelve_bit_maximum(void)
{
    TEST_ASSERT_EQUAL_UINT16(PCA9685_COUNT_MAX, protocore_pca9685_us_to_count(20000, 50));
    TEST_ASSERT_EQUAL_UINT16(PCA9685_COUNT_MAX, protocore_pca9685_us_to_count(1000000, 50));
    TEST_ASSERT_EQUAL_UINT16(4095, PCA9685_COUNT_MAX);
}

// Table 6: each channel is LEDn_ON_L, LEDn_ON_H, LEDn_OFF_L, LEDn_OFF_H, the count split 8 LSBs
// then 4 MSBs, so the write is the register address followed by those four bytes.
void test_set_pwm_bytes_layout(void)
{
    uint8_t buf[8];
    // ON = 0, OFF = 0x0ABC: the 12-bit count splits into 0xBC low and 0x0A high.
    TEST_ASSERT_EQUAL_size_t(5, protocore_pca9685_set_pwm_bytes(buf, sizeof(buf), 0, 0, 0x0ABC));
    TEST_ASSERT_EQUAL_HEX8(0x06, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0xBC, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x0A, buf[4]);

    // channel 15 addresses its own register block, and a full-scale count fills both halves.
    TEST_ASSERT_EQUAL_size_t(5, protocore_pca9685_set_pwm_bytes(buf, sizeof(buf), 15, 0x0FFF, 0x0FFF));
    TEST_ASSERT_EQUAL_HEX8(0x42, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x0F, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x0F, buf[4]);
}

// Section 7.3.3: LEDn_ON_H[4] set is "always ON" and LEDn_OFF_H[4] set is "always OFF"; bits 7:5 of
// both _H registers are reserved and non-writable, so nothing above bit 4 may be emitted.
void test_full_on_and_full_off_flags(void)
{
    uint8_t buf[8];
    TEST_ASSERT_EQUAL_HEX16(0x1000, PCA9685_FULL_ON);
    TEST_ASSERT_EQUAL_HEX16(0x1000, PCA9685_FULL_OFF);

    TEST_ASSERT_EQUAL_size_t(5, protocore_pca9685_set_pwm_bytes(buf, sizeof(buf), 0, PCA9685_FULL_ON, 0));
    TEST_ASSERT_EQUAL_HEX8(0x10, buf[2]); // ON_H bit 4
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[4]);

    TEST_ASSERT_EQUAL_size_t(5, protocore_pca9685_set_pwm_bytes(buf, sizeof(buf), 0, 0, PCA9685_FULL_OFF));
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x10, buf[4]); // OFF_H bit 4

    // anything a caller sets above bit 12 belongs to no field and never reaches the reserved bits.
    TEST_ASSERT_EQUAL_size_t(5, protocore_pca9685_set_pwm_bytes(buf, sizeof(buf), 0, 0xFFFF, 0xFFFF));
    TEST_ASSERT_EQUAL_HEX8(0x1F, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x1F, buf[4]);
}

// A short buffer or a channel the part does not have yields no write at all: a truncated one would
// be read as a different register's value.
void test_set_pwm_bytes_refuses_bad_arguments(void)
{
    uint8_t buf[8];
    buf[0] = 0xAA;
    TEST_ASSERT_EQUAL_size_t(0, protocore_pca9685_set_pwm_bytes(NULL, 5, 0, 0, 100));
    TEST_ASSERT_EQUAL_size_t(0, protocore_pca9685_set_pwm_bytes(buf, 4, 0, 0, 100));
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[0]); // untouched
    TEST_ASSERT_EQUAL_size_t(0, protocore_pca9685_set_pwm_bytes(buf, sizeof(buf), PCA9685_CHANNELS, 0, 100));
    TEST_ASSERT_EQUAL_size_t(0, protocore_pca9685_set_pwm_bytes(buf, sizeof(buf), 255, 0, 100));
}
