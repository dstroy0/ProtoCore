// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the TI ADS1115 codec (server/peripherals/ads1115/ads1115.h).
//
// The governing document is the ADS1113/4/5 data sheet SBAS444B. Table 9 lays the Config register
// out bit by bit (OS 15, MUX 14:12, PGA 11:9, MODE 8, DR 7:5, COMP_MODE 4, COMP_POL 3, COMP_LAT 2,
// COMP_QUE 1:0) and publishes "Default = 8583h"; Table 4 publishes the ideal output codes, fixing
// one LSB at FS/2^15.
//
// test_sbas444_reset_value_with_the_mux_moved_to_ain0_gnd is the load-bearing case. 0x8583 is a
// value the data sheet prints, and it decomposes as OS=1, MUX=000, PGA=010, MODE=1, DR=100,
// COMP_QUE=11. Single-ended AIN0 is MUX=100 (Table 9), so the only field that moves is bits 14:12
// and the word must come out 0xC583: an encoder with any field at the wrong offset cannot land on
// the published number with one documented field changed.

#include "server/peripherals/ads1115/ads1115.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// SBAS444B Table 9: "Default = 8583h" = OS 1 | MUX 000 | PGA 010 | MODE 1 | DR 100 | COMP_QUE 11.
//   0x8000 (OS=1) | 0x0000 (MUX=000) | 0x0400 (PGA=010) | 0x0100 (MODE=1) | 0x0080 (DR=100)
//   | 0x0003 (COMP_QUE=11) = 0x8583.
// Bits [14:12] MUX: "100 : AINP = AIN0 and AINN = GND" -> 0x4000, so the single-ended AIN0 form of
// the same settings is 0x8583 | 0x4000 = 0xC583.
void test_sbas444_reset_value_with_the_mux_moved_to_ain0_gnd(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xC583u, protocore_ads1115_config_single(0u, ADS1115_GAIN_2, ADS1115_DR_128));
}

// Bits [14:12] MUX: 100/101/110/111 select AIN0/AIN1/AIN2/AIN3 against GND. Every other field is
// held at the reset value, so each word is 0xC583 with the channel number added at bit 12.
void test_sbas444_mux_encoding_for_each_single_ended_channel(void)
{
    static const uint16_t WANT[4] = {0xC583u, 0xD583u, 0xE583u, 0xF583u};
    for (uint8_t ch = 0u; ch < 4u; ch++)
    {
        uint16_t cfg = protocore_ads1115_config_single(ch, ADS1115_GAIN_2, ADS1115_DR_128);
        TEST_ASSERT_EQUAL_HEX16(WANT[ch], cfg);
        TEST_ASSERT_EQUAL_HEX16((uint16_t)(0x4u | ch), (uint16_t)((cfg >> 12) & 0x7u));
    }
}

// Bits [11:9] PGA: 000 +/-6.144 V, 001 +/-4.096, 010 +/-2.048, 011 +/-1.024, 100 +/-0.512,
// 101 +/-0.256. The gain code is the field value, so it lands at bit 9 and nowhere else.
void test_sbas444_pga_encoding(void)
{
    static const uint16_t WANT[6] = {0xC183u, 0xC383u, 0xC583u, 0xC783u, 0xC983u, 0xCB83u};
    for (uint8_t g = ADS1115_GAIN_TWOTHIRDS; g <= ADS1115_GAIN_16; g++)
    {
        uint16_t cfg = protocore_ads1115_config_single(0u, g, ADS1115_DR_128);
        TEST_ASSERT_EQUAL_HEX16(WANT[g], cfg);
        TEST_ASSERT_EQUAL_HEX16((uint16_t)g, (uint16_t)((cfg >> 9) & 0x7u));
    }
}

// Bits [7:5] DR: 000 8 SPS through 111 860 SPS. The rate code is the field value at bit 5.
void test_sbas444_data_rate_encoding(void)
{
    static const uint16_t WANT[8] = {0xC503u, 0xC523u, 0xC543u, 0xC563u, 0xC583u, 0xC5A3u, 0xC5C3u, 0xC5E3u};
    for (uint8_t dr = ADS1115_DR_8; dr <= ADS1115_DR_860; dr++)
    {
        uint16_t cfg = protocore_ads1115_config_single(0u, ADS1115_GAIN_2, dr);
        TEST_ASSERT_EQUAL_HEX16(WANT[dr], cfg);
        TEST_ASSERT_EQUAL_HEX16((uint16_t)dr, (uint16_t)((cfg >> 5) & 0x7u));
    }
}

// Bit 15 OS "1 : Begin a single conversion", bit 8 MODE "1 : Power-down single-shot mode", bits
// [1:0] COMP_QUE "11 : Disable comparator", and the three comparator control bits stay at their
// documented defaults of 0. Checked across every field combination the encoder accepts.
void test_sbas444_start_single_shot_and_comparator_disabled(void)
{
    for (uint8_t ch = 0u; ch < 4u; ch++)
    {
        for (uint8_t g = ADS1115_GAIN_TWOTHIRDS; g <= ADS1115_GAIN_16; g++)
        {
            for (uint8_t dr = ADS1115_DR_8; dr <= ADS1115_DR_860; dr++)
            {
                uint16_t cfg = protocore_ads1115_config_single(ch, g, dr);
                TEST_ASSERT_EQUAL_HEX16(0x8000u, (uint16_t)(cfg & 0x8000u)); // OS = 1
                TEST_ASSERT_EQUAL_HEX16(0x0100u, (uint16_t)(cfg & 0x0100u)); // MODE = 1
                TEST_ASSERT_EQUAL_HEX16(0x0003u, (uint16_t)(cfg & 0x0003u)); // COMP_QUE = 11
                TEST_ASSERT_EQUAL_HEX16(0x0000u, (uint16_t)(cfg & 0x001Cu)); // COMP_MODE/POL/LAT = 0
            }
        }
    }
}

// Header: "Out-of-range fields fall back to channel 0 / gain +/-2.048 V / 128 SPS", which is the
// data sheet's own default for the two fields that have one.
void test_out_of_range_fields_fall_back_to_the_defaults(void)
{
    uint16_t want = protocore_ads1115_config_single(0u, ADS1115_GAIN_2, ADS1115_DR_128);
    TEST_ASSERT_EQUAL_HEX16(0xC583u, want);
    TEST_ASSERT_EQUAL_HEX16(want, protocore_ads1115_config_single(4u, ADS1115_GAIN_2, ADS1115_DR_128));
    TEST_ASSERT_EQUAL_HEX16(want, protocore_ads1115_config_single(255u, ADS1115_GAIN_2, ADS1115_DR_128));
    TEST_ASSERT_EQUAL_HEX16(want, protocore_ads1115_config_single(0u, 6u, ADS1115_DR_128));
    TEST_ASSERT_EQUAL_HEX16(want, protocore_ads1115_config_single(0u, 255u, ADS1115_DR_128));
    TEST_ASSERT_EQUAL_HEX16(want, protocore_ads1115_config_single(0u, ADS1115_GAIN_2, 8u));
    TEST_ASSERT_EQUAL_HEX16(want, protocore_ads1115_config_single(255u, 255u, 255u));
}

// SBAS444B Table 4 "Input Signal versus Ideal Output Code": +FS/2^15 is code 0001h, so one LSB is
// the full-scale range divided by 32768. Each expectation below is that division, chosen at a raw
// count where it comes out whole:
//   +/-6.144 V: 6144000/32768 =  187.5 uV/LSB, x16  =  3000 uV
//   +/-4.096 V: 4096000/32768 =  125    uV/LSB, x8  =  1000 uV
//   +/-2.048 V: 2048000/32768 =   62.5  uV/LSB, x16 =  1000 uV
//   +/-1.024 V: 1024000/32768 =   31.25 uV/LSB, x32 =  1000 uV
//   +/-0.512 V:  512000/32768 =   15.625 uV/LSB, x64 = 1000 uV
//   +/-0.256 V:  256000/32768 =    7.8125 uV/LSB, x128 = 1000 uV
void test_sbas444_lsb_is_full_scale_over_32768(void)
{
    TEST_ASSERT_EQUAL_INT32(3000, protocore_ads1115_raw_to_uv(16, ADS1115_GAIN_TWOTHIRDS));
    TEST_ASSERT_EQUAL_INT32(1000, protocore_ads1115_raw_to_uv(8, ADS1115_GAIN_1));
    TEST_ASSERT_EQUAL_INT32(1000, protocore_ads1115_raw_to_uv(16, ADS1115_GAIN_2));
    TEST_ASSERT_EQUAL_INT32(1000, protocore_ads1115_raw_to_uv(32, ADS1115_GAIN_4));
    TEST_ASSERT_EQUAL_INT32(1000, protocore_ads1115_raw_to_uv(64, ADS1115_GAIN_8));
    TEST_ASSERT_EQUAL_INT32(1000, protocore_ads1115_raw_to_uv(128, ADS1115_GAIN_16));
}

// Table 4: code 0 is 0 V, code 8000h is -FS, code 7FFFh is FS(2^15-1)/2^15. The negative end is
// whole; the positive end is one LSB short of full scale.
void test_sbas444_table4_endpoints(void)
{
    static const int32_t FS_UV[6] = {6144000, 4096000, 2048000, 1024000, 512000, 256000};
    for (uint8_t g = ADS1115_GAIN_TWOTHIRDS; g <= ADS1115_GAIN_16; g++)
    {
        TEST_ASSERT_EQUAL_INT32(0, protocore_ads1115_raw_to_uv(0, g));
        // 8000h = -32768: -FS exactly, since -32768 * FS / 32768 = -FS
        TEST_ASSERT_EQUAL_INT32(-FS_UV[g], protocore_ads1115_raw_to_uv((int16_t)-32768, g));
        // 7FFFh = 32767: FS * 32767 / 32768, one LSB below full scale
        TEST_ASSERT_EQUAL_INT32((int32_t)(((int64_t)32767 * FS_UV[g]) / 32768), protocore_ads1115_raw_to_uv(32767, g));
    }
}

// The conversion is a scale by a constant, so it is odd about zero: the same magnitude of input
// gives the same magnitude of output on either side.
void test_conversion_is_odd_about_zero(void)
{
    for (uint8_t g = ADS1115_GAIN_TWOTHIRDS; g <= ADS1115_GAIN_16; g++)
    {
        for (int32_t raw = 1; raw <= 32767; raw += 977)
        {
            int32_t pos = protocore_ads1115_raw_to_uv((int16_t)raw, g);
            int32_t neg = protocore_ads1115_raw_to_uv((int16_t)(-raw), g);
            TEST_ASSERT_EQUAL_INT32(pos, -neg);
        }
    }
}

// A wider full-scale range means a coarser LSB, so the same raw count reads a larger voltage at
// every step down the gain table.
void test_lower_gain_reads_a_larger_voltage_for_the_same_code(void)
{
    for (uint8_t g = ADS1115_GAIN_TWOTHIRDS; g < ADS1115_GAIN_16; g++)
    {
        TEST_ASSERT_TRUE(protocore_ads1115_raw_to_uv(1000, g) > protocore_ads1115_raw_to_uv(1000, (uint8_t)(g + 1u)));
    }
}

// Header: an out-of-range gain falls back to the +/- 2.048 V default rather than indexing off the
// end of the full-scale table.
void test_raw_to_uv_out_of_range_gain_falls_back(void)
{
    int32_t want = protocore_ads1115_raw_to_uv(16, ADS1115_GAIN_2);
    TEST_ASSERT_EQUAL_INT32(1000, want);
    TEST_ASSERT_EQUAL_INT32(want, protocore_ads1115_raw_to_uv(16, 6u));
    TEST_ASSERT_EQUAL_INT32(want, protocore_ads1115_raw_to_uv(16, 255u));
}

// The register addresses the codec writes to, per SBAS444B Table 6 "Register Address".
void test_sbas444_register_addresses(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00u, ADS1115_REG_CONVERSION);
    TEST_ASSERT_EQUAL_HEX8(0x01u, ADS1115_REG_CONFIG);
}
