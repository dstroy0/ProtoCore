// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the MAC-derived device UUID (services/system/device_id). The expected
// values are RFC 4122 v5 UUIDs (DNS namespace, name = lowercase MAC hex) computed
// with Python's uuid.uuid5, so the implementation is checked against the standard.

#include "services/system/device_id/device_id.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_uuid_matches_reference_aabbccddeeff()
{
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    char out[PC_UUID_STR_LEN];
    pc_uuid_from_mac(mac, out);
    TEST_ASSERT_EQUAL_STRING("3814fb88-565c-5dc8-9b91-60d4002b6edc", out);
}

void test_uuid_matches_reference_001122334455()
{
    uint8_t mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    char out[PC_UUID_STR_LEN];
    pc_uuid_from_mac(mac, out);
    TEST_ASSERT_EQUAL_STRING("007c79f3-33f0-5a0c-9713-70921f69f8ce", out);
}

void test_uuid_is_deterministic()
{
    uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    char a[PC_UUID_STR_LEN], b[PC_UUID_STR_LEN];
    pc_uuid_from_mac(mac, a);
    pc_uuid_from_mac(mac, b);
    TEST_ASSERT_EQUAL_STRING(a, b);
}

void test_uuid_version_and_variant_bits()
{
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    char out[PC_UUID_STR_LEN];
    pc_uuid_from_mac(mac, out);
    TEST_ASSERT_EQUAL_size_t(36, strlen(out));
    TEST_ASSERT_EQUAL_CHAR('5', out[14]); // version nibble
    char v = out[19];                     // variant nibble: 8, 9, a, or b
    TEST_ASSERT_TRUE(v == '8' || v == '9' || v == 'a' || v == 'b');
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_uuid_matches_reference_aabbccddeeff);
    RUN_TEST(test_uuid_matches_reference_001122334455);
    RUN_TEST(test_uuid_is_deterministic);
    RUN_TEST(test_uuid_version_and_variant_bits);
    return UNITY_END();
}
