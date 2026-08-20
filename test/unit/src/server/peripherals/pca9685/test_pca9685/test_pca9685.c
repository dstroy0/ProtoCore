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

// The bus cases at the bottom drive a datasheet model of the part (test/core_setup/hal/host/devices/
// pca9685_device.h) rather than asserting the bytes that went out: the driver programs the part
// and the registers are read back from it, so a sequence that puts the right bytes on the wire in
// the wrong order - PRE_SCALE before SLEEP, say - fails where a capture assertion would pass.

#include "server/peripherals/pca9685/pca9685.h"

#include "devices/pca9685_device.h"

#include <unity.h>

static protocore_pca9685_dev s_part;

void setUp(void)
{
    protocore_bus_host_reset();
    protocore_bus_host_detach_all();
    protocore_pca9685_dev_place(&s_part, (uint16_t)PROTOCORE_PCA9685_I2C_ADDR);
}
void tearDown(void)
{
    protocore_bus_host_detach_all();
}

// Section 7.3.5, equation (2): prescale value = round(25 MHz / (4096 x 200)) - 1 = 30, and Table 7
// gives PRE_SCALE (address FEh) the reset value 0001 1110 = 0x1E = 30.
void test_datasheet_prescale_example(void)
{
    Pca9685V.prescale_args.freq_hz = 200;
    Pca9685.prescale(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_HEX8(0x1E, Pca9685V.value);
    Pca9685V.prescale_args.freq_hz = 200;
    Pca9685.prescale(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_UINT8(30, Pca9685V.value);
}

// Equation (1) at other rates, each derived the same way:
//   50 Hz:  25e6 / (4096 * 50)   = 122.070 -> 122 - 1 = 121 = 0x79
//   60 Hz:  25e6 / (4096 * 60)   = 101.725 -> 102 - 1 = 101 = 0x65
//   1000 Hz:25e6 / (4096 * 1000) =   6.104 ->   6 - 1 =   5
//   1526 Hz:25e6 / (4096 * 1526) =   4.000 ->   4 - 1 =   3, the hardware minimum
void test_equation1_at_other_rates(void)
{
    Pca9685V.prescale_args.freq_hz = 50;
    Pca9685.prescale(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_UINT8(121, Pca9685V.value);
    Pca9685V.prescale_args.freq_hz = 60;
    Pca9685.prescale(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_UINT8(101, Pca9685V.value);
    Pca9685V.prescale_args.freq_hz = 1000;
    Pca9685.prescale(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_UINT8(5, Pca9685V.value);
    Pca9685V.prescale_args.freq_hz = 1526;
    Pca9685.prescale(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_UINT8(3, Pca9685V.value);
}

// Section 7.3.5: "The hardware forces a minimum value that can be loaded into the PRE_SCALE
// register at 3." Anything the equation puts below that is clamped rather than written through,
// and the register is a byte so the far end clamps at 255.
void test_prescale_is_clamped_to_the_register_range(void)
{
    Pca9685V.prescale_args.freq_hz = 3000;
    Pca9685.prescale(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_UINT8(3, Pca9685V.value);
    Pca9685V.prescale_args.freq_hz = 1000000;
    Pca9685.prescale(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_UINT8(3, Pca9685V.value);
    Pca9685V.prescale_args.freq_hz = 10;
    Pca9685.prescale(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_UINT8(255, Pca9685V.value);
    Pca9685V.prescale_args.freq_hz = 1;
    Pca9685.prescale(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_UINT8(255, Pca9685V.value);
    // a zero rate has no prescale, so it takes the slowest the register can name.
    Pca9685V.prescale_args.freq_hz = 0;
    Pca9685.prescale(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_UINT8(255, Pca9685V.value);
}

// Table 6: LED0_ON_L is 06h and each channel is four consecutive registers, so LED1_ON_L is 0Ah and
// LED15_ON_L is 42h.
void test_datasheet_channel_register_addresses(void)
{
    Pca9685V.channel_reg_args.channel = 0;
    Pca9685.channel_reg(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_HEX8(0x06, Pca9685V.value);
    Pca9685V.channel_reg_args.channel = 1;
    Pca9685.channel_reg(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_HEX8(0x0A, Pca9685V.value);
    Pca9685V.channel_reg_args.channel = 2;
    Pca9685.channel_reg(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_HEX8(0x0E, Pca9685V.value);
    Pca9685V.channel_reg_args.channel = 15;
    Pca9685.channel_reg(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_HEX8(0x42, Pca9685V.value);
    Pca9685V.channel_reg_args.channel = 0;
    Pca9685.channel_reg(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_HEX8(PCA9685_REG_LED0_ON_L, Pca9685V.value);

    // MODE1 / MODE2 / PRE_SCALE sit where Table 5 and Table 7 put them.
    TEST_ASSERT_EQUAL_HEX8(0x00, PCA9685_REG_MODE1);
    TEST_ASSERT_EQUAL_HEX8(0x01, PCA9685_REG_MODE2);
    TEST_ASSERT_EQUAL_HEX8(0xFE, PCA9685_REG_PRESCALE);

    // channel 16 is past the sixteen the part has, so there is no register to name.
    Pca9685V.channel_reg_args.channel = PCA9685_CHANNELS;
    Pca9685.channel_reg(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_HEX8(0x00, Pca9685V.value);
    Pca9685V.channel_reg_args.channel = 255;
    Pca9685.channel_reg(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_HEX8(0x00, Pca9685V.value);
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
    Pca9685V.us_to_count_args.microseconds = 1000;
    Pca9685V.us_to_count_args.freq_hz = 50;
    Pca9685.us_to_count(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_UINT16(205, Pca9685V.count);
    Pca9685V.us_to_count_args.microseconds = 1500;
    Pca9685V.us_to_count_args.freq_hz = 50;
    Pca9685.us_to_count(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_UINT16(307, Pca9685V.count);
    Pca9685V.us_to_count_args.microseconds = 2000;
    Pca9685V.us_to_count_args.freq_hz = 50;
    Pca9685.us_to_count(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_UINT16(410, Pca9685V.count);
    Pca9685V.us_to_count_args.microseconds = 1500;
    Pca9685V.us_to_count_args.freq_hz = 200;
    Pca9685.us_to_count(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_UINT16(1229, Pca9685V.count);
    Pca9685V.us_to_count_args.microseconds = 0;
    Pca9685V.us_to_count_args.freq_hz = 50;
    Pca9685.us_to_count(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_UINT16(0, Pca9685V.count);
}

// Section 7.3.4: "The LEDn_ON and LEDn_OFF counts can vary from 0 to 4095." A pulse as long as the
// whole 20 ms period at 50 Hz lands on 4096, which is one past the field, so it saturates rather
// than wrapping to 0 and turning the output off.
void test_pulse_width_saturates_at_the_twelve_bit_maximum(void)
{
    Pca9685V.us_to_count_args.microseconds = 20000;
    Pca9685V.us_to_count_args.freq_hz = 50;
    Pca9685.us_to_count(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_UINT16(PCA9685_COUNT_MAX, Pca9685V.count);
    Pca9685V.us_to_count_args.microseconds = 1000000;
    Pca9685V.us_to_count_args.freq_hz = 50;
    Pca9685.us_to_count(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_UINT16(PCA9685_COUNT_MAX, Pca9685V.count);
    TEST_ASSERT_EQUAL_UINT16(4095, PCA9685_COUNT_MAX);
}

// Table 6: each channel is LEDn_ON_L, LEDn_ON_H, LEDn_OFF_L, LEDn_OFF_H, the count split 8 LSBs
// then 4 MSBs, so the write is the register address followed by those four bytes.
void test_set_pwm_bytes_layout(void)
{
    uint8_t buf[8];
    // ON = 0, OFF = 0x0ABC: the 12-bit count splits into 0xBC low and 0x0A high.
    Pca9685V.set_pwm_bytes_args.buf = buf;
    Pca9685V.set_pwm_bytes_args.cap = sizeof(buf);
    Pca9685V.set_pwm_bytes_args.channel = 0;
    Pca9685V.set_pwm_bytes_args.on = 0;
    Pca9685V.set_pwm_bytes_args.off = 0x0ABC;
    Pca9685.set_pwm_bytes(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_size_t(5, Pca9685V.n);
    TEST_ASSERT_EQUAL_HEX8(0x06, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0xBC, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x0A, buf[4]);

    // channel 15 addresses its own register block, and a full-scale count fills both halves.
    Pca9685V.set_pwm_bytes_args.buf = buf;
    Pca9685V.set_pwm_bytes_args.cap = sizeof(buf);
    Pca9685V.set_pwm_bytes_args.channel = 15;
    Pca9685V.set_pwm_bytes_args.on = 0x0FFF;
    Pca9685V.set_pwm_bytes_args.off = 0x0FFF;
    Pca9685.set_pwm_bytes(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_size_t(5, Pca9685V.n);
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

    Pca9685V.set_pwm_bytes_args.buf = buf;
    Pca9685V.set_pwm_bytes_args.cap = sizeof(buf);
    Pca9685V.set_pwm_bytes_args.channel = 0;
    Pca9685V.set_pwm_bytes_args.on = PCA9685_FULL_ON;
    Pca9685V.set_pwm_bytes_args.off = 0;
    Pca9685.set_pwm_bytes(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_size_t(5, Pca9685V.n);
    TEST_ASSERT_EQUAL_HEX8(0x10, buf[2]); // ON_H bit 4
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[4]);

    Pca9685V.set_pwm_bytes_args.buf = buf;
    Pca9685V.set_pwm_bytes_args.cap = sizeof(buf);
    Pca9685V.set_pwm_bytes_args.channel = 0;
    Pca9685V.set_pwm_bytes_args.on = 0;
    Pca9685V.set_pwm_bytes_args.off = PCA9685_FULL_OFF;
    Pca9685.set_pwm_bytes(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_size_t(5, Pca9685V.n);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x10, buf[4]); // OFF_H bit 4

    // anything a caller sets above bit 12 belongs to no field and never reaches the reserved bits.
    Pca9685V.set_pwm_bytes_args.buf = buf;
    Pca9685V.set_pwm_bytes_args.cap = sizeof(buf);
    Pca9685V.set_pwm_bytes_args.channel = 0;
    Pca9685V.set_pwm_bytes_args.on = 0xFFFF;
    Pca9685V.set_pwm_bytes_args.off = 0xFFFF;
    Pca9685.set_pwm_bytes(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_size_t(5, Pca9685V.n);
    TEST_ASSERT_EQUAL_HEX8(0x1F, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x1F, buf[4]);
}

// A short buffer or a channel the part does not have yields no write at all: a truncated one would
// be read as a different register's value.
void test_set_pwm_bytes_refuses_bad_arguments(void)
{
    uint8_t buf[8];
    buf[0] = 0xAA;
    Pca9685V.set_pwm_bytes_args.buf = NULL;
    Pca9685V.set_pwm_bytes_args.cap = 5;
    Pca9685V.set_pwm_bytes_args.channel = 0;
    Pca9685V.set_pwm_bytes_args.on = 0;
    Pca9685V.set_pwm_bytes_args.off = 100;
    Pca9685.set_pwm_bytes(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_size_t(0, Pca9685V.n);
    Pca9685V.set_pwm_bytes_args.buf = buf;
    Pca9685V.set_pwm_bytes_args.cap = 4;
    Pca9685V.set_pwm_bytes_args.channel = 0;
    Pca9685V.set_pwm_bytes_args.on = 0;
    Pca9685V.set_pwm_bytes_args.off = 100;
    Pca9685.set_pwm_bytes(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_size_t(0, Pca9685V.n);
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[0]); // untouched
    Pca9685V.set_pwm_bytes_args.buf = buf;
    Pca9685V.set_pwm_bytes_args.cap = sizeof(buf);
    Pca9685V.set_pwm_bytes_args.channel = PCA9685_CHANNELS;
    Pca9685V.set_pwm_bytes_args.on = 0;
    Pca9685V.set_pwm_bytes_args.off = 100;
    Pca9685.set_pwm_bytes(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_size_t(0, Pca9685V.n);
    Pca9685V.set_pwm_bytes_args.buf = buf;
    Pca9685V.set_pwm_bytes_args.cap = sizeof(buf);
    Pca9685V.set_pwm_bytes_args.channel = 255;
    Pca9685V.set_pwm_bytes_args.on = 0;
    Pca9685V.set_pwm_bytes_args.off = 100;
    Pca9685.set_pwm_bytes(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_size_t(0, Pca9685V.n);
}

// ---------------------------------------------------------------------------
// Over the bus, against the device model
// ---------------------------------------------------------------------------

// Rev. 4 Table 5 publishes MODE1's reset as 11h, Table 6 publishes MODE2's as 04h, and Table 8
// publishes PRE_SCALE's as 1Eh. Asserted through the platform seam, because every case below
// reads a register back and a model that started somewhere else would agree with a wrong driver.
void test_rev4_model_reset_values(void)
{
    uint8_t reg = 0x00u;
    uint8_t r[2] = {0u, 0u};
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_PCA9685_I2C_ADDR, &reg, 1u, r, 1u, 10u));
    TEST_ASSERT_EQUAL_HEX8(0x11u, r[0]); // MODE1: SLEEP and ALLCALL set at power-up
    reg = 0x01u;
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_PCA9685_I2C_ADDR, &reg, 1u, r, 1u, 10u));
    TEST_ASSERT_EQUAL_HEX8(0x04u, r[0]); // MODE2: OUTDRV totem-pole
    reg = 0xFEu;
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_PCA9685_I2C_ADDR, &reg, 1u, r, 1u, 10u));
    TEST_ASSERT_EQUAL_HEX8(0x1Eu, r[0]); // PRE_SCALE: 200 Hz
}

// Rev. 4 Table 4 note [1]: "Writes to PRE_SCALE register are blocked when SLEEP bit is logic 0."
// The part is awake here, so the write is dropped and the frequency does not move.
void test_rev4_a_prescale_write_while_awake_is_dropped(void)
{
    uint8_t wake[2] = {0x00u, 0x00u}; // MODE1 = 0: SLEEP clear
    TEST_ASSERT_TRUE(protocore_platform_i2c_write(0u, (uint16_t)PROTOCORE_PCA9685_I2C_ADDR, wake, 2u, 10u));
    uint8_t pre[2] = {0xFEu, 0x79u}; // 50 Hz, if it took
    TEST_ASSERT_TRUE(protocore_platform_i2c_write(0u, (uint16_t)PROTOCORE_PCA9685_I2C_ADDR, pre, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX8(0x1Eu, s_part.reg[0xFEu]); // still the reset value
    // asleep, the same write takes
    uint8_t sleep[2] = {0x00u, 0x10u};
    TEST_ASSERT_TRUE(protocore_platform_i2c_write(0u, (uint16_t)PROTOCORE_PCA9685_I2C_ADDR, sleep, 2u, 10u));
    TEST_ASSERT_TRUE(protocore_platform_i2c_write(0u, (uint16_t)PROTOCORE_PCA9685_I2C_ADDR, pre, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX8(0x79u, s_part.reg[0xFEu]);
}

// The sequence begin() runs is the one the part requires: it sleeps first, so the PRE_SCALE write
// takes, and 7.3.5 Eq 1 turns the programmed prescale back into the frequency that was asked for.
void test_rev4_begin_leaves_the_part_at_the_frequency_it_was_given(void)
{
    Pca9685V.begin_args.addr = (uint8_t)PROTOCORE_PCA9685_I2C_ADDR;
    Pca9685V.begin_args.freq_hz = 50u;
    Pca9685.begin(protocore_pca9685_span());
    TEST_ASSERT_TRUE(Pca9685V.ok);
    // Eq 2's own worked form at 50 Hz: round(25e6 / (4096 * 50)) - 1 = 121 = 0x79
    TEST_ASSERT_EQUAL_HEX8(0x79u, s_part.reg[0xFEu]);
    TEST_ASSERT_EQUAL_UINT32(50u, protocore_pca9685_dev_freq_hz(&s_part));
    // and the part is left awake, auto-incrementing, driving totem-pole outputs
    TEST_ASSERT_EQUAL_HEX8(0x00u, (uint8_t)(s_part.reg[0x00u] & 0x10u)); // SLEEP clear
    TEST_ASSERT_EQUAL_HEX8(0x20u, (uint8_t)(s_part.reg[0x00u] & 0x20u)); // AI set
    TEST_ASSERT_EQUAL_HEX8(0x04u, s_part.reg[0x01u]);                    // MODE2 OUTDRV
}

// The datasheet's own 200 Hz example, driven end to end: Eq 2 gives prescale 30, which is also
// PRE_SCALE's reset value, so the part ends up where it started. 30 is a rounded solution, so what
// it actually gives is 25e6 / (4096 x 31) = 196.9 Hz - the "default is 200 Hz" in Table 8 names the
// request, not the achievable rate. 50 Hz lands on 121, where 25e6 / (4096 x 122) = 50.03 rounds
// back to the number that was asked for.
void test_rev4_the_200hz_example_programs_the_reset_prescale(void)
{
    Pca9685V.begin_args.addr = (uint8_t)PROTOCORE_PCA9685_I2C_ADDR;
    Pca9685V.begin_args.freq_hz = 200u;
    Pca9685.begin(protocore_pca9685_span());
    TEST_ASSERT_TRUE(Pca9685V.ok);
    TEST_ASSERT_EQUAL_HEX8(0x1Eu, s_part.reg[0xFEu]);
    TEST_ASSERT_EQUAL_UINT32(196u, protocore_pca9685_dev_freq_hz(&s_part));
}

// Rev. 4 7.3.3: each channel is a 12-bit ON count and a 12-bit OFF count. The driver writes both
// in one five-byte transfer, and the part holds what it was sent.
void test_rev4_set_pwm_lands_in_the_channels_own_registers(void)
{
    Pca9685V.begin_args.addr = (uint8_t)PROTOCORE_PCA9685_I2C_ADDR;
    Pca9685V.begin_args.freq_hz = 50u;
    Pca9685.begin(protocore_pca9685_span());

    Pca9685V.set_pwm_args.channel = 0u;
    Pca9685V.set_pwm_args.on = 0u;
    Pca9685V.set_pwm_args.off = 307u;
    Pca9685.set_pwm(protocore_pca9685_span());
    TEST_ASSERT_TRUE(Pca9685V.ok);
    TEST_ASSERT_EQUAL_UINT16(0u, protocore_pca9685_dev_on(&s_part, 0u));
    TEST_ASSERT_EQUAL_UINT16(307u, protocore_pca9685_dev_off(&s_part, 0u));
    // and nothing else moved
    TEST_ASSERT_EQUAL_UINT16(0u, protocore_pca9685_dev_off(&s_part, 1u));
}

// Rev. 4 Table 4: the channels are four registers apart from LED0_ON_L at 06h, so each one lands
// in its own block and a channel written last does not disturb the ones before it.
void test_rev4_each_channel_has_its_own_registers(void)
{
    Pca9685V.begin_args.addr = (uint8_t)PROTOCORE_PCA9685_I2C_ADDR;
    Pca9685V.begin_args.freq_hz = 50u;
    Pca9685.begin(protocore_pca9685_span());

    for (uint8_t ch = 0u; ch < PCA9685_CHANNELS; ch++)
    {
        Pca9685V.set_pwm_args.channel = ch;
        Pca9685V.set_pwm_args.on = 0u;
        Pca9685V.set_pwm_args.off = (uint16_t)(100u + 11u * ch);
        Pca9685.set_pwm(protocore_pca9685_span());
        TEST_ASSERT_TRUE(Pca9685V.ok);
    }
    for (uint8_t ch = 0u; ch < PCA9685_CHANNELS; ch++)
    {
        TEST_ASSERT_EQUAL_UINT16((uint16_t)(100u + 11u * ch), protocore_pca9685_dev_off(&s_part, ch));
    }
}

// A servo pulse end to end: 7.3.3's count is the fraction of the period, so 1.5 ms of a 20 ms
// period at 50 Hz is round(1500 * 4096 * 50 / 1e6) = 307 counts, and the part holds that.
void test_rev4_a_servo_pulse_lands_as_its_fraction_of_the_period(void)
{
    Pca9685V.begin_args.addr = (uint8_t)PROTOCORE_PCA9685_I2C_ADDR;
    Pca9685V.begin_args.freq_hz = 50u;
    Pca9685.begin(protocore_pca9685_span());

    Pca9685V.set_servo_us_args.channel = 3u;
    Pca9685V.set_servo_us_args.microseconds = 1500u;
    Pca9685.set_servo_us(protocore_pca9685_span());
    TEST_ASSERT_TRUE(Pca9685V.ok);
    TEST_ASSERT_EQUAL_UINT16(0u, protocore_pca9685_dev_on(&s_part, 3u));
    TEST_ASSERT_EQUAL_UINT16(307u, protocore_pca9685_dev_off(&s_part, 3u));
    // the ends of a common servo travel, at the same frequency
    Pca9685V.set_servo_us_args.channel = 3u;
    Pca9685V.set_servo_us_args.microseconds = 1000u;
    Pca9685.set_servo_us(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_UINT16(205u, protocore_pca9685_dev_off(&s_part, 3u));
    Pca9685V.set_servo_us_args.channel = 3u;
    Pca9685V.set_servo_us_args.microseconds = 2000u;
    Pca9685.set_servo_us(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_UINT16(410u, protocore_pca9685_dev_off(&s_part, 3u));
}

// The same pulse is a different count at a different frequency, because the count is a fraction of
// the period: 1.5 ms of a 5 ms period at 200 Hz is four times the count it is at 50 Hz.
void test_a_servo_pulse_follows_the_frequency_begin_programmed(void)
{
    Pca9685V.begin_args.addr = (uint8_t)PROTOCORE_PCA9685_I2C_ADDR;
    Pca9685V.begin_args.freq_hz = 200u;
    Pca9685.begin(protocore_pca9685_span());
    Pca9685V.set_servo_us_args.channel = 0u;
    Pca9685V.set_servo_us_args.microseconds = 1500u;
    Pca9685.set_servo_us(protocore_pca9685_span());
    TEST_ASSERT_TRUE(Pca9685V.ok);
    TEST_ASSERT_EQUAL_UINT16(1229u, protocore_pca9685_dev_off(&s_part, 0u));
}

// begin() sends later transfers to the address it was given, and back to the strapped default when
// handed zero - so the address is state and not a constant.
void test_begin_sends_later_transfers_to_the_address_it_was_given(void)
{
    protocore_bus_host_detach_all();
    protocore_pca9685_dev_place(&s_part, 0x41u);
    Pca9685V.begin_args.addr = 0x41u;
    Pca9685V.begin_args.freq_hz = 50u;
    Pca9685.begin(protocore_pca9685_span());
    TEST_ASSERT_TRUE(Pca9685V.ok);
    TEST_ASSERT_EQUAL_HEX8(0x79u, s_part.reg[0xFEu]);
    TEST_ASSERT_EQUAL_HEX16(0x41u, protocore_bus_host_log[protocore_bus_host_log_len - 1u].target);
    Pca9685V.begin_args.addr = 0u;
    Pca9685V.begin_args.freq_hz = 50u;
    Pca9685.begin(protocore_pca9685_span());
    TEST_ASSERT_EQUAL_HEX16(PROTOCORE_PCA9685_I2C_ADDR, protocore_bus_host_log[protocore_bus_host_log_len - 1u].target);
}

// An out-of-range channel is refused before anything reaches the bus.
void test_set_pwm_refuses_an_out_of_range_channel(void)
{
    Pca9685V.set_pwm_args.channel = PCA9685_CHANNELS;
    Pca9685V.set_pwm_args.on = 0u;
    Pca9685V.set_pwm_args.off = 100u;
    Pca9685.set_pwm(protocore_pca9685_span());
    TEST_ASSERT_FALSE(Pca9685V.ok);
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_bus_host_log_len);
}

// A refused transfer is reported rather than passed off as a programmed part.
void test_a_refused_transfer_fails_begin(void)
{
    protocore_bus_host_fail = 1u;
    Pca9685V.begin_args.addr = (uint8_t)PROTOCORE_PCA9685_I2C_ADDR;
    Pca9685V.begin_args.freq_hz = 50u;
    Pca9685.begin(protocore_pca9685_span());
    TEST_ASSERT_FALSE(Pca9685V.ok);
}
