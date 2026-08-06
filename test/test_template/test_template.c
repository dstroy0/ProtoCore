// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for send_template() {{name}} placeholder substitution.

#include "protocore.h"
#include <stdio.h>
#include <string.h>

#include <unity.h>
#include "network_drivers/transport/tcp.h"

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

static const char *resolver(const char *name)
{
    if (strcmp(name, "name") == 0)
    {
        return "World";
    }
    if (strcmp(name, "x") == 0)
    {
        return "1";
    }
    if (strcmp(name, "y") == 0)
    {
        return "2";
    }
    return NULL; // unknown -> empty
}

static void h_basic(uint8_t slot, HttpReq *req)
{
    (void)req;
    send_template(slot, 200, "text/html", "Hello {{name}}!", resolver);
}
static void h_multi(uint8_t slot, HttpReq *req)
{
    (void)req;
    send_template(slot, 200, "text/plain", "{{x}}+{{y}}", resolver);
}
static void h_unknown(uint8_t slot, HttpReq *req)
{
    (void)req;
    send_template(slot, 200, "text/plain", "a{{zzz}}b", resolver);
}
static void h_unterm(uint8_t slot, HttpReq *req)
{
    (void)req;
    send_template(slot, 200, "text/plain", "a {{ b", resolver);
}
static void h_null(uint8_t slot, HttpReq *req)
{
    (void)req;
    send_template(slot, 200, "text/plain", "x{{name}}y", NULL);
}

void setUp()
{
    pc_server_reset();
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].proto = PROTO_HTTP; // dispatch requires an explicit protocol
        conn_pool[i].pcb = pc_net_host_pcb();
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
    handle();
}

// ====================================================================
// TESTS
// ====================================================================

void test_basic_substitution()
{
    on_http("/t", HTTP_GET, h_basic);
    feed_and_handle(0, "GET /t HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "Hello World!"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Length: 12")); // "Hello World!" = 12
}

void test_multiple_placeholders()
{
    on_http("/t", HTTP_GET, h_multi);
    feed_and_handle(0, "GET /t HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "1+2"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Length: 3"));
}

void test_unknown_placeholder_is_empty()
{
    on_http("/t", HTTP_GET, h_unknown);
    feed_and_handle(0, "GET /t HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();
    // body is "ab"; ensure the placeholder text is gone
    TEST_ASSERT_NOT_NULL(strstr(r, "\r\n\r\nab"));
    TEST_ASSERT_NULL(strstr(r, "zzz"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Length: 2"));
}

void test_unterminated_placeholder_is_literal()
{
    on_http("/t", HTTP_GET, h_unterm);
    feed_and_handle(0, "GET /t HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "a {{ b"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Length: 6"));
}

void test_null_resolver_empties_all()
{
    on_http("/t", HTTP_GET, h_null);
    feed_and_handle(0, "GET /t HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "\r\n\r\nxy"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Length: 2"));
}

void test_head_suppresses_body_keeps_length()
{
    on_http("/t", HTTP_GET, h_basic);
    feed_and_handle(0, "HEAD /t HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Length: 12"));
    TEST_ASSERT_NULL(strstr(r, "Hello World!")); // HEAD: headers only
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_basic_substitution);
    RUN_TEST(test_multiple_placeholders);
    RUN_TEST(test_unknown_placeholder_is_empty);
    RUN_TEST(test_unterminated_placeholder_is_literal);
    RUN_TEST(test_null_resolver_empties_all);
    RUN_TEST(test_head_suppresses_body_keeps_length);
    return UNITY_END();
}
