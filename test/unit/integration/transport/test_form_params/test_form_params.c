// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/transport/tcp/common.h"
#include "protocore.h"
#include <stdio.h>

#include "network_drivers/transport/tcp/tcp.h"
#include "rx_feed.h"
#include <unity.h>

static char g_a[64], g_b[64], g_missing[64];
static proto_bool g_found_a, g_found_b, g_found_missing;

static void h_form(uint8_t slot, HttpReq *req)
{
    HttpParser.get_form_args.req = req;
    HttpParser.get_form_args.key = "a";
    HttpParser.get_form_args.out = g_a;
    HttpParser.get_form_args.out_size = sizeof(g_a);
    HttpParser.get_form(protocore_http_parser_span());
    g_found_a = HttpParser.ok;
    HttpParser.get_form_args.req = req;
    HttpParser.get_form_args.key = "b";
    HttpParser.get_form_args.out = g_b;
    HttpParser.get_form_args.out_size = sizeof(g_b);
    HttpParser.get_form(protocore_http_parser_span());
    g_found_b = HttpParser.ok;
    HttpParser.get_form_args.req = req;
    HttpParser.get_form_args.key = "nope";
    HttpParser.get_form_args.out = g_missing;
    HttpParser.get_form_args.out_size = sizeof(g_missing);
    HttpParser.get_form(protocore_http_parser_span());
    g_found_missing = HttpParser.ok;
    send_text(slot, 200, "text/plain", "ok");
}

static char g_trunc[4];
static proto_bool g_found_trunc;
static void h_form_trunc(uint8_t slot, HttpReq *req)
{
    HttpParser.get_form_args.req = req;
    HttpParser.get_form_args.key = "a";
    HttpParser.get_form_args.out = g_trunc;
    HttpParser.get_form_args.out_size = sizeof(g_trunc);
    HttpParser.get_form(protocore_http_parser_span());
    g_found_trunc = HttpParser.ok;
    send_text(slot, 200, "text/plain", "ok");
}

void setUp()
{
    protocore_server_reset();
    g_a[0] = g_b[0] = g_missing[0] = g_trunc[0] = '\0';
    g_found_a = g_found_b = g_found_missing = g_found_trunc = PROTO_FALSE;
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

static void feed_and_handle(uint8_t slot, const char *req_str)
{
    push_str(slot, req_str);
    HttpConn.slot = slot;
    HttpConn.parse(protocore_http_conn_span());
    handle();
}

static const char *kPost = "POST /f HTTP/1.1\r\nHost: x\r\n"
                           "Content-Type: application/x-www-form-urlencoded\r\n"
                           "Content-Length: 9\r\n\r\na=bob&b=1";

void test_form_fields_parsed()
{
    on_http("/f", HTTP_POST, h_form);
    feed_and_handle(0, kPost);
    TEST_ASSERT_TRUE(g_found_a);
    TEST_ASSERT_EQUAL_STRING("bob", g_a);
    TEST_ASSERT_TRUE(g_found_b);
    TEST_ASSERT_EQUAL_STRING("1", g_b);
}

void test_form_missing_key_returns_false()
{
    on_http("/f", HTTP_POST, h_form);
    feed_and_handle(0, kPost);
    TEST_ASSERT_FALSE(g_found_missing);
    TEST_ASSERT_EQUAL_STRING("", g_missing);
}

void test_form_empty_value()
{
    on_http("/f", HTTP_POST, h_form);
    feed_and_handle(0, "POST /f HTTP/1.1\r\nHost: x\r\n"
                       "Content-Type: application/x-www-form-urlencoded\r\n"
                       "Content-Length: 4\r\n\r\na=&b");
    TEST_ASSERT_TRUE(g_found_a);
    TEST_ASSERT_EQUAL_STRING("", g_a);
}

void test_form_wrong_content_type_ignored()
{
    on_http("/f", HTTP_POST, h_form);
    feed_and_handle(0, "POST /f HTTP/1.1\r\nHost: x\r\n"
                       "Content-Type: text/plain\r\n"
                       "Content-Length: 9\r\n\r\na=bob&b=1");
    TEST_ASSERT_FALSE(g_found_a);
}

void test_form_value_truncated_to_buffer()
{
    on_http("/f", HTTP_POST, h_form_trunc);
    feed_and_handle(0, "POST /f HTTP/1.1\r\nHost: x\r\n"
                       "Content-Type: application/x-www-form-urlencoded\r\n"
                       "Content-Length: 11\r\n\r\na=abcdefghij");
    TEST_ASSERT_TRUE(g_found_trunc);

    TEST_ASSERT_EQUAL_STRING("abc", g_trunc);
}

