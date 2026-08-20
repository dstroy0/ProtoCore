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

static proto_bool g_ap_hit;
static proto_bool g_sta_hit;
static void h_ap(uint8_t slot, HttpReq *req)
{
    (void)req;
    g_ap_hit = PROTO_TRUE;
    send_text(slot, 200, "text/plain", "ap");
}
static void h_sta(uint8_t slot, HttpReq *req)
{
    (void)req;
    g_sta_hit = PROTO_TRUE;
    send_text(slot, 200, "text/plain", "sta");
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
    g_called = PROTO_FALSE;
    protocore_ap_ip = 0;
}

void tearDown()
{
    tcp_capture_disable();
}

static const char *do_req(protocore_if_kind iface, const char *req_str)
{
    conn_pool[0] = (TcpConn){0};
    conn_pool[0].id = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].proto = PROTO_HTTP;
    conn_pool[0].pcb = protocore_net_host_pcb();
    conn_pool[0].iface = iface;
    HttpConnV.slot = 0;
    HttpConn.reset(protocore_http_conn_span());
    tcp_capture_reset();
    push_str(0, req_str);
    HttpConnV.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    handle();
    return tcp_captured();
}

void test_ap_only_matches_on_ap()
{
    on_http_iface("/cfg", HTTP_GET, h_ok, PROTOCORE_IF_WIFI_AP);
    const char *r = do_req(PROTOCORE_IF_WIFI_AP, "GET /cfg HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
}

void test_ap_only_hidden_on_sta()
{
    on_http_iface("/cfg", HTTP_GET, h_ok, PROTOCORE_IF_WIFI_AP);
    const char *r = do_req(PROTOCORE_IF_WIFI_STA, "GET /cfg HTTP/1.1\r\n\r\n");
    TEST_ASSERT_FALSE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(r, "404 Not Found"));
}

void test_sta_only_matches_on_sta()
{
    on_http_iface("/api", HTTP_GET, h_ok, PROTOCORE_IF_WIFI_STA);
    const char *r = do_req(PROTOCORE_IF_WIFI_STA, "GET /api HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
}

void test_sta_only_hidden_on_ap()
{
    on_http_iface("/api", HTTP_GET, h_ok, PROTOCORE_IF_WIFI_STA);
    const char *r = do_req(PROTOCORE_IF_WIFI_AP, "GET /api HTTP/1.1\r\n\r\n");
    TEST_ASSERT_FALSE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(r, "404 Not Found"));
}

void test_unfiltered_route_matches_any_interface()
{
    on_http("/x", HTTP_GET, h_ok);
    const char *r1 = do_req(PROTOCORE_IF_WIFI_AP, "GET /x HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(r1, "200 OK"));

    g_called = PROTO_FALSE;
    const char *r2 = do_req(PROTOCORE_IF_WIFI_STA, "GET /x HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(r2, "200 OK"));
}

void test_same_path_two_interfaces_picks_correct()
{

    g_ap_hit = g_sta_hit = PROTO_FALSE;
    on_http_iface("/p", HTTP_GET, h_ap, PROTOCORE_IF_WIFI_AP);
    on_http_iface("/p", HTTP_GET, h_sta, PROTOCORE_IF_WIFI_STA);

    const char *r = do_req(PROTOCORE_IF_WIFI_STA, "GET /p HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(g_sta_hit);
    TEST_ASSERT_FALSE(g_ap_hit);
    TEST_ASSERT_NOT_NULL(strstr(r, "sta"));
}

void test_set_ap_ip_updates_global()
{
    set_ap_ip(0x0104A8C0u);
    TEST_ASSERT_EQUAL_UINT32(0x0104A8C0u, protocore_ap_ip);
    set_ap_ip(0);
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_ap_ip);
}
