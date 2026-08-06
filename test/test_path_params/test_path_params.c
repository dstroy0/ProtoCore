// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for `:name` path parameters and http_get_param().

#include "protocore.h"
#include <stdio.h>
#include <string.h>

#include <unity.h>
#include "network_drivers/transport/tcp.h"

static proto_bool g_called;
static char g_a[32], g_b[32];
static proto_bool g_found_a, g_found_b, g_found_missing;

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

static void copy_param(const HttpReq *req, const char *key, char *out, size_t n, proto_bool *found)
{
    const char *v = http_get_param(req, key);
    *found = (v != NULL);
    if (v)
    {
        strncpy(out, v, n - 1);
        out[n - 1] = '\0';
    }
    else
    {
        out[0] = '\0';
    }
}

static void h_one(uint8_t slot, HttpReq *req)
{
    g_called = PROTO_TRUE;
    copy_param(req, "id", g_a, sizeof(g_a), &g_found_a);
    char dummy[8];
    copy_param(req, "nope", dummy, sizeof(dummy), &g_found_missing);
    send_text(slot, 200, "text/plain", "ok");
}

static void h_two(uint8_t slot, HttpReq *req)
{
    g_called = PROTO_TRUE;
    copy_param(req, "uid", g_a, sizeof(g_a), &g_found_a);
    copy_param(req, "pid", g_b, sizeof(g_b), &g_found_b);
    send_text(slot, 200, "text/plain", "ok");
}

static void h_exact(uint8_t slot, HttpReq *req)
{
    (void)req;
    g_called = PROTO_TRUE;
    send_text(slot, 200, "text/plain", "exact");
}

void setUp()
{
    pc_server_reset();
    g_called = g_found_a = g_found_b = g_found_missing = PROTO_FALSE;
    g_a[0] = g_b[0] = '\0';
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

void test_single_param_captured()
{
    on_http("/users/:id", HTTP_GET, h_one);
    feed_and_handle(0, "GET /users/42 HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(g_called);
    TEST_ASSERT_TRUE(g_found_a);
    TEST_ASSERT_EQUAL_STRING("42", g_a);
}

void test_multiple_params_captured()
{
    on_http("/users/:uid/posts/:pid", HTTP_GET, h_two);
    feed_and_handle(0, "GET /users/7/posts/99 HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(g_called);
    TEST_ASSERT_EQUAL_STRING("7", g_a);
    TEST_ASSERT_EQUAL_STRING("99", g_b);
}

void test_missing_param_returns_null()
{
    on_http("/users/:id", HTTP_GET, h_one);
    feed_and_handle(0, "GET /users/42 HTTP/1.1\r\n\r\n");
    TEST_ASSERT_FALSE(g_found_missing);
}

void test_literal_segment_mismatch_404()
{
    on_http("/users/:id", HTTP_GET, h_one);
    feed_and_handle(0, "GET /accounts/42 HTTP/1.1\r\n\r\n");
    TEST_ASSERT_FALSE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "404"));
}

void test_extra_segment_does_not_match()
{
    on_http("/users/:id", HTTP_GET, h_one);
    feed_and_handle(0, "GET /users/42/extra HTTP/1.1\r\n\r\n");
    TEST_ASSERT_FALSE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "404"));
}

void test_empty_param_value_does_not_match()
{
    on_http("/users/:id", HTTP_GET, h_one);
    feed_and_handle(0, "GET /users/ HTTP/1.1\r\n\r\n");
    TEST_ASSERT_FALSE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "404"));
}

void test_exact_route_still_matches()
{
    on_http("/health", HTTP_GET, h_exact);
    feed_and_handle(0, "GET /health HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "exact"));
}

void test_param_route_wrong_method_405()
{
    on_http("/users/:id", HTTP_GET, h_one);
    feed_and_handle(0, "POST /users/42 HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    TEST_ASSERT_FALSE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "405"));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_single_param_captured);
    RUN_TEST(test_multiple_params_captured);
    RUN_TEST(test_missing_param_returns_null);
    RUN_TEST(test_literal_segment_mismatch_404);
    RUN_TEST(test_extra_segment_does_not_match);
    RUN_TEST(test_empty_param_value_does_not_match);
    RUN_TEST(test_exact_route_still_matches);
    RUN_TEST(test_param_route_wrong_method_405);
    return UNITY_END();
}
