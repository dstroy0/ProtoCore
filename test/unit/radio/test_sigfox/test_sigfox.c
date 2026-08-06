// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the Sigfox AT-command codec (services/radio/sigfox): the AT$SF uplink command
// (uppercase hex encoding of the payload), its bounds (12-byte payload cap, output cap),
// and the OK / ERROR / PENDING response classification. Pure host tests.

#include "services/radio/sigfox/sigfox.h"
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_build_uplink_hex_encode()
{
    const uint8_t payload[2] = {0xAB, 0x12};
    char out[32];
    uint16_t n = pc_sigfox_build_uplink(payload, 2, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("AT$SF=AB12\r\n", out);
    TEST_ASSERT_EQUAL_UINT16(12, n);
}

void test_build_uplink_single_byte()
{
    const uint8_t payload[1] = {0x0F};
    char out[32];
    uint16_t n = pc_sigfox_build_uplink(payload, 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("AT$SF=0F\r\n", out);
    TEST_ASSERT_EQUAL_UINT16(10, n);
}

void test_build_uplink_bounds()
{
    uint8_t big[13] = {0};
    char out[64];
    TEST_ASSERT_EQUAL_UINT16(0, pc_sigfox_build_uplink(big, 13, out, sizeof(out))); // > 12-byte cap
    TEST_ASSERT_EQUAL_UINT16(0, pc_sigfox_build_uplink(big, 0, out, sizeof(out)));  // empty
    uint8_t p[4] = {1, 2, 3, 4};
    char small[10];
    TEST_ASSERT_EQUAL_UINT16(0, pc_sigfox_build_uplink(p, 4, small, sizeof(small))); // needs 17, cap 10
}

void test_build_uplink_null_args()
{
    const uint8_t payload[2] = {0xAB, 0x12};
    char out[32];
    TEST_ASSERT_EQUAL_UINT16(0, pc_sigfox_build_uplink(payload, 2, NULL, sizeof(out))); // null out
    TEST_ASSERT_EQUAL_UINT16(0, pc_sigfox_build_uplink(NULL, 2, out, sizeof(out)));     // null payload
}

void test_parse_response_ok()
{
    TEST_ASSERT_EQUAL_INT(SIGFOX_OK, pc_sigfox_parse_response("OK\r\n", 4));
}

void test_parse_response_error()
{
    TEST_ASSERT_EQUAL_INT(SIGFOX_ERROR, pc_sigfox_parse_response("ERROR\r\n", 7));
}

void test_parse_response_pending()
{
    TEST_ASSERT_EQUAL_INT(SIGFOX_PENDING, pc_sigfox_parse_response("AT$SF=AB12\r\n", 12)); // echo, no verdict yet
    TEST_ASSERT_EQUAL_INT(SIGFOX_PENDING, pc_sigfox_parse_response("", 0));
}

void test_parse_response_null_buf()
{
    TEST_ASSERT_EQUAL_INT(SIGFOX_PENDING, pc_sigfox_parse_response(NULL, 5));
}

void test_parse_response_error_wins()
{
    // If a buffer holds both (e.g. an echoed "OK" token then an ERROR), ERROR is reported.
    const char *both = "sending OK... ERROR";
    TEST_ASSERT_EQUAL_INT(SIGFOX_ERROR, pc_sigfox_parse_response(both, (uint16_t)strlen(both)));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_build_uplink_hex_encode);
    RUN_TEST(test_build_uplink_single_byte);
    RUN_TEST(test_build_uplink_bounds);
    RUN_TEST(test_build_uplink_null_args);
    RUN_TEST(test_parse_response_ok);
    RUN_TEST(test_parse_response_error);
    RUN_TEST(test_parse_response_pending);
    RUN_TEST(test_parse_response_null_buf);
    RUN_TEST(test_parse_response_error_wins);
    return UNITY_END();
}
