// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the typed NVS config store (services/storage/config_store), exercised
// against the host in-memory backend: string / u32 / blob round-trips, defaults
// on missing keys, overwrite, capacity bounds, erase, clear, and table limits.

#include "services/storage/config_store/config_store.h"
#include <stdio.h>
#include <string.h>

#include <unity.h>

void setUp()
{
    pc_config_begin("test");
    pc_config_clear();
}
void tearDown()
{
}

void test_str_round_trip()
{
    TEST_ASSERT_TRUE(pc_config_set_str("ssid", "myssid"));
    char out[32];
    size_t n = pc_config_get_str("ssid", out, sizeof(out), "def");
    TEST_ASSERT_EQUAL_size_t(6, n);
    TEST_ASSERT_EQUAL_STRING("myssid", out);
}

void test_str_default_when_missing()
{
    char out[32];
    size_t n = pc_config_get_str("nope", out, sizeof(out), "fallback");
    TEST_ASSERT_EQUAL_STRING("fallback", out);
    TEST_ASSERT_EQUAL_size_t(8, n);
}

void test_str_overwrite()
{
    pc_config_set_str("k", "a");
    pc_config_set_str("k", "bb");
    char out[8];
    pc_config_get_str("k", out, sizeof(out), "");
    TEST_ASSERT_EQUAL_STRING("bb", out);
}

void test_str_truncates_to_capacity()
{
    pc_config_set_str("k", "123456789");
    char out[4];
    size_t n = pc_config_get_str("k", out, sizeof(out), "");
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_STRING("123", out);
}

void test_u32_round_trip()
{
    TEST_ASSERT_TRUE(pc_config_set_u32("ip", 0xC0A80164u)); // 192.168.1.100
    TEST_ASSERT_EQUAL_UINT32(0xC0A80164u, pc_config_get_u32("ip", 0));
}

void test_u32_default_when_missing()
{
    TEST_ASSERT_EQUAL_UINT32(42u, pc_config_get_u32("nope", 42u));
}

void test_blob_round_trip()
{
    uint8_t in[5] = {1, 2, 3, 4, 5};
    TEST_ASSERT_TRUE(pc_config_set_blob("psk", in, sizeof(in)));
    uint8_t out[5] = {0};
    size_t n = pc_config_get_blob("psk", out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(5, n);
    TEST_ASSERT_EQUAL_MEMORY(in, out, 5);
}

void test_blob_bounded_by_capacity()
{
    uint8_t in[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    pc_config_set_blob("b", in, sizeof(in));
    uint8_t out[4] = {0};
    size_t n = pc_config_get_blob("b", out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_MEMORY(in, out, 4);
}

void test_blob_missing_returns_zero()
{
    uint8_t out[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_config_get_blob("nope", out, sizeof(out)));
}

void test_erase_removes_key()
{
    pc_config_set_str("k", "v");
    TEST_ASSERT_TRUE(pc_config_erase("k"));
    char out[8];
    pc_config_get_str("k", out, sizeof(out), "gone");
    TEST_ASSERT_EQUAL_STRING("gone", out);
    TEST_ASSERT_FALSE(pc_config_erase("k")); // already gone
}

void test_clear_wipes_namespace()
{
    pc_config_set_str("a", "1");
    pc_config_set_u32("b", 2);
    pc_config_clear();
    char out[8];
    pc_config_get_str("a", out, sizeof(out), "x");
    TEST_ASSERT_EQUAL_STRING("x", out);
    TEST_ASSERT_EQUAL_UINT32(9u, pc_config_get_u32("b", 9u));
}

void test_table_full_rejects_new_key()
{
    char key[8];
    for (int i = 0; i < PC_CONFIG_MAX_ENTRIES; i++)
    {
        snprintf(key, sizeof(key), "k%d", i);
        TEST_ASSERT_TRUE(pc_config_set_u32(key, (uint32_t)i));
    }
    TEST_ASSERT_FALSE(pc_config_set_u32("overflow", 1));
}

void test_existing_key_overwrites_even_when_full()
{
    char key[8];
    for (int i = 0; i < PC_CONFIG_MAX_ENTRIES; i++)
    {
        snprintf(key, sizeof(key), "k%d", i);
        pc_config_set_u32(key, (uint32_t)i);
    }
    TEST_ASSERT_TRUE(pc_config_set_u32("k0", 999u)); // existing key, no new slot needed
    TEST_ASSERT_EQUAL_UINT32(999u, pc_config_get_u32("k0", 0));
}

void test_key_too_long_rejected()
{
    char longkey[PC_CONFIG_KEY_MAX + 4];
    memset(longkey, 'k', sizeof(longkey) - 1);
    longkey[sizeof(longkey) - 1] = '\0';
    TEST_ASSERT_FALSE(pc_config_set_str(longkey, "v"));
}

void test_setter_getter_null_guards()
{
    pc_config_begin("ns");
    TEST_ASSERT_FALSE(pc_config_set_str("k", NULL));                     // null value
    TEST_ASSERT_EQUAL_size_t(0, pc_config_get_str("k", NULL, 0, "def")); // null out
    TEST_ASSERT_FALSE(pc_config_set_blob("k", NULL, 4));                 // null data
}

void test_key_ok_rejects_null_and_empty_key()
{
    TEST_ASSERT_FALSE(pc_config_set_str(NULL, "v")); // null key
    char out[8];
    size_t n = pc_config_get_str("", out, sizeof(out), NULL); // empty key, no default
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_EQUAL_size_t(0, n);
}

void test_get_str_zero_capacity_with_nonnull_out()
{
    char out[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_config_get_str("ssid", out, 0, "def"));
}

void test_set_blob_rejects_len_over_capacity()
{
    uint8_t big[PC_CONFIG_VAL_MAX + 1];
    memset(big, 0xAA, sizeof(big));
    TEST_ASSERT_FALSE(pc_config_set_blob("big", big, sizeof(big))); // len > PC_CONFIG_VAL_MAX
}

void test_get_u32_short_entry_returns_default()
{
    pc_config_set_str("short", "ab"); // stored length (incl. terminator) is 3, < 4 bytes
    TEST_ASSERT_EQUAL_UINT32(7u, pc_config_get_u32("short", 7u));
}

void test_get_u32_rejects_invalid_key()
{
    TEST_ASSERT_EQUAL_UINT32(5u, pc_config_get_u32(NULL, 5u));
}

void test_get_blob_rejects_invalid_key()
{
    uint8_t out[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_config_get_blob(NULL, out, sizeof(out)));
}

void test_get_blob_null_out_with_existing_key()
{
    uint8_t in[3] = {9, 8, 7};
    pc_config_set_blob("bk", in, sizeof(in));
    TEST_ASSERT_EQUAL_size_t(0, pc_config_get_blob("bk", NULL, 8));
}

void test_get_blob_entry_shorter_than_capacity()
{
    uint8_t in[3] = {1, 2, 3};
    pc_config_set_blob("bk2", in, sizeof(in));
    uint8_t out[10] = {0};
    size_t n = pc_config_get_blob("bk2", out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_MEMORY(in, out, 3);
}

void test_erase_rejects_invalid_key()
{
    TEST_ASSERT_FALSE(pc_config_erase(NULL));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_str_round_trip);
    RUN_TEST(test_str_default_when_missing);
    RUN_TEST(test_str_overwrite);
    RUN_TEST(test_str_truncates_to_capacity);
    RUN_TEST(test_u32_round_trip);
    RUN_TEST(test_u32_default_when_missing);
    RUN_TEST(test_blob_round_trip);
    RUN_TEST(test_blob_bounded_by_capacity);
    RUN_TEST(test_blob_missing_returns_zero);
    RUN_TEST(test_erase_removes_key);
    RUN_TEST(test_clear_wipes_namespace);
    RUN_TEST(test_table_full_rejects_new_key);
    RUN_TEST(test_existing_key_overwrites_even_when_full);
    RUN_TEST(test_key_too_long_rejected);
    RUN_TEST(test_setter_getter_null_guards);
    RUN_TEST(test_key_ok_rejects_null_and_empty_key);
    RUN_TEST(test_get_str_zero_capacity_with_nonnull_out);
    RUN_TEST(test_set_blob_rejects_len_over_capacity);
    RUN_TEST(test_get_u32_short_entry_returns_default);
    RUN_TEST(test_get_u32_rejects_invalid_key);
    RUN_TEST(test_get_blob_rejects_invalid_key);
    RUN_TEST(test_get_blob_null_out_with_existing_key);
    RUN_TEST(test_get_blob_entry_shorter_than_capacity);
    RUN_TEST(test_erase_rejects_invalid_key);
    return UNITY_END();
}
