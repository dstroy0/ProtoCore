// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/transport/tcp/common.h"
#include "protocore.h"
#include <stdio.h>
#include <string.h>

#include "network_drivers/transport/tcp/tcp.h"
#include "rx_feed.h"
#include <unity.h>

static proto_bool g_called;

static void h_ok(uint8_t slot, HttpReq *req)
{
    (void)req;
    g_called = PROTO_TRUE;
    send_text(slot, 200, "text/plain", "ok");
}

void setUp()
{
    protocore_server_reset();
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].proto = PROTO_HTTP;
        conn_pool[i].pcb = protocore_net_host_pcb();
        HttpConn.slot = i;
        HttpConn.reset(HttpConn.internal);
    }
    Ws.init(Ws.internal);
    Sse.init(Sse.internal);
    tcp_capture_reset();
    g_called = PROTO_FALSE;
}

void tearDown()
{
    tcp_capture_disable();
}

static proto_bool hit(const char *method, const char *path)
{
    conn_pool[0] = (TcpConn){0};
    conn_pool[0].id = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].proto = PROTO_HTTP;
    conn_pool[0].pcb = protocore_net_host_pcb();
    HttpConn.slot = 0;
    HttpConn.reset(HttpConn.internal);
    tcp_capture_reset();
    g_called = PROTO_FALSE;
    char req[160];
    snprintf(req, sizeof(req), "%s %s HTTP/1.1\r\n\r\n", method, path);
    push_str(0, req);
    HttpConn.slot = 0;
    HttpConn.parse(HttpConn.internal);
    handle();
    return g_called;
}

void test_numeric_class_plus()
{
    on_regex("/sensor/[0-9]+", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/sensor/42"));
    TEST_ASSERT_TRUE(hit("GET", "/sensor/7"));
    TEST_ASSERT_FALSE(hit("GET", "/sensor/abc"));
    TEST_ASSERT_FALSE(hit("GET", "/sensor/"));
    TEST_ASSERT_FALSE(hit("GET", "/sensor/4a"));
}

void test_dot_star_matches_rest()
{
    on_regex("/files/.*", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/files/"));
    TEST_ASSERT_TRUE(hit("GET", "/files/a"));
    TEST_ASSERT_TRUE(hit("GET", "/files/deep/a/b"));
    TEST_ASSERT_FALSE(hit("GET", "/other"));
}

void test_escaped_dot_extension()
{
    on_regex("/img/.+\\.png", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/img/cat.png"));
    TEST_ASSERT_FALSE(hit("GET", "/img/cat.jpg"));
    TEST_ASSERT_FALSE(hit("GET", "/img/.png"));
}

void test_optional_quantifier()
{
    on_regex("/colou?r", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/color"));
    TEST_ASSERT_TRUE(hit("GET", "/colour"));
    TEST_ASSERT_FALSE(hit("GET", "/colouur"));
}

void test_range_class_only()
{
    on_regex("/[a-z]+", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/abc"));
    TEST_ASSERT_FALSE(hit("GET", "/ABC"));
    TEST_ASSERT_FALSE(hit("GET", "/a1"));
}

void test_negated_class()
{
    on_regex("/x[^/]+", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/xabc"));
    TEST_ASSERT_FALSE(hit("GET", "/x/"));
}

void test_anchored_full_match()
{
    on_regex("/api/v[12]", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/api/v1"));
    TEST_ASSERT_TRUE(hit("GET", "/api/v2"));
    TEST_ASSERT_FALSE(hit("GET", "/api/v3"));
    TEST_ASSERT_FALSE(hit("GET", "/api/v12"));
    TEST_ASSERT_FALSE(hit("GET", "/api/v1/x"));
}

void test_method_still_enforced()
{
    on_regex("/sensor/[0-9]+", HTTP_GET, h_ok);

    TEST_ASSERT_FALSE(hit("POST", "/sensor/42"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "405"));
}

void test_pathological_pattern_terminates_no_match()
{

    on_regex("/a*a*a*a*a*b", HTTP_GET, h_ok);
    TEST_ASSERT_FALSE(hit("GET", "/aaaaaaaaaaaaaaaaaaaaaaaac"));
}

void test_escape_class_digit()
{
    on_regex("/d/\\d+", HTTP_GET, h_ok);
    on_regex("/D/\\D+", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/d/42"));
    TEST_ASSERT_FALSE(hit("GET", "/d/x"));
    TEST_ASSERT_TRUE(hit("GET", "/D/abc"));
    TEST_ASSERT_FALSE(hit("GET", "/D/123"));
}

void test_escape_class_word()
{
    on_regex("/w/\\w+", HTTP_GET, h_ok);
    on_regex("/W/\\W+", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/w/ab_9"));
    TEST_ASSERT_FALSE(hit("GET", "/w/--"));
    TEST_ASSERT_TRUE(hit("GET", "/W/---"));
    TEST_ASSERT_FALSE(hit("GET", "/W/abc"));
}

void test_escape_class_space()
{
    on_regex("/s/\\S+", HTTP_GET, h_ok);
    on_regex("/z/\\sx", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/s/abc"));
    TEST_ASSERT_FALSE(hit("GET", "/z/qx"));
}

void test_class_escaped_members()
{
    on_regex("/c/[\\.]+", HTTP_GET, h_ok);
    on_regex("/r/[0-\\9]+", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/c/..."));
    TEST_ASSERT_FALSE(hit("GET", "/c/x"));
    TEST_ASSERT_TRUE(hit("GET", "/r/507"));
    TEST_ASSERT_FALSE(hit("GET", "/r/9a"));
}

void test_trailing_backslash_atom()
{
    TEST_ASSERT_TRUE(regex_match("a", "a"));
    TEST_ASSERT_FALSE(regex_match("a\\", "a"));
    TEST_ASSERT_FALSE(regex_match("a\\", "ab"));
}

void test_class_leading_bracket_is_literal()
{
    TEST_ASSERT_TRUE(regex_match("[]]", "]"));
    TEST_ASSERT_FALSE(regex_match("[]]", "x"));
    TEST_ASSERT_TRUE(regex_match("[^]]", "x"));
    TEST_ASSERT_FALSE(regex_match("[^]]", "]"));
}

void test_class_unterminated_fails_closed()
{
    TEST_ASSERT_TRUE(regex_match("[abc", "a"));
    TEST_ASSERT_TRUE(regex_match("[abc", "b"));
    TEST_ASSERT_FALSE(regex_match("[abc", "c"));
    TEST_ASSERT_FALSE(regex_match("[abc", "x"));
}

void test_class_trailing_backslash_in_body()
{
    TEST_ASSERT_TRUE(regex_match("[a\\", "a"));
    TEST_ASSERT_FALSE(regex_match("[a\\", "\\"));
}

void test_class_escaped_bound_at_end()
{
    TEST_ASSERT_TRUE(regex_match("[a\\]", "a"));
    TEST_ASSERT_TRUE(regex_match("[a\\]", "\\"));
    TEST_ASSERT_FALSE(regex_match("[a\\]", "b"));
}

void test_empty_class_matches_nothing()
{
    TEST_ASSERT_FALSE(regex_match("[]", "x"));
    TEST_ASSERT_FALSE(regex_match("[]", "]"));
    TEST_ASSERT_FALSE(regex_match("[]", ""));
}

void test_class_trailing_dash_is_literal()
{
    TEST_ASSERT_TRUE(regex_match("[a-]+", "a-a"));
    TEST_ASSERT_TRUE(regex_match("[a-]+", "-"));
    TEST_ASSERT_FALSE(regex_match("[a-]+", "b"));
}

void test_class_two_ranges()
{
    TEST_ASSERT_TRUE(regex_match("[a-z0-9]+", "a1"));
    TEST_ASSERT_TRUE(regex_match("[a-z0-9]+", "9z"));
    TEST_ASSERT_FALSE(regex_match("[a-z0-9]+", "-"));
}

void test_escape_class_digit_low_edge()
{
    TEST_ASSERT_FALSE(regex_match("\\d", "-"));
    TEST_ASSERT_FALSE(regex_match("\\d", "x"));
    TEST_ASSERT_TRUE(regex_match("\\D", "-"));
    TEST_ASSERT_TRUE(regex_match("\\d", "5"));
    TEST_ASSERT_FALSE(regex_match("\\D", "5"));
}

void test_escape_class_word_edges()
{
    TEST_ASSERT_TRUE(regex_match("\\w", "A"));
    TEST_ASSERT_TRUE(regex_match("\\w", "9"));
    TEST_ASSERT_TRUE(regex_match("\\w", "_"));
    TEST_ASSERT_FALSE(regex_match("\\w", "~"));
    TEST_ASSERT_FALSE(regex_match("\\w", "-"));
    TEST_ASSERT_FALSE(regex_match("\\W", "A"));
    TEST_ASSERT_FALSE(regex_match("\\W", "9"));
    TEST_ASSERT_FALSE(regex_match("\\W", "_"));
    TEST_ASSERT_TRUE(regex_match("\\W", "~"));
    TEST_ASSERT_TRUE(regex_match("\\W", "-"));
}

void test_escape_class_space_direct()
{
    TEST_ASSERT_TRUE(regex_match("\\s", " "));
    TEST_ASSERT_TRUE(regex_match("\\s", "\t"));
    TEST_ASSERT_FALSE(regex_match("\\s", "q"));
    TEST_ASSERT_FALSE(regex_match("\\S", " "));
    TEST_ASSERT_FALSE(regex_match("\\S", "\t"));
    TEST_ASSERT_TRUE(regex_match("\\S", "q"));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_numeric_class_plus);
    RUN_TEST(test_dot_star_matches_rest);
    RUN_TEST(test_escaped_dot_extension);
    RUN_TEST(test_optional_quantifier);
    RUN_TEST(test_range_class_only);
    RUN_TEST(test_negated_class);
    RUN_TEST(test_anchored_full_match);
    RUN_TEST(test_method_still_enforced);
    RUN_TEST(test_pathological_pattern_terminates_no_match);
    RUN_TEST(test_escape_class_digit);
    RUN_TEST(test_escape_class_word);
    RUN_TEST(test_escape_class_space);
    RUN_TEST(test_class_escaped_members);
    RUN_TEST(test_trailing_backslash_atom);
    RUN_TEST(test_class_leading_bracket_is_literal);
    RUN_TEST(test_class_unterminated_fails_closed);
    RUN_TEST(test_class_trailing_backslash_in_body);
    RUN_TEST(test_class_escaped_bound_at_end);
    RUN_TEST(test_empty_class_matches_nothing);
    RUN_TEST(test_class_trailing_dash_is_literal);
    RUN_TEST(test_class_two_ranges);
    RUN_TEST(test_escape_class_digit_low_edge);
    RUN_TEST(test_escape_class_word_edges);
    RUN_TEST(test_escape_class_space_direct);
    return UNITY_END();
}
