// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for per-route STA/AP interface filters (PC::on(..., pc_if_kind)).
// The connection's interface is normally stamped at accept time from its local
// IP; here we set conn_pool[slot].iface directly to exercise the routing gate.

#include "protocore.h"
#include <stdio.h>
#include <string.h>

#include <unity.h>
#include "network_drivers/transport/tcp.h"

static proto_bool g_called;

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
    g_called = PROTO_FALSE;
    pc_ap_ip = 0;
}

void tearDown()
{
    tcp_capture_disable();
}

// Arm slot 0 with a given ingress interface, then dispatch one request.
static const char *do_req(pc_if_kind iface, const char *req_str)
{
    conn_pool[0] = (TcpConn){0};
    conn_pool[0].id = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].proto = PROTO_HTTP; // dispatch requires an explicit protocol
    conn_pool[0].pcb = pc_net_host_pcb();
    conn_pool[0].iface = iface;
    http_reset(0);
    tcp_capture_reset();
    push_str(0, req_str);
    http_parse(0);
    handle();
    return tcp_captured();
}

// ====================================================================
// TESTS
// ====================================================================

void test_ap_only_matches_on_ap()
{
    on_http_iface("/cfg", HTTP_GET, h_ok, PC_IF_WIFI_AP);
    const char *r = do_req(PC_IF_WIFI_AP, "GET /cfg HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
}

void test_ap_only_hidden_on_sta()
{
    on_http_iface("/cfg", HTTP_GET, h_ok, PC_IF_WIFI_AP);
    const char *r = do_req(PC_IF_WIFI_STA, "GET /cfg HTTP/1.1\r\n\r\n");
    TEST_ASSERT_FALSE(g_called); // route invisible on STA
    TEST_ASSERT_NOT_NULL(strstr(r, "404 Not Found"));
}

void test_sta_only_matches_on_sta()
{
    on_http_iface("/api", HTTP_GET, h_ok, PC_IF_WIFI_STA);
    const char *r = do_req(PC_IF_WIFI_STA, "GET /api HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
}

void test_sta_only_hidden_on_ap()
{
    on_http_iface("/api", HTTP_GET, h_ok, PC_IF_WIFI_STA);
    const char *r = do_req(PC_IF_WIFI_AP, "GET /api HTTP/1.1\r\n\r\n");
    TEST_ASSERT_FALSE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(r, "404 Not Found"));
}

void test_unfiltered_route_matches_any_interface()
{
    on_http("/x", HTTP_GET, h_ok); // PC_IF_ANY
    const char *r1 = do_req(PC_IF_WIFI_AP, "GET /x HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(r1, "200 OK"));

    g_called = PROTO_FALSE;
    const char *r2 = do_req(PC_IF_WIFI_STA, "GET /x HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(r2, "200 OK"));
}

void test_same_path_two_interfaces_picks_correct()
{
    // Same path bound to different interfaces; the request's interface decides.
    g_ap_hit = g_sta_hit = PROTO_FALSE;
    on_http_iface("/p", HTTP_GET, h_ap, PC_IF_WIFI_AP);
    on_http_iface("/p", HTTP_GET, h_sta, PC_IF_WIFI_STA);

    const char *r = do_req(PC_IF_WIFI_STA, "GET /p HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(g_sta_hit);
    TEST_ASSERT_FALSE(g_ap_hit);
    TEST_ASSERT_NOT_NULL(strstr(r, "sta"));
}

void test_set_ap_ip_updates_global()
{
    set_ap_ip(0x0104A8C0u); // 192.168.4.1 in network byte order
    TEST_ASSERT_EQUAL_UINT32(0x0104A8C0u, pc_ap_ip);
    set_ap_ip(0);
    TEST_ASSERT_EQUAL_UINT32(0u, pc_ap_ip);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_ap_only_matches_on_ap);
    RUN_TEST(test_ap_only_hidden_on_sta);
    RUN_TEST(test_sta_only_matches_on_sta);
    RUN_TEST(test_sta_only_hidden_on_ap);
    RUN_TEST(test_unfiltered_route_matches_any_interface);
    RUN_TEST(test_same_path_two_interfaces_picks_correct);
    RUN_TEST(test_set_ap_ip_updates_global);
    return UNITY_END();
}
