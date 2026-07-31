// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the SDI-12 codec (services/peripherals/sdi12): the command builders, the measurement
// response parser (atttn), the data-value splitter, and the SDI-12 CRC (compute / encode /
// verify round-trip). Pure host tests.

#include "services/peripherals/sdi12/sdi12.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_command_builders()
{
    char buf[16];
    TEST_ASSERT_EQUAL_size_t(2, pc_sdi12_build_ack(buf, sizeof(buf), '0'));
    TEST_ASSERT_EQUAL_STRING("0!", buf);
    TEST_ASSERT_EQUAL_size_t(3, pc_sdi12_build_identify(buf, sizeof(buf), '0'));
    TEST_ASSERT_EQUAL_STRING("0I!", buf);
    pc_sdi12_build_measure(buf, sizeof(buf), '3', false);
    TEST_ASSERT_EQUAL_STRING("3M!", buf);
    pc_sdi12_build_measure(buf, sizeof(buf), '3', true);
    TEST_ASSERT_EQUAL_STRING("3MC!", buf);
    pc_sdi12_build_concurrent(buf, sizeof(buf), '1', false);
    TEST_ASSERT_EQUAL_STRING("1C!", buf);
    pc_sdi12_build_data(buf, sizeof(buf), '0', 0);
    TEST_ASSERT_EQUAL_STRING("0D0!", buf);
    pc_sdi12_build_change_address(buf, sizeof(buf), '0', '5');
    TEST_ASSERT_EQUAL_STRING("0A5!", buf);
    pc_sdi12_build_query_address(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("?!", buf);
}

// Continuous-measurement (aR<n>! / aRC<n>!) and verification (aV!) builders.
void test_build_continuous_and_verify()
{
    char buf[16];
    TEST_ASSERT_EQUAL_size_t(4, pc_sdi12_build_continuous(buf, sizeof(buf), '0', 0, false));
    TEST_ASSERT_EQUAL_STRING("0R0!", buf);
    pc_sdi12_build_continuous(buf, sizeof(buf), '2', 5, false);
    TEST_ASSERT_EQUAL_STRING("2R5!", buf);
    pc_sdi12_build_continuous(buf, sizeof(buf), '1', 3, true); // CRC variant inserts the 'C'
    TEST_ASSERT_EQUAL_STRING("1RC3!", buf);
    TEST_ASSERT_EQUAL_size_t(0, pc_sdi12_build_continuous(buf, sizeof(buf), '0', 10, false)); // index > 9

    pc_sdi12_build_verify(buf, sizeof(buf), '7');
    TEST_ASSERT_EQUAL_STRING("7V!", buf);
}

// Additional-measurement (aM<n>! / aMC<n>!) and additional-concurrent (aC<n>! / aCC<n>!) builders.
void test_build_additional_measurements()
{
    char buf[16];
    TEST_ASSERT_EQUAL_size_t(4, pc_sdi12_build_measure_additional(buf, sizeof(buf), '0', 1, false));
    TEST_ASSERT_EQUAL_STRING("0M1!", buf);
    pc_sdi12_build_measure_additional(buf, sizeof(buf), '3', 9, false);
    TEST_ASSERT_EQUAL_STRING("3M9!", buf);
    pc_sdi12_build_measure_additional(buf, sizeof(buf), '1', 2, true); // CRC variant inserts the 'C'
    TEST_ASSERT_EQUAL_STRING("1MC2!", buf);

    pc_sdi12_build_concurrent_additional(buf, sizeof(buf), '0', 1, false);
    TEST_ASSERT_EQUAL_STRING("0C1!", buf);
    pc_sdi12_build_concurrent_additional(buf, sizeof(buf), '2', 4, true);
    TEST_ASSERT_EQUAL_STRING("2CC4!", buf);

    // The index must be 1..9 (0 is the primary aM! / aC!, handled by the base builders).
    TEST_ASSERT_EQUAL_size_t(0, pc_sdi12_build_measure_additional(buf, sizeof(buf), '0', 0, false));
    TEST_ASSERT_EQUAL_size_t(0, pc_sdi12_build_measure_additional(buf, sizeof(buf), '0', 10, false));
    TEST_ASSERT_EQUAL_size_t(0, pc_sdi12_build_concurrent_additional(buf, sizeof(buf), '0', 0, false));
    TEST_ASSERT_EQUAL_size_t(0, pc_sdi12_build_concurrent_additional(buf, sizeof(buf), '0', 10, false));
}

void test_parse_measure_m()
{
    // aM! response "0" + "012" (12 s) + "2" (2 values).
    const char *resp = "00122\r\n";
    char addr = 0;
    uint16_t ready = 0;
    uint8_t n = 0;
    TEST_ASSERT_TRUE(pc_sdi12_parse_measure(resp, strlen(resp), &addr, &ready, &n));
    TEST_ASSERT_EQUAL_CHAR('0', addr);
    TEST_ASSERT_EQUAL_UINT16(12, ready);
    TEST_ASSERT_EQUAL_UINT8(2, n);
}

void test_parse_identify()
{
    // aI! response: addr 0, SDI-12 v1.4, vendor "ACMEINC " (space-padded to 8), model "SNS100", version "1.0".
    const char *resp = "014ACMEINC SNS1001.0";
    Sdi12Identity id;
    TEST_ASSERT_TRUE(pc_sdi12_parse_identify(resp, strlen(resp), &id));
    TEST_ASSERT_EQUAL_CHAR('0', id.addr);
    TEST_ASSERT_EQUAL_STRING("14", id.sdi_version);
    TEST_ASSERT_EQUAL_STRING("ACMEINC ", id.vendor);
    TEST_ASSERT_EQUAL_STRING("SNS100", id.model);
    TEST_ASSERT_EQUAL_STRING("1.0", id.sensor_version);

    // A response carrying an optional field after the 20 fixed octets still parses the fixed fields.
    const char *resp2 = "113MYVENDORMODEL92.5OPTIONAL";
    TEST_ASSERT_TRUE(pc_sdi12_parse_identify(resp2, strlen(resp2), &id));
    TEST_ASSERT_EQUAL_CHAR('1', id.addr);
    TEST_ASSERT_EQUAL_STRING("13", id.sdi_version);
    TEST_ASSERT_EQUAL_STRING("MYVENDOR", id.vendor);
    TEST_ASSERT_EQUAL_STRING("MODEL9", id.model);
    TEST_ASSERT_EQUAL_STRING("2.5", id.sensor_version);

    // A too-short response and null args are rejected.
    TEST_ASSERT_FALSE(pc_sdi12_parse_identify(resp, 19, &id));
    TEST_ASSERT_FALSE(pc_sdi12_parse_identify(nullptr, 20, &id));
    TEST_ASSERT_FALSE(pc_sdi12_parse_identify(resp, strlen(resp), nullptr));
}

void test_parse_measure_concurrent_two_digit_count()
{
    // aC! response "0" + "013" (13 s) + "10" (10 values).
    const char *resp = "001310\r\n";
    char addr = 0;
    uint16_t ready = 0;
    uint8_t n = 0;
    TEST_ASSERT_TRUE(pc_sdi12_parse_measure(resp, strlen(resp), &addr, &ready, &n));
    TEST_ASSERT_EQUAL_UINT16(13, ready);
    TEST_ASSERT_EQUAL_UINT8(10, n);
}

void test_parse_values()
{
    const char *resp = "0+3.14-2.5+0.001\r\n";
    float v[4];
    size_t n = 0;
    TEST_ASSERT_TRUE(pc_sdi12_parse_values(resp, strlen(resp), v, 4, &n));
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.14f, v[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -2.5f, v[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.001f, v[2]);
}

void test_crc_roundtrip()
{
    // Build a response, append the SDI-12 CRC, then verify it (and that corruption fails).
    char resp[32];
    strcpy(resp, "0+3.14");
    size_t len = strlen(resp);
    char crc[SDI12_CRC_CHARS];
    pc_sdi12_crc_encode(pc_sdi12_crc16((const uint8_t *)resp, len), crc);
    memcpy(resp + len, crc, SDI12_CRC_CHARS);
    size_t total = len + SDI12_CRC_CHARS;

    TEST_ASSERT_TRUE(pc_sdi12_check_crc(resp, total));

    // a CRLF after the CRC is tolerated.
    resp[total] = '\r';
    resp[total + 1] = '\n';
    TEST_ASSERT_TRUE(pc_sdi12_check_crc(resp, total + 2));

    // corrupting a data octet breaks the CRC.
    char bad[32];
    memcpy(bad, resp, total);
    bad[1] ^= 0x01;
    TEST_ASSERT_FALSE(pc_sdi12_check_crc(bad, total));
}

// The CRC octets are always printable (0x40..0x7F), per the SDI-12 encoding.
void test_crc_encode_printable()
{
    char crc[SDI12_CRC_CHARS];
    pc_sdi12_crc_encode(0xFFFF, crc);
    for (int i = 0; i < SDI12_CRC_CHARS; i++)
    {
        TEST_ASSERT_TRUE((uint8_t)crc[i] >= 0x40 && (uint8_t)crc[i] <= 0x7F);
    }
}

// Builder / parser / CRC reject and skip branches.
void test_sdi12_error_paths()
{
    char buf[16];
    TEST_ASSERT_EQUAL_size_t(0, pc_sdi12_build(nullptr, sizeof(buf), '0', "M")); // null buf
    TEST_ASSERT_EQUAL_size_t(0, pc_sdi12_build(buf, sizeof(buf), '0', nullptr)); // null body
    TEST_ASSERT_EQUAL_size_t(0, pc_sdi12_build(buf, 2, '0', "M"));               // cap too small
    TEST_ASSERT_EQUAL_size_t(0, pc_sdi12_build_data(buf, sizeof(buf), '0', 10)); // d_index > 9

    char addr;
    uint16_t ready;
    uint8_t n;
    TEST_ASSERT_FALSE(pc_sdi12_parse_measure(nullptr, 5, &addr, &ready, &n)); // null resp
    TEST_ASSERT_FALSE(pc_sdi12_parse_measure("012", 3, &addr, &ready, &n));   // len < 5
    TEST_ASSERT_FALSE(pc_sdi12_parse_measure("0X122", 5, &addr, &ready, &n)); // non-digit in the ttt field
    TEST_ASSERT_FALSE(pc_sdi12_parse_measure("0120X", 5, &addr, &ready, &n)); // non-digit value count

    float v[4];
    size_t cnt = 0;
    TEST_ASSERT_FALSE(pc_sdi12_parse_values(nullptr, 3, v, 4, &cnt));     // null resp
    TEST_ASSERT_FALSE(pc_sdi12_parse_values("0+1", 3, nullptr, 4, &cnt)); // null out
    TEST_ASSERT_FALSE(pc_sdi12_parse_values("0+1", 3, v, 4, nullptr));    // null count
    // A non +/- separator is skipped; a trailing '+' with no digits is skipped.
    const char *r = "0X+1.5+\r\n";
    TEST_ASSERT_TRUE(pc_sdi12_parse_values(r, strlen(r), v, 4, &cnt));
    TEST_ASSERT_EQUAL_size_t(1, cnt);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.5f, v[0]);

    TEST_ASSERT_FALSE(pc_sdi12_check_crc(nullptr, 5));  // null
    TEST_ASSERT_FALSE(pc_sdi12_check_crc("ab\r\n", 4)); // after trimming CRLF, too short for data + CRC
}

// pc_sdi12_build_concurrent's with_crc=true arm ("CC") wasn't exercised elsewhere.
void test_build_concurrent_crc()
{
    char buf[16];
    pc_sdi12_build_concurrent(buf, sizeof(buf), '2', true);
    TEST_ASSERT_EQUAL_STRING("2CC!", buf);
}

// addr / ready_sec / num_values are each optional (nullptr skips the corresponding write).
void test_parse_measure_null_outputs()
{
    const char *resp = "00122\r\n";
    TEST_ASSERT_TRUE(pc_sdi12_parse_measure(resp, strlen(resp), nullptr, nullptr, nullptr));

    char addr = 0;
    uint16_t ready = 0;
    uint8_t n = 0;
    TEST_ASSERT_TRUE(pc_sdi12_parse_measure(resp, strlen(resp), nullptr, &ready, &n));
    TEST_ASSERT_TRUE(pc_sdi12_parse_measure(resp, strlen(resp), &addr, nullptr, &n));
    TEST_ASSERT_TRUE(pc_sdi12_parse_measure(resp, strlen(resp), &addr, &ready, nullptr));
}

// Exactly 5 octets, no CRLF: the value-count loop must stop because i reaches len,
// not because it hit a non-digit octet.
void test_parse_measure_count_runs_to_buffer_end()
{
    const char *resp = "00125";
    uint8_t n = 0;
    TEST_ASSERT_TRUE(pc_sdi12_parse_measure(resp, strlen(resp), nullptr, nullptr, &n));
    TEST_ASSERT_EQUAL_UINT8(5, n);
}

void test_parse_values_stops_at_max()
{
    float v[4];
    size_t n = 0;

    // 3 values present but max is 2: the loop must exit via cnt<max turning false.
    const char *resp = "0+1+2+3\r\n";
    TEST_ASSERT_TRUE(pc_sdi12_parse_values(resp, strlen(resp), v, 2, &n));
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, v[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.0f, v[1]);

    // No terminator and fewer values than max: the loop must exit via i<len turning
    // false (there is no '\r'/'\n' to trigger the internal break).
    const char *resp_no_term = "0+1.5";
    n = 0;
    TEST_ASSERT_TRUE(pc_sdi12_parse_values(resp_no_term, strlen(resp_no_term), v, 4, &n));
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.5f, v[0]);
}

void test_parse_values_bare_lf_and_minus_no_digits()
{
    // A lone '\n' terminator (no preceding '\r') exercises the c=='\n' branch directly
    // (c=='\r' must evaluate false first).
    const char *resp_lf = "0+1.5\n";
    float v[4];
    size_t n = 0;
    TEST_ASSERT_TRUE(pc_sdi12_parse_values(resp_lf, strlen(resp_lf), v, 4, &n));
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.5f, v[0]);

    // A '-' with no digits following it leaves pc_strtof's end == start, so it must
    // be skipped like any other non-numeric octet.
    const char *resp_minus = "0-X+2.5\r\n";
    n = 0;
    TEST_ASSERT_TRUE(pc_sdi12_parse_values(resp_minus, strlen(resp_minus), v, 4, &n));
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.5f, v[0]);
}

// An all-CR/LF buffer trims down to len == 0 (the trim loop's len>0 guard turning
// false), which is then rejected for being shorter than the CRC.
void test_check_crc_trims_to_nothing()
{
    TEST_ASSERT_FALSE(pc_sdi12_check_crc("\r\n", 2));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_command_builders);
    RUN_TEST(test_build_continuous_and_verify);
    RUN_TEST(test_build_additional_measurements);
    RUN_TEST(test_parse_measure_m);
    RUN_TEST(test_parse_identify);
    RUN_TEST(test_parse_measure_concurrent_two_digit_count);
    RUN_TEST(test_parse_values);
    RUN_TEST(test_crc_roundtrip);
    RUN_TEST(test_crc_encode_printable);
    RUN_TEST(test_sdi12_error_paths);
    RUN_TEST(test_build_concurrent_crc);
    RUN_TEST(test_parse_measure_null_outputs);
    RUN_TEST(test_parse_measure_count_runs_to_buffer_end);
    RUN_TEST(test_parse_values_stops_at_max);
    RUN_TEST(test_parse_values_bare_lf_and_minus_no_digits);
    RUN_TEST(test_check_crc_trims_to_nothing);
    return UNITY_END();
}
