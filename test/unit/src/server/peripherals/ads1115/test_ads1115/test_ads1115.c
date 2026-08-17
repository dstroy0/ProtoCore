// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

// The bus cases below drive a datasheet model of the part (core_setup/hal/host/devices/
// ads1115_device.h) rather than a primed byte queue: a suite puts a voltage on a pin, the driver
// composes its own transfers, and the code that comes back is the part's arithmetic on that
// voltage. A field at the wrong offset, a byte in the wrong order, or a register read from the
// wrong pointer all show up as a wrong number of microvolts.

#include "server/peripherals/ads1115/ads1115.h"

#include "devices/ads1115_device.h"

#include <unity.h>

static protocore_ads1115_dev s_part;

void setUp(void)
{
    protocore_bus_host_reset();
    protocore_bus_host_detach_all();
    protocore_ads1115_dev_place(&s_part, (uint16_t)PROTOCORE_ADS1115_I2C_ADDR);
}
void tearDown(void)
{
    protocore_bus_host_detach_all();
}

// SBAS444B Table 9: "Default = 8583h" = OS 1 | MUX 000 | PGA 010 | MODE 1 | DR 100 | COMP_QUE 11.
//   0x8000 (OS=1) | 0x0000 (MUX=000) | 0x0400 (PGA=010) | 0x0100 (MODE=1) | 0x0080 (DR=100)
//   | 0x0003 (COMP_QUE=11) = 0x8583.
// Bits [14:12] MUX: "100 : AINP = AIN0 and AINN = GND" -> 0x4000, so the single-ended AIN0 form of
// the same settings is 0x8583 | 0x4000 = 0xC583.
void test_sbas444_reset_value_with_the_mux_moved_to_ain0_gnd(void)
{
    Ads1115.config_single_args.channel = 0u;
    Ads1115.config_single_args.gain = ADS1115_GAIN_2;
    Ads1115.config_single_args.dr = ADS1115_DR_128;
    Ads1115.config_single(protocore_ads1115_span());
    TEST_ASSERT_EQUAL_HEX16(0xC583u, Ads1115.word);
}

// Bits [14:12] MUX: 100/101/110/111 select AIN0/AIN1/AIN2/AIN3 against GND. Every other field is
// held at the reset value, so each word is 0xC583 with the channel number added at bit 12.
void test_sbas444_mux_encoding_for_each_single_ended_channel(void)
{
    static const uint16_t WANT[4] = {0xC583u, 0xD583u, 0xE583u, 0xF583u};
    for (uint8_t ch = 0u; ch < 4u; ch++)
    {
        Ads1115.config_single_args.channel = ch;
        Ads1115.config_single_args.gain = ADS1115_GAIN_2;
        Ads1115.config_single_args.dr = ADS1115_DR_128;
        Ads1115.config_single(protocore_ads1115_span());
        uint16_t cfg = Ads1115.word;
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
        Ads1115.config_single_args.channel = 0u;
        Ads1115.config_single_args.gain = g;
        Ads1115.config_single_args.dr = ADS1115_DR_128;
        Ads1115.config_single(protocore_ads1115_span());
        uint16_t cfg = Ads1115.word;
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
        Ads1115.config_single_args.channel = 0u;
        Ads1115.config_single_args.gain = ADS1115_GAIN_2;
        Ads1115.config_single_args.dr = dr;
        Ads1115.config_single(protocore_ads1115_span());
        uint16_t cfg = Ads1115.word;
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
                Ads1115.config_single_args.channel = ch;
                Ads1115.config_single_args.gain = g;
                Ads1115.config_single_args.dr = dr;
                Ads1115.config_single(protocore_ads1115_span());
                uint16_t cfg = Ads1115.word;
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
    Ads1115.config_single_args.channel = 0u;
    Ads1115.config_single_args.gain = ADS1115_GAIN_2;
    Ads1115.config_single_args.dr = ADS1115_DR_128;
    Ads1115.config_single(protocore_ads1115_span());
    uint16_t want = Ads1115.word;
    TEST_ASSERT_EQUAL_HEX16(0xC583u, want);
    Ads1115.config_single_args.channel = 4u;
    Ads1115.config_single_args.gain = ADS1115_GAIN_2;
    Ads1115.config_single_args.dr = ADS1115_DR_128;
    Ads1115.config_single(protocore_ads1115_span());
    TEST_ASSERT_EQUAL_HEX16(want, Ads1115.word);
    Ads1115.config_single_args.channel = 255u;
    Ads1115.config_single_args.gain = ADS1115_GAIN_2;
    Ads1115.config_single_args.dr = ADS1115_DR_128;
    Ads1115.config_single(protocore_ads1115_span());
    TEST_ASSERT_EQUAL_HEX16(want, Ads1115.word);
    Ads1115.config_single_args.channel = 0u;
    Ads1115.config_single_args.gain = 6u;
    Ads1115.config_single_args.dr = ADS1115_DR_128;
    Ads1115.config_single(protocore_ads1115_span());
    TEST_ASSERT_EQUAL_HEX16(want, Ads1115.word);
    Ads1115.config_single_args.channel = 0u;
    Ads1115.config_single_args.gain = 255u;
    Ads1115.config_single_args.dr = ADS1115_DR_128;
    Ads1115.config_single(protocore_ads1115_span());
    TEST_ASSERT_EQUAL_HEX16(want, Ads1115.word);
    Ads1115.config_single_args.channel = 0u;
    Ads1115.config_single_args.gain = ADS1115_GAIN_2;
    Ads1115.config_single_args.dr = 8u;
    Ads1115.config_single(protocore_ads1115_span());
    TEST_ASSERT_EQUAL_HEX16(want, Ads1115.word);
    Ads1115.config_single_args.channel = 255u;
    Ads1115.config_single_args.gain = 255u;
    Ads1115.config_single_args.dr = 255u;
    Ads1115.config_single(protocore_ads1115_span());
    TEST_ASSERT_EQUAL_HEX16(want, Ads1115.word);
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
    Ads1115.raw_to_uv_args.raw = 16;
    Ads1115.raw_to_uv_args.gain = ADS1115_GAIN_TWOTHIRDS;
    Ads1115.raw_to_uv(protocore_ads1115_span());
    TEST_ASSERT_EQUAL_INT32(3000, Ads1115.uv);
    Ads1115.raw_to_uv_args.raw = 8;
    Ads1115.raw_to_uv_args.gain = ADS1115_GAIN_1;
    Ads1115.raw_to_uv(protocore_ads1115_span());
    TEST_ASSERT_EQUAL_INT32(1000, Ads1115.uv);
    Ads1115.raw_to_uv_args.raw = 16;
    Ads1115.raw_to_uv_args.gain = ADS1115_GAIN_2;
    Ads1115.raw_to_uv(protocore_ads1115_span());
    TEST_ASSERT_EQUAL_INT32(1000, Ads1115.uv);
    Ads1115.raw_to_uv_args.raw = 32;
    Ads1115.raw_to_uv_args.gain = ADS1115_GAIN_4;
    Ads1115.raw_to_uv(protocore_ads1115_span());
    TEST_ASSERT_EQUAL_INT32(1000, Ads1115.uv);
    Ads1115.raw_to_uv_args.raw = 64;
    Ads1115.raw_to_uv_args.gain = ADS1115_GAIN_8;
    Ads1115.raw_to_uv(protocore_ads1115_span());
    TEST_ASSERT_EQUAL_INT32(1000, Ads1115.uv);
    Ads1115.raw_to_uv_args.raw = 128;
    Ads1115.raw_to_uv_args.gain = ADS1115_GAIN_16;
    Ads1115.raw_to_uv(protocore_ads1115_span());
    TEST_ASSERT_EQUAL_INT32(1000, Ads1115.uv);
}

// Table 4: code 0 is 0 V, code 8000h is -FS, code 7FFFh is FS(2^15-1)/2^15. The negative end is
// whole; the positive end is one LSB short of full scale.
void test_sbas444_table4_endpoints(void)
{
    static const int32_t FS_UV[6] = {6144000, 4096000, 2048000, 1024000, 512000, 256000};
    for (uint8_t g = ADS1115_GAIN_TWOTHIRDS; g <= ADS1115_GAIN_16; g++)
    {
        Ads1115.raw_to_uv_args.raw = 0;
        Ads1115.raw_to_uv_args.gain = g;
        Ads1115.raw_to_uv(protocore_ads1115_span());
        TEST_ASSERT_EQUAL_INT32(0, Ads1115.uv);
        // 8000h = -32768: -FS exactly, since -32768 * FS / 32768 = -FS
        Ads1115.raw_to_uv_args.raw = (int16_t)-32768;
        Ads1115.raw_to_uv_args.gain = g;
        Ads1115.raw_to_uv(protocore_ads1115_span());
        TEST_ASSERT_EQUAL_INT32(-FS_UV[g], Ads1115.uv);
        // 7FFFh = 32767: FS * 32767 / 32768, one LSB below full scale
        Ads1115.raw_to_uv_args.raw = 32767;
        Ads1115.raw_to_uv_args.gain = g;
        Ads1115.raw_to_uv(protocore_ads1115_span());
        TEST_ASSERT_EQUAL_INT32((int32_t)(((int64_t)32767 * FS_UV[g]) / 32768), Ads1115.uv);
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
            Ads1115.raw_to_uv_args.raw = (int16_t)raw;
            Ads1115.raw_to_uv_args.gain = g;
            Ads1115.raw_to_uv(protocore_ads1115_span());
            int32_t pos = Ads1115.uv;
            Ads1115.raw_to_uv_args.raw = (int16_t)(-raw);
            Ads1115.raw_to_uv_args.gain = g;
            Ads1115.raw_to_uv(protocore_ads1115_span());
            int32_t neg = Ads1115.uv;
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
        // The wider range is captured before the narrower one runs: both report through the one
        // namespace, so comparing them in a single expression would compare the second with itself.
        Ads1115.raw_to_uv_args.raw = 1000;
        Ads1115.raw_to_uv_args.gain = g;
        Ads1115.raw_to_uv(protocore_ads1115_span());
        const int32_t wider = Ads1115.uv;
        Ads1115.raw_to_uv_args.raw = 1000;
        Ads1115.raw_to_uv_args.gain = (uint8_t)(g + 1u);
        Ads1115.raw_to_uv(protocore_ads1115_span());
        const int32_t narrower = Ads1115.uv;
        TEST_ASSERT_TRUE(wider > narrower);
    }
}

// Header: an out-of-range gain falls back to the +/- 2.048 V default rather than indexing off the
// end of the full-scale table.
void test_raw_to_uv_out_of_range_gain_falls_back(void)
{
    Ads1115.raw_to_uv_args.raw = 16;
    Ads1115.raw_to_uv_args.gain = ADS1115_GAIN_2;
    Ads1115.raw_to_uv(protocore_ads1115_span());
    int32_t want = Ads1115.uv;
    TEST_ASSERT_EQUAL_INT32(1000, want);
    Ads1115.raw_to_uv_args.raw = 16;
    Ads1115.raw_to_uv_args.gain = 6u;
    Ads1115.raw_to_uv(protocore_ads1115_span());
    TEST_ASSERT_EQUAL_INT32(want, Ads1115.uv);
    Ads1115.raw_to_uv_args.raw = 16;
    Ads1115.raw_to_uv_args.gain = 255u;
    Ads1115.raw_to_uv(protocore_ads1115_span());
    TEST_ASSERT_EQUAL_INT32(want, Ads1115.uv);
}

// The register addresses the codec writes to, per SBAS444B Table 6 "Register Address".
void test_sbas444_register_addresses(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00u, ADS1115_REG_CONVERSION);
    TEST_ASSERT_EQUAL_HEX8(0x01u, ADS1115_REG_CONFIG);
}

// ---------------------------------------------------------------------------
// Over the bus, against the device model
// ---------------------------------------------------------------------------

// SBAS444E 8.1.2 and 8.1.4 publish the reset values, and 8.1.1 makes the address pointer persist
// until a write moves it. Asserted through the platform seam, because every case below reads a
// register the pointer selected and a model that lost the pointer would answer plausibly wrong.
void test_sbas444e_model_reset_values_and_a_persistent_address_pointer(void)
{
    uint8_t reg = 0x01u; // point at the config register
    uint8_t r[2] = {0u, 0u};
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_ADS1115_I2C_ADDR, &reg, 1u, r, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX16(0x8583u, (uint16_t)(((uint16_t)r[0] << 8) | r[1]));
    // No pointer this time: 8.1.1 says the part still answers from the register it was left on.
    r[0] = 0u;
    r[1] = 0u;
    TEST_ASSERT_TRUE(protocore_platform_i2c_read(0u, (uint16_t)PROTOCORE_ADS1115_I2C_ADDR, r, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX16(0x8583u, (uint16_t)(((uint16_t)r[0] << 8) | r[1]));
    reg = 0x02u; // Lo_thresh, reset 8000h
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_ADS1115_I2C_ADDR, &reg, 1u, r, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX16(0x8000u, (uint16_t)(((uint16_t)r[0] << 8) | r[1]));
    reg = 0x03u; // Hi_thresh, reset 7FFFh
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_ADS1115_I2C_ADDR, &reg, 1u, r, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX16(0x7FFFu, (uint16_t)(((uint16_t)r[0] << 8) | r[1]));
    reg = 0x00u; // conversion, reset 0000h, and nothing has started one
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_ADS1115_I2C_ADDR, &reg, 1u, r, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX16(0x0000u, (uint16_t)(((uint16_t)r[0] << 8) | r[1]));
}

// SBAS444E 7.5.3: a register write is the pointer byte then the register's two bytes, MSB first;
// a read of another register is a pointer write followed by two bytes back. So one reading is two
// transfers to the device address, and the config word on the wire is the one the encoder built.
void test_sbas444e_one_reading_is_a_config_write_then_a_conversion_read(void)
{
    Ads1115.config_single_args.channel = 0u;
    Ads1115.config_single_args.gain = ADS1115_GAIN_2;
    Ads1115.config_single_args.dr = (uint8_t)PROTOCORE_ADS1115_DR;
    Ads1115.config_single(protocore_ads1115_span());
    const uint16_t want_cfg = Ads1115.word;

    int16_t raw = 0;
    Ads1115.read_raw_args.channel = 0u;
    Ads1115.read_raw_args.gain = ADS1115_GAIN_2;
    Ads1115.read_raw_args.raw = &raw;
    Ads1115.read_raw(protocore_ads1115_span());
    TEST_ASSERT_TRUE(Ads1115.ok);

    uint32_t len = 0u;
    const uint8_t *tx = protocore_bus_host_written(&len);
    TEST_ASSERT_EQUAL_UINT32(2u, protocore_bus_host_log_len);
    // the config write: pointer 01h, then the word
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_BUS_HOST_I2C, protocore_bus_host_log[0].kind);
    TEST_ASSERT_EQUAL_HEX16(PROTOCORE_ADS1115_I2C_ADDR, protocore_bus_host_log[0].target);
    TEST_ASSERT_EQUAL_UINT32(3u, protocore_bus_host_log[0].wlen);
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_bus_host_log[0].rlen);
    TEST_ASSERT_EQUAL_HEX8(0x01u, tx[protocore_bus_host_log[0].woff]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(want_cfg >> 8), tx[protocore_bus_host_log[0].woff + 1u]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(want_cfg & 0xFFu), tx[protocore_bus_host_log[0].woff + 2u]);
    // the conversion read: pointer 00h, two bytes back
    TEST_ASSERT_EQUAL_HEX16(PROTOCORE_ADS1115_I2C_ADDR, protocore_bus_host_log[1].target);
    TEST_ASSERT_EQUAL_UINT32(1u, protocore_bus_host_log[1].wlen);
    TEST_ASSERT_EQUAL_UINT32(2u, protocore_bus_host_log[1].rlen);
    TEST_ASSERT_EQUAL_HEX8(0x00u, tx[protocore_bus_host_log[1].woff]);
    TEST_ASSERT_EQUAL_UINT32(4u, len);
}

// SBAS444E 7.3.3 Table 7-1: at PGA 010b the range is +/-2.048 V, so one LSB is 62.5 uV and
// 1.000000 V is code 16000 exactly (1000000 * 32768 / 2048000). Read off AIN0 through the driver.
void test_sbas444e_a_reading_is_the_input_divided_by_the_lsb(void)
{
    s_part.ain_uv[0] = 1000000;
    int16_t raw = 0;
    Ads1115.read_raw_args.channel = 0u;
    Ads1115.read_raw_args.gain = ADS1115_GAIN_2;
    Ads1115.read_raw_args.raw = &raw;
    Ads1115.read_raw(protocore_ads1115_span());
    TEST_ASSERT_TRUE(Ads1115.ok);
    TEST_ASSERT_EQUAL_INT16(16000, raw);
}

// The reading and the scaling are inverses, so an input that is a whole number of LSBs comes back
// as the voltage that was applied.
void test_sbas444e_read_uv_returns_the_applied_voltage(void)
{
    s_part.ain_uv[0] = 1000000;
    int32_t uv = 0;
    Ads1115.read_uv_args.channel = 0u;
    Ads1115.read_uv_args.gain = ADS1115_GAIN_2;
    Ads1115.read_uv_args.microvolts = &uv;
    Ads1115.read_uv(protocore_ads1115_span());
    TEST_ASSERT_TRUE(Ads1115.ok);
    TEST_ASSERT_EQUAL_INT32(1000000, uv);
}

// SBAS444E 7.5.4: the code is two's complement, so a negative input reads back negative through
// the 16-bit register and the int16 the driver hands out. -1.000000 V at +/-2.048 V is -16000,
// which is C180h on the wire.
void test_sbas444e_a_negative_input_reads_a_twos_complement_code(void)
{
    s_part.ain_uv[0] = -1000000;
    int16_t raw = 0;
    Ads1115.read_raw_args.channel = 0u;
    Ads1115.read_raw_args.gain = ADS1115_GAIN_2;
    Ads1115.read_raw_args.raw = &raw;
    Ads1115.read_raw(protocore_ads1115_span());
    TEST_ASSERT_TRUE(Ads1115.ok);
    TEST_ASSERT_EQUAL_INT16(-16000, raw);
    TEST_ASSERT_EQUAL_HEX16(0xC180u, (uint16_t)raw);
    int32_t uv = 0;
    Ads1115.read_uv_args.channel = 0u;
    Ads1115.read_uv_args.gain = ADS1115_GAIN_2;
    Ads1115.read_uv_args.microvolts = &uv;
    Ads1115.read_uv(protocore_ads1115_span());
    TEST_ASSERT_TRUE(Ads1115.ok);
    TEST_ASSERT_EQUAL_INT32(-1000000, uv);
}

// SBAS444E 7.5.4: "The output clips at these codes for signals that exceed full-scale." 3.000 V
// into the +/-2.048 V range reads 7FFFh, and back out that is one LSB short of full scale.
void test_sbas444e_an_input_past_full_scale_clips(void)
{
    s_part.ain_uv[0] = 3000000;
    int16_t raw = 0;
    Ads1115.read_raw_args.channel = 0u;
    Ads1115.read_raw_args.gain = ADS1115_GAIN_2;
    Ads1115.read_raw_args.raw = &raw;
    Ads1115.read_raw(protocore_ads1115_span());
    TEST_ASSERT_TRUE(Ads1115.ok);
    TEST_ASSERT_EQUAL_HEX16(0x7FFFu, (uint16_t)raw);
    s_part.ain_uv[0] = -3000000;
    Ads1115.read_raw_args.channel = 0u;
    Ads1115.read_raw_args.gain = ADS1115_GAIN_2;
    Ads1115.read_raw_args.raw = &raw;
    Ads1115.read_raw(protocore_ads1115_span());
    TEST_ASSERT_TRUE(Ads1115.ok);
    TEST_ASSERT_EQUAL_HEX16(0x8000u, (uint16_t)raw);
}

// SBAS444E 8.1.3 MUX[2:0] 100b..111b select AIN0..AIN3 against GND. Four different voltages on the
// four pins, so a channel that reaches the wrong pin reads the wrong one of them.
void test_sbas444e_each_channel_reads_its_own_pin(void)
{
    static const int32_t APPLIED[4] = {62500, 125000, 250000, 500000}; // 1, 2, 4, 8 kLSB at 62.5 uV
    static const int16_t WANT[4] = {1000, 2000, 4000, 8000};
    for (uint8_t ch = 0u; ch < 4u; ch++)
    {
        s_part.ain_uv[ch] = APPLIED[ch];
    }
    for (uint8_t ch = 0u; ch < 4u; ch++)
    {
        int16_t raw = 0;
        Ads1115.read_raw_args.channel = ch;
        Ads1115.read_raw_args.gain = ADS1115_GAIN_2;
        Ads1115.read_raw_args.raw = &raw;
        Ads1115.read_raw(protocore_ads1115_span());
        TEST_ASSERT_TRUE(Ads1115.ok);
        TEST_ASSERT_EQUAL_INT16(WANT[ch], raw);
    }
}

// SBAS444E 7.3.3 Table 7-1: the same input reads a larger code at a narrower range, by the ratio
// of the two LSB sizes. 0.256000 V is full scale at PGA 101b and one twenty-fourth of it at 000b,
// so the code doubles at every step down the table.
void test_sbas444e_the_gain_selects_the_range_the_part_converts_against(void)
{
    static const int32_t FS_UV[6] = {6144000, 4096000, 2048000, 1024000, 512000, 256000};
    s_part.ain_uv[0] = 128000; // 0.128 V: half of the narrowest full scale, so nothing clips
    for (uint8_t g = ADS1115_GAIN_TWOTHIRDS; g <= ADS1115_GAIN_16; g++)
    {
        int16_t raw = 0;
        Ads1115.read_raw_args.channel = 0u;
        Ads1115.read_raw_args.gain = g;
        Ads1115.read_raw_args.raw = &raw;
        Ads1115.read_raw(protocore_ads1115_span());
        TEST_ASSERT_TRUE(Ads1115.ok);
        TEST_ASSERT_EQUAL_INT16((int16_t)((int64_t)128000 * 32768 / FS_UV[g]), raw);
    }
}

// The reading is what the part read, not what it read last time: the same pin at a new voltage
// gives a new code, because 8.1.3 OS starts a fresh conversion on every config write.
void test_a_second_reading_sees_the_new_input(void)
{
    int16_t raw = 0;
    s_part.ain_uv[0] = 62500; // 1000 LSB
    Ads1115.read_raw_args.channel = 0u;
    Ads1115.read_raw_args.gain = ADS1115_GAIN_2;
    Ads1115.read_raw_args.raw = &raw;
    Ads1115.read_raw(protocore_ads1115_span());
    TEST_ASSERT_EQUAL_INT16(1000, raw);
    s_part.ain_uv[0] = 125000; // 2000 LSB
    Ads1115.read_raw_args.channel = 0u;
    Ads1115.read_raw_args.gain = ADS1115_GAIN_2;
    Ads1115.read_raw_args.raw = &raw;
    Ads1115.read_raw(protocore_ads1115_span());
    TEST_ASSERT_EQUAL_INT16(2000, raw);
}

// A part at another address does not answer, so the reading fails rather than returning someone
// else's bytes.
void test_a_part_at_another_address_is_not_read(void)
{
    protocore_bus_host_detach_all();
    protocore_ads1115_dev_place(&s_part, (uint16_t)(PROTOCORE_ADS1115_I2C_ADDR + 1));
    s_part.ain_uv[0] = 1000000;
    int16_t raw = -1;
    Ads1115.read_raw_args.channel = 0u;
    Ads1115.read_raw_args.gain = ADS1115_GAIN_2;
    Ads1115.read_raw_args.raw = &raw;
    Ads1115.read_raw(protocore_ads1115_span());
    TEST_ASSERT_TRUE(Ads1115.ok); // the transfers completed; nothing answered them
    TEST_ASSERT_EQUAL_INT16(0, raw);
}

// begin() takes the address the part is strapped to, and every later transfer goes there.
void test_begin_sends_later_transfers_to_the_address_it_was_given(void)
{
    protocore_bus_host_detach_all();
    protocore_ads1115_dev_place(&s_part, 0x4Bu); // ADDR tied so A1 A0 = 11
    s_part.ain_uv[0] = 1000000;
    Ads1115.begin_args.addr = 0x4Bu;
    Ads1115.begin(protocore_ads1115_span());
    TEST_ASSERT_TRUE(Ads1115.ok);
    int16_t raw = 0;
    Ads1115.read_raw_args.channel = 0u;
    Ads1115.read_raw_args.gain = ADS1115_GAIN_2;
    Ads1115.read_raw_args.raw = &raw;
    Ads1115.read_raw(protocore_ads1115_span());
    TEST_ASSERT_TRUE(Ads1115.ok);
    TEST_ASSERT_EQUAL_INT16(16000, raw);
    TEST_ASSERT_EQUAL_HEX16(0x4Bu, protocore_bus_host_log[protocore_bus_host_log_len - 1u].target);
    // and back to the strapped default, so the address is state and not a constant
    Ads1115.begin_args.addr = 0u;
    Ads1115.begin(protocore_ads1115_span());
    Ads1115.read_raw_args.channel = 0u;
    Ads1115.read_raw_args.gain = ADS1115_GAIN_2;
    Ads1115.read_raw_args.raw = &raw;
    Ads1115.read_raw(protocore_ads1115_span());
    TEST_ASSERT_EQUAL_HEX16(PROTOCORE_ADS1115_I2C_ADDR,
                            protocore_bus_host_log[protocore_bus_host_log_len - 1u].target);
}

// A bus that refuses the config write is reported, not converted: the reading fails before it
// asks for a conversion register that holds nothing new.
void test_a_refused_config_write_fails_the_reading(void)
{
    s_part.ain_uv[0] = 1000000;
    protocore_bus_host_fail = 1u;
    int16_t raw = -1;
    Ads1115.read_raw_args.channel = 0u;
    Ads1115.read_raw_args.gain = ADS1115_GAIN_2;
    Ads1115.read_raw_args.raw = &raw;
    Ads1115.read_raw(protocore_ads1115_span());
    TEST_ASSERT_FALSE(Ads1115.ok);
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_bus_host_log_len); // the refused transfer never ran
}

// The same for the read that follows it: the config went out, the conversion register did not come
// back, and the reading says so rather than handing out a stale sample.
void test_a_refused_conversion_read_fails_the_reading(void)
{
    s_part.ain_uv[0] = 1000000;
    protocore_bus_host_fail = 2u; // let the config write through, refuse the read after it
    int16_t raw = -1;
    Ads1115.read_raw_args.channel = 0u;
    Ads1115.read_raw_args.gain = ADS1115_GAIN_2;
    Ads1115.read_raw_args.raw = &raw;
    Ads1115.read_raw(protocore_ads1115_span());
    TEST_ASSERT_FALSE(Ads1115.ok);
}

// read_uv fails the same way read_raw does, and leaves the caller's microvolts alone.
void test_read_uv_reports_a_failed_transfer(void)
{
    s_part.ain_uv[0] = 1000000;
    protocore_bus_host_fail = 1u;
    int32_t uv = 12345;
    Ads1115.read_uv_args.channel = 0u;
    Ads1115.read_uv_args.gain = ADS1115_GAIN_2;
    Ads1115.read_uv_args.microvolts = &uv;
    Ads1115.read_uv(protocore_ads1115_span());
    TEST_ASSERT_FALSE(Ads1115.ok);
    TEST_ASSERT_EQUAL_INT32(12345, uv);
}

// A null destination is refused before anything reaches the bus.
void test_read_raw_refuses_a_null_destination(void)
{
    Ads1115.read_raw_args.channel = 0u;
    Ads1115.read_raw_args.gain = ADS1115_GAIN_2;
    Ads1115.read_raw_args.raw = 0;
    Ads1115.read_raw(protocore_ads1115_span());
    TEST_ASSERT_FALSE(Ads1115.ok);
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_bus_host_log_len);
}
