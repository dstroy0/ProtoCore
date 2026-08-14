// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "protocore.h"
#include "network_drivers/transport/tcp/common.h"
#include <stdio.h>
#include <string.h>

#include "rx_feed.h"
#include <unity.h>
#if PROTOCORE_ENABLE_CSRF
#include "network_drivers/transport/tcp/tcp.h"
#include "server/security/csrf/csrf.h"
#endif

static proto_bool handler_called = PROTO_FALSE;

static void handle_ok(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    handler_called = PROTO_TRUE;
    send_text(slot_id, 200, "text/plain", "OK");
}

void setUp()
{
    protocore_server_reset();
    handler_called = PROTO_FALSE;
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].proto = PROTO_HTTP;
        conn_pool[i].pcb = protocore_net_host_pcb();
        http_reset(i);
    }
    ws_init();
    protocore_sse_init();
    tcp_capture_reset();
#if PROTOCORE_ENABLE_CSRF

    static const uint8_t protocore_csrf_key[16] = {0x53, 0x65, 0x63, 0x72, 0x65, 0x74, 0x4b, 0x65,
                                                   0x79, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36};
    protocore_csrf_set_secret(protocore_csrf_key, sizeof(protocore_csrf_key));
#endif
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

static void feed_unsafe(uint8_t slot, const char *method, const char *path)
{
    char reqbuf[256];
#if PROTOCORE_ENABLE_CSRF
    char tok[CSRF_TOKEN_BUF];
    protocore_csrf_issue(tok, sizeof(tok));
    snprintf(reqbuf, sizeof(reqbuf), "%s %s HTTP/1.1\r\nX-CSRF-Token: %s\r\n\r\n", method, path, tok);
#else
    snprintf(reqbuf, sizeof(reqbuf), "%s %s HTTP/1.1\r\n\r\n", method, path);
#endif
    feed_and_handle(slot, reqbuf);
}

void test_method_mismatch_returns_405()
{
    on_http("/res", HTTP_POST, handle_ok);
    feed_and_handle(0, "GET /res HTTP/1.1\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "405 Method Not Allowed"));
}

void test_405_includes_allow_header()
{
    on_http("/res", HTTP_POST, handle_ok);
    feed_unsafe(0, "DELETE", "/res");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Allow: POST"));
}

void test_405_allow_lists_all_methods_for_path()
{
    on_http("/res", HTTP_POST, handle_ok);
    on_http("/res", HTTP_DELETE, handle_ok);
    feed_and_handle(0, "GET /res HTTP/1.1\r\n\r\n");
    const char *resp = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(resp, "405"));
    TEST_ASSERT_NOT_NULL(strstr(resp, "POST"));
    TEST_ASSERT_NOT_NULL(strstr(resp, "DELETE"));
}

void test_unknown_path_still_404_not_405()
{
    on_http("/res", HTTP_POST, handle_ok);
    feed_and_handle(0, "GET /nope HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "404"));
}

void test_unknown_method_returns_501()
{
    on_http("/res", HTTP_GET, handle_ok);
    feed_and_handle(0, "FOO /res HTTP/1.1\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "501 Not Implemented"));
}

void test_unknown_method_not_treated_as_get()
{

    on_http("/res", HTTP_GET, handle_ok);
    feed_and_handle(0, "XGET /res HTTP/1.1\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
}

void test_head_runs_get_handler_without_body()
{
    on_http("/res", HTTP_GET, handle_ok);
    feed_and_handle(0, "HEAD /res HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(handler_called);
    const char *resp = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(resp, "200 OK"));

    TEST_ASSERT_NOT_NULL(strstr(resp, "Content-Length: 2"));

    const char *sep = strstr(resp, "\r\n\r\n");
    TEST_ASSERT_NOT_NULL(sep);
    TEST_ASSERT_EQUAL_STRING("\r\n\r\n", sep);
}

void test_get_route_advertises_head_in_allow()
{
    on_http("/res", HTTP_GET, handle_ok);
    feed_unsafe(0, "POST", "/res");
    const char *resp = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(resp, "405"));
    TEST_ASSERT_NOT_NULL(strstr(resp, "GET"));
    TEST_ASSERT_NOT_NULL(strstr(resp, "HEAD"));
}

void test_head_on_post_only_route_405()
{
    on_http("/res", HTTP_POST, handle_ok);
    feed_and_handle(0, "HEAD /res HTTP/1.1\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "405"));
}

#if PROTOCORE_ENABLE_WEBSOCKET

void test_http_parse_skips_ws_upgraded_slot()
{
    WsConn *ws = ws_alloc(2);
    TEST_ASSERT_NOT_NULL(ws);

    const uint8_t frame[] = {0x81, 0x85, 0x01, 0x02, 0x03, 0x04, 0x41, 0x43, 0x42, 0x45, 0x44};
    TcpConn *c = &conn_pool[2];
    for (size_t i = 0; i < sizeof(frame); i++)
    {
        c->rx_buffer[c->rx_head] = frame[i];
        c->rx_head = (c->rx_head + 1) % RX_BUF_SIZE;
    }
    size_t tail_before = c->rx_tail;

    http_parse(2);

    TEST_ASSERT_EQUAL_size_t(tail_before, c->rx_tail);
    ws_free(2);
}
#endif

void test_correct_method_still_dispatches()
{
    on_http("/res", HTTP_GET, handle_ok);
    feed_and_handle(0, "GET /res HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
}

#if PROTOCORE_REQUEST_TIMEOUT_MS > 0

void test_slowloris_incomplete_request_reaped_past_deadline()
{
    on_http("/res", HTTP_GET, handle_ok);
    conn_pool[0].req_start_ms = 1;
    push_str(0, "GET /res HTTP/1.1\r\nHost: x\r\n");
    http_parse(0);
    TEST_ASSERT_NOT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);

    set_millis(1 + PROTOCORE_REQUEST_TIMEOUT_MS);

    conn_pool[0].last_activity_ms = 1 + PROTOCORE_REQUEST_TIMEOUT_MS;
    handle();

    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "408 Request Timeout"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Connection: close\r\n"));
    TEST_ASSERT_EQUAL(0, (int)conn_pool[0].req_start_ms);
    TEST_ASSERT_FALSE(handler_called);
}

void test_incomplete_request_survives_before_deadline()
{
    on_http("/res", HTTP_GET, handle_ok);
    conn_pool[0].req_start_ms = 1;
    push_str(0, "GET /res HTTP/1.1\r\nHost: x\r\n");
    http_parse(0);

    set_millis(PROTOCORE_REQUEST_TIMEOUT_MS);
    conn_pool[0].last_activity_ms = PROTOCORE_REQUEST_TIMEOUT_MS;
    handle();

    TEST_ASSERT_NULL(strstr(tcp_captured(), "408"));
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NOT_EQUAL(0, (int)conn_pool[0].req_start_ms);
}

void test_completed_slow_request_not_reaped()
{

    on_http("/res", HTTP_GET, handle_ok);
    conn_pool[0].req_start_ms = 1;
    feed_and_handle(0, "GET /res HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(handler_called);
    TEST_ASSERT_EQUAL(0, (int)conn_pool[0].req_start_ms);

    tcp_capture_reset();
    set_millis(1 + PROTOCORE_REQUEST_TIMEOUT_MS + 1);
    handle();
    TEST_ASSERT_NULL(strstr(tcp_captured(), "408"));
}

void test_streaming_body_upload_not_reaped_past_deadline()
{

    on_http("/res", HTTP_POST, handle_ok);
    conn_pool[0].req_start_ms = 1;
    http_pool[0].parse_state = PARSE_BODY;
    http_pool[0].content_length = 100000;
    http_pool[0].body_bytes_read = 10;
    set_millis(1 + PROTOCORE_REQUEST_TIMEOUT_MS + 5000);
    conn_pool[0].last_activity_ms = 1 + PROTOCORE_REQUEST_TIMEOUT_MS + 5000;
    handle();

    TEST_ASSERT_NULL(strstr(tcp_captured(), "408"));
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}
#endif

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_method_mismatch_returns_405);
    RUN_TEST(test_405_includes_allow_header);
    RUN_TEST(test_405_allow_lists_all_methods_for_path);
    RUN_TEST(test_unknown_path_still_404_not_405);
    RUN_TEST(test_unknown_method_returns_501);
    RUN_TEST(test_unknown_method_not_treated_as_get);
    RUN_TEST(test_head_runs_get_handler_without_body);
    RUN_TEST(test_get_route_advertises_head_in_allow);
    RUN_TEST(test_head_on_post_only_route_405);
#if PROTOCORE_ENABLE_WEBSOCKET
    RUN_TEST(test_http_parse_skips_ws_upgraded_slot);
#endif
    RUN_TEST(test_correct_method_still_dispatches);
#if PROTOCORE_REQUEST_TIMEOUT_MS > 0
    RUN_TEST(test_slowloris_incomplete_request_reaped_past_deadline);
    RUN_TEST(test_incomplete_request_survives_before_deadline);
    RUN_TEST(test_completed_slow_request_not_reaped);
    RUN_TEST(test_streaming_body_upload_not_reaped_past_deadline);
#endif
    return UNITY_END();
}
