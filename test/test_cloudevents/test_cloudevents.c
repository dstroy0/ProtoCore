// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the CloudEvents v1.0 envelope (services/iot/cloudevents): the
// structured-JSON builder and the binary-mode ce-* header reader. Pure host tests.

#include "network_drivers/presentation/http/http_parser/http_parser.h"
#include "services/iot/cloudevents/cloudevents.h"
#include "shared_primitives/numparse.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static void feed_request(uint8_t slot, const char *raw)
{
    http_parser_reset(&http_pool[slot]);
    for (const char *p = raw; *p; p++)
    {
        http_parser_feed(&http_pool[slot], (uint8_t)*p);
    }
}

// A minimal event carries the three required attributes and specversion 1.0.
void test_build_minimal()
{
    CloudEvent ce = {0};
    ce.id = "1001";
    ce.source = "/devices/esp32-1";
    ce.type = "com.example.sensor.reading";
    char buf[256];
    size_t n = pc_cloudevents_build_json(buf, sizeof(buf), &ce);
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"specversion\":\"1.0\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"id\":\"1001\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"source\":\"/devices/esp32-1\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"type\":\"com.example.sensor.reading\""));
    TEST_ASSERT_NULL(strstr(buf, "\"data\"")); // no data when none supplied
}

// A missing required attribute fails closed (returns 0).
void test_build_requires_id_source_type()
{
    char buf[256];
    CloudEvent a = {0};
    a.source = "/s";
    a.type = "t"; // no id
    TEST_ASSERT_EQUAL_size_t(0, pc_cloudevents_build_json(buf, sizeof(buf), &a));
    CloudEvent b = {0};
    b.id = "1";
    b.type = "t"; // no source
    TEST_ASSERT_EQUAL_size_t(0, pc_cloudevents_build_json(buf, sizeof(buf), &b));
    CloudEvent c = {0};
    c.id = "1";
    c.source = "/s"; // no type
    TEST_ASSERT_EQUAL_size_t(0, pc_cloudevents_build_json(buf, sizeof(buf), &c));
}

// data_json is emitted verbatim (a JSON value), with datacontenttype defaulting.
void test_build_with_json_data()
{
    CloudEvent ce = {0};
    ce.id = "7";
    ce.source = "/s";
    ce.type = "t";
    ce.subject = "temp";
    ce.data_json = "{\"celsius\":23.5}";
    char buf[256];
    size_t n = pc_cloudevents_build_json(buf, sizeof(buf), &ce);
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"subject\":\"temp\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"datacontenttype\":\"application/json\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"data\":{\"celsius\":23.5}")); // verbatim, not a quoted string
}

// data_str is emitted as a JSON string (escaped).
void test_build_with_string_data()
{
    CloudEvent ce = {0};
    ce.id = "8";
    ce.source = "/s";
    ce.type = "t";
    ce.datacontenttype = "text/plain";
    ce.data_str = "hi \"there\"";
    char buf[256];
    TEST_ASSERT_GREATER_THAN(0, (int)pc_cloudevents_build_json(buf, sizeof(buf), &ce));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"datacontenttype\":\"text/plain\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"data\":\"hi \\\"there\\\"\"")); // quoted + escaped
}

// A too-small buffer fails closed (returns 0).
void test_build_overflow_fails_closed()
{
    CloudEvent ce = {0};
    ce.id = "1";
    ce.source = "/s";
    ce.type = "t";
    char buf[16]; // far too small for the envelope
    TEST_ASSERT_EQUAL_size_t(0, pc_cloudevents_build_json(buf, sizeof(buf), &ce));
}

// Binary mode: read an inbound event's ce-* headers.
void test_from_headers_binary_mode()
{
    feed_request(0, "POST /events HTTP/1.1\r\nHost: x\r\n"
                    "ce-id: abc-1\r\nce-source: /producer\r\nce-type: com.example.test\r\n"
                    "ce-subject: s1\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}");
    CloudEvent ce;
    TEST_ASSERT_TRUE(pc_cloudevents_from_headers(&http_pool[0], &ce));
    TEST_ASSERT_EQUAL_STRING("abc-1", ce.id);
    TEST_ASSERT_EQUAL_STRING("/producer", ce.source);
    TEST_ASSERT_EQUAL_STRING("com.example.test", ce.type);
    TEST_ASSERT_EQUAL_STRING("s1", ce.subject);
    TEST_ASSERT_EQUAL_STRING("application/json", ce.datacontenttype);
}

// Missing a required ce-* header -> not a valid binary CloudEvent.
void test_from_headers_missing_required()
{
    feed_request(0, "POST /events HTTP/1.1\r\nHost: x\r\nce-id: abc-1\r\nce-source: /p\r\n\r\n"); // no ce-type
    CloudEvent ce;
    TEST_ASSERT_FALSE(pc_cloudevents_from_headers(&http_pool[0], &ce));
}

// build_json argument guards, the datacontenttype-without-data branch, and the
// from_headers null guard.
void test_guards_and_datacontenttype_only()
{
    char buf[256];
    CloudEvent ce = {0};
    ce.id = "1";
    ce.source = "/s";
    ce.type = "t";
    TEST_ASSERT_EQUAL_size_t(0, pc_cloudevents_build_json(NULL, sizeof(buf), &ce)); // null buf
    TEST_ASSERT_EQUAL_size_t(0, pc_cloudevents_build_json(buf, 0, &ce));            // zero cap
    TEST_ASSERT_EQUAL_size_t(0, pc_cloudevents_build_json(buf, sizeof(buf), NULL)); // null event

    // datacontenttype set but no data_json/data_str -> the third data branch emits only the type.
    ce.datacontenttype = "application/json";
    size_t n = pc_cloudevents_build_json(buf, sizeof(buf), &ce);
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"datacontenttype\":\"application/json\""));
    TEST_ASSERT_NULL(strstr(buf, "\"data\":")); // no data value emitted

    CloudEvent out;
    TEST_ASSERT_FALSE(pc_cloudevents_from_headers(NULL, &out)); // null request
}

// ce_present() must treat a non-null but empty string as absent, not just a null
// pointer (the s[0] != '\0' branch, distinct from the s != NULL branch).
void test_present_empty_string_is_absent()
{
    CloudEvent ce = {0};
    ce.id = "1";
    ce.source = "/s";
    ce.type = "t";
    ce.subject = ""; // present pointer, empty content -> must NOT be emitted
    char buf[256];
    size_t n = pc_cloudevents_build_json(buf, sizeof(buf), &ce);
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_NULL(strstr(buf, "\"subject\""));
}

// data_json set to a non-null empty string is treated the same as "no data_json"
// (falls through past the data_json branch to the data_str branch below it).
void test_data_json_empty_string_falls_through()
{
    CloudEvent ce = {0};
    ce.id = "1";
    ce.source = "/s";
    ce.type = "t";
    ce.data_json = ""; // non-null, empty
    ce.data_str = "fallback";
    char buf[256];
    size_t n = pc_cloudevents_build_json(buf, sizeof(buf), &ce);
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"data\":\"fallback\""));
}

// An explicit datacontenttype alongside data_json is emitted verbatim instead of the
// "application/json" default.
void test_data_json_explicit_datacontenttype()
{
    CloudEvent ce = {0};
    ce.id = "1";
    ce.source = "/s";
    ce.type = "t";
    ce.data_json = "{\"x\":1}";
    ce.datacontenttype = "application/cloudevents+json";
    char buf[256];
    size_t n = pc_cloudevents_build_json(buf, sizeof(buf), &ce);
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"datacontenttype\":\"application/cloudevents+json\""));
    TEST_ASSERT_NULL(strstr(buf, "\"datacontenttype\":\"application/json\""));
}

// data_str with no datacontenttype omits the datacontenttype key entirely.
void test_data_str_without_datacontenttype()
{
    CloudEvent ce = {0};
    ce.id = "1";
    ce.source = "/s";
    ce.type = "t";
    ce.data_str = "plain";
    char buf[256];
    size_t n = pc_cloudevents_build_json(buf, sizeof(buf), &ce);
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"data\":\"plain\""));
    TEST_ASSERT_NULL(strstr(buf, "\"datacontenttype\""));
}

// from_headers with a valid request but a null out pointer must fail closed.
void test_from_headers_null_out()
{
    feed_request(0, "POST /events HTTP/1.1\r\nHost: x\r\nce-id: a\r\nce-source: /p\r\nce-type: t\r\n\r\n");
    TEST_ASSERT_FALSE(pc_cloudevents_from_headers(&http_pool[0], NULL));
}

// from_headers with id and (separately) source individually missing, exercising both
// short-circuit legs of the trailing id && source && type check.
void test_from_headers_missing_id_then_missing_source()
{
    feed_request(0, "POST /events HTTP/1.1\r\nHost: x\r\nce-source: /p\r\nce-type: t\r\n\r\n"); // no ce-id
    CloudEvent a;
    TEST_ASSERT_FALSE(pc_cloudevents_from_headers(&http_pool[0], &a));

    feed_request(1, "POST /events HTTP/1.1\r\nHost: x\r\nce-id: i\r\nce-type: t\r\n\r\n"); // no ce-source
    CloudEvent b;
    TEST_ASSERT_FALSE(pc_cloudevents_from_headers(&http_pool[1], &b));
}

// pc_np_ws / pc_np_digit: every whitespace class plus a non-matching fallthrough,
// and digits below/at/above the '0'-'9' range, direct-called for full branch coverage
// of these no-stdlib strtol/strtoul/strtod helpers (numparse.h).
void test_numparse_ws_digit_predicates()
{
    TEST_ASSERT_TRUE(pc_np_ws(' '));
    TEST_ASSERT_TRUE(pc_np_ws('\t'));
    TEST_ASSERT_TRUE(pc_np_ws('\n'));
    TEST_ASSERT_TRUE(pc_np_ws('\r'));
    TEST_ASSERT_TRUE(pc_np_ws('\f'));
    TEST_ASSERT_TRUE(pc_np_ws('\v'));
    TEST_ASSERT_FALSE(pc_np_ws('a')); // not whitespace: falls through every comparison

    TEST_ASSERT_FALSE(pc_np_digit('/')); // just below '0'
    TEST_ASSERT_TRUE(pc_np_digit('5'));
    TEST_ASSERT_FALSE(pc_np_digit(':')); // just above '9'
}

// pc_strtol: leading whitespace, both signs and no sign, a no-digits ("no number")
// input, and a null end pointer, covering every branch in the function body.
void test_numparse_strtol()
{
    const char *end = NULL;

    long v = pc_strtol(" 42", &end);
    TEST_ASSERT_EQUAL(42, v);
    TEST_ASSERT_EQUAL_STRING("", end); // stopped at the terminating NUL

    v = pc_strtol("-7", &end);
    TEST_ASSERT_EQUAL(-7, v);

    v = pc_strtol("+9", &end);
    TEST_ASSERT_EQUAL(9, v);

    const char *s = "abc"; // no digits at all -> "no number": *end == s, result 0
    v = pc_strtol(s, &end);
    TEST_ASSERT_EQUAL(0, v);
    TEST_ASSERT_TRUE(end == s);

    v = pc_strtol("123", NULL); // null end pointer must not be dereferenced
    TEST_ASSERT_EQUAL(123, v);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_build_minimal);
    RUN_TEST(test_build_requires_id_source_type);
    RUN_TEST(test_build_with_json_data);
    RUN_TEST(test_build_with_string_data);
    RUN_TEST(test_build_overflow_fails_closed);
    RUN_TEST(test_from_headers_binary_mode);
    RUN_TEST(test_from_headers_missing_required);
    RUN_TEST(test_guards_and_datacontenttype_only);
    RUN_TEST(test_present_empty_string_is_absent);
    RUN_TEST(test_data_json_empty_string_falls_through);
    RUN_TEST(test_data_json_explicit_datacontenttype);
    RUN_TEST(test_data_str_without_datacontenttype);
    RUN_TEST(test_from_headers_null_out);
    RUN_TEST(test_from_headers_missing_id_then_missing_source);
    RUN_TEST(test_numparse_ws_digit_predicates);
    RUN_TEST(test_numparse_strtol);
    return UNITY_END();
}
