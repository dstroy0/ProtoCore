// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/transport/tcp/common.h"
#include "protocore.h"
#include <string.h>

#include "network_drivers/transport/tcp/tcp.h"
#include "rx_feed.h"
#include <unity.h>

static void diag_handler(uint8_t slot, HttpReq *req)
{
    (void)req;
    diag(slot);
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
        HttpConn.reset(protocore_http_conn_span());
    }
    Ws.init(protocore_ws_span());
    Sse.init(protocore_sse_span());
    tcp_capture_reset();
}

void tearDown()
{
    tcp_capture_disable();
}

void test_diag_serves_build_info_json()
{
    on_http("/diag", HTTP_GET, diag_handler);
    push_str(0, "GET /diag HTTP/1.1\r\nHost: x\r\n\r\n");
    HttpConn.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    handle();

    const char *resp = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(resp, "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(resp, "application/json"));
    TEST_ASSERT_NOT_NULL(strstr(resp, "\"lib\":\"ProtoCore\""));
    TEST_ASSERT_NOT_NULL(strstr(resp, "\"features\""));
    TEST_ASSERT_NOT_NULL(strstr(resp, "\"config\""));
}

void test_diag_json_braces_balanced()
{
    on_http("/diag2", HTTP_GET, diag_handler);
    push_str(0, "GET /diag2 HTTP/1.1\r\nHost: x\r\n\r\n");
    HttpConn.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    handle();
    const char *j = tcp_captured();
    int depth = 0, min_depth = 0;
    for (const char *p = j; *p; p++)
    {
        if (*p == '{')
        {
            depth++;
        }
        else if (*p == '}')
        {
            depth--;
        }
        if (depth < min_depth)
        {
            min_depth = depth;
        }
    }
    TEST_ASSERT_EQUAL_INT(0, depth);
    TEST_ASSERT_EQUAL_INT(0, min_depth);
}

