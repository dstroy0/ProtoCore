// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the dashboard widget table and its JSON serializers (server/web/dashboard/dashboard.h).
//
// Both documents are consumed by a browser, so RFC 8259 governs them and is what the expectations
// are taken from: sec 4's object grammar, sec 5's array grammar, sec 6's number production (base
// ten, no leading zeros), and sec 7's rule that a quotation mark, a reverse solidus and the control
// characters U+0000..U+001F MUST be escaped inside a string.
//
// test_a_label_is_escaped_per_rfc8259_section_7 is the load-bearing case. A label and a unit are
// the application's strings, not this module's, so a quote in one lands straight in the layout
// document; if it is not escaped the page's JSON.parse throws and the whole dashboard is blank
// rather than one widget being wrong.

#include "server/web/dashboard/dashboard.h"
#include <string.h>

#include <unity.h>

static const protocore_widget WIDGETS[] = {
    {PROTOCORE_WIDGET_GAUGE, "Temperature", "temp", -40.0f, 125.0f, "C"},
    {PROTOCORE_WIDGET_VALUE, "Uptime", "up", 0.0f, 0.0f, ""},
};
static const size_t NW = sizeof(WIDGETS) / sizeof(WIDGETS[0]);

static char g_key[32];
static float g_value;
static int g_calls;

static void on_control(const char *key, float value)
{
    strncpy(g_key, key, sizeof(g_key) - 1);
    g_key[sizeof(g_key) - 1] = '\0';
    g_value = value;
    g_calls++;
}

void setUp(void)
{
    g_key[0] = '\0';
    g_value = 0.0f;
    g_calls = 0;
    protocore_dashboard_on_control(NULL);
    protocore_dashboard_configure(WIDGETS, (uint8_t)NW);
}
void tearDown(void)
{
}

static const char *layout(void)
{
    static char buf[2048];
    protocore_dashboard_layout_json(buf, (uint32_t)sizeof(buf));
    return buf;
}

static const char *values(void)
{
    static char buf[2048];
    protocore_dashboard_values_json(buf, (uint32_t)sizeof(buf));
    return buf;
}

// RFC 8259 sec 5: the layout is one array of widget objects, commas between them and none after the
// last. sec 6: the scale bounds are decimal numbers, so -40 and 125 are written as they are.
void test_layout_is_an_rfc8259_array_of_objects(void)
{
    TEST_ASSERT_EQUAL_STRING("["
                             "{\"type\":\"gauge\",\"label\":\"Temperature\",\"key\":\"temp\","
                             "\"min\":-40,\"max\":125,\"unit\":\"C\"},"
                             "{\"type\":\"value\",\"label\":\"Uptime\",\"key\":\"up\","
                             "\"min\":0,\"max\":0,\"unit\":\"\"}"
                             "]",
                             layout());

    char buf[512];
    int32_t n = protocore_dashboard_layout_json(buf, (uint32_t)sizeof(buf));
    TEST_ASSERT_EQUAL_INT32((int32_t)strlen(buf), n);
}

// RFC 8259 sec 4: the values document is one flat object of key/number members. Every configured
// widget appears, whether or not the application has fed it, so the page never has to guess whether
// a missing key means zero or means no reading.
void test_values_is_an_rfc8259_object_of_every_key(void)
{
    TEST_ASSERT_EQUAL_STRING("{\"temp\":0,\"up\":0}", values());

    TEST_ASSERT_TRUE(protocore_dashboard_set("temp", 23.5f));
    TEST_ASSERT_EQUAL_STRING("{\"temp\":23.5,\"up\":0}", values());

    TEST_ASSERT_TRUE(protocore_dashboard_set("up", 0.25f));
    TEST_ASSERT_EQUAL_STRING("{\"temp\":23.5,\"up\":0.25}", values());

    // Negative readings keep their sign, which sec 6's number production carries as a leading minus.
    TEST_ASSERT_TRUE(protocore_dashboard_set("temp", -12.5f));
    TEST_ASSERT_EQUAL_STRING("{\"temp\":-12.5,\"up\":0.25}", values());
}

// A reading is addressed by key, so a key the table does not declare is refused rather than landing
// on a neighbouring widget.
void test_a_reading_for_an_undeclared_key_is_refused(void)
{
    TEST_ASSERT_FALSE(protocore_dashboard_set("nosuch", 1.0f));
    TEST_ASSERT_FALSE(protocore_dashboard_set(NULL, 1.0f));
    TEST_ASSERT_FALSE(protocore_dashboard_set("", 1.0f));
    TEST_ASSERT_EQUAL_STRING("{\"temp\":0,\"up\":0}", values());
}

// Rebinding the table resets every reading, so a value fed against the previous layout cannot show
// up under the new one.
void test_rebinding_the_table_clears_the_readings(void)
{
    TEST_ASSERT_TRUE(protocore_dashboard_set("temp", 99.0f));
    protocore_dashboard_configure(WIDGETS, (uint8_t)NW);
    TEST_ASSERT_EQUAL_STRING("{\"temp\":0,\"up\":0}", values());
}

// Every widget style has its own name in the layout, and a style outside the enum is reported as
// the plain readout rather than as nothing.
void test_every_widget_style_has_a_name(void)
{
    static const protocore_widget ALL[] = {
        {PROTOCORE_WIDGET_VALUE, "a", "a", 0.0f, 1.0f, ""},
        {PROTOCORE_WIDGET_GAUGE, "b", "b", 0.0f, 1.0f, ""},
        {PROTOCORE_WIDGET_BAR, "c", "c", 0.0f, 1.0f, ""},
        {PROTOCORE_WIDGET_SPARKLINE, "d", "d", 0.0f, 1.0f, ""},
        {PROTOCORE_WIDGET_CHART, "e", "e", 0.0f, 1.0f, ""},
        {PROTOCORE_WIDGET_BUTTON, "f", "f", 0.0f, 1.0f, ""},
        {PROTOCORE_WIDGET_TOGGLE, "g", "g", 0.0f, 1.0f, ""},
        {PROTOCORE_WIDGET_SLIDER, "h", "h", 0.0f, 1.0f, ""},
        {(protocore_widget_type)99, "i", "i", 0.0f, 1.0f, ""},
    };
    static const char *const NAMES[] = {"\"type\":\"value\",\"label\":\"a\"",
                                        "\"type\":\"gauge\",\"label\":\"b\"",
                                        "\"type\":\"bar\",\"label\":\"c\"",
                                        "\"type\":\"sparkline\",\"label\":\"d\"",
                                        "\"type\":\"chart\",\"label\":\"e\"",
                                        "\"type\":\"button\",\"label\":\"f\"",
                                        "\"type\":\"toggle\",\"label\":\"g\"",
                                        "\"type\":\"slider\",\"label\":\"h\"",
                                        "\"type\":\"value\",\"label\":\"i\""};

    protocore_dashboard_configure(ALL, (uint8_t)(sizeof(ALL) / sizeof(ALL[0])));
    const char *doc = layout();
    for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++)
    {
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(doc, NAMES[i]), NAMES[i]);
    }
}

// RFC 8259 sec 7: a quotation mark and a reverse solidus inside a string MUST be escaped. Both come
// straight from the application's widget table.
void test_a_label_is_escaped_per_rfc8259_section_7(void)
{
    static const protocore_widget QUOTED[] = {
        {PROTOCORE_WIDGET_VALUE, "say \"hi\"", "back\\slash", 0.0f, 1.0f, "\""},
    };
    protocore_dashboard_configure(QUOTED, 1);

    const char *doc = layout();
    TEST_ASSERT_NOT_NULL(strstr(doc, "\"label\":\"say \\\"hi\\\"\""));
    TEST_ASSERT_NOT_NULL(strstr(doc, "\"key\":\"back\\\\slash\""));
    TEST_ASSERT_NOT_NULL(strstr(doc, "\"unit\":\"\\\"\""));

    // The same key reaches the values document, and is escaped there too.
    TEST_ASSERT_NOT_NULL(strstr(values(), "\"back\\\\slash\":0"));
}

// RFC 8259 sec 7 names the control characters U+0000 through U+001F alongside the quote and the
// reverse solidus as characters that MUST be escaped, so none of them may appear raw in the
// document. Whether the escape is the two-character form or \u00XX is the encoder's choice.
void test_a_control_character_in_a_label_is_escaped(void)
{
    static const protocore_widget CTRL[] = {
        {PROTOCORE_WIDGET_VALUE, "line\nfeed\ttab", "k", 0.0f, 1.0f, ""},
    };
    protocore_dashboard_configure(CTRL, 1);

    const char *doc = layout();
    for (const char *p = doc; *p; p++)
    {
        TEST_ASSERT_TRUE_MESSAGE((unsigned char)*p >= 0x20u, "a control character reached the document raw");
    }
}

// A control message names one key and one number. The parser reports both, and it matches the
// message's own quoted "k" / "v" rather than any occurrence of those letters inside a key.
void test_a_control_message_yields_its_key_and_value(void)
{
    char key[32];
    float v = 0.0f;

    TEST_ASSERT_TRUE(protocore_dashboard_parse_control("{\"k\":\"temp\",\"v\":42.5}", key, sizeof(key), &v));
    TEST_ASSERT_EQUAL_STRING("temp", key);
    TEST_ASSERT_EQUAL_FLOAT(42.5f, v);

    // A key beginning with the letters the parser searches for is not mistaken for them.
    TEST_ASSERT_TRUE(protocore_dashboard_parse_control("{\"k\":\"volts\",\"v\":-3.25}", key, sizeof(key), &v));
    TEST_ASSERT_EQUAL_STRING("volts", key);
    TEST_ASSERT_EQUAL_FLOAT(-3.25f, v);

    // Whitespace around the separators is skipped.
    TEST_ASSERT_TRUE(protocore_dashboard_parse_control("{\"k\" : \"led\", \"v\" : 1}", key, sizeof(key), &v));
    TEST_ASSERT_EQUAL_STRING("led", key);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, v);

    // The two members may arrive in either order.
    TEST_ASSERT_TRUE(protocore_dashboard_parse_control("{\"v\":7,\"k\":\"led\"}", key, sizeof(key), &v));
    TEST_ASSERT_EQUAL_STRING("led", key);
    TEST_ASSERT_EQUAL_FLOAT(7.0f, v);
}

// A message missing either member, carrying no number, or with an unterminated key is refused: a
// control that reached a device with a guessed value would move real hardware.
void test_a_malformed_control_message_is_refused(void)
{
    char key[32];
    float v = 0.0f;

    TEST_ASSERT_FALSE(protocore_dashboard_parse_control("{\"k\":\"led\"}", key, sizeof(key), &v));
    TEST_ASSERT_FALSE(protocore_dashboard_parse_control("{\"v\":1}", key, sizeof(key), &v));
    TEST_ASSERT_FALSE(protocore_dashboard_parse_control("{\"k\":\"led\",\"v\":}", key, sizeof(key), &v));
    TEST_ASSERT_FALSE(protocore_dashboard_parse_control("{\"k\":\"led\",\"v\":abc}", key, sizeof(key), &v));
    TEST_ASSERT_FALSE(protocore_dashboard_parse_control("{\"k\":led,\"v\":1}", key, sizeof(key), &v));
    TEST_ASSERT_FALSE(protocore_dashboard_parse_control("", key, sizeof(key), &v));

    // A key longer than the destination is refused rather than silently shortened into another key.
    char small[4];
    TEST_ASSERT_FALSE(protocore_dashboard_parse_control("{\"k\":\"toolong\",\"v\":1}", small, sizeof(small), &v));
    TEST_ASSERT_EQUAL_STRING("", small);

    TEST_ASSERT_FALSE(protocore_dashboard_parse_control(NULL, key, sizeof(key), &v));
    TEST_ASSERT_FALSE(protocore_dashboard_parse_control("{\"k\":\"led\",\"v\":1}", NULL, sizeof(key), &v));
    TEST_ASSERT_FALSE(protocore_dashboard_parse_control("{\"k\":\"led\",\"v\":1}", key, 0, &v));
    TEST_ASSERT_FALSE(protocore_dashboard_parse_control("{\"k\":\"led\",\"v\":1}", key, sizeof(key), NULL));
}

// The dispatch delivers a well-formed message to the registered callback, and reports false when
// there is nobody registered to deliver it to.
void test_dispatch_reaches_the_registered_callback(void)
{
    TEST_ASSERT_FALSE(protocore_dashboard_dispatch_control("{\"k\":\"temp\",\"v\":5}"));
    TEST_ASSERT_EQUAL_INT(0, g_calls);

    protocore_dashboard_on_control(on_control);
    TEST_ASSERT_TRUE(protocore_dashboard_dispatch_control("{\"k\":\"temp\",\"v\":5}"));
    TEST_ASSERT_EQUAL_INT(1, g_calls);
    TEST_ASSERT_EQUAL_STRING("temp", g_key);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, g_value);

    // A malformed message never reaches the callback.
    TEST_ASSERT_FALSE(protocore_dashboard_dispatch_control("{\"k\":\"temp\"}"));
    TEST_ASSERT_EQUAL_INT(1, g_calls);
}

// A table larger than the compile-time widget limit is clamped to it rather than serialized past
// the value array it is indexed against.
void test_a_table_past_the_widget_limit_is_clamped(void)
{
    static protocore_widget many[PROTOCORE_DASHBOARD_MAX_WIDGETS + 4];
    static char keys[PROTOCORE_DASHBOARD_MAX_WIDGETS + 4][4];
    for (size_t i = 0; i < sizeof(many) / sizeof(many[0]); i++)
    {
        keys[i][0] = 'k';
        keys[i][1] = (char)('a' + (i % 26));
        keys[i][2] = '\0';
        many[i].type = PROTOCORE_WIDGET_VALUE;
        many[i].label = keys[i];
        many[i].key = keys[i];
        many[i].min = 0.0f;
        many[i].max = 1.0f;
        many[i].unit = "";
    }
    protocore_dashboard_configure(many, (uint8_t)(sizeof(many) / sizeof(many[0])));

    // One member per widget means one colon per widget, so counting them counts the widgets.
    const char *doc = values();
    int colons = 0;
    for (const char *p = doc; *p; p++)
    {
        colons += (*p == ':');
    }
    TEST_ASSERT_EQUAL_INT(PROTOCORE_DASHBOARD_MAX_WIDGETS, colons);
}

// With no table bound there is no layout and no values to report, and a serializer with no
// destination or no room writes nothing.
void test_serializing_with_nothing_to_serialize_is_refused(void)
{
    char out[128];
    TEST_ASSERT_EQUAL_INT32(0, protocore_dashboard_layout_json(NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_INT32(0, protocore_dashboard_values_json(NULL, sizeof(out)));

    char sentinel[8] = {'z', '\0'};
    TEST_ASSERT_EQUAL_INT32(0, protocore_dashboard_layout_json(sentinel, 0));
    TEST_ASSERT_EQUAL_INT32(0, protocore_dashboard_values_json(sentinel, 0));
    TEST_ASSERT_EQUAL_CHAR('z', sentinel[0]);

    protocore_dashboard_configure(NULL, 2);
    TEST_ASSERT_EQUAL_INT32(0, protocore_dashboard_layout_json(out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_EQUAL_INT32(0, protocore_dashboard_values_json(out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_FALSE(protocore_dashboard_set("temp", 1.0f));
}

// A buffer too small for the whole document reports 0 and is left empty: half an array is not
// JSON, and a caller that measures the buffer instead of reading the count would ship the fragment
// to the page, which fails to parse it rather than rendering a short dashboard.
void test_a_short_buffer_fails_closed(void)
{
    char full[512];
    int32_t n = protocore_dashboard_layout_json(full, (uint32_t)sizeof(full));
    TEST_ASSERT_TRUE(n > 0);

    // One byte short of the whole document: the count fits but the terminator does not.
    char tight[512];
    TEST_ASSERT_EQUAL_INT32(0, protocore_dashboard_layout_json(tight, (uint32_t)n));
    TEST_ASSERT_EQUAL_STRING("", tight);
    TEST_ASSERT_EQUAL_INT32(n, protocore_dashboard_layout_json(tight, (uint32_t)n + 1));

    // Room for the opening bracket but not the first widget, so the failure lands mid-document.
    char tiny[4];
    TEST_ASSERT_EQUAL_INT32(0, protocore_dashboard_layout_json(tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_STRING("", tiny);
    TEST_ASSERT_EQUAL_INT32(0, protocore_dashboard_values_json(tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_STRING("", tiny);
}
