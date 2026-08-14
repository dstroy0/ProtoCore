// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the OpenADR 3.0 event / report JSON codec (services/energy/openadr/openadr.h).
//
// The object member names and nesting come from the module's own documented OpenADR 3.0 shape, not
// from a standard this repo can cite verbatim. What IS standard is the serialization: RFC 8259 sec 7
// says every string is delimited by quotation marks and that "the quotation mark, reverse solidus,
// and the control characters (U+0000 through U+001F)" MUST be escaped, with the two-character forms
// for the named few and \u00XX for the rest.
//
// test_rfc8259_string_escaping is therefore the load-bearing case: a demand-response signal whose
// resource name carries a tab or a quote either escapes correctly or produces a document the VTN
// rejects outright, and the failure only shows up on the one device whose name contains it.

#include "services/energy/openadr/openadr.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static char g_out[1024];

// One SIMPLE interval: the whole document, member for member.
void test_event_document_shape(void)
{
    OpenAdrInterval iv;
    iv.start = 1700000000u;
    iv.duration = 3600u;
    iv.type = "SIMPLE";
    iv.value = 1.0;

    size_t n = protocore_openadr_event("P1", "E1", &iv, 1u, g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING("{\"objectType\":\"EVENT\",\"programID\":\"P1\",\"eventName\":\"E1\",\"intervals\":["
                             "{\"id\":0,\"interval\":{\"start\":1700000000,\"duration\":3600},"
                             "\"payloads\":[{\"type\":\"SIMPLE\",\"values\":[1.000]}]}]}",
                             g_out);
    TEST_ASSERT_EQUAL_UINT(strlen(g_out), n);
}

// Interval ids run 0, 1, 2 in emission order and the array separators sit between them, so a VTN
// reading the array back gets the same schedule in the same order.
void test_event_carries_every_interval_in_order(void)
{
    OpenAdrInterval iv[3];
    for (size_t i = 0; i < 3; i++)
    {
        iv[i].start = (uint32_t)(1000u + i);
        iv[i].duration = 60u;
        iv[i].type = "PRICE";
        iv[i].value = 0.25;
    }

    size_t n = protocore_openadr_event("P", "E", iv, 3u, g_out, sizeof(g_out));
    TEST_ASSERT_TRUE(n > 0u);
    TEST_ASSERT_NOT_NULL(strstr(g_out, "{\"id\":0,\"interval\":{\"start\":1000,\"duration\":60}"));
    TEST_ASSERT_NOT_NULL(strstr(g_out, "{\"id\":1,\"interval\":{\"start\":1001,\"duration\":60}"));
    TEST_ASSERT_NOT_NULL(strstr(g_out, "{\"id\":2,\"interval\":{\"start\":1002,\"duration\":60}"));
    TEST_ASSERT_NOT_NULL(strstr(g_out, "\"values\":[0.250]"));
    // Two interval objects are separated, never concatenated.
    TEST_ASSERT_NOT_NULL(strstr(g_out, "}]},{\"id\":1"));

    // No intervals is an empty array, not a missing member.
    n = protocore_openadr_event("P", "E", NULL, 0u, g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING("{\"objectType\":\"EVENT\",\"programID\":\"P\",\"eventName\":\"E\",\"intervals\":[]}",
                             g_out);
    TEST_ASSERT_EQUAL_UINT(strlen(g_out), n);
}

// The report document, member for member.
void test_report_document_shape(void)
{
    size_t n = protocore_openadr_report("P1", "EV-7", "meter-1", -1.5, 1700000000u, g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING("{\"objectType\":\"REPORT\",\"programID\":\"P1\",\"eventID\":\"EV-7\","
                             "\"resources\":[{\"resourceName\":\"meter-1\","
                             "\"intervals\":[{\"interval\":{\"start\":1700000000},"
                             "\"payloads\":[{\"type\":\"READING\",\"values\":[-1.500]}]}]}]}",
                             g_out);
    TEST_ASSERT_EQUAL_UINT(strlen(g_out), n);
}

// RFC 8259 sec 7: the quotation mark, the reverse solidus and every control character U+0000..U+001F
// MUST be escaped. The named two-character forms are \" \\ \b \f \n \r \t; anything else in that
// range takes \u00XX.
void test_rfc8259_string_escaping(void)
{
    size_t n = protocore_openadr_report("a\"b", "c\\d", "e\tf\rg\nh", 0.0, 0u, g_out, sizeof(g_out));
    TEST_ASSERT_TRUE(n > 0u);
    TEST_ASSERT_NOT_NULL(strstr(g_out, "\"programID\":\"a\\\"b\""));
    TEST_ASSERT_NOT_NULL(strstr(g_out, "\"eventID\":\"c\\\\d\""));
    TEST_ASSERT_NOT_NULL(strstr(g_out, "\"resourceName\":\"e\\tf\\rg\\nh\""));
    // No raw control octet survives into the document.
    for (size_t i = 0; i < n; i++)
    {
        TEST_ASSERT_TRUE_MESSAGE((unsigned char)g_out[i] >= 0x20u, "raw control character in JSON output");
    }

    // A control character with no named form takes the six-character \u00XX escape.
    n = protocore_openadr_report("\x01\x1f", "e", "r", 0.0, 0u, g_out, sizeof(g_out));
    TEST_ASSERT_TRUE(n > 0u);
    TEST_ASSERT_NOT_NULL(strstr(g_out, "\"programID\":\"\\u0001\\u001f\""));

    // A null string member serializes as the empty string rather than being dropped.
    n = protocore_openadr_report(NULL, NULL, NULL, 0.0, 0u, g_out, sizeof(g_out));
    TEST_ASSERT_TRUE(n > 0u);
    TEST_ASSERT_NOT_NULL(strstr(g_out, "\"programID\":\"\""));
    TEST_ASSERT_NOT_NULL(strstr(g_out, "\"eventID\":\"\""));
    TEST_ASSERT_NOT_NULL(strstr(g_out, "\"resourceName\":\"\""));
}

// Values are emitted with three decimal places and half-up rounding at the milli-unit. Each case
// below is exact in binary, so the expected text follows from the definition rather than from what
// the formatter happens to produce: 0.0625 * 1000 = 62.5, +0.5 = 63 -> "0.063".
void test_payload_value_formatting(void)
{
    struct
    {
        double value;
        const char *want;
    } static const CASES[] = {
        {0.0, "\"values\":[0.000]"},         {1.0, "\"values\":[1.000]"},
        {0.5, "\"values\":[0.500]"},         {0.25, "\"values\":[0.250]"},
        {0.0625, "\"values\":[0.063]"},      {-0.5, "\"values\":[-0.500]"},
        {-2.75, "\"values\":[-2.750]"},      {1024.5, "\"values\":[1024.500]"},
        {0.001953125, "\"values\":[0.002]"}, // 1/512 = 1.953125 milli, rounds to 2
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        size_t n = protocore_openadr_report("P", "E", "R", CASES[i].value, 0u, g_out, sizeof(g_out));
        TEST_ASSERT_TRUE(n > 0u);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, CASES[i].want), CASES[i].want);
    }
}

// Epoch seconds are decimal integers with no separators or quotes, up to the 32-bit ceiling.
void test_timestamps_are_plain_decimal_integers(void)
{
    TEST_ASSERT_TRUE(protocore_openadr_report("P", "E", "R", 0.0, 0u, g_out, sizeof(g_out)) > 0u);
    TEST_ASSERT_NOT_NULL(strstr(g_out, "{\"start\":0}"));
    TEST_ASSERT_TRUE(protocore_openadr_report("P", "E", "R", 0.0, 4294967295u, g_out, sizeof(g_out)) > 0u);
    TEST_ASSERT_NOT_NULL(strstr(g_out, "{\"start\":4294967295}"));

    OpenAdrInterval iv;
    iv.start = 4294967295u;
    iv.duration = 4294967295u;
    iv.type = "LOAD_CONTROL";
    iv.value = 0.0;
    TEST_ASSERT_TRUE(protocore_openadr_event("P", "E", &iv, 1u, g_out, sizeof(g_out)) > 0u);
    TEST_ASSERT_NOT_NULL(strstr(g_out, "\"start\":4294967295,\"duration\":4294967295"));
}

// A buffer that cannot hold the whole document reports 0 rather than a truncated object: half a JSON
// document is not a document the VTN can parse, and a caller that trusted the length would post it.
void test_overflow_reports_zero(void)
{
    OpenAdrInterval iv;
    iv.start = 1u;
    iv.duration = 1u;
    iv.type = "SIMPLE";
    iv.value = 1.0;

    size_t full = protocore_openadr_event("P1", "E1", &iv, 1u, g_out, sizeof(g_out));
    TEST_ASSERT_TRUE(full > 0u);

    char small[64];
    TEST_ASSERT_TRUE(full + 1u > sizeof(small));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_openadr_event("P1", "E1", &iv, 1u, small, sizeof(small)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_openadr_event("P1", "E1", &iv, 1u, g_out, full)); // no room for the NUL
    TEST_ASSERT_EQUAL_UINT(full, protocore_openadr_event("P1", "E1", &iv, 1u, g_out, full + 1u));

    TEST_ASSERT_EQUAL_UINT(0u, protocore_openadr_event("P1", "E1", &iv, 1u, g_out, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_openadr_event("P1", "E1", &iv, 1u, NULL, sizeof(g_out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_openadr_event("P1", "E1", NULL, 1u, g_out, sizeof(g_out)));

    size_t rep = protocore_openadr_report("P1", "E1", "R", 1.0, 1u, g_out, sizeof(g_out));
    TEST_ASSERT_TRUE(rep > 0u);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_openadr_report("P1", "E1", "R", 1.0, 1u, g_out, rep));
    TEST_ASSERT_EQUAL_UINT(rep, protocore_openadr_report("P1", "E1", "R", 1.0, 1u, g_out, rep + 1u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_openadr_report("P1", "E1", "R", 1.0, 1u, NULL, sizeof(g_out)));
}
