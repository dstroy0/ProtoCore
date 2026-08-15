// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/transport/tcp/common.h"
#include "protocore.h"
#include <string.h>

#include "network_drivers/transport/tcp/tcp.h"
#include "rx_feed.h"
#include <unity.h>

static int handler_calls = 0;

static void handle_ok(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    handler_calls++;
    send_text(slot_id, 200, "text/plain", "OK");
}

void setUp()
{
    protocore_server_reset();
    on_http("/res", HTTP_GET, handle_ok);
    handler_calls = 0;
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].proto = PROTO_HTTP;
        conn_pool[i].pcb = protocore_net_host_pcb();
        HttpConn.slot = i;
        HttpConn.conn_open(HttpConn.internal);
    }
    Ws.init(Ws.internal);
    Sse.init(Sse.internal);
    tcp_capture_reset();
}

void tearDown()
{
    tcp_capture_disable();
}

static void feed_and_handle(uint8_t slot, const char *req_str)
{
    push_str(slot, req_str);
    HttpConn.slot = slot;
    HttpConn.parse(HttpConn.internal);
    handle();
}

void test_http11_default_keeps_alive()
{
    feed_and_handle(0, "GET /res HTTP/1.1\r\n\r\n");
    const char *resp = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(resp, "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(resp, "Connection: keep-alive"));
    TEST_ASSERT_NULL(strstr(resp, "Connection: close"));

    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NOT_NULL(conn_pool[0].pcb);
    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[0].parse_state);
}

void test_http11_explicit_close()
{
    feed_and_handle(0, "GET /res HTTP/1.1\r\nConnection: close\r\n\r\n");
    const char *resp = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(resp, "Connection: close"));
    TEST_ASSERT_NULL(strstr(resp, "keep-alive"));
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
}

void test_http10_default_closes()
{
    feed_and_handle(0, "GET /res HTTP/1.0\r\n\r\n");
    const char *resp = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(resp, "Connection: close"));
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

void test_http10_explicit_keepalive()
{
    feed_and_handle(0, "GET /res HTTP/1.0\r\nConnection: keep-alive\r\n\r\n");
    const char *resp = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(resp, "Connection: keep-alive"));
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

void test_connection_token_list_close()
{

    feed_and_handle(0, "GET /res HTTP/1.1\r\nConnection: keep-alive, close\r\n\r\n");
    const char *resp = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(resp, "Connection: close"));
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

void test_two_sequential_requests_same_slot()
{
    feed_and_handle(0, "GET /res HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(1, handler_calls);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);

    tcp_capture_reset();
    feed_and_handle(0, "GET /res HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(2, handler_calls);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

void test_pipelined_requests()
{

    push_str(0, "GET /res HTTP/1.1\r\n\r\nGET /res HTTP/1.1\r\n\r\n");
    HttpConn.slot = 0;
    HttpConn.parse(HttpConn.internal);
    for (int i = 0; i < 4; i++)
    {
        handle();
    }
    TEST_ASSERT_EQUAL(2, handler_calls);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

void test_404_still_keeps_alive()
{

    feed_and_handle(0, "GET /nope HTTP/1.1\r\n\r\n");
    const char *resp = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(resp, "404"));
    TEST_ASSERT_NOT_NULL(strstr(resp, "Connection: keep-alive"));
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

void test_max_requests_cap_closes()
{

    feed_and_handle(0, "GET /res HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);

    tcp_capture_reset();
    feed_and_handle(0, "GET /res HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);

    tcp_capture_reset();
    feed_and_handle(0, "GET /res HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Connection: close"));
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL(3, handler_calls);
}

void test_fresh_connection_resets_count()
{

    for (int i = 0; i < 3; i++)
    {
        tcp_capture_reset();
        feed_and_handle(0, "GET /res HTTP/1.1\r\n\r\n");
    }
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);

    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].proto = PROTO_HTTP;
    conn_pool[0].pcb = protocore_net_host_pcb();
    HttpConn.slot = 0;
    HttpConn.conn_open(HttpConn.internal);

    tcp_capture_reset();
    feed_and_handle(0, "GET /res HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Connection: keep-alive"));
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

void test_conn_token_ws_and_bare_keepalive()
{
    feed_and_handle(0, "GET /res HTTP/1.1\r\nConnection: keep-alive , close\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Connection: close"));

    tcp_capture_reset();
    feed_and_handle(1, "GET /res HTTP/1.1\r\nConnection: keep-alive\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Connection: keep-alive"));
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[1].state);
}

void test_conn_token_delimiter_runs_and_trailing_ows()
{

    feed_and_handle(0, "GET /res HTTP/1.1\r\nConnection: , \tkeep-alive\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Connection: keep-alive"));
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);

    tcp_capture_reset();
    feed_and_handle(1, "GET /res HTTP/1.1\r\nConnection: keep-alive \t, \r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Connection: keep-alive"));
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[1].state);

    tcp_capture_reset();
    feed_and_handle(2, "GET /res HTTP/1.1\r\nConnection: xlose\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Connection: keep-alive"));
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[2].state);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_conn_token_ws_and_bare_keepalive);
    RUN_TEST(test_conn_token_delimiter_runs_and_trailing_ows);
    RUN_TEST(test_http11_default_keeps_alive);
    RUN_TEST(test_http11_explicit_close);
    RUN_TEST(test_http10_default_closes);
    RUN_TEST(test_http10_explicit_keepalive);
    RUN_TEST(test_connection_token_list_close);
    RUN_TEST(test_two_sequential_requests_same_slot);
    RUN_TEST(test_pipelined_requests);
    RUN_TEST(test_404_still_keeps_alive);
    RUN_TEST(test_max_requests_cap_closes);
    RUN_TEST(test_fresh_connection_resets_count);
    return UNITY_END();
}
