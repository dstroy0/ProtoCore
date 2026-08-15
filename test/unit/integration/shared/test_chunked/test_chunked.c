// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/transport/tcp/common.h"
#include "protocore.h"
#include "shared/hex/hex.h"
#include <stdio.h>
#include <string.h>

#include "network_drivers/transport/tcp/tcp.h"
#include "rx_feed.h"
#include <unity.h>

static int g_log_status;
static int g_log_len;
static void log_cb(const char *method, const char *path, int status, int body_len)
{
    (void)method;
    (void)path;
    g_log_status = status;
    g_log_len = body_len;
}

static int g_step;

static size_t src_hello(uint8_t *buf, size_t cap, void *ctx)
{
    (void)cap;
    (void)ctx;
    if (g_step++ == 0)
    {
        memcpy(buf, "hello", 5);
        return 5;
    }
    return 0;
}
static size_t src_multi(uint8_t *buf, size_t cap, void *ctx)
{
    (void)cap;
    (void)ctx;
    if (g_step == 0)
    {
        g_step = 1;
        memcpy(buf, "ab", 2);
        return 2;
    }
    if (g_step == 1)
    {
        g_step = 2;
        memcpy(buf, "cdef", 4);
        return 4;
    }
    return 0;
}
static size_t src_printf(uint8_t *buf, size_t cap, void *ctx)
{
    (void)ctx;
    if (g_step++ == 0)
    {
        return (size_t)snprintf((char *)buf, cap, "x=%d", 42);
    }
    return 0;
}
static size_t src_ok(uint8_t *buf, size_t cap, void *ctx)
{
    (void)cap;
    (void)ctx;
    if (g_step++ == 0)
    {
        memcpy(buf, "ok", 2);
        return 2;
    }
    return 0;
}
static size_t src_empty(uint8_t *buf, size_t cap, void *ctx)
{
    (void)buf;
    (void)cap;
    (void)ctx;
    return 0;
}
static size_t src_two5(uint8_t *buf, size_t cap, void *ctx)
{
    (void)cap;
    (void)ctx;
    if (g_step == 0)
    {
        g_step = 1;
        memcpy(buf, "hello", 5);
        return 5;
    }
    if (g_step == 1)
    {
        g_step = 2;
        memcpy(buf, "world", 5);
        return 5;
    }
    return 0;
}

static const int BIG_TOTAL = 16000;
static size_t src_big(uint8_t *buf, size_t cap, void *ctx)
{
    (void)ctx;
    size_t produced = (size_t)g_step;
    if (produced >= (size_t)BIG_TOTAL)
    {
        return 0;
    }
    size_t n = (size_t)BIG_TOTAL - produced;
    if (n > cap)
    {
        n = cap;
    }
    for (size_t i = 0; i < n; i++)
    {
        buf[i] = (uint8_t)('A' + ((produced + i) % 26));
    }
    g_step += (int)n;
    return n;
}

static size_t src_overreport(uint8_t *buf, size_t cap, void *ctx)
{
    (void)ctx;
    if (g_step++ == 0)
    {
        memset(buf, 'Z', cap);
        return cap + 100;
    }
    return 0;
}

static void h_hello(uint8_t s, HttpReq *r)
{
    (void)r;
    send_chunked(s, 200, "text/plain", src_hello, NULL);
}
static void h_multi(uint8_t s, HttpReq *r)
{
    (void)r;
    send_chunked(s, 200, "text/plain", src_multi, NULL);
}
static void h_printf(uint8_t s, HttpReq *r)
{
    (void)r;
    send_chunked(s, 200, "text/plain", src_printf, NULL);
}
static void h_ok(uint8_t s, HttpReq *r)
{
    (void)r;
    send_chunked(s, 200, "text/plain", src_ok, NULL);
}
static void h_empty(uint8_t s, HttpReq *r)
{
    (void)r;
    send_chunked(s, 200, "text/plain", src_empty, NULL);
}
static void h_two5(uint8_t s, HttpReq *r)
{
    (void)r;
    send_chunked(s, 200, "text/plain", src_two5, NULL);
}
static void h_big(uint8_t s, HttpReq *r)
{
    (void)r;
    send_chunked(s, 200, "application/octet-stream", src_big, NULL);
}
static void h_overreport(uint8_t s, HttpReq *r)
{
    (void)r;
    send_chunked(s, 200, "application/octet-stream", src_overreport, NULL);
}
static void h_with_hdr(uint8_t s, HttpReq *r)
{
    (void)r;
    proto_add_response_header(s, "X-Stream", "1");
    send_chunked(s, 200, "text/plain", src_hello, NULL);
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
        HttpConn.reset(HttpConn.internal);
    }
    Ws.init(Ws.internal);
    Sse.init(Sse.internal);
    tcp_capture_reset();
    mock_sndbuf_set(MOCK_SNDBUF_DEFAULT);
    g_log_status = 0;
    g_log_len = -1;
    g_step = 0;
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

void test_headers_announce_chunked_no_content_length()
{
    on_http("/c", HTTP_GET, h_hello);
    feed_and_handle(0, "GET /c HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Transfer-Encoding: chunked\r\n"));
    TEST_ASSERT_NULL(strstr(r, "Content-Length"));
}

void test_single_chunk_framing()
{
    on_http("/c", HTTP_GET, h_hello);
    feed_and_handle(0, "GET /c HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();

    TEST_ASSERT_NOT_NULL(strstr(r, "5\r\nhello\r\n0\r\n\r\n"));
}

void test_multiple_chunks_in_order()
{
    on_http("/c", HTTP_GET, h_multi);
    feed_and_handle(0, "GET /c HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "2\r\nab\r\n4\r\ncdef\r\n0\r\n\r\n"));
}

void test_printf_chunk()
{
    on_http("/c", HTTP_GET, h_printf);
    feed_and_handle(0, "GET /c HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "4\r\nx=42\r\n0\r\n\r\n"));
}

void test_single_piece_then_terminator()
{
    on_http("/c", HTTP_GET, h_ok);
    feed_and_handle(0, "GET /c HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();

    TEST_ASSERT_NOT_NULL(strstr(r, "2\r\nok\r\n0\r\n\r\n"));
}

void test_empty_body_is_just_terminator()
{
    on_http("/c", HTTP_GET, h_empty);
    feed_and_handle(0, "GET /c HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();
    const char *body = strstr(r, "\r\n\r\n");
    TEST_ASSERT_NOT_NULL(body);

    TEST_ASSERT_EQUAL_STRING("\r\n\r\n0\r\n\r\n", body);
}

void test_large_chunked_body_not_truncated()
{
    on_request_log(log_cb);
    on_http("/c", HTTP_GET, h_big);
    feed_and_handle(0, "GET /c HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();

    TEST_ASSERT_NOT_NULL(strstr(r, "0\r\n\r\n"));
    TEST_ASSERT_EQUAL_INT(200, g_log_status);
    TEST_ASSERT_EQUAL_INT(BIG_TOTAL, g_log_len);
}

void test_head_sends_headers_only()
{
    on_http("/c", HTTP_GET, h_hello);
    feed_and_handle(0, "HEAD /c HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "Transfer-Encoding: chunked\r\n"));
    TEST_ASSERT_NULL(strstr(r, "hello"));
    TEST_ASSERT_NULL(strstr(r, "0\r\n\r\n"));
}

void test_custom_header_injected_into_chunked()
{
    on_http("/c", HTTP_GET, h_with_hdr);
    feed_and_handle(0, "GET /c HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "X-Stream: 1\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(r, "5\r\nhello\r\n"));
}

void test_log_hook_reports_total_body_length()
{
    on_request_log(log_cb);
    on_http("/c", HTTP_GET, h_two5);
    feed_and_handle(0, "GET /c HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(200, g_log_status);
    TEST_ASSERT_EQUAL_INT(10, g_log_len);
}

void test_http10_falls_back_to_close_delimited()
{
    on_http("/c", HTTP_GET, h_multi);
    feed_and_handle(0, "GET /c HTTP/1.0\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
    TEST_ASSERT_NULL(strstr(r, "Transfer-Encoding"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Connection: close\r\n"));

    const char *body = strstr(r, "\r\n\r\n");
    TEST_ASSERT_NOT_NULL(body);
    TEST_ASSERT_EQUAL_STRING("\r\n\r\nabcdef", body);
}

void test_http10_large_body_not_truncated()
{
    on_request_log(log_cb);
    on_http("/c", HTTP_GET, h_big);
    feed_and_handle(0, "GET /c HTTP/1.0\r\n\r\n");
    TEST_ASSERT_NULL(strstr(tcp_captured(), "Transfer-Encoding"));
    TEST_ASSERT_EQUAL_INT(200, g_log_status);
    TEST_ASSERT_EQUAL_INT(BIG_TOTAL, g_log_len);
}

void test_chunked_backpressure_resumes_across_polls()
{
    on_http("/c", HTTP_GET, h_two5);
    mock_sndbuf_set(8);
    feed_and_handle(0, "GET /c HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "Transfer-Encoding: chunked\r\n"));
    TEST_ASSERT_NULL(strstr(r, "hello"));
    TEST_ASSERT_NULL(strstr(r, "0\r\n\r\n"));

    mock_sndbuf_set(MOCK_SNDBUF_DEFAULT);
    handle();
    r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "5\r\nhello\r\n5\r\nworld\r\n0\r\n\r\n"));
}

void test_chunked_source_overreport_clamped()
{
    on_request_log(log_cb);
    on_http("/c", HTTP_GET, h_overreport);
    feed_and_handle(0, "GET /c HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();

    char expect_sz[16];
    snprintf(expect_sz, sizeof(expect_sz), "%x\r\n", (unsigned)CHUNK_BUF_SIZE);
    TEST_ASSERT_NOT_NULL(strstr(r, expect_sz));
    TEST_ASSERT_NOT_NULL(strstr(r, "0\r\n\r\n"));
    TEST_ASSERT_EQUAL_INT(CHUNK_BUF_SIZE, g_log_len);
}

void test_hex_u32_size_line()
{
    char out[8];
    size_t nd = protocore_hex_u32(0, out);
    TEST_ASSERT_EQUAL_size_t(1, nd);
    TEST_ASSERT_EQUAL_HEX8('0', out[0]);

    const uint32_t vals[] = {1, 0xF, 0x10, 0x5A0, 0xFFFF, 0x12345, 0xFFFFFFFFu};
    for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
    {
        char ref[16];
        int rn = snprintf(ref, sizeof(ref), "%x", (unsigned)vals[i]);
        nd = protocore_hex_u32(vals[i], out);
        TEST_ASSERT_EQUAL_size_t((size_t)rn, nd);
        TEST_ASSERT_EQUAL_MEMORY(ref, out, nd);
    }
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_hex_u32_size_line);
    RUN_TEST(test_chunked_source_overreport_clamped);
    RUN_TEST(test_chunked_backpressure_resumes_across_polls);
    RUN_TEST(test_headers_announce_chunked_no_content_length);
    RUN_TEST(test_single_chunk_framing);
    RUN_TEST(test_multiple_chunks_in_order);
    RUN_TEST(test_printf_chunk);
    RUN_TEST(test_single_piece_then_terminator);
    RUN_TEST(test_empty_body_is_just_terminator);
    RUN_TEST(test_large_chunked_body_not_truncated);
    RUN_TEST(test_head_sends_headers_only);
    RUN_TEST(test_custom_header_injected_into_chunked);
    RUN_TEST(test_log_hook_reports_total_body_length);
    RUN_TEST(test_http10_falls_back_to_close_delimited);
    RUN_TEST(test_http10_large_body_not_truncated);
    return UNITY_END();
}
