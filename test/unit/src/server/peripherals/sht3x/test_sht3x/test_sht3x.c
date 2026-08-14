// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Sensirion SHT3x codec (server/peripherals/sht3x/sht3x.h).
//
// Expected values come from the SHT3x-DIS data sheet (version 7, December 2022): Table 20 for the
// CRC-8 properties and its worked example, section 4.13 for the two conversion formulas, and
// Tables 9 / 14 / 17 / 16 for the command codes.
//
// test_datasheet_crc_check_value is the load-bearing case. Table 20 publishes exactly one CRC
// example, "CRC (0xBEEF) = 0x92", and it is the only thing standing between a corrupted I2C read
// and a temperature that is silently wrong. The same published word carries the six-byte response
// test, so that vector's input octets are the data sheet's rather than this codec's own output.

#include "server/peripherals/sht3x/sht3x.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Table 20: polynomial 0x31, initialization 0xFF, no reflection, final XOR 0x00,
// "Examples  CRC (0xBEEF) = 0x92".
void test_datasheet_crc_check_value(void)
{
    static const uint8_t BEEF[2] = {0xBE, 0xEF};
    TEST_ASSERT_EQUAL_HEX8(0x92, protocore_sht3x_crc8(BEEF, sizeof(BEEF)));

    // the CRC covers exactly the two data bytes, so a different pair gives a different check byte
    static const uint8_t OTHER[2] = {0xBE, 0xEE};
    TEST_ASSERT_NOT_EQUAL(0x92, protocore_sht3x_crc8(OTHER, sizeof(OTHER)));
    static const uint8_t SWAPPED[2] = {0xEF, 0xBE};
    TEST_ASSERT_NOT_EQUAL(0x92, protocore_sht3x_crc8(SWAPPED, sizeof(SWAPPED)));

    // an initialization of 0xFF rather than 0x00 is what makes a zero word check non-zero
    static const uint8_t ZEROS[2] = {0x00, 0x00};
    TEST_ASSERT_NOT_EQUAL(0x00, protocore_sht3x_crc8(ZEROS, sizeof(ZEROS)));
}

// Section 4.13: T[C] = -45 + 175 * S_T / (2^16 - 1). In milli-degrees, and with 65535 = 5 * 13107,
// three points come out exact:
//   S = 0     -> -45000 + 0                  = -45000
//   S = 13107 -> -45000 + 175000/5   = -45000 + 35000  = -10000
//   S = 26214 -> -45000 + 175000*2/5 = -45000 + 70000  =  25000
//   S = 39321 -> -45000 + 175000*3/5 = -45000 + 105000 =  60000
//   S = 65535 -> -45000 + 175000                       = 130000
void test_datasheet_temperature_formula(void)
{
    TEST_ASSERT_EQUAL_INT32(-45000, protocore_sht3x_temp_mc(0));
    TEST_ASSERT_EQUAL_INT32(-10000, protocore_sht3x_temp_mc(13107));
    TEST_ASSERT_EQUAL_INT32(25000, protocore_sht3x_temp_mc(26214));
    TEST_ASSERT_EQUAL_INT32(60000, protocore_sht3x_temp_mc(39321));
    TEST_ASSERT_EQUAL_INT32(130000, protocore_sht3x_temp_mc(65535));
}

// Section 4.13: RH[%] = 100 * S_RH / (2^16 - 1), in milli-percent, at the same exact points.
//   S = 0 -> 0; 13107 -> 20000; 26214 -> 40000; 39321 -> 60000; 65535 -> 100000
void test_datasheet_humidity_formula(void)
{
    TEST_ASSERT_EQUAL_INT32(0, protocore_sht3x_rh_mpct(0));
    TEST_ASSERT_EQUAL_INT32(20000, protocore_sht3x_rh_mpct(13107));
    TEST_ASSERT_EQUAL_INT32(40000, protocore_sht3x_rh_mpct(26214));
    TEST_ASSERT_EQUAL_INT32(60000, protocore_sht3x_rh_mpct(39321));
    TEST_ASSERT_EQUAL_INT32(100000, protocore_sht3x_rh_mpct(65535));
}

// The formulas are monotonic in the raw tick and never leave the sensor's own range: full scale is
// -45 C .. 130 C and 0 .. 100 %RH, and a reading past either end is a decode bug, not a reading.
void test_conversions_stay_inside_the_sensor_range(void)
{
    int32_t prev_t = protocore_sht3x_temp_mc(0);
    int32_t prev_h = protocore_sht3x_rh_mpct(0);
    for (uint32_t raw = 257; raw <= 65535u; raw += 257)
    {
        const int32_t t = protocore_sht3x_temp_mc((uint16_t)raw);
        const int32_t h = protocore_sht3x_rh_mpct((uint16_t)raw);
        TEST_ASSERT_TRUE(t >= prev_t);
        TEST_ASSERT_TRUE(h >= prev_h);
        TEST_ASSERT_TRUE(t >= -45000 && t <= 130000);
        TEST_ASSERT_TRUE(h >= 0 && h <= 100000);
        prev_t = t;
        prev_h = h;
    }
}

// Section 4.4: the sensor sends two temperature bytes, then their CRC, then two humidity bytes and
// their CRC. Both words here are the data sheet's own 0xBEEF, so the six input octets are entirely
// published values; the two outputs are the formulas applied to S = 48879:
//   T  = -45000 + 175000 * 48879 / 65535. 175000 * 48879 = 8 553 825 000, and
//        8 553 825 000 / 65535 = 130 523 remainder 195, so T = -45000 + 130523 =  85523
//   RH =           100000 * 48879 / 65535. 100000 * 48879 = 4 887 900 000, and
//        4 887 900 000 / 65535 =  74 584 remainder 37 560, so RH =              74584
void test_six_byte_response(void)
{
    static const uint8_t RESP[6] = {0xBE, 0xEF, 0x92, 0xBE, 0xEF, 0x92};
    int32_t t = 0;
    int32_t h = 0;
    TEST_ASSERT_TRUE(protocore_sht3x_parse(RESP, &t, &h));
    TEST_ASSERT_EQUAL_INT32(85523, t);
    TEST_ASSERT_EQUAL_INT32(74584, h);

    // either output is optional; the CRCs are still checked
    TEST_ASSERT_TRUE(protocore_sht3x_parse(RESP, NULL, &h));
    TEST_ASSERT_EQUAL_INT32(74584, h);
    TEST_ASSERT_TRUE(protocore_sht3x_parse(RESP, &t, NULL));
    TEST_ASSERT_EQUAL_INT32(85523, t);
    TEST_ASSERT_TRUE(protocore_sht3x_parse(RESP, NULL, NULL));
}

// A corrupted read is refused rather than converted: an SHT3x on a long or noisy bus is exactly
// what the per-word CRC exists for, and a wrong temperature that looks plausible is the failure
// mode worth stopping.
void test_corrupt_response_is_refused(void)
{
    uint8_t bad[6];
    int32_t t = 0x5A5A5A;
    int32_t h = 0x5A5A5A;

    static const uint8_t GOOD[6] = {0xBE, 0xEF, 0x92, 0xBE, 0xEF, 0x92};
    for (size_t i = 0; i < 6; i++)
    {
        for (size_t k = 0; k < 6; k++)
        {
            bad[k] = GOOD[k];
        }
        bad[i] ^= 0x01;
        TEST_ASSERT_FALSE(protocore_sht3x_parse(bad, &t, &h));
    }
    // nothing was written through on the refusal
    TEST_ASSERT_EQUAL_INT32(0x5A5A5A, t);
    TEST_ASSERT_EQUAL_INT32(0x5A5A5A, h);

    TEST_ASSERT_FALSE(protocore_sht3x_parse(NULL, &t, &h));
}

// Tables 9, 14, 16 and 17: the 16-bit commands, sent most significant byte first.
void test_datasheet_command_codes(void)
{
    // Table 9, single shot with clock stretching disabled: high 0x2400, medium 0x240B, low 0x2416
    TEST_ASSERT_EQUAL_HEX16(0x2400, SHT3X_CMD_SINGLE_HIGH);
    TEST_ASSERT_EQUAL_HEX16(0x240B, SHT3X_CMD_SINGLE_MED);
    TEST_ASSERT_EQUAL_HEX16(0x2416, SHT3X_CMD_SINGLE_LOW);
    // Table 14 soft reset, Table 17 read status register, Table 16 heater on / off
    TEST_ASSERT_EQUAL_HEX16(0x30A2, SHT3X_CMD_SOFT_RESET);
    TEST_ASSERT_EQUAL_HEX16(0xF32D, SHT3X_CMD_READ_STATUS);
    TEST_ASSERT_EQUAL_HEX16(0x306D, SHT3X_CMD_HEATER_ON);
    TEST_ASSERT_EQUAL_HEX16(0x3066, SHT3X_CMD_HEATER_OFF);
}
