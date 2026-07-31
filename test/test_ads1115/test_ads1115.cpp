// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the ADS1115 ADC codec (services/peripherals/ads1115): building the 16-bit config word for a
// single-shot single-ended reading (channel MUX, gain, data rate, start/mode/comparator bits,
// with out-of-range fallbacks), and converting a signed 16-bit sample to microvolts across the
// gain's full-scale range. The I2C transfer is ESP32-only.

#include "services/peripherals/ads1115/ads1115.h"
#include <stdint.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_config_word()
{
    // ch0, +/-4.096V, 128 SPS: OS|MUX_AIN0|PGA1|MODE_SINGLE|DR128|COMP_DISABLE.
    TEST_ASSERT_EQUAL_HEX16(0xC383,
                            pc_ads1115_config_single(0, Ads1115Gain::ADS1115_GAIN_1, Ads1115DataRate::ADS1115_DR_128));
    // ch3, +/-6.144V, 860 SPS.
    TEST_ASSERT_EQUAL_HEX16(
        0xF1E3, pc_ads1115_config_single(3, Ads1115Gain::ADS1115_GAIN_TWOTHIRDS, Ads1115DataRate::ADS1115_DR_860));
    // ch1, +/-0.256V (gain 16), 8 SPS: OS|MUX_AIN1(0x5000)|PGA16(0x0A00)|MODE|DR8(0)|COMP.
    TEST_ASSERT_EQUAL_HEX16(0xDB03,
                            pc_ads1115_config_single(1, Ads1115Gain::ADS1115_GAIN_16, Ads1115DataRate::ADS1115_DR_8));
}

void test_config_fallbacks()
{
    // Out-of-range channel/gain/dr fall back to ch0 / +/-2.048V / 128 SPS = 0xC583.
    TEST_ASSERT_EQUAL_HEX16(0xC583, pc_ads1115_config_single(9, 99, 99));
}

void test_raw_to_uv()
{
    // gain 1 (+/-4.096 V) -> 125 uV/LSB.
    TEST_ASSERT_EQUAL_INT32(4095875, pc_ads1115_raw_to_uv(32767, Ads1115Gain::ADS1115_GAIN_1));   // near +full-scale
    TEST_ASSERT_EQUAL_INT32(-4096000, pc_ads1115_raw_to_uv(-32768, Ads1115Gain::ADS1115_GAIN_1)); // -full-scale
    TEST_ASSERT_EQUAL_INT32(0, pc_ads1115_raw_to_uv(0, Ads1115Gain::ADS1115_GAIN_1));
    // gain 2 (+/-2.048 V): half-scale count -> 1.024 V.
    TEST_ASSERT_EQUAL_INT32(1024000, pc_ads1115_raw_to_uv(16384, Ads1115Gain::ADS1115_GAIN_2));
    // gain 2/3 (+/-6.144 V): 187.5 uV/LSB, negative sample.
    TEST_ASSERT_EQUAL_INT32(-1536000, pc_ads1115_raw_to_uv(-8192, Ads1115Gain::ADS1115_GAIN_TWOTHIRDS));
}

void test_raw_to_uv_gain_clamp()
{
    // An out-of-range gain code clamps to GAIN_2 (its FSR), so the conversion never indexes past the
    // FSR table.
    TEST_ASSERT_EQUAL_INT32(pc_ads1115_raw_to_uv(16384, Ads1115Gain::ADS1115_GAIN_2), pc_ads1115_raw_to_uv(16384, 99));
}

void test_host_i2c_stubs_fail_closed()
{
    // On a host build there is no I2C: begin and both reads fail closed (false).
    TEST_ASSERT_FALSE(pc_ads1115_begin(0x48));
    int16_t raw = 0;
    TEST_ASSERT_FALSE(pc_ads1115_read_raw(0, Ads1115Gain::ADS1115_GAIN_2, &raw));
    int32_t uv = 0;
    TEST_ASSERT_FALSE(pc_ads1115_read_uv(0, Ads1115Gain::ADS1115_GAIN_2, &uv));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_config_word);
    RUN_TEST(test_config_fallbacks);
    RUN_TEST(test_raw_to_uv);
    RUN_TEST(test_raw_to_uv_gain_clamp);
    RUN_TEST(test_host_i2c_stubs_fail_closed);
    return UNITY_END();
}
