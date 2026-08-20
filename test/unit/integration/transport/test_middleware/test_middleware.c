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

static proto_bool g_handler_called;
static int g_log_count;
static char g_order[16];
static size_t g_order_len;

static void order_push(char c)
{
    if (g_order_len + 1 < sizeof(g_order))
    {
        g_order[g_order_len++] = c;
        g_order[g_order_len] = '\0';
    }
}

static void arm_slot(uint8_t slot)
{
    conn_pool[slot] = (TcpConn){0};
    conn_pool[slot].id = slot;
    conn_pool[slot].state = CONN_ACTIVE;
    conn_pool[slot].proto = PROTO_HTTP;
    conn_pool[slot].pcb = protocore_net_host_pcb();
    HttpConnV.slot = slot;
    HttpConn.reset(protocore_http_conn_span());
    tcp_capture_reset();
}

static const char *do_req(uint8_t slot, const char *req_str)
{
    arm_slot(slot);
    push_str(slot, req_str);
    HttpConnV.slot = slot;
    HttpConn.parse(protocore_http_conn_span());
    handle();
    return tcp_captured();
}

static void h_ok(uint8_t slot, HttpReq *req)
{
    (void)req;
    g_handler_called = PROTO_TRUE;
    send_text(slot, 200, "text/plain", "OK");
}

static MwResult mw_pass(uint8_t slot, HttpReq *req)
{
    (void)slot;
    (void)req;
    g_log_count++;
    return MW_NEXT;
}

static MwResult mw_inject_header(uint8_t slot, HttpReq *req)
{
    (void)req;
    proto_add_response_header(slot, "X-MW", "1");
    return MW_NEXT;
}

static MwResult mw_block(uint8_t slot, HttpReq *req)
{
    (void)req;
    send_text(slot, 403, "text/plain", "Forbidden");
    return MW_HALT;
}

static MwResult mw_order_a(uint8_t slot, HttpReq *req)
{
    (void)slot;
    (void)req;
    order_push('A');
    return MW_NEXT;
}
static MwResult mw_order_b(uint8_t slot, HttpReq *req)
{
    (void)slot;
    (void)req;
    order_push('B');
    return MW_NEXT;
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
        HttpConnV.slot = i;
        HttpConn.reset(protocore_http_conn_span());
    }
    Ws.init(protocore_ws_span());
    Sse.init(protocore_sse_span());
    tcp_capture_reset();

    g_handler_called = PROTO_FALSE;
    g_log_count = 0;
    g_order[0] = '\0';
    g_order_len = 0;
    set_millis(0);
}

void tearDown()
{
    tcp_capture_disable();
}

void test_middleware_runs_then_handler()
{
    use(mw_pass);
    on_http("/t", HTTP_GET, h_ok);
    const char *r = do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(1, g_log_count);
    TEST_ASSERT_TRUE(g_handler_called);
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
}

void test_middleware_runs_for_unmatched_route()
{

    use(mw_pass);
    const char *r = do_req(0, "GET /missing HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(1, g_log_count);
    TEST_ASSERT_FALSE(g_handler_called);
    TEST_ASSERT_NOT_NULL(strstr(r, "404 Not Found"));
}

void test_middleware_can_inject_response_header()
{
    use(mw_inject_header);
    on_http("/t", HTTP_GET, h_ok);
    const char *r = do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(r, "X-MW: 1\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
}

void test_middleware_halt_short_circuits_handler()
{
    use(mw_block);
    on_http("/t", HTTP_GET, h_ok);
    const char *r = do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(r, "403 Forbidden"));
    TEST_ASSERT_FALSE(g_handler_called);
}

void test_middleware_runs_in_registration_order()
{
    use(mw_order_a);
    use(mw_order_b);
    on_http("/t", HTTP_GET, h_ok);
    do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL_STRING("AB", g_order);
}

void test_use_respects_capacity_cap()
{

    for (int i = 0; i < MAX_MIDDLEWARE + 3; i++)
    {
        use(mw_pass);
    }
    on_http("/t", HTTP_GET, h_ok);
    do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(MAX_MIDDLEWARE, g_log_count);
    TEST_ASSERT_TRUE(g_handler_called);
}

void test_rate_limit_allows_then_rejects()
{
    enable_rate_limit(2, 1000);
    on_http("/t", HTTP_GET, h_ok);

    const char *r1 = do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(r1, "200 OK"));
    const char *r2 = do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(r2, "200 OK"));

    const char *r3 = do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(r3, "429 Too Many Requests"));
    TEST_ASSERT_NOT_NULL(strstr(r3, "Retry-After: 1\r\n"));
}

void test_rate_limit_window_resets()
{
    enable_rate_limit(2, 1000);
    on_http("/t", HTTP_GET, h_ok);

    do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    const char *over = do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(over, "429"));

    set_millis(1000);
    const char *after = do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(after, "200 OK"));
}

void test_rate_limit_disabled_by_default()
{
    on_http("/t", HTTP_GET, h_ok);
    for (int i = 0; i < 5; i++)
    {
        const char *r = do_req(0, "GET /t HTTP/1.1\r\n\r\n");
        TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
    }
}

void test_use_rejects_null_middleware()
{
    use(NULL);
    use(mw_pass);
    on_http("/t", HTTP_GET, h_ok);
    do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(1, g_log_count);
    TEST_ASSERT_TRUE(g_handler_called);
}

void test_rate_limit_zero_window_disables()
{
    enable_rate_limit(1, 0);
    on_http("/t", HTTP_GET, h_ok);
    for (int i = 0; i < 5; i++)
    {
        const char *r = do_req(0, "GET /t HTTP/1.1\r\n\r\n");
        TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
    }
}
