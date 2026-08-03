// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the partition-map core (services/storage/partition_monitor): the
// type/subtype -> kind classifier and the JSON serializer. The flash walk is
// ESP32-only and a no-op on the host.

#include "services/storage/partition_monitor/partition_monitor.h"
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_kind_app()
{
    TEST_ASSERT_EQUAL_STRING("factory", pc_partition_kind(0, 0x00));
    TEST_ASSERT_EQUAL_STRING("ota", pc_partition_kind(0, 0x10));
    TEST_ASSERT_EQUAL_STRING("ota", pc_partition_kind(0, 0x11));
    TEST_ASSERT_EQUAL_STRING("test", pc_partition_kind(0, 0x20));
    TEST_ASSERT_EQUAL_STRING("app", pc_partition_kind(0, 0x05)); // app, not a known subtype
}

void test_kind_data()
{
    TEST_ASSERT_EQUAL_STRING("otadata", pc_partition_kind(1, 0x00));
    TEST_ASSERT_EQUAL_STRING("phy", pc_partition_kind(1, 0x01));
    TEST_ASSERT_EQUAL_STRING("nvs", pc_partition_kind(1, 0x02));
    TEST_ASSERT_EQUAL_STRING("coredump", pc_partition_kind(1, 0x03));
    TEST_ASSERT_EQUAL_STRING("littlefs", pc_partition_kind(1, 0x83));
    TEST_ASSERT_EQUAL_STRING("spiffs", pc_partition_kind(1, 0x82));
    TEST_ASSERT_EQUAL_STRING("data", pc_partition_kind(1, 0x77)); // unknown data subtype
}

void test_json()
{
    pc_partition_info p[2] = {
        {"nvs", 1, 0x02, 0x9000, 0x6000, PROTO_FALSE},
        {"app0", 0, 0x10, 0x10000, 0x140000, PROTO_TRUE},
    };
    char buf[512];
    int n = pc_partition_json(p, 2, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"partitions\":[{\"label\":\"nvs\",\"kind\":\"nvs\",\"type\":1,\"subtype\":2,\"addr\":36864,\"size\":24576,"
        "\"running\":false},"
        "{\"label\":\"app0\",\"kind\":\"ota\",\"type\":0,\"subtype\":16,\"addr\":65536,\"size\":1310720,"
        "\"running\":true}]}",
        buf);
}

void test_json_small_buffer_fails_closed()
{
    pc_partition_info p[1] = {{"nvs", 1, 0x02, 0x9000, 0x6000, PROTO_FALSE}};
    char buf[8];
    TEST_ASSERT_EQUAL_INT(0, pc_partition_json(p, 1, buf, sizeof(buf)));
}

void test_collect_host_stub()
{
    pc_partition_info p[4];
    TEST_ASSERT_EQUAL_UINT8(0, pc_partition_collect(p, 4));
}

void test_partition_kind_data_subtypes()
{
    TEST_ASSERT_EQUAL_STRING("nvs_keys", pc_partition_kind(0x01, 0x04));
    TEST_ASSERT_EQUAL_STRING("fat", pc_partition_kind(0x01, 0x81));
}

void test_json_null_out_and_zero_cap()
{
    pc_partition_info p[1] = {{"nvs", 1, 0x02, 0x9000, 0x6000, PROTO_FALSE}};
    // out == NULL fails closed before touching the buffer.
    TEST_ASSERT_EQUAL_INT(0, pc_partition_json(p, 1, NULL, 16));
    // cap == 0 also fails closed, independent of the out == NULL check.
    char buf[16];
    TEST_ASSERT_EQUAL_INT(0, pc_partition_json(p, 1, buf, 0));
}

void test_json_null_parts()
{
    char buf[16] = "stale";
    TEST_ASSERT_EQUAL_INT(0, pc_partition_json(NULL, 1, buf, sizeof(buf)));
    // out[0] is cleared before the parts == NULL check runs.
    TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_json_entry_overflow_fails_closed()
{
    pc_partition_info p[1] = {{"nvs", 1, 0x02, 0x9000, 0x6000, PROTO_FALSE}};
    // 20 bytes fits the opening `{"partitions":[` (15 chars) but not the first
    // entry, so the per-entry append fails and the call fails closed.
    char buf[20];
    TEST_ASSERT_EQUAL_INT(0, pc_partition_json(p, 1, buf, sizeof(buf)));
}

void test_json_closing_bracket_overflow_fails_closed()
{
    pc_partition_info p[1] = {{"nvs", 1, 0x02, 0x9000, 0x6000, PROTO_FALSE}};
    // 107 bytes fits the opening bracket + the one entry (106 bytes total) but
    // not the closing `]}`, so that final append fails and the call fails
    // closed.
    char buf[107];
    TEST_ASSERT_EQUAL_INT(0, pc_partition_json(p, 1, buf, sizeof(buf)));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_kind_app);
    RUN_TEST(test_kind_data);
    RUN_TEST(test_json);
    RUN_TEST(test_json_small_buffer_fails_closed);
    RUN_TEST(test_collect_host_stub);
    RUN_TEST(test_partition_kind_data_subtypes);
    RUN_TEST(test_json_null_out_and_zero_cap);
    RUN_TEST(test_json_null_parts);
    RUN_TEST(test_json_entry_overflow_fails_closed);
    RUN_TEST(test_json_closing_bracket_overflow_fails_closed);
    return UNITY_END();
}
