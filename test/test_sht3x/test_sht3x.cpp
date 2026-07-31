// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the Sensirion SHT3x codec (services/peripherals/sht3x): the CRC-8 against the datasheet
// check value (0xBEEF -> 0x92), the raw-tick -> milli-unit temperature/humidity conversions at
// the range endpoints and a mid value, and the six-byte single-shot response parse (both CRCs
// checked, corrupt CRC rejected, null out-pointers tolerated). The I2C transfer is ESP32-only.

#include "services/peripherals/sht3x/sht3x.h"
#include <stdint.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_crc8_datasheet_vector()
{
    const uint8_t v[2] = {0xBE, 0xEF};
    TEST_ASSERT_EQUAL_HEX8(0x92, pc_sht3x_crc8(v, 2)); // Sensirion datasheet check value
}

void test_conversion()
{
    // Endpoints of the linear map are exact.
    TEST_ASSERT_EQUAL_INT32(-45000, pc_sht3x_temp_mc(0));     // -45.000 C
    TEST_ASSERT_EQUAL_INT32(130000, pc_sht3x_temp_mc(65535)); // +130.000 C
    TEST_ASSERT_EQUAL_INT32(0, pc_sht3x_rh_mpct(0));          // 0.000 %
    TEST_ASSERT_EQUAL_INT32(100000, pc_sht3x_rh_mpct(65535)); // 100.000 %

    // Mid values (integer-truncated milli-units).
    TEST_ASSERT_EQUAL_INT32(23360, pc_sht3x_temp_mc(0x6400)); // 23.360 C
    TEST_ASSERT_EQUAL_INT32(50000, pc_sht3x_rh_mpct(0x8000)); // 50.000 %
}

// Build a valid six-byte response for raw T=0x6400, RH=0x8000 (CRC filled by the verified crc8).
static void build_response(uint8_t r[6])
{
    r[0] = 0x64;
    r[1] = 0x00;
    r[2] = pc_sht3x_crc8(r, 2);
    r[3] = 0x80;
    r[4] = 0x00;
    r[5] = pc_sht3x_crc8(r + 3, 2);
}

void test_parse_valid()
{
    uint8_t r[6];
    build_response(r);
    int32_t t = 0, h = 0;
    TEST_ASSERT_TRUE(pc_sht3x_parse(r, &t, &h));
    TEST_ASSERT_EQUAL_INT32(23360, t);
    TEST_ASSERT_EQUAL_INT32(50000, h);
}

void test_parse_bad_crc()
{
    uint8_t r[6];
    int32_t t = 0, h = 0;
    build_response(r);
    r[2] ^= 0xFF; // corrupt the temperature CRC
    TEST_ASSERT_FALSE(pc_sht3x_parse(r, &t, &h));

    build_response(r);
    r[5] ^= 0xFF; // corrupt the humidity CRC
    TEST_ASSERT_FALSE(pc_sht3x_parse(r, &t, &h));
}

void test_parse_null_out()
{
    uint8_t r[6];
    build_response(r);
    TEST_ASSERT_TRUE(pc_sht3x_parse(r, nullptr, nullptr)); // must verify CRCs without writing out
}

void test_parse_null_resp()
{
    int32_t t = 0, h = 0;
    TEST_ASSERT_FALSE(pc_sht3x_parse(nullptr, &t, &h)); // null response buffer must be rejected
}

void test_host_i2c_stubs()
{
    // Host build: no I2C. begin() fails and read() reports failure.
    TEST_ASSERT_FALSE(pc_sht3x_begin(0x44));
    int32_t t = 0, h = 0;
    TEST_ASSERT_FALSE(pc_sht3x_read(&t, &h));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_crc8_datasheet_vector);
    RUN_TEST(test_conversion);
    RUN_TEST(test_parse_valid);
    RUN_TEST(test_parse_bad_crc);
    RUN_TEST(test_parse_null_out);
    RUN_TEST(test_parse_null_resp);
    RUN_TEST(test_host_i2c_stubs);
    return UNITY_END();
}
