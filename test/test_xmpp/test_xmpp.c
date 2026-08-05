// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/xmpp: the XMPP stanza builder + minimal reader.

#include "services/iot/xmpp/xmpp.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_escape(void)
{
    const char *in = "a<b&c>d'e\"f";
    char out[64];
    size_t n = pc_xmpp_escape(in, strlen(in), out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("a&lt;b&amp;c&gt;d&apos;e&quot;f", out);
    TEST_ASSERT_EQUAL_size_t(strlen("a&lt;b&amp;c&gt;d&apos;e&quot;f"), n);
    // Overflow -> 0.
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_escape(in, strlen(in), out, 5));
}

void test_message(void)
{
    char out[128];
    size_t n = pc_xmpp_message("juliet@example.com", NULL, "chat", "Hello & <world>", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(
        "<message to=\"juliet@example.com\" type=\"chat\"><body>Hello &amp; &lt;world&gt;</body></message>", out);
    TEST_ASSERT_TRUE(n > 0);
}

void test_presence(void)
{
    char out[64];
    pc_xmpp_presence(NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("<presence/>", out);
    pc_xmpp_presence("unavailable", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("<presence type=\"unavailable\"/>", out);
}

void test_iq(void)
{
    char out[64];
    pc_xmpp_iq("get", "1", "<query xmlns='jabber:iq:roster'/>", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("<iq type=\"get\" id=\"1\"><query xmlns='jabber:iq:roster'/></iq>", out);
}

void test_stanza_name(void)
{
    char name[32];
    const char *m = "<message to='x'><body>hi</body></message>";
    TEST_ASSERT_EQUAL_size_t(7, pc_xmpp_stanza_name(m, strlen(m), name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("message", name);
    const char *s = "<?xml version='1.0'?><stream:stream to='a'>";
    pc_xmpp_stanza_name(s, strlen(s), name, sizeof(name));
    TEST_ASSERT_EQUAL_STRING("stream:stream", name);
}

void test_attr(void)
{
    char val[64];
    const char *m = "<message to=\"a@b\" from=\"c@d\" type=\"chat\">body</message>";
    TEST_ASSERT_EQUAL_size_t(3, pc_xmpp_attr(m, strlen(m), "to", val, sizeof(val)));
    TEST_ASSERT_EQUAL_STRING("a@b", val);
    pc_xmpp_attr(m, strlen(m), "from", val, sizeof(val));
    TEST_ASSERT_EQUAL_STRING("c@d", val);
    pc_xmpp_attr(m, strlen(m), "type", val, sizeof(val));
    TEST_ASSERT_EQUAL_STRING("chat", val);
    // Absent attribute -> 0.
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_attr(m, strlen(m), "id", val, sizeof(val)));
}

void test_escape_all_entities_and_overflow(void)
{
    char buf[64];
    // Every escapable character plus a normal one exercises each switch case in put_escaped.
    TEST_ASSERT_TRUE(pc_xmpp_escape("a&<>'\"b", 7, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("a&amp;&lt;&gt;&apos;&quot;b", buf);
    // Null in / out fail closed.
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_escape(NULL, 3, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_escape("x", 1, NULL, sizeof(buf)));
    // Overflow on an escaped char (needs 5 bytes) and on a plain char (both put paths' false side).
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_escape("&", 1, buf, 3));
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_escape("abcd", 4, buf, 3));
}

void test_builders_overflow_fail_closed(void)
{
    char tiny[8]; // every builder's put-chain fails partway -> finish(!ok) returns 0
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_stream_open("a", "b", tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_message("to", "from", "chat", "hi", tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_presence("available", tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_iq("get", "id1", "<query/>", tiny, sizeof(tiny)));
    // Header fits but the body/child does not (the optional-block put fails).
    char mid[20];
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_message("t", NULL, NULL, "aBodyThatWillNotFit", mid, sizeof(mid)));
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_iq("t", NULL, "<aChildThatWillNotFit/>", mid, sizeof(mid)));
}

void test_builders_omit_optional_and_null_attrs(void)
{
    // body/child null skip the optional block; null attr values skip put_attr (its `!value` true side).
    char buf[128];
    TEST_ASSERT_TRUE(pc_xmpp_message("to", NULL, NULL, NULL, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "<message"));
    TEST_ASSERT_NULL(strstr(buf, "<body>"));
    TEST_ASSERT_TRUE(pc_xmpp_iq("set", NULL, NULL, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NULL(strstr(buf, "<child"));
    TEST_ASSERT_TRUE(pc_xmpp_presence(NULL, buf, sizeof(buf)) > 0); // null type -> bare <presence/>
    TEST_ASSERT_TRUE(pc_xmpp_stream_open(NULL, NULL, buf, sizeof(buf)) > 0);
}

void test_stanza_name_edges(void)
{
    char out[32];
    // Each terminator: '>', '/', space, tab, newline.
    TEST_ASSERT_TRUE(pc_xmpp_stanza_name("<iq>", 4, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING("iq", out);
    TEST_ASSERT_TRUE(pc_xmpp_stanza_name("<presence/>", 11, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING("presence", out);
    TEST_ASSERT_TRUE(pc_xmpp_stanza_name("<message x>", 11, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING("message", out);
    pc_xmpp_stanza_name("<tag\tx>", 7, out, sizeof(out)); // tab terminator
    pc_xmpp_stanza_name("<tag\nx>", 7, out, sizeof(out)); // newline terminator
    // Skips declaration / comment / close tag; no start tag; null; overflow.
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_stanza_name("<?xml?>", 7, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_stanza_name("<!--c-->", 8, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_stanza_name("</close>", 8, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_stanza_name("no tag", 6, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_stanza_name(NULL, 5, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_stanza_name("<message>", 9, out, 3)); // cap too small
}

void test_attr_edges(void)
{
    char out[32];
    // Single-quoted value + the leading-space substring guard (must not match 'to' inside 'xto').
    TEST_ASSERT_TRUE(pc_xmpp_attr("<m xto='no' to='yes'>", 21, "to", out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING("yes", out);
    // Unquoted value, null args, and value overflow all fail closed.
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_attr("<m to=x>", 8, "to", out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_attr(NULL, 5, "to", out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_attr("<m to='x'>", 10, NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_attr("<m to='alice'>", 14, "to", out, 3)); // cap too small
}

// put_attr() writes ` name="value"` in five separately bounded steps. A capacity that runs out at
// each of them in turn fails the whole stanza closed rather than emitting a half-written attribute.
void test_put_attr_fails_at_each_step(void)
{
    char buf[64];
    // "<presence"(9) then ' '(10) "type"(14) '="'(16) "ab"(18) '"'(19) "/>"(21).
    const size_t caps[] = {10, 11, 15, 17, 19, 21};
    for (size_t i = 0; i < sizeof(caps) / sizeof(caps[0]); i++)
    {
        TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_presence("ab", buf, caps[i]));
    }
    TEST_ASSERT_EQUAL_size_t(21, pc_xmpp_presence("ab", buf, 22)); // one more byte for the NUL
    TEST_ASSERT_EQUAL_STRING("<presence type=\"ab\"/>", buf);
}

// Each successive write in pc_xmpp_message() - the open tag, the three attributes, '>', and the
// optional <body> block - fails closed on its own exact capacity boundary.
void test_message_fails_at_each_step(void)
{
    char buf[80];
    // <message(8) to=(16) from=(28) type=(40) >(41) <body>(47) hi(49) </body>(56) </message>(66)
    const size_t caps[] = {8, 9, 17, 29, 41, 47, 48, 56, 66};
    for (size_t i = 0; i < sizeof(caps) / sizeof(caps[0]); i++)
    {
        TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_message("to", "from", "chat", "hi", buf, caps[i]));
    }
    TEST_ASSERT_EQUAL_size_t(66, pc_xmpp_message("to", "from", "chat", "hi", buf, 67));
    TEST_ASSERT_EQUAL_STRING("<message to=\"to\" from=\"from\" type=\"chat\"><body>hi</body></message>", buf);
}

// The same for pc_xmpp_iq(): the open tag, both attributes, '>', the child payload, the close tag.
void test_iq_fails_at_each_step(void)
{
    char buf[64];
    // <iq(3) type=(14) id=(23) >(24) <q/>(28) </iq>(33)
    const size_t caps[] = {3, 16, 24, 28, 33};
    for (size_t i = 0; i < sizeof(caps) / sizeof(caps[0]); i++)
    {
        TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_iq("get", "id1", "<q/>", buf, caps[i]));
    }
    TEST_ASSERT_EQUAL_size_t(33, pc_xmpp_iq("get", "id1", "<q/>", buf, 34));
    TEST_ASSERT_EQUAL_STRING("<iq type=\"get\" id=\"id1\"><q/></iq>", buf);
}

// And for pc_xmpp_stream_open(): the 35-byte preamble, each optional JID attribute, then the
// fixed namespace/version tail.
void test_stream_open_fails_at_each_step(void)
{
    char buf[160];
    const size_t caps[] = {35, 37, 46, 60};
    for (size_t i = 0; i < sizeof(caps) / sizeof(caps[0]); i++)
    {
        TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_stream_open("a", "b", buf, caps[i]));
    }
    TEST_ASSERT_EQUAL_size_t(136, pc_xmpp_stream_open("a", "b", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("<?xml version='1.0'?><stream:stream from=\"a\" to=\"b\""
                             " xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams'"
                             " version='1.0'>",
                             buf);
}

// Both readers fail closed on a null output buffer and on a zero capacity, not only on null input.
void test_readers_reject_null_out_and_zero_cap(void)
{
    char out[16];
    const char *m = "<message to='a'>";
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_stanza_name(m, 16, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_stanza_name(m, 16, out, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_attr(m, 16, "to", NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_attr(m, 16, "to", out, 0));
}

// A stanza name, a start tag and a quoted value may each run to the end of the supplied slice with
// no terminator: the length bound stops the scan instead of reading past it.
void test_readers_stop_at_end_of_buffer(void)
{
    char out[32];
    TEST_ASSERT_EQUAL_size_t(2, pc_xmpp_stanza_name("<iq", 3, out, sizeof(out))); // no '>', space or '/'
    TEST_ASSERT_EQUAL_STRING("iq", out);

    TEST_ASSERT_EQUAL_size_t(1, pc_xmpp_attr("<m to='x'", 9, "to", out, sizeof(out))); // tag never closes
    TEST_ASSERT_EQUAL_STRING("x", out);

    TEST_ASSERT_EQUAL_size_t(3, pc_xmpp_attr("<m to='abc>", 11, "to", out, sizeof(out))); // quote never closes
    TEST_ASSERT_EQUAL_STRING("abc", out);
}

// An attribute whose name merely *starts with* the requested one is not a match - only a name
// immediately followed by '=' counts - and a value that is not quoted at all fails closed.
void test_attr_name_must_be_followed_by_equals(void)
{
    char out[32];
    TEST_ASSERT_EQUAL_size_t(1, pc_xmpp_attr("<m top='1' to='2'>", 18, "to", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("2", out);
    TEST_ASSERT_EQUAL_size_t(0, pc_xmpp_attr("<m to=x junk>", 13, "to", out, sizeof(out)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_put_attr_fails_at_each_step);
    RUN_TEST(test_message_fails_at_each_step);
    RUN_TEST(test_iq_fails_at_each_step);
    RUN_TEST(test_stream_open_fails_at_each_step);
    RUN_TEST(test_readers_reject_null_out_and_zero_cap);
    RUN_TEST(test_readers_stop_at_end_of_buffer);
    RUN_TEST(test_attr_name_must_be_followed_by_equals);
    RUN_TEST(test_escape);
    RUN_TEST(test_message);
    RUN_TEST(test_presence);
    RUN_TEST(test_iq);
    RUN_TEST(test_stanza_name);
    RUN_TEST(test_attr);
    RUN_TEST(test_escape_all_entities_and_overflow);
    RUN_TEST(test_builders_overflow_fail_closed);
    RUN_TEST(test_builders_omit_optional_and_null_attrs);
    RUN_TEST(test_stanza_name_edges);
    RUN_TEST(test_attr_edges);
    return UNITY_END();
}
