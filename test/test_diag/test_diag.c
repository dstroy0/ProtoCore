// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Exercises the build-flag reporter (diag() / PC_ENABLE_DIAG): the gated diag()
// entry point and its DIAG_DOC frame spec are only compiled when the flag is on,
// so this env (native_diag) is what keeps that code building + running in CI
// rather than bit-rotting.

#include "protocore.h"
#include <string.h>

#include <unity.h>
#include "network_drivers/transport/tcp.h"

static void push_str(uint8_t slot, const char *s)
{
    TcpConn *c = &conn_pool[slot];
    for (size_t i = 0; s[i]; i++)
    {
        c->rx_buffer[c->rx_head] = (uint8_t)s[i];
        c->rx_head = (c->rx_head + 1) % RX_BUF_SIZE;
    }
}

static void diag_handler(uint8_t slot, HttpReq *req)
{
    (void)req;
    diag(slot);
}

void setUp()
{
    pc_server_reset();
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].proto = PROTO_HTTP;
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

// GET on a route that calls diag() returns 200 application/json carrying the
// compile-time build-info document (lib name + features + config objects).
void test_diag_serves_build_info_json()
{
    on_http("/diag", HTTP_GET, diag_handler);
    push_str(0, "GET /diag HTTP/1.1\r\nHost: x\r\n\r\n");
    http_parse(0);
    handle();

    const char *resp = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(resp, "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(resp, "application/json"));
    TEST_ASSERT_NOT_NULL(strstr(resp, "\"lib\":\"ProtoCore\""));
    TEST_ASSERT_NOT_NULL(strstr(resp, "\"features\""));
    TEST_ASSERT_NOT_NULL(strstr(resp, "\"config\""));
}

// The served document balances its braces. The frame spec is a flat table, so an unbalanced result
// means a literal in the spec is wrong - which is exactly what this catches and nothing else does.
void test_diag_json_braces_balanced()
{
    on_http("/diag2", HTTP_GET, diag_handler);
    push_str(0, "GET /diag2 HTTP/1.1\r\nHost: x\r\n\r\n");
    http_parse(0);
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
    TEST_ASSERT_EQUAL_INT(0, depth);     // every { closed
    TEST_ASSERT_EQUAL_INT(0, min_depth); // never closed before opened
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_diag_serves_build_info_json);
    RUN_TEST(test_diag_json_braces_balanced);
    return UNITY_END();
}
