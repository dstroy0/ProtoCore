// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for schema-driven config export/restore (services/storage/config_io) over
// the host in-memory config store: export formatting, round-trip, unknown-key
// skipping, and fail-closed overflow.

#include "services/storage/config_io/config_io.h"
#include "services/storage/config_store/config_store.h"
#include <string.h>

#include <unity.h>

static const pc_cfg_field SCHEMA[] = {
    {"ssid", PC_CFG_STR},
    {"port", PC_CFG_U32},
    {"name", PC_CFG_STR},
};
static const size_t N = sizeof(SCHEMA) / sizeof(SCHEMA[0]);

void setUp()
{
    pc_config_begin("t");
    pc_config_clear();
}
void tearDown()
{
}

void test_export_format()
{
    pc_config_begin("t");
    pc_config_set_str("ssid", "myssid");
    pc_config_set_u32("port", 8080);
    pc_config_set_str("name", "node1");
    char buf[256];
    int n = pc_config_export("t", SCHEMA, N, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("ssid=myssid\nport=8080\nname=node1\n", buf);
}

void test_round_trip()
{
    pc_config_begin("t");
    pc_config_set_str("ssid", "abc");
    pc_config_set_u32("port", 1234);
    pc_config_set_str("name", "x");
    char buf[256];
    pc_config_export("t", SCHEMA, N, buf, sizeof(buf));

    pc_config_clear(); // wipe
    TEST_ASSERT_EQUAL_UINT32(0, pc_config_get_u32("port", 0));

    int c = pc_config_import("t", SCHEMA, N, buf, strlen(buf));
    TEST_ASSERT_EQUAL_INT(3, c);
    char s[32];
    pc_config_get_str("ssid", s, sizeof(s), "");
    TEST_ASSERT_EQUAL_STRING("abc", s);
    TEST_ASSERT_EQUAL_UINT32(1234, pc_config_get_u32("port", 0));
}

void test_import_skips_unknown_keys()
{
    pc_config_begin("t");
    const char *txt = "port=42\nbogus=99\n";
    int c = pc_config_import("t", SCHEMA, N, txt, strlen(txt));
    TEST_ASSERT_EQUAL_INT(1, c); // only "port" is in the schema
    TEST_ASSERT_EQUAL_UINT32(42, pc_config_get_u32("port", 0));
}

void test_export_overflow_fails_closed()
{
    pc_config_begin("t");
    pc_config_set_str("ssid", "value");
    char buf[4];
    TEST_ASSERT_EQUAL_INT(0, pc_config_export("t", SCHEMA, N, buf, sizeof(buf)));
}

void test_export_import_null_guards()
{
    char out[128];
    pc_cfg_field fields[1] = {0};
    TEST_ASSERT_EQUAL_INT(0, pc_config_export("ns", fields, 1, NULL, sizeof(out))); // null out
    TEST_ASSERT_EQUAL_INT(0, pc_config_export("ns", NULL, 1, out, sizeof(out)));    // null fields
    TEST_ASSERT_EQUAL_INT(0, pc_config_import("ns", NULL, 1, "text", 4));           // null fields
    TEST_ASSERT_EQUAL_INT(0, pc_config_import("ns", fields, 1, NULL, 0));           // null text
}

void test_export_zero_cap_fails_closed()
{
    pc_config_begin("t");
    pc_config_set_str("ssid", "value");
    char buf[8] = {'z', '\0'}; // sentinel: a zero-cap call must return before touching it
    TEST_ASSERT_EQUAL_INT(0, pc_config_export("t", SCHEMA, N, buf, 0));
    TEST_ASSERT_EQUAL_CHAR('z', buf[0]); // untouched, confirming the cap==0 early return
}

void test_field_type_skips_null_key_entries()
{
    // A malformed schema entry (null key) must be skipped by field_type's lookup, not
    // dereferenced, so a valid entry later in the table is still found.
    static const pc_cfg_field schema_with_gap[] = {
        {NULL, PC_CFG_U32},
        {"zz", PC_CFG_U32},
    };
    pc_config_begin("t");
    const char *txt = "zz=7\n";
    int c = pc_config_import("t", schema_with_gap, 2, txt, strlen(txt));
    TEST_ASSERT_EQUAL_INT(1, c);
    TEST_ASSERT_EQUAL_UINT32(7, pc_config_get_u32("zz", 0));
}

void test_config_apply_field_rejects_unknown_type()
{
    // A field whose type is neither PC_CFG_STR nor PC_CFG_U32 (a malformed schema
    // entry) must be rejected rather than applied by either setter.
    static const pc_cfg_field bad_schema[] = {
        {"weird", (pc_cfg_type)(9)},
    };
    pc_config_begin("t");
    const char *txt = "weird=5\n";
    int c = pc_config_import("t", bad_schema, 1, txt, strlen(txt));
    TEST_ASSERT_EQUAL_INT(0, c);
}

void test_import_line_without_equals_and_no_trailing_newline()
{
    // "bogus" has no '=' (exercises the "no '=' on this line" skip path), and the
    // text ends without a trailing '\n' (exercises the end-of-line scan running off
    // the end of the buffer instead of finding '\n').
    pc_config_begin("t");
    const char *txt = "bogus\nport=42";
    int c = pc_config_import("t", SCHEMA, N, txt, strlen(txt));
    TEST_ASSERT_EQUAL_INT(1, c);
    TEST_ASSERT_EQUAL_UINT32(42, pc_config_get_u32("port", 0));
}

void test_import_key_and_value_length_boundaries()
{
    // Three malformed lines, one per length guard on the key=val split:
    //   - "=novaluekey"          : klen == 0 (nothing before '=')
    //   - "0123456789abcdef=5"   : klen == PC_KEY_MAX (16), one past the 15-char cap
    //   - "aa=<128 x's>"         : vlen == PC_VAL_MAX (128), one past the 127-char cap
    // None should be applied, and none should touch the fixed key[]/val[] buffers.
    pc_config_begin("t");
    char txt[300];
    txt[0] = '\0';
    strcat(txt, "=novaluekey\n");
    strcat(txt, "0123456789abcdef=5\n");
    strcat(txt, "aa=");
    size_t pos = strlen(txt);
    for (size_t i = 0; i < 128; i++)
    {
        txt[pos++] = 'x';
    }
    txt[pos++] = '\n';
    txt[pos] = '\0';

    int c = pc_config_import("t", SCHEMA, N, txt, strlen(txt));
    TEST_ASSERT_EQUAL_INT(0, c);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_export_format);
    RUN_TEST(test_round_trip);
    RUN_TEST(test_import_skips_unknown_keys);
    RUN_TEST(test_export_overflow_fails_closed);
    RUN_TEST(test_export_import_null_guards);
    RUN_TEST(test_export_zero_cap_fails_closed);
    RUN_TEST(test_field_type_skips_null_key_entries);
    RUN_TEST(test_config_apply_field_rejects_unknown_type);
    RUN_TEST(test_import_line_without_equals_and_no_trailing_newline);
    RUN_TEST(test_import_key_and_value_length_boundaries);
    return UNITY_END();
}
