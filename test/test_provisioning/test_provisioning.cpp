// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for pc_prov_form_field(): the x-www-form-urlencoded field
// extractor + URL-decoder used by the WiFi provisioning captive portal. The
// rest of the provisioning module is ESP32-only (softAP / lwIP UDP / NVS).

#include "services/system/provisioning_service/provisioning_service.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// Plain fields are extracted by name.
void test_plain_fields()
{
    char v[64];
    TEST_ASSERT_TRUE(pc_prov_form_field("ssid=MyAP&psk=secret", "ssid", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("MyAP", v);
    TEST_ASSERT_TRUE(pc_prov_form_field("ssid=MyAP&psk=secret", "psk", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("secret", v);
}

// '+' decodes to space and %XX to the byte; value may be the last field.
void test_url_decoding()
{
    char v[64];
    TEST_ASSERT_TRUE(pc_prov_form_field("ssid=My+AP&psk=p%40ss%21", "ssid", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("My AP", v);
    TEST_ASSERT_TRUE(pc_prov_form_field("ssid=My+AP&psk=p%40ss%21", "psk", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("p@ss!", v);
}

// A missing field returns false and an empty string.
void test_missing_field()
{
    char v[64];
    TEST_ASSERT_FALSE(pc_prov_form_field("ssid=x", "psk", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("", v);
}

// The key matches only whole field names, not substrings of another field.
void test_no_substring_match()
{
    char v[64];
    TEST_ASSERT_TRUE(pc_prov_form_field("myssid=wrong&ssid=right", "ssid", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("right", v);
}

// The key can be a strict prefix of a longer field name at a valid start position
// (start-of-body or just after '&'); the match must still fail because the char
// right after the key isn't '=', so the parser keeps scanning to the real field.
void test_no_prefix_match()
{
    char v[64];
    TEST_ASSERT_TRUE(pc_prov_form_field("ssidx=wrong&ssid=right", "ssid", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("right", v);
}

// A %XX escape with an invalid first hex digit is left as a literal '%' plus
// the following characters (no decode, no extra advance).
void test_invalid_hex_escape_first_digit()
{
    char v[64];
    TEST_ASSERT_TRUE(pc_prov_form_field("ssid=a%zzb", "ssid", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("a%zzb", v);
}

// A %XX escape with a valid first digit but an invalid second digit is likewise
// left undecoded.
void test_invalid_hex_escape_second_digit()
{
    char v[64];
    TEST_ASSERT_TRUE(pc_prov_form_field("ssid=a%4zb", "ssid", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("a%4zb", v);
}

// The output is bounded by cap and always null-terminated.
void test_capacity_bound()
{
    char v[4];
    TEST_ASSERT_TRUE(pc_prov_form_field("ssid=abcdef", "ssid", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("abc", v); // 3 chars + null
}

void test_form_field_null_guards()
{
    // Any null argument (or zero cap) fails closed and leaves a writable out empty.
    char v[8] = "x";
    TEST_ASSERT_FALSE(pc_prov_form_field(NULL, "ssid", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("", v);
    TEST_ASSERT_FALSE(pc_prov_form_field("ssid=x", NULL, v, sizeof(v)));
    TEST_ASSERT_FALSE(pc_prov_form_field("ssid=x", "ssid", NULL, sizeof(v)));
    TEST_ASSERT_FALSE(pc_prov_form_field("ssid=x", "ssid", v, 0));
}

void test_host_provisioning_stubs()
{
    // On host there is no NVS/WiFi: load reports no stored creds and clears the buffers; clear no-ops.
    char ssid[8] = "x", psk[8] = "y";
    TEST_ASSERT_FALSE(pc_provisioning_load(ssid, sizeof(ssid), psk, sizeof(psk)));
    TEST_ASSERT_EQUAL_STRING("", ssid);
    TEST_ASSERT_EQUAL_STRING("", psk);
    pc_provisioning_clear(); // no-op, must not crash
}

// The ssid and psk guards in the host stub are independent; exercise both of their
// false paths (null pointer, and a valid pointer paired with a zero capacity).
void test_provisioning_load_partial_null_or_zero_cap()
{
    char psk[8] = "y";
    TEST_ASSERT_FALSE(pc_provisioning_load(NULL, 8, psk, 0));
    TEST_ASSERT_EQUAL_STRING("y", psk); // psk_cap == 0 => psk left untouched

    char ssid[8] = "z";
    TEST_ASSERT_FALSE(pc_provisioning_load(ssid, 0, NULL, 8));
    TEST_ASSERT_EQUAL_STRING("z", ssid); // ssid_cap == 0 => ssid left untouched
}

// pc_provisioning_begin() on host is a stub that only (void)s both arguments;
// call it to prove that. PC is forward-declared in provisioning_service.h and
// never given a full definition on this (non-Arduino) build, so a minimal local
// stand-in is enough to bind the reference without ever being dereferenced.
class PC
{
};

void test_provisioning_begin_stub()
{
    pc_provisioning_begin(server, "TestAP"); // must not crash, must not touch server
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_plain_fields);
    RUN_TEST(test_url_decoding);
    RUN_TEST(test_missing_field);
    RUN_TEST(test_no_substring_match);
    RUN_TEST(test_no_prefix_match);
    RUN_TEST(test_invalid_hex_escape_first_digit);
    RUN_TEST(test_invalid_hex_escape_second_digit);
    RUN_TEST(test_capacity_bound);
    RUN_TEST(test_form_field_null_guards);
    RUN_TEST(test_host_provisioning_stubs);
    RUN_TEST(test_provisioning_load_partial_null_or_zero_cap);
    RUN_TEST(test_provisioning_begin_stub);
    return UNITY_END();
}
