// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/transport/tcp/common.h"
#include "network_drivers/transport/tcp/tcp.h"
#include "protocore.h"
#include "server/web/web_terminal/web_terminal.h"
#include <stdio.h>
#include <string.h>

#include "rx_feed.h"
#include <unity.h>

static char g_cmd[64];
static uint8_t g_cmd_client;

static void on_cmd(const char *line, uint8_t client_id)
{
    snprintf(g_cmd, sizeof(g_cmd), "%s", line);
    g_cmd_client = client_id;
}

static size_t build_frame(uint8_t *dst, WsOpcode opcode, const uint8_t *payload, uint16_t len)
{
    size_t pos = 0;
    dst[pos++] = 0x80u | (uint8_t)opcode;
    dst[pos++] = 0x80u | (uint8_t)len;
    dst[pos++] = 0;
    dst[pos++] = 0;
    dst[pos++] = 0;
    dst[pos++] = 0;
    if (payload && len)
    {
        memcpy(dst + pos, payload, len);
        pos += len;
    }
    return pos;
}

static proto_bool g_skip_begin = PROTO_FALSE;

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
    g_cmd[0] = '\0';
    g_cmd_client = 0xFF;
    WebTerminal.begin_args.path = "/terminal";
    WebTerminal.begin(protocore_web_terminal_span());
    if (g_skip_begin)
    {
        return;
    }
    WebTerminal.on_command_args.cb = on_cmd;
    WebTerminal.on_command(protocore_web_terminal_span());
}

void tearDown()
{
    tcp_capture_disable();
}

static uint8_t do_handshake(uint8_t slot)
{
    push_str(slot, "GET /terminal/ws HTTP/1.1\r\nHost: x\r\n"
                   "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                   "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                   "Sec-WebSocket-Version: 13\r\n\r\n");
    HttpConn.slot = slot;
    HttpConn.parse(protocore_http_conn_span());
    handle();
    Ws.slot = slot;
    Ws.find(protocore_ws_span());
    WsConn *ws = Ws.found;
    return ws ? ws->ws_id : 0xFF;
}

void test_serves_terminal_page()
{
    push_str(0, "GET /terminal HTTP/1.1\r\nHost: x\r\n\r\n");
    HttpConn.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    handle();
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(r, "text/html"));
    TEST_ASSERT_NOT_NULL(strstr(r, "PC Terminal"));
    TEST_ASSERT_NOT_NULL(strstr(r, "#080c08"));
}

void test_ws_upgrade_tracks_client()
{
    WebTerminal.client_count(protocore_web_terminal_span());
    TEST_ASSERT_EQUAL_UINT(0, WebTerminal.value);
    uint8_t wid = do_handshake(0);
    TEST_ASSERT_NOT_EQUAL(0xFF, wid);
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "101 Switching Protocols"));
    WebTerminal.client_count(protocore_web_terminal_span());
    TEST_ASSERT_EQUAL_UINT(1, WebTerminal.value);
}

void test_ws_upgrade_requires_connection_token()
{
    push_str(0, "GET /terminal/ws HTTP/1.1\r\nHost: x\r\n"
                "Upgrade: websocket\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n");
    HttpConn.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    handle();
    Ws.slot = 0;
    Ws.find(protocore_ws_span());
    TEST_ASSERT_NULL(Ws.found);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "400"));
}

void test_ws_upgrade_rejects_bad_key_length()
{
    push_str(0, "GET /terminal/ws HTTP/1.1\r\nHost: x\r\n"
                "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Key: c2hvcnQ=\r\nSec-WebSocket-Version: 13\r\n\r\n");
    HttpConn.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    handle();
    Ws.slot = 0;
    Ws.find(protocore_ws_span());
    TEST_ASSERT_NULL(Ws.found);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "400"));
}

void test_command_delivered_to_callback()
{
    uint8_t wid = do_handshake(0);
    uint8_t frame[32];
    size_t n = build_frame(frame, WS_OP_TEXT, (const uint8_t *)"reboot", 6);
    push_bytes(0, frame, n);
    handle();
    TEST_ASSERT_EQUAL_STRING("reboot", g_cmd);
    TEST_ASSERT_EQUAL_UINT(wid, g_cmd_client);
}

void test_broadcast_reaches_client()
{
    do_handshake(0);
    tcp_capture_reset();
    WebTerminal.print_args.s = "hello browser";
    WebTerminal.print(protocore_web_terminal_span());
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "hello browser"));
}

void test_printf_broadcast()
{
    do_handshake(0);
    tcp_capture_reset();
    static const protocore_field COUNT[] = {{PROTOCORE_FK_LIT, 0, 6, "count="}, PROTOCORE_U32, PROTOCORE_END};
    char out[32];
    frame.build(out, sizeof(out), COUNT, (const protocore_fval[]){PROTOCORE_VU32(7u)}, 1);
    WebTerminal.print_args.s = out;
    WebTerminal.print(protocore_web_terminal_span());
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "count=7"));
}

void test_no_broadcast_without_clients()
{

    tcp_capture_reset();
    WebTerminal.print_args.s = "nobody home";
    WebTerminal.print(protocore_web_terminal_span());
    WebTerminal.client_count(protocore_web_terminal_span());
    TEST_ASSERT_EQUAL_UINT(0, WebTerminal.value);
    TEST_ASSERT_NULL(strstr(tcp_captured(), "nobody home"));
}

void test_close_clears_client()
{
    do_handshake(0);
    WebTerminal.client_count(protocore_web_terminal_span());
    TEST_ASSERT_EQUAL_UINT(1, WebTerminal.value);
    Ws.slot = 0;
    Ws.find(protocore_ws_span());
    WsConn *ws = Ws.found;
    ws->parse_state = WS_CLOSED;
    handle();
    WebTerminal.client_count(protocore_web_terminal_span());
    TEST_ASSERT_EQUAL_UINT(0, WebTerminal.value);
}

static const char *get_path(uint8_t slot, const char *path)
{
    char req[128];
    snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: x\r\n\r\n", path);
    push_str(slot, req);
    HttpConn.slot = slot;
    HttpConn.parse(protocore_http_conn_span());
    handle();
    return tcp_captured();
}

void test_api_inert_before_begin()
{
    WebTerminal.client_count(protocore_web_terminal_span());
    TEST_ASSERT_EQUAL_UINT(0, WebTerminal.value);
    tcp_capture_reset();
    WebTerminal.print_args.s = "early";
    WebTerminal.print(protocore_web_terminal_span());
    WebTerminal.println_args.s = "early";
    WebTerminal.println(protocore_web_terminal_span());
    static const protocore_field EARLY[] = {{PROTOCORE_FK_LIT, 0, 6, "early "}, PROTOCORE_U32, PROTOCORE_END};
    char out[32];
    frame.build(out, sizeof(out), EARLY, (const protocore_fval[]){PROTOCORE_VU32(1u)}, 1);
    WebTerminal.print_args.s = out;
    WebTerminal.print(protocore_web_terminal_span());
    TEST_ASSERT_EQUAL_size_t(0, strlen(tcp_captured()));
}

void test_println_appends_newline()
{
    do_handshake(0);
    tcp_capture_reset();
    WebTerminal.println_args.s = "line one";
    WebTerminal.println(protocore_web_terminal_span());
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "line one\n"));

    tcp_capture_reset();
    WebTerminal.println_args.s = NULL;
    WebTerminal.println(protocore_web_terminal_span());
    const char *r = tcp_captured();
    size_t n = strlen(r);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_HEX8('\n', (uint8_t)r[n - 1]);
}

void test_print_null_is_ignored()
{
    do_handshake(0);
    tcp_capture_reset();
    WebTerminal.print_args.s = NULL;
    WebTerminal.print(protocore_web_terminal_span());
    TEST_ASSERT_EQUAL_size_t(0, strlen(tcp_captured()));
}

void test_begin_defaults_path_when_missing()
{
    protocore_server_reset();
    WebTerminal.begin_args.path = NULL;
    WebTerminal.begin(protocore_web_terminal_span());
    tcp_capture_reset();
    TEST_ASSERT_NOT_NULL(strstr(get_path(0, "/terminal"), "PC Terminal"));

    protocore_server_reset();
    WebTerminal.begin_args.path = "";
    WebTerminal.begin(protocore_web_terminal_span());
    tcp_capture_reset();
    TEST_ASSERT_NOT_NULL(strstr(get_path(1, "/terminal"), "PC Terminal"));
}

void test_message_without_callback()
{
    do_handshake(0);
    WebTerminal.on_command_args.cb = NULL;
    WebTerminal.on_command(protocore_web_terminal_span());
    uint8_t frame[32];
    size_t n = build_frame(frame, WS_OP_TEXT, (const uint8_t *)"ignored", 7);
    push_bytes(0, frame, n);
    handle();
    TEST_ASSERT_EQUAL_STRING("", g_cmd);
}

void test_stale_client_slot_is_skipped()
{
    do_handshake(0);
    WebTerminal.client_count(protocore_web_terminal_span());
    TEST_ASSERT_EQUAL_UINT(1, WebTerminal.value);
    Ws.slot = 0;
    Ws.find(protocore_ws_span());
    WsConn *ws = Ws.found;
    TEST_ASSERT_NOT_NULL(ws);
    ws->active = PROTO_FALSE;
    WebTerminal.client_count(protocore_web_terminal_span());
    TEST_ASSERT_EQUAL_UINT(0, WebTerminal.value);
    tcp_capture_reset();
    WebTerminal.print_args.s = "ghost";
    WebTerminal.print(protocore_web_terminal_span());
    TEST_ASSERT_NULL(strstr(tcp_captured(), "ghost"));
}

int main()
{
    UNITY_BEGIN();
    g_skip_begin = PROTO_TRUE;
    RUN_TEST(test_api_inert_before_begin);
    g_skip_begin = PROTO_FALSE;
    RUN_TEST(test_serves_terminal_page);
    RUN_TEST(test_ws_upgrade_tracks_client);
    RUN_TEST(test_ws_upgrade_requires_connection_token);
    RUN_TEST(test_ws_upgrade_rejects_bad_key_length);
    RUN_TEST(test_command_delivered_to_callback);
    RUN_TEST(test_broadcast_reaches_client);
    RUN_TEST(test_printf_broadcast);
    RUN_TEST(test_no_broadcast_without_clients);
    RUN_TEST(test_close_clears_client);
    RUN_TEST(test_println_appends_newline);
    RUN_TEST(test_print_null_is_ignored);
    RUN_TEST(test_begin_defaults_path_when_missing);
    RUN_TEST(test_message_without_callback);
    RUN_TEST(test_stale_client_slot_is_skipped);
    return UNITY_END();
}
