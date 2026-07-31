// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for http_get_form(): application/x-www-form-urlencoded body
// field lookup.

#include "protocore.h"
#include <stdio.h>
#include <string.h>
#include <unity.h>

static PC server;

static char g_a[64], g_b[64], g_missing[64];
static bool g_found_a, g_found_b, g_found_missing;

static void push_str(uint8_t slot, const char *s)
{
    TcpConn *c = &conn_pool[slot];
    for (size_t i = 0; s[i]; i++)
    {
        size_t next = (c->rx_head + 1) % RX_BUF_SIZE;
        c->rx_buffer[c->rx_head] = (uint8_t)s[i];
        c->rx_head = next;
    }
}

static void h_form(uint8_t slot, HttpReq *req)
{
    g_found_a = http_get_form(req, "a", g_a, sizeof(g_a));
    g_found_b = http_get_form(req, "b", g_b, sizeof(g_b));
    g_found_missing = http_get_form(req, "nope", g_missing, sizeof(g_missing));
    server.send(slot, 200, "text/plain", "ok");
}

static char g_trunc[4];
static bool g_found_trunc;
static void h_form_trunc(uint8_t slot, HttpReq *req)
{
    g_found_trunc = http_get_form(req, "a", g_trunc, sizeof(g_trunc));
    server.send(slot, 200, "text/plain", "ok");
}

void setUp()
{
    server = PC();
    g_a[0] = g_b[0] = g_missing[0] = g_trunc[0] = '\0';
    g_found_a = g_found_b = g_found_missing = g_found_trunc = false;
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = {};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = ConnState::CONN_ACTIVE;
        conn_pool[i].proto = ConnProto::PROTO_HTTP; // dispatch requires an explicit protocol
        conn_pool[i].pcb = &_mock_pcb;
        http_reset(i);
    }
    ws_init();
    pc_sse_init();
    tcp_capture_reset();
}

void tearDown()
{
    tcp_capture_disable();
}

static void feed_and_handle(uint8_t slot, const char *req_str)
{
    push_str(slot, req_str);
    http_parse(slot);
    server.handle();
}

// "a=bob&b=1" is 9 bytes
static const char *kPost = "POST /f HTTP/1.1\r\nHost: x\r\n"
                           "Content-Type: application/x-www-form-urlencoded\r\n"
                           "Content-Length: 9\r\n\r\na=bob&b=1";

// ====================================================================
// TESTS
// ====================================================================

void test_form_fields_parsed()
{
    server.on("/f", HttpMethod::HTTP_POST, h_form);
    feed_and_handle(0, kPost);
    TEST_ASSERT_TRUE(g_found_a);
    TEST_ASSERT_EQUAL_STRING("bob", g_a);
    TEST_ASSERT_TRUE(g_found_b);
    TEST_ASSERT_EQUAL_STRING("1", g_b);
}

void test_form_missing_key_returns_false()
{
    server.on("/f", HttpMethod::HTTP_POST, h_form);
    feed_and_handle(0, kPost);
    TEST_ASSERT_FALSE(g_found_missing);
    TEST_ASSERT_EQUAL_STRING("", g_missing);
}

void test_form_empty_value()
{
    server.on("/f", HttpMethod::HTTP_POST, h_form);
    feed_and_handle(0, "POST /f HTTP/1.1\r\nHost: x\r\n"
                       "Content-Type: application/x-www-form-urlencoded\r\n"
                       "Content-Length: 4\r\n\r\na=&b"); // a= (empty), b (no =)
    TEST_ASSERT_TRUE(g_found_a);
    TEST_ASSERT_EQUAL_STRING("", g_a);
}

void test_form_wrong_content_type_ignored()
{
    server.on("/f", HttpMethod::HTTP_POST, h_form);
    feed_and_handle(0, "POST /f HTTP/1.1\r\nHost: x\r\n"
                       "Content-Type: text/plain\r\n"
                       "Content-Length: 9\r\n\r\na=bob&b=1");
    TEST_ASSERT_FALSE(g_found_a); // not urlencoded → not parsed
}

void test_form_value_truncated_to_buffer()
{
    server.on("/f", HttpMethod::HTTP_POST, h_form_trunc);
    feed_and_handle(0, "POST /f HTTP/1.1\r\nHost: x\r\n"
                       "Content-Type: application/x-www-form-urlencoded\r\n"
                       "Content-Length: 11\r\n\r\na=abcdefghij");
    TEST_ASSERT_TRUE(g_found_trunc);
    // out buffer is 4 bytes -> 3 chars + NUL
    TEST_ASSERT_EQUAL_STRING("abc", g_trunc);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_form_fields_parsed);
    RUN_TEST(test_form_missing_key_returns_false);
    RUN_TEST(test_form_empty_value);
    RUN_TEST(test_form_wrong_content_type_ignored);
    RUN_TEST(test_form_value_truncated_to_buffer);
    return UNITY_END();
}
