// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the webhook builders (services/net/webhook): IFTTT URL + payload
// formatting, JSON escaping, omitted values, and fail-closed overflow. Firing
// goes through the http_client (ESP32) and is not exercised here.

#include "services/net/webhook/webhook.h"

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_ifttt_url()
{
    char buf[160];
    int n = pc_ifttt_url("button_pressed", "abc123", buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("https://maker.ifttt.com/trigger/button_pressed/with/key/abc123", buf);
}

void test_payload_three_values()
{
    char buf[128];
    int n = pc_ifttt_payload("a", "b", "c", buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("{\"value1\":\"a\",\"value2\":\"b\",\"value3\":\"c\"}", buf);
}

void test_payload_omits_nulls()
{
    char buf[128];
    pc_ifttt_payload("only1", NULL, NULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("{\"value1\":\"only1\"}", buf);

    pc_ifttt_payload(NULL, "mid", NULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("{\"value2\":\"mid\"}", buf);

    pc_ifttt_payload(NULL, NULL, NULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("{}", buf);
}

void test_payload_escapes_json()
{
    char buf[128];
    pc_ifttt_payload("he said \"hi\"", "a\\b", NULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("{\"value1\":\"he said \\\"hi\\\"\",\"value2\":\"a\\\\b\"}", buf);
}

void test_overflow_fails_closed()
{
    char buf[8];
    TEST_ASSERT_EQUAL_INT(0, pc_ifttt_url("event", "key", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(0, pc_ifttt_payload("aaaa", "bbbb", "cccc", buf, sizeof(buf)));
}

void test_ifttt_trigger_and_post_stub()
{
    // Host build (no HTTP client): webhook_post is a -1 stub; ifttt_trigger builds url+payload then posts.
    TEST_ASSERT_EQUAL_INT(-1, pc_webhook_post("http://x/y", "{}"));
    TEST_ASSERT_EQUAL_INT(-1, pc_ifttt_trigger("evt", "key", "1", "2", "3"));
}

// Null / zero-cap argument guards on the pure builders: fail closed, clearing the output
// buffer when one is provided.
void test_builder_arg_guards()
{
    char buf[64];
    buf[0] = 'x';
    TEST_ASSERT_EQUAL_INT(0, pc_ifttt_url(NULL, "k", buf, sizeof(buf)));   // null event
    TEST_ASSERT_EQUAL_STRING("", buf);                                     // cleared
    TEST_ASSERT_EQUAL_INT(0, pc_ifttt_url("e", NULL, buf, sizeof(buf)));   // null key
    TEST_ASSERT_EQUAL_INT(0, pc_ifttt_url("e", "k", NULL, 10));            // null out (no clear)
    TEST_ASSERT_EQUAL_INT(0, pc_ifttt_url("e", "k", buf, 0));              // zero cap
    TEST_ASSERT_EQUAL_INT(0, pc_ifttt_payload("a", NULL, NULL, NULL, 64)); // null out
    TEST_ASSERT_EQUAL_INT(0, pc_ifttt_payload("a", NULL, NULL, buf, 0));   // zero cap
}

// put_escaped fails closed when the escaped value would overrun the buffer, both on a
// plain character and on an escape sequence landing at the boundary.
void test_payload_escape_overflow_fails_closed()
{
    char buf[16];
    // "{\"value1\":\"" is 11 chars; a 10-char plain value overruns mid-escape-loop.
    TEST_ASSERT_EQUAL_INT(0, pc_ifttt_payload("aaaaaaaaaa", NULL, NULL, buf, sizeof(buf)));
    // A value whose escape ('"' -> two bytes) lands with < 2 bytes left also fails closed.
    TEST_ASSERT_EQUAL_INT(0, pc_ifttt_payload("aaa\"", NULL, NULL, buf, sizeof(buf)));
}

// pc_ifttt_trigger returns -1 when the url or payload cannot be built (too long for its
// fixed stack buffer), before any post is attempted.
void test_trigger_build_failures()
{
    char bigev[200];
    memset(bigev, 'e', 190);
    bigev[190] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, pc_ifttt_trigger(bigev, "k", "1", NULL, NULL)); // url overflows url[160]

    char bigval[400];
    memset(bigval, 'v', 350);
    bigval[350] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, pc_ifttt_trigger("e", "k", bigval, NULL, NULL)); // payload overflows body[256]
}

// Exact-capacity failures where the in-progress put() call itself returns false (as
// opposed to test_payload_escape_overflow_fails_closed / test_overflow_fails_closed,
// which fail earlier and leave later fields skipped by short-circuit). Each case sizes
// cap so every field up to the target one fits exactly, and the target field's put()
// call is the one that overruns by a single byte.
void test_payload_write_fails_at_each_field(void)
{
    char buf[16];

    // cap=2: "{" fits (pos=1); the opening '"' of value1 does not (1+1>=2).
    TEST_ASSERT_EQUAL_INT(0, pc_ifttt_payload("z", NULL, NULL, buf, 2));

    // cap=11: "{" + '"' + "value1" fit (pos=8); the "\":\"" separator does not (8+3>=11).
    TEST_ASSERT_EQUAL_INT(0, pc_ifttt_payload("x", NULL, NULL, buf, 11));

    // cap=13: everything through the escaped 1-char value fits (pos=12); the closing
    // '"' does not (12+1>=13).
    TEST_ASSERT_EQUAL_INT(0, pc_ifttt_payload("y", NULL, NULL, buf, 13));

    // cap=14: the whole first (and only) value fits (pos=13); the final "}" does not
    // (13+1>=14).
    TEST_ASSERT_EQUAL_INT(0, pc_ifttt_payload("y", NULL, NULL, buf, 14));
}

// When the comma before a second value fails to fit, ok goes false mid-iteration and
// every later put() call in that same pc_ifttt_payload() invocation is skipped by
// short-circuit (never actually invoked) - distinct from one of those put() calls
// failing on its own, which test_payload_write_fails_at_each_field covers above.
void test_payload_comma_failure_skips_rest(void)
{
    char buf[16];
    // "{\"value1\":\"a\"" is 13 chars (pos=13); the "," before value2 does not fit (13+1>=14).
    TEST_ASSERT_EQUAL_INT(0, pc_ifttt_payload("a", "b", NULL, buf, 14));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_ifttt_url);
    RUN_TEST(test_payload_three_values);
    RUN_TEST(test_payload_omits_nulls);
    RUN_TEST(test_payload_escapes_json);
    RUN_TEST(test_overflow_fails_closed);
    RUN_TEST(test_ifttt_trigger_and_post_stub);
    RUN_TEST(test_builder_arg_guards);
    RUN_TEST(test_payload_escape_overflow_fails_closed);
    RUN_TEST(test_trigger_build_failures);
    RUN_TEST(test_payload_write_fails_at_each_field);
    RUN_TEST(test_payload_comma_failure_skips_rest);
    return UNITY_END();
}
