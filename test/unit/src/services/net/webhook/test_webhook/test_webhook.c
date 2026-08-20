// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the outbound webhook builders (services/net/webhook/webhook.h).
//
// No standard defines "webhook" and the IFTTT Maker path shape is one service's own convention, so
// the URI cases here are properties of that shape (scheme, segment order, segment content) rather
// than values a spec publishes. What the content builder emits IS governed: RFC 8259 sec 4 fixes
// the object grammar and sec 7 states that the quotation mark and the reverse solidus MUST be
// escaped with a preceding reverse solidus.
//
// test_rfc8259_escapes_quote_and_reverse_solidus is the load-bearing case: an unescaped quotation
// mark inside a value closes the string early, and everything after it is read as object syntax by
// the receiver.

#include "services/net/webhook/webhook.h"
#include <string.h>

#include <unity.h>

static uint8_t webhook_work[16]; // the borrow an entry takes; Webhook never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static char g_out[512];

static int build_url(const char *event, const char *key, char *out, size_t cap)
{
    WebhookV.ifttt.event = event;
    WebhookV.ifttt.key = key;
    WebhookV.build.out = out;
    WebhookV.build.cap = cap;
    Webhook.ifttt_url(webhook_work);
    return WebhookV.n;
}

static int build_payload(const char *v1, const char *v2, const char *v3, char *out, size_t cap)
{
    WebhookV.ifttt.value1 = v1;
    WebhookV.ifttt.value2 = v2;
    WebhookV.ifttt.value3 = v3;
    WebhookV.build.out = out;
    WebhookV.build.cap = cap;
    Webhook.ifttt_payload(webhook_work);
    return WebhookV.n;
}

// RFC 3986 sec 3.3: a path is segments separated by "/". The Maker URI names the event and the key
// as segments, in that order, under an https scheme (RFC 9110 sec 4.2.2).
void test_target_uri_carries_event_then_key_as_segments(void)
{
    const int n = build_url("button_pressed", "abc123", g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING("https://maker.ifttt.com/trigger/button_pressed/with/key/abc123", g_out);
    TEST_ASSERT_EQUAL_INT((int)strlen(g_out), n);
    TEST_ASSERT_EQUAL_INT(0, strncmp(g_out, "https://", 8));

    // Swapping the two values swaps the two segments and nothing else: neither is baked in.
    (void)build_url("abc123", "button_pressed", g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING("https://maker.ifttt.com/trigger/abc123/with/key/button_pressed", g_out);
}

// RFC 8259 sec 4: object = begin-object [ member *( value-separator member ) ] end-object, and a
// member is a string, a name-separator, then a value.
void test_rfc8259_object_grammar(void)
{
    (void)build_payload("a", "b", "c", g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING("{\"value1\":\"a\",\"value2\":\"b\",\"value3\":\"c\"}", g_out);
}

// An absent value omits its member entirely, so the separators land only between the members that
// are present and an object with none is the empty object.
void test_absent_values_omit_their_member(void)
{
    (void)build_payload("only1", NULL, NULL, g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING("{\"value1\":\"only1\"}", g_out);

    (void)build_payload(NULL, "mid", NULL, g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING("{\"value2\":\"mid\"}", g_out);

    (void)build_payload(NULL, NULL, "last", g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING("{\"value3\":\"last\"}", g_out);

    (void)build_payload("a", NULL, "c", g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING("{\"value1\":\"a\",\"value3\":\"c\"}", g_out);

    (void)build_payload(NULL, NULL, NULL, g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING("{}", g_out);
}

// RFC 8259 sec 7: "All Unicode characters may be placed within the quotation marks, except for the
// characters that MUST be escaped: quotation mark, reverse solidus, and the control characters".
void test_rfc8259_escapes_quote_and_reverse_solidus(void)
{
    (void)build_payload("he said \"hi\"", "a\\b", NULL, g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING("{\"value1\":\"he said \\\"hi\\\"\",\"value2\":\"a\\\\b\"}", g_out);

    // A lone reverse solidus at the end of a value still doubles: the escape is per octet.
    (void)build_payload("trail\\", NULL, NULL, g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING("{\"value1\":\"trail\\\\\"}", g_out);

    // The reported count is the octets written, which the escapes make longer than the input.
    const int n = build_payload("\"", NULL, NULL, g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING("{\"value1\":\"\\\"\"}", g_out);
    TEST_ASSERT_EQUAL_INT((int)strlen(g_out), n);
}

// A value that does not fit leaves the region empty and reports nothing written: half an object is
// not a shorter object, and a receiver parsing it would reject the whole request.
void test_overflow_writes_nothing(void)
{
    char small[8];
    small[0] = 'x';
    TEST_ASSERT_EQUAL_INT(0, build_url("event", "key", small, sizeof(small)));
    TEST_ASSERT_EQUAL_STRING("", small);

    small[0] = 'x';
    TEST_ASSERT_EQUAL_INT(0, build_payload("aaaa", "bbbb", "cccc", small, sizeof(small)));
    TEST_ASSERT_EQUAL_STRING("", small);

    // An escape that would land one octet past the end fails the same way, mid-value.
    char sixteen[16];
    TEST_ASSERT_EQUAL_INT(0, build_payload("aaaaaaaaaa", NULL, NULL, sixteen, sizeof(sixteen)));
    TEST_ASSERT_EQUAL_STRING("", sixteen);
    TEST_ASSERT_EQUAL_INT(0, build_payload("aaa\"", NULL, NULL, sixteen, sizeof(sixteen)));
    TEST_ASSERT_EQUAL_STRING("", sixteen);
}

// The smallest object, {}, is 2 octets and needs a third for the terminator: one octet less writes
// nothing at all.
void test_exact_capacity_boundary(void)
{
    char buf[4];
    TEST_ASSERT_EQUAL_INT(2, build_payload(NULL, NULL, NULL, buf, 3));
    TEST_ASSERT_EQUAL_STRING("{}", buf);
    TEST_ASSERT_EQUAL_INT(0, build_payload(NULL, NULL, NULL, buf, 2));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

// Every field of the object is a separate bounded write, so a capacity that stops the build at any
// one of them still yields nothing rather than a partial object.
void test_every_field_fails_closed(void)
{
    char buf[16];
    static const size_t CAPS[] = {2, 11, 13, 14};
    for (size_t i = 0; i < sizeof(CAPS) / sizeof(CAPS[0]); i++)
    {
        buf[0] = 'x';
        TEST_ASSERT_EQUAL_INT(0, build_payload("y", NULL, NULL, buf, CAPS[i]));
        TEST_ASSERT_EQUAL_STRING("", buf);
    }
    // "{\"value1\":\"a\"" is 13 octets, so at cap 14 the value-separator before value2 is what
    // does not fit.
    buf[0] = 'x';
    TEST_ASSERT_EQUAL_INT(0, build_payload("a", "b", NULL, buf, 14));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

// A missing segment value, a missing region, or a zero-length one is refused; the region is
// cleared when there is one to clear.
void test_builder_argument_guards(void)
{
    char buf[64];
    buf[0] = 'x';
    TEST_ASSERT_EQUAL_INT(0, build_url(NULL, "k", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf);
    buf[0] = 'x';
    TEST_ASSERT_EQUAL_INT(0, build_url("e", NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf);
    TEST_ASSERT_EQUAL_INT(0, build_url("e", "k", NULL, 64));
    TEST_ASSERT_EQUAL_INT(0, build_url("e", "k", buf, 0));

    TEST_ASSERT_EQUAL_INT(0, build_payload("a", NULL, NULL, NULL, 64));
    TEST_ASSERT_EQUAL_INT(0, build_payload("a", NULL, NULL, buf, 0));
}

// This build carries no outbound HTTP client, so a POST reports a transport error rather than a
// status code: RFC 9110 sec 15.1 status codes are three digits, and a negative value is not one.
void test_post_reports_no_transport(void)
{
    WebhookV.request.target_uri = "https://maker.ifttt.com/trigger/e/with/key/k";
    WebhookV.request.content = "{}";
    Webhook.post(webhook_work);
    TEST_ASSERT_EQUAL_INT(-1, WebhookV.i32);
    TEST_ASSERT_TRUE(WebhookV.i32 < 0);
}

// A trigger builds both frames, then posts: with no transport under it the result is the same -1,
// and it reports -1 for a build that does not fit before any POST is formed.
void test_trigger_builds_then_posts(void)
{
    WebhookV.ifttt.event = "evt";
    WebhookV.ifttt.key = "key";
    WebhookV.ifttt.value1 = "1";
    WebhookV.ifttt.value2 = "2";
    WebhookV.ifttt.value3 = "3";
    Webhook.ifttt_trigger(webhook_work);
    TEST_ASSERT_EQUAL_INT(-1, WebhookV.i32);

    char long_event[200];
    memset(long_event, 'e', 190);
    long_event[190] = '\0';
    WebhookV.ifttt.event = long_event; // the URI frame is 160 octets
    Webhook.ifttt_trigger(webhook_work);
    TEST_ASSERT_EQUAL_INT(-1, WebhookV.i32);
    TEST_ASSERT_EQUAL_INT(0, WebhookV.n);

    char long_value[400];
    memset(long_value, 'v', 350);
    long_value[350] = '\0';
    WebhookV.ifttt.event = "evt";
    WebhookV.ifttt.value1 = long_value; // the content frame is 256 octets
    WebhookV.ifttt.value2 = NULL;
    WebhookV.ifttt.value3 = NULL;
    Webhook.ifttt_trigger(webhook_work);
    TEST_ASSERT_EQUAL_INT(-1, WebhookV.i32);
    TEST_ASSERT_EQUAL_INT(0, WebhookV.n);
}

// A POST with no target URI or no content names nothing to send, so it never reaches a transport.
void test_post_argument_guards(void)
{
    WebhookV.request.target_uri = NULL;
    WebhookV.request.content = "{}";
    Webhook.post(webhook_work);
    TEST_ASSERT_TRUE(WebhookV.i32 < 0);

    WebhookV.request.target_uri = "https://example.com/hook";
    WebhookV.request.content = NULL;
    Webhook.post(webhook_work);
    TEST_ASSERT_TRUE(WebhookV.i32 < 0);
}
