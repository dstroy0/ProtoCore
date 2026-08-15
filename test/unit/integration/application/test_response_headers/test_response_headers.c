// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/application/ntp_service/ntp_service.h"
#include "network_drivers/transport/tcp/common.h"
#include "protocore.h"
#include <stdio.h>
#include <string.h>

#include "network_drivers/transport/tcp/tcp.h"
#include "rx_feed.h"
#include <unity.h>

static void h_one_header(uint8_t slot, HttpReq *req)
{
    (void)req;
    proto_add_response_header(slot, "X-Custom", "hello");
    send_text(slot, 200, "text/plain", "ok");
}

static void h_two_headers(uint8_t slot, HttpReq *req)
{
    (void)req;
    proto_add_response_header(slot, "X-One", "1");
    proto_add_response_header(slot, "X-Two", "2");
    send_text(slot, 200, "text/plain", "ok");
}

static void h_cookie(uint8_t slot, HttpReq *req)
{
    (void)req;
    set_cookie(slot, "session", "abc123", NULL);
    send_text(slot, 200, "text/plain", "ok");
}

static void h_cookie_attrs(uint8_t slot, HttpReq *req)
{
    (void)req;
    set_cookie(slot, "session", "abc123", "Path=/; HttpOnly; Max-Age=3600");
    send_text(slot, 200, "text/plain", "ok");
}

static void h_header_empty(uint8_t slot, HttpReq *req)
{
    (void)req;
    proto_add_response_header(slot, "X-Empty", "yes");
    send_empty(slot, 204);
}

static void h_header_redirect(uint8_t slot, HttpReq *req)
{
    (void)req;
    proto_add_response_header(slot, "X-Redir", "yes");
    redirect(slot, 302, "/elsewhere");
}

static void h_plain(uint8_t slot, HttpReq *req)
{
    (void)req;
    send_text(slot, 200, "text/plain", "ok");
}

static void h_clear(uint8_t slot, HttpReq *req)
{
    (void)req;
    proto_add_response_header(slot, "X-Gone", "1");
    clear_response_headers(slot);
    send_text(slot, 200, "text/plain", "ok");
}

static void h_oversized(uint8_t slot, HttpReq *req)
{
    (void)req;
    static char big[EXTRA_HDR_BUF_SIZE + 64];
    memset(big, 'A', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    proto_add_response_header(slot, "X-Big", big);
    proto_add_response_header(slot, "X-Small", "ok");
    send_text(slot, 200, "text/plain", "ok");
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
    protocore_ntp_set_test_epoch(0);
}

void tearDown()
{
    tcp_capture_disable();
    protocore_ntp_set_test_epoch(0);
}

static void feed_and_handle(uint8_t slot, const char *req_str)
{
    push_str(slot, req_str);
    HttpConn.slot = slot;
    HttpConn.parse(HttpConn.internal);
    handle();
}

void test_single_custom_header_present()
{
    on_http("/h", HTTP_GET, h_one_header);
    feed_and_handle(0, "GET /h HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "X-Custom: hello\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
}

void test_multiple_custom_headers_present()
{
    on_http("/h", HTTP_GET, h_two_headers);
    feed_and_handle(0, "GET /h HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "X-One: 1\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "X-Two: 2\r\n"));
}

void test_set_cookie_basic()
{
    on_http("/h", HTTP_GET, h_cookie);
    feed_and_handle(0, "GET /h HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Set-Cookie: session=abc123\r\n"));
}

void test_set_cookie_with_attrs()
{
    on_http("/h", HTTP_GET, h_cookie_attrs);
    feed_and_handle(0, "GET /h HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Set-Cookie: session=abc123; Path=/; HttpOnly; Max-Age=3600\r\n"));
}

void test_custom_header_on_send_empty()
{
    on_http("/h", HTTP_GET, h_header_empty);
    feed_and_handle(0, "GET /h HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "204"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "X-Empty: yes\r\n"));
}

void test_custom_header_on_redirect()
{
    on_http("/h", HTTP_GET, h_header_redirect);
    feed_and_handle(0, "GET /h HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Location: /elsewhere\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "X-Redir: yes\r\n"));
}

void test_headers_do_not_leak_across_requests()
{
    on_http("/h", HTTP_GET, h_one_header);
    on_http("/p", HTTP_GET, h_plain);

    feed_and_handle(0, "GET /h HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "X-Custom: hello\r\n"));

    conn_pool[0] = (TcpConn){0};
    conn_pool[0].id = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].proto = PROTO_HTTP;
    conn_pool[0].pcb = protocore_net_host_pcb();
    HttpConn.slot = 0;
    HttpConn.reset(HttpConn.internal);
    tcp_capture_reset();

    feed_and_handle(0, "GET /p HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NULL(strstr(tcp_captured(), "X-Custom"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
}

void test_clear_response_headers()
{
    on_http("/h", HTTP_GET, h_clear);
    feed_and_handle(0, "GET /h HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NULL(strstr(tcp_captured(), "X-Gone"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
}

void test_oversized_header_dropped_whole()
{
    on_http("/h", HTTP_GET, h_oversized);
    feed_and_handle(0, "GET /h HTTP/1.1\r\n\r\n");

    TEST_ASSERT_NULL(strstr(tcp_captured(), "X-Big"));

    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "X-Small: ok\r\n"));
}

void test_date_header_emitted_when_time_set()
{
    protocore_ntp_set_test_epoch(784111777);
    on_http("/h", HTTP_GET, h_plain);
    feed_and_handle(0, "GET /h HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n"));
}

void test_date_header_omitted_when_clockless()
{
    protocore_ntp_set_test_epoch(0);
    on_http("/h", HTTP_GET, h_plain);
    feed_and_handle(0, "GET /h HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NULL(strstr(tcp_captured(), "Date:"));
}

void test_ntp_host_seam_accessors()
{

    TEST_ASSERT_FALSE(protocore_ntp_begin("UTC0", "a.pool.ntp.org", "b.pool.ntp.org"));
    protocore_ntp_set_test_epoch(0);
    TEST_ASSERT_FALSE(protocore_ntp_synced());
    TEST_ASSERT_EQUAL_INT(0, (long)protocore_ntp_epoch());
    TEST_ASSERT_EQUAL_UINT32(0, protocore_ntp_time_source());
    protocore_ntp_set_test_epoch(784111777);
    TEST_ASSERT_TRUE(protocore_ntp_synced());
    TEST_ASSERT_EQUAL_INT(784111777, (long)protocore_ntp_epoch());
    TEST_ASSERT_EQUAL_UINT32(784111777, protocore_ntp_time_source());

    char buf[40];
    TEST_ASSERT_EQUAL_UINT(0, protocore_ntp_http_date(NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT(0, protocore_ntp_http_date(buf, 0));

    TEST_ASSERT_TRUE(protocore_ntp_http_date(buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("Sun, 06 Nov 1994 08:49:37 GMT", buf);

    protocore_ntp_set_test_epoch((time_t)100000000000000000LL);
    buf[0] = 'x';
    TEST_ASSERT_EQUAL_UINT(0, protocore_ntp_http_date(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_CHAR('\0', buf[0]);
    protocore_ntp_set_test_epoch(0);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_ntp_host_seam_accessors);
    RUN_TEST(test_date_header_emitted_when_time_set);
    RUN_TEST(test_date_header_omitted_when_clockless);
    RUN_TEST(test_single_custom_header_present);
    RUN_TEST(test_multiple_custom_headers_present);
    RUN_TEST(test_set_cookie_basic);
    RUN_TEST(test_set_cookie_with_attrs);
    RUN_TEST(test_custom_header_on_send_empty);
    RUN_TEST(test_custom_header_on_redirect);
    RUN_TEST(test_headers_do_not_leak_across_requests);
    RUN_TEST(test_clear_response_headers);
    RUN_TEST(test_oversized_header_dropped_whole);
    return UNITY_END();
}
