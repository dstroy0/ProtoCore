// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the dashboard widget-table JSON serializers (services/web/dashboard
// core). Pure logic - no server - so it runs on the host.

#include "services/web/dashboard/dashboard.h"
#include <stdio.h>
#include <string.h>
#include <unity.h>

static const pc_widget W[] = {
    {PC_WIDGET_GAUGE, "Temp", "temp", 0, 100, "C"},
    {PC_WIDGET_VALUE, "Count", "count", 0, 0, ""},
};

void setUp()
{
    pc_dashboard_configure(W, 2);
}
void tearDown()
{
}

// The widget table serializes to the layout JSON the page renders from.
void test_layout_json()
{
    char buf[512];
    int n = pc_dashboard_layout_json(buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING(
        "[{\"type\":\"gauge\",\"label\":\"Temp\",\"key\":\"temp\",\"min\":0,\"max\":100,\"unit\":\"C\"},"
        "{\"type\":\"value\",\"label\":\"Count\",\"key\":\"count\",\"min\":0,\"max\":0,\"unit\":\"\"}]",
        buf);
}

// Fresh configure() zeroes every value.
void test_values_json_initial_zero()
{
    char buf[256];
    pc_dashboard_values_json(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("{\"temp\":0,\"count\":0}", buf);
}

// set() updates by key and the values JSON reflects it.
void test_set_and_values()
{
    TEST_ASSERT_TRUE(pc_dashboard_set("temp", 23.5f));
    TEST_ASSERT_TRUE(pc_dashboard_set("count", 7.0f));
    char buf[256];
    pc_dashboard_values_json(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("{\"temp\":23.5,\"count\":7}", buf);
}

// An unknown key is rejected.
void test_set_unknown_key()
{
    TEST_ASSERT_FALSE(pc_dashboard_set("nope", 1.0f));
}

// Reconfiguring resets the values.
void test_configure_resets_values()
{
    pc_dashboard_set("temp", 99.0f);
    pc_dashboard_configure(W, 2);
    char buf[256];
    pc_dashboard_values_json(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("{\"temp\":0,\"count\":0}", buf);
}

// A buffer too small to hold the JSON fails closed (returns 0, no overflow).
void test_small_buffer_fails_closed()
{
    char buf[8];
    TEST_ASSERT_EQUAL_INT(0, pc_dashboard_layout_json(buf, sizeof(buf)));
}

// A well-formed control message parses into a key + value.
void test_parse_control_ok()
{
    char key[32];
    float v = -1;
    TEST_ASSERT_TRUE(pc_dashboard_parse_control("{\"k\":\"toggle1\",\"v\":1}", key, sizeof(key), &v));
    TEST_ASSERT_EQUAL_STRING("toggle1", key);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, v);
}

// Fractional control values parse too.
void test_parse_control_float()
{
    char key[32];
    float v = 0;
    TEST_ASSERT_TRUE(pc_dashboard_parse_control("{\"k\":\"speed\",\"v\":3.5}", key, sizeof(key), &v));
    TEST_ASSERT_EQUAL_STRING("speed", key);
    TEST_ASSERT_EQUAL_FLOAT(3.5f, v);
}

// Malformed messages are rejected.
void test_parse_control_rejects_malformed()
{
    char key[32];
    float v;
    TEST_ASSERT_FALSE(pc_dashboard_parse_control("{\"k\":\"x\"}", key, sizeof(key), &v));
    TEST_ASSERT_FALSE(pc_dashboard_parse_control("{\"v\":1}", key, sizeof(key), &v));
    TEST_ASSERT_FALSE(pc_dashboard_parse_control("not json", key, sizeof(key), &v));
    TEST_ASSERT_FALSE(pc_dashboard_parse_control("{\"k\":\"x\",\"v\":}", key, sizeof(key), &v));
}

static char g_ctrl_key[32];
static float g_ctrl_val;
static int g_ctrl_calls;
static void ctrl_cb(const char *key, float value)
{
    snprintf(g_ctrl_key, sizeof(g_ctrl_key), "%s", key);
    g_ctrl_val = value;
    g_ctrl_calls++;
}

// dispatch parses and invokes the registered callback; malformed -> no call.
void test_dispatch_control_invokes_cb()
{
    g_ctrl_calls = 0;
    g_ctrl_val = 0;
    g_ctrl_key[0] = '\0';
    pc_dashboard_on_control(ctrl_cb);
    TEST_ASSERT_TRUE(pc_dashboard_dispatch_control("{\"k\":\"led\",\"v\":1}"));
    TEST_ASSERT_EQUAL_INT(1, g_ctrl_calls);
    TEST_ASSERT_EQUAL_STRING("led", g_ctrl_key);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, g_ctrl_val);
    TEST_ASSERT_FALSE(pc_dashboard_dispatch_control("garbage"));
    TEST_ASSERT_EQUAL_INT(1, g_ctrl_calls);
}

// The new control / chart widget types serialize with their type names.
void test_layout_control_types()
{
    static const pc_widget CW[] = {
        {PC_WIDGET_CHART, "C", "c", 0, 1, ""},
        {PC_WIDGET_BUTTON, "B", "b", 0, 0, ""},
        {PC_WIDGET_TOGGLE, "T", "t", 0, 1, ""},
        {PC_WIDGET_SLIDER, "S", "s", 0, 100, "%"},
    };
    pc_dashboard_configure(CW, 4);
    char buf[512];
    pc_dashboard_layout_json(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"type\":\"chart\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"type\":\"button\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"type\":\"toggle\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"type\":\"slider\""));
}

// The bar and sparkline widget types serialize with their type names.
void test_layout_bar_sparkline_types()
{
    static const pc_widget AW[] = {
        {PC_WIDGET_BAR, "B", "b", 0, 1, ""},
        {PC_WIDGET_SPARKLINE, "S", "s", 0, 1, ""},
    };
    pc_dashboard_configure(AW, 2);
    char buf[256];
    TEST_ASSERT_TRUE(pc_dashboard_layout_json(buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"type\":\"bar\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"type\":\"sparkline\""));
}

// With no widget table configured, set() and the serializers fail closed.
void test_null_widget_table_guards()
{
    pc_dashboard_configure(NULL, 0); // clear the table
    char buf[64];
    TEST_ASSERT_FALSE(pc_dashboard_set("temp", 1.0f));                    // !s_widgets
    TEST_ASSERT_FALSE(pc_dashboard_set(NULL, 1.0f));                   // !key
    TEST_ASSERT_EQUAL_INT(0, pc_dashboard_layout_json(buf, sizeof(buf))); // !s_widgets
    TEST_ASSERT_EQUAL_INT(0, pc_dashboard_values_json(buf, sizeof(buf))); // !s_widgets
    TEST_ASSERT_EQUAL_INT(0, pc_dashboard_layout_json(buf, 0));           // cap == 0
    TEST_ASSERT_EQUAL_INT(0, pc_dashboard_values_json(buf, 0));           // cap == 0
}

// Each JSON serializer fails closed when the opening bracket, a widget fragment, or
// the closing bracket does not fit the output buffer.
void test_json_overflow_paths()
{
    char buf[8];
    TEST_ASSERT_EQUAL_INT(0, pc_dashboard_layout_json(buf, 1)); // '[' does not fit
    TEST_ASSERT_EQUAL_INT(0, pc_dashboard_values_json(buf, 1)); // '{' does not fit
    TEST_ASSERT_EQUAL_INT(0, pc_dashboard_layout_json(buf, 2)); // widget fragment spills
    TEST_ASSERT_EQUAL_INT(0, pc_dashboard_values_json(buf, 2)); // widget fragment spills

    pc_dashboard_configure(W, 0);                               // zero widgets: only the brackets are written
    TEST_ASSERT_EQUAL_INT(0, pc_dashboard_layout_json(buf, 2)); // ']' does not fit
    TEST_ASSERT_EQUAL_INT(0, pc_dashboard_values_json(buf, 2)); // '}' does not fit
}

// Control-message parsing: null args, a missing colon, whitespace tolerance, an
// unterminated key and a key too long for the buffer.
void test_parse_control_edges()
{
    char key[32];
    float v = 0.0f;
    TEST_ASSERT_FALSE(pc_dashboard_parse_control(NULL, key, sizeof(key), &v));
    TEST_ASSERT_FALSE(pc_dashboard_parse_control("{}", NULL, sizeof(key), &v));
    TEST_ASSERT_FALSE(pc_dashboard_parse_control("{}", key, 0, &v));
    TEST_ASSERT_FALSE(pc_dashboard_parse_control("{}", key, sizeof(key), NULL));

    TEST_ASSERT_FALSE(pc_dashboard_parse_control("{\"k\" 5}", key, sizeof(key), &v)); // no ':' after key

    TEST_ASSERT_TRUE(pc_dashboard_parse_control("{\"k\" : \"temp\" , \"v\" : 3}", key, sizeof(key), &v));
    TEST_ASSERT_EQUAL_STRING("temp", key); // whitespace around ':' tolerated
    TEST_ASSERT_EQUAL_FLOAT(3.0f, v);

    TEST_ASSERT_FALSE(pc_dashboard_parse_control("{\"k\":\"unterminated", key, sizeof(key), &v));

    char small[4];
    TEST_ASSERT_FALSE(pc_dashboard_parse_control("{\"k\":\"toolong\",\"v\":1}", small, sizeof(small), &v));
}

// A widget with null label/key/unit must not crash the serializers, and set()'s key
// scan must short-circuit past it (not dereference the null key) on the way to a
// widget that does match.
void test_null_widget_fields_are_skipped_and_serialize_empty()
{
    static const pc_widget NULL_FIELDS_W[] = {
        {PC_WIDGET_VALUE, NULL, NULL, 0, 0, NULL},
        {PC_WIDGET_VALUE, "Real", "real", 0, 0, ""},
    };
    pc_dashboard_configure(NULL_FIELDS_W, 2);

    TEST_ASSERT_TRUE(pc_dashboard_set("real", 42.0f));

    char buf[256];
    TEST_ASSERT_TRUE(pc_dashboard_layout_json(buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"label\":\"\",\"key\":\"\",\"min\":0,\"max\":0,\"unit\":\"\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"key\":\"real\""));

    TEST_ASSERT_TRUE(pc_dashboard_values_json(buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"\":0"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"real\":42"));
}

// A null output pointer fails closed even when cap is nonzero (cap == 0 is covered by
// test_null_widget_table_guards already).
void test_serializers_null_out_pointer()
{
    TEST_ASSERT_EQUAL_INT(0, pc_dashboard_layout_json(NULL, 64));
    TEST_ASSERT_EQUAL_INT(0, pc_dashboard_values_json(NULL, 64));
}

// Tabs, not just spaces, are tolerated around the ':' in a control message.
void test_parse_control_tab_whitespace()
{
    char key[32];
    float v = 0.0f;
    TEST_ASSERT_TRUE(pc_dashboard_parse_control("{\"k\"\t:\t\"tabkey\",\"v\":1}", key, sizeof(key), &v));
    TEST_ASSERT_EQUAL_STRING("tabkey", key);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, v);
}

// A "k" value that is not a quoted string (e.g. a bare number) is rejected.
void test_parse_control_non_string_key_value()
{
    char key[32];
    float v = 0.0f;
    TEST_ASSERT_FALSE(pc_dashboard_parse_control("{\"k\":5,\"v\":1}", key, sizeof(key), &v));
}

// An unterminated key that runs off the end of the message (no closing quote anywhere,
// even with a "v" field present so parsing actually reaches the key scan) is rejected.
void test_parse_control_unterminated_key_runs_to_eof()
{
    char key[32];
    float v = 0.0f;
    TEST_ASSERT_FALSE(pc_dashboard_parse_control("{\"v\":1,\"k\":\"unterminated", key, sizeof(key), &v));
}

// dispatch_control() with a well-formed message but no registered callback parses fine
// but reports no callback was invoked.
void test_dispatch_control_no_callback_registered()
{
    pc_dashboard_on_control(NULL);
    TEST_ASSERT_FALSE(pc_dashboard_dispatch_control("{\"k\":\"x\",\"v\":1}"));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_layout_bar_sparkline_types);
    RUN_TEST(test_null_widget_table_guards);
    RUN_TEST(test_json_overflow_paths);
    RUN_TEST(test_parse_control_edges);
    RUN_TEST(test_layout_json);
    RUN_TEST(test_values_json_initial_zero);
    RUN_TEST(test_set_and_values);
    RUN_TEST(test_set_unknown_key);
    RUN_TEST(test_configure_resets_values);
    RUN_TEST(test_small_buffer_fails_closed);
    RUN_TEST(test_parse_control_ok);
    RUN_TEST(test_parse_control_float);
    RUN_TEST(test_parse_control_rejects_malformed);
    RUN_TEST(test_dispatch_control_invokes_cb);
    RUN_TEST(test_layout_control_types);
    RUN_TEST(test_null_widget_fields_are_skipped_and_serialize_empty);
    RUN_TEST(test_serializers_null_out_pointer);
    RUN_TEST(test_parse_control_tab_whitespace);
    RUN_TEST(test_parse_control_non_string_key_value);
    RUN_TEST(test_parse_control_unterminated_key_runs_to_eof);
    RUN_TEST(test_dispatch_control_no_callback_registered);
    return UNITY_END();
}
