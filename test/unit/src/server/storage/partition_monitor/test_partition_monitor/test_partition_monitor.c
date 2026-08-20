// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the flash partition-map monitor (server/storage/partition_monitor/partition_monitor.h).
//
// The load-bearing case is test_kind_matches_the_esp_idf_subtype_registry. The type/subtype numbers
// are not this module's to choose: they are the values Espressif publishes in esp_partition.h's
// esp_partition_type_t / esp_partition_subtype_t (APP = 0x00 with FACTORY 0x00, OTA_0..OTA_15
// 0x10..0x1F, TEST 0x20; DATA = 0x01 with OTA 0x00, PHY 0x01, NVS 0x02, COREDUMP 0x03, NVS_KEYS
// 0x04, EFUSE_EM 0x05, UNDEFINED 0x06, ESPHTTPD 0x80, FAT 0x81, SPIFFS 0x82, LITTLEFS 0x83). A
// classifier that shifted a boundary would label the running app slot as data on a real device.
//
// The report is held to RFC 8259: sec 4's object grammar, sec 5's array grammar, and sec 3's
// literal-name production for the boolean, which is the lowercase word and never a quoted string.

#include "server/storage/partition_monitor/partition_monitor.h"
#include <string.h>

#include <unity.h>

static uint8_t partition_monitor_work[16]; // the borrow an entry takes; PartitionMonitor never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static protocore_partition_info part(const char *label, uint8_t type, uint8_t subtype, uint32_t addr, uint32_t size,
                                     proto_bool running)
{
    protocore_partition_info p;
    memset(&p, 0, sizeof(p));
    strncpy(p.label, label, sizeof(p.label) - 1);
    p.type = type;
    p.subtype = subtype;
    p.address = addr;
    p.size = size;
    p.running = running;
    return p;
}

// Every name comes from the esp_partition subtype the entry carries, at the boundaries of each run
// the registry defines.
void test_kind_matches_the_esp_idf_subtype_registry(void)
{
    // Type 0x00 (APP).
    PartitionMonitorV.kind_args.type = 0x00;
    PartitionMonitorV.kind_args.subtype = 0x00;
    PartitionMonitor.kind(partition_monitor_work);
    TEST_ASSERT_EQUAL_STRING("factory", PartitionMonitorV.text);
    PartitionMonitorV.kind_args.type = 0x00;
    PartitionMonitorV.kind_args.subtype = 0x10;
    PartitionMonitor.kind(partition_monitor_work);
    TEST_ASSERT_EQUAL_STRING("ota", PartitionMonitorV.text); // OTA_0
    PartitionMonitorV.kind_args.type = 0x00;
    PartitionMonitorV.kind_args.subtype = 0x1F;
    PartitionMonitor.kind(partition_monitor_work);
    TEST_ASSERT_EQUAL_STRING("ota", PartitionMonitorV.text); // OTA_15
    PartitionMonitorV.kind_args.type = 0x00;
    PartitionMonitorV.kind_args.subtype = 0x20;
    PartitionMonitor.kind(partition_monitor_work);
    TEST_ASSERT_EQUAL_STRING("test", PartitionMonitorV.text); // APP_TEST

    // Either side of the OTA run: 0x0F is below OTA_0 and 0x21 is above APP_TEST, so neither is an
    // OTA slot and both fall through to the generic app name.
    PartitionMonitorV.kind_args.type = 0x00;
    PartitionMonitorV.kind_args.subtype = 0x0F;
    PartitionMonitor.kind(partition_monitor_work);
    TEST_ASSERT_EQUAL_STRING("app", PartitionMonitorV.text);
    PartitionMonitorV.kind_args.type = 0x00;
    PartitionMonitorV.kind_args.subtype = 0x21;
    PartitionMonitor.kind(partition_monitor_work);
    TEST_ASSERT_EQUAL_STRING("app", PartitionMonitorV.text);

    // Type 0x01 (DATA).
    PartitionMonitorV.kind_args.type = 0x01;
    PartitionMonitorV.kind_args.subtype = 0x00;
    PartitionMonitor.kind(partition_monitor_work);
    TEST_ASSERT_EQUAL_STRING("otadata", PartitionMonitorV.text);
    PartitionMonitorV.kind_args.type = 0x01;
    PartitionMonitorV.kind_args.subtype = 0x01;
    PartitionMonitor.kind(partition_monitor_work);
    TEST_ASSERT_EQUAL_STRING("phy", PartitionMonitorV.text);
    PartitionMonitorV.kind_args.type = 0x01;
    PartitionMonitorV.kind_args.subtype = 0x02;
    PartitionMonitor.kind(partition_monitor_work);
    TEST_ASSERT_EQUAL_STRING("nvs", PartitionMonitorV.text);
    PartitionMonitorV.kind_args.type = 0x01;
    PartitionMonitorV.kind_args.subtype = 0x03;
    PartitionMonitor.kind(partition_monitor_work);
    TEST_ASSERT_EQUAL_STRING("coredump", PartitionMonitorV.text);
    PartitionMonitorV.kind_args.type = 0x01;
    PartitionMonitorV.kind_args.subtype = 0x04;
    PartitionMonitor.kind(partition_monitor_work);
    TEST_ASSERT_EQUAL_STRING("nvs_keys", PartitionMonitorV.text);
    PartitionMonitorV.kind_args.type = 0x01;
    PartitionMonitorV.kind_args.subtype = 0x81;
    PartitionMonitor.kind(partition_monitor_work);
    TEST_ASSERT_EQUAL_STRING("fat", PartitionMonitorV.text);
    PartitionMonitorV.kind_args.type = 0x01;
    PartitionMonitorV.kind_args.subtype = 0x82;
    PartitionMonitor.kind(partition_monitor_work);
    TEST_ASSERT_EQUAL_STRING("spiffs", PartitionMonitorV.text);
    PartitionMonitorV.kind_args.type = 0x01;
    PartitionMonitorV.kind_args.subtype = 0x83;
    PartitionMonitor.kind(partition_monitor_work);
    TEST_ASSERT_EQUAL_STRING("littlefs", PartitionMonitorV.text);

    // Registry entries this build names no kind for still report a kind rather than nothing.
    PartitionMonitorV.kind_args.type = 0x01;
    PartitionMonitorV.kind_args.subtype = 0x05;
    PartitionMonitor.kind(partition_monitor_work);
    TEST_ASSERT_EQUAL_STRING("data", PartitionMonitorV.text); // EFUSE_EM
    PartitionMonitorV.kind_args.type = 0x01;
    PartitionMonitorV.kind_args.subtype = 0x06;
    PartitionMonitor.kind(partition_monitor_work);
    TEST_ASSERT_EQUAL_STRING("data", PartitionMonitorV.text); // DATA_UNDEFINED
    PartitionMonitorV.kind_args.type = 0x01;
    PartitionMonitorV.kind_args.subtype = 0x80;
    PartitionMonitor.kind(partition_monitor_work);
    TEST_ASSERT_EQUAL_STRING("data", PartitionMonitorV.text); // ESPHTTPD
}

// A type that is neither APP nor DATA is classified by its subtype against the data table, since
// only type 0x00 is treated as an application partition.
void test_a_non_app_type_is_classified_as_data(void)
{
    PartitionMonitorV.kind_args.type = 0x02;
    PartitionMonitorV.kind_args.subtype = 0x02;
    PartitionMonitor.kind(partition_monitor_work);
    TEST_ASSERT_EQUAL_STRING("nvs", PartitionMonitorV.text);
    PartitionMonitorV.kind_args.type = 0xFF;
    PartitionMonitorV.kind_args.subtype = 0xFF;
    PartitionMonitor.kind(partition_monitor_work);
    TEST_ASSERT_EQUAL_STRING("data", PartitionMonitorV.text);
}

// RFC 8259 sec 4 object and sec 5 array: one object per partition inside a single "partitions"
// array, commas between elements and none after the last. sec 3's literal names are lowercase and
// unquoted, so running is true / false rather than "true" / "false".
void test_report_is_an_rfc8259_document(void)
{
    protocore_partition_info parts[2];
    parts[0] = part("app0", 0x00, 0x10, 0x10000u, 0x140000u, PROTO_TRUE);
    parts[1] = part("storage", 0x01, 0x83, 0x290000u, 0x170000u, PROTO_FALSE);

    char out[512];
    PartitionMonitorV.json_args.parts = parts;
    PartitionMonitorV.json_args.count = 2;
    PartitionMonitorV.json_args.out = out;
    PartitionMonitorV.json_args.cap = sizeof(out);
    PartitionMonitor.json(partition_monitor_work);
    int32_t n = PartitionMonitorV.n;
    TEST_ASSERT_EQUAL_STRING("{\"partitions\":["
                             "{\"label\":\"app0\",\"kind\":\"ota\",\"type\":0,\"subtype\":16,"
                             "\"addr\":65536,\"size\":1310720,\"running\":true},"
                             "{\"label\":\"storage\",\"kind\":\"littlefs\",\"type\":1,\"subtype\":131,"
                             "\"addr\":2686976,\"size\":1507328,\"running\":false}"
                             "]}",
                             out);
    TEST_ASSERT_EQUAL_INT32((int32_t)strlen(out), n);
}

// sec 5: an array with no elements is still an array, so a device whose table was not walked emits
// an empty list rather than nothing at all.
void test_an_empty_table_is_still_an_array(void)
{
    protocore_partition_info parts[1];
    parts[0] = part("x", 0x00, 0x00, 0u, 0u, PROTO_FALSE);

    char out[64];
    PartitionMonitorV.json_args.parts = parts;
    PartitionMonitorV.json_args.count = 0;
    PartitionMonitorV.json_args.out = out;
    PartitionMonitorV.json_args.cap = sizeof(out);
    PartitionMonitor.json(partition_monitor_work);
    int32_t n = PartitionMonitorV.n;
    TEST_ASSERT_EQUAL_STRING("{\"partitions\":[]}", out);
    TEST_ASSERT_EQUAL_INT32((int32_t)strlen(out), n);
}

// sec 6: numbers are decimal with no leading zeros, so a zero address is "0" and the 32-bit maximum
// is written out in full rather than clipped.
void test_numbers_span_the_whole_32_bit_range(void)
{
    protocore_partition_info parts[1];
    parts[0] = part("nvs", 0x01, 0x02, 0u, 4294967295u, PROTO_FALSE);

    char out[256];
    PartitionMonitorV.json_args.parts = parts;
    PartitionMonitorV.json_args.count = 1;
    PartitionMonitorV.json_args.out = out;
    PartitionMonitorV.json_args.cap = sizeof(out);
    PartitionMonitor.json(partition_monitor_work);
    (void)PartitionMonitorV.n;
    TEST_ASSERT_EQUAL_STRING("{\"partitions\":["
                             "{\"label\":\"nvs\",\"kind\":\"nvs\",\"type\":1,\"subtype\":2,"
                             "\"addr\":0,\"size\":4294967295,\"running\":false}"
                             "]}",
                             out);
}

// RFC 8259 sec 7: a quotation mark and a reverse solidus inside a string MUST be escaped. A label
// comes off a device's partition table, so it is not this module's to trust.
void test_a_label_is_escaped_per_rfc8259_section_7(void)
{
    protocore_partition_info parts[1];
    parts[0] = part("a\"b\\c", 0x01, 0x02, 0u, 0u, PROTO_FALSE);

    char out[256];
    PartitionMonitorV.json_args.parts = parts;
    PartitionMonitorV.json_args.count = 1;
    PartitionMonitorV.json_args.out = out;
    PartitionMonitorV.json_args.cap = sizeof(out);
    PartitionMonitor.json(partition_monitor_work);
    (void)PartitionMonitorV.n;
    TEST_ASSERT_NOT_NULL(strstr(out, "\"label\":\"a\\\"b\\\\c\""));
}

// A buffer too small for the whole document reports 0 and leaves an empty string: a truncated
// object is not JSON, and a dashboard would fail to parse it rather than show a short list.
void test_a_short_buffer_fails_closed(void)
{
    protocore_partition_info parts[2];
    parts[0] = part("app0", 0x00, 0x10, 0x10000u, 0x140000u, PROTO_TRUE);
    parts[1] = part("storage", 0x01, 0x83, 0x290000u, 0x170000u, PROTO_FALSE);

    char full[512];
    PartitionMonitorV.json_args.parts = parts;
    PartitionMonitorV.json_args.count = 2;
    PartitionMonitorV.json_args.out = full;
    PartitionMonitorV.json_args.cap = sizeof(full);
    PartitionMonitor.json(partition_monitor_work);
    int32_t n = PartitionMonitorV.n;
    TEST_ASSERT_TRUE(n > 0);

    // One byte short of the whole document, and short enough to fail on the opening frame.
    char tight[512];
    PartitionMonitorV.json_args.parts = parts;
    PartitionMonitorV.json_args.count = 2;
    PartitionMonitorV.json_args.out = tight;
    PartitionMonitorV.json_args.cap = (uint32_t)n;
    PartitionMonitor.json(partition_monitor_work);
    TEST_ASSERT_EQUAL_INT32(0, PartitionMonitorV.n);
    TEST_ASSERT_EQUAL_STRING("", tight);

    char tiny[8];
    PartitionMonitorV.json_args.parts = parts;
    PartitionMonitorV.json_args.count = 2;
    PartitionMonitorV.json_args.out = tiny;
    PartitionMonitorV.json_args.cap = sizeof(tiny);
    PartitionMonitor.json(partition_monitor_work);
    TEST_ASSERT_EQUAL_INT32(0, PartitionMonitorV.n);
    TEST_ASSERT_EQUAL_STRING("", tiny);
}

// A call with no destination, no room, or no table writes nothing and reports nothing written.
void test_missing_arguments_are_refused(void)
{
    protocore_partition_info parts[1];
    parts[0] = part("nvs", 0x01, 0x02, 0u, 0u, PROTO_FALSE);

    char out[128];
    PartitionMonitorV.json_args.parts = parts;
    PartitionMonitorV.json_args.count = 1;
    PartitionMonitorV.json_args.out = NULL;
    PartitionMonitorV.json_args.cap = sizeof(out);
    PartitionMonitor.json(partition_monitor_work);
    TEST_ASSERT_EQUAL_INT32(0, PartitionMonitorV.n);
    PartitionMonitorV.json_args.parts = NULL;
    PartitionMonitorV.json_args.count = 1;
    PartitionMonitorV.json_args.out = out;
    PartitionMonitorV.json_args.cap = sizeof(out);
    PartitionMonitor.json(partition_monitor_work);
    TEST_ASSERT_EQUAL_INT32(0, PartitionMonitorV.n);
    TEST_ASSERT_EQUAL_STRING("", out);

    char sentinel[8] = {'z', '\0'};
    PartitionMonitorV.json_args.parts = parts;
    PartitionMonitorV.json_args.count = 1;
    PartitionMonitorV.json_args.out = sentinel;
    PartitionMonitorV.json_args.cap = 0;
    PartitionMonitor.json(partition_monitor_work);
    TEST_ASSERT_EQUAL_INT32(0, PartitionMonitorV.n);
    TEST_ASSERT_EQUAL_CHAR('z', sentinel[0]);
}

// The flash walk needs esp_partition, so on a host it reports no partitions rather than inventing
// any: a report built from it is empty, never fabricated.
void test_the_flash_walk_reports_nothing_off_target(void)
{
    protocore_partition_info out[4];
    PartitionMonitorV.collect_args.out = out;
    PartitionMonitorV.collect_args.max = 4;
    PartitionMonitor.collect(partition_monitor_work);
    TEST_ASSERT_EQUAL_UINT8(0, PartitionMonitorV.u8);
    PartitionMonitorV.collect_args.out = NULL;
    PartitionMonitorV.collect_args.max = 4;
    PartitionMonitor.collect(partition_monitor_work);
    TEST_ASSERT_EQUAL_UINT8(0, PartitionMonitorV.u8);
    PartitionMonitorV.collect_args.out = out;
    PartitionMonitorV.collect_args.max = 0;
    PartitionMonitor.collect(partition_monitor_work);
    TEST_ASSERT_EQUAL_UINT8(0, PartitionMonitorV.u8);
}
