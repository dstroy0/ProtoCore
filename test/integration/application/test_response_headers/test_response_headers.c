// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for custom response headers and cookies:
//   proto_add_response_header(), set_cookie(, NULL), clear_response_headers().
//
// Tests verify that:
//   - A queued header appears in a send() response
//   - Multiple headers all appear
//   - set_cookie(, NULL) emits Set-Cookie (with and without attributes)
//   - Custom headers appear on send_empty() and redirect() too
//   - Headers do NOT leak from one request to the next on the same slot
//   - clear_response_headers() discards queued headers
//   - An oversized header is dropped whole (no malformed half-line)

#include "network_drivers/application/ntp_service/ntp_service.h" // pc_ntp_set_test_epoch() for the Date-header tests
#include "protocore.h"
#include <stdio.h>
#include <string.h>

#include <unity.h>
#include "network_drivers/transport/tcp.h"
#include "rx_feed.h"


// Handlers exercising the various response paths -----------------------------

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
    send_text(slot, 200, "text/plain", "ok"); // no custom headers
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
    proto_add_response_header(slot, "X-Big", big); // must be dropped whole
    proto_add_response_header(slot, "X-Small", "ok");
    send_text(slot, 200, "text/plain", "ok");
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
    pc_ntp_set_test_epoch(0); // clockless by default; Date tests opt in
}

void tearDown()
{
    tcp_capture_disable();
    pc_ntp_set_test_epoch(0);
}

static void feed_and_handle(uint8_t slot, const char *req_str)
{
    push_str(slot, req_str);
    http_parse(slot);
    handle();
}

// ====================================================================
// TESTS
// ====================================================================

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

    // First request queues X-Custom on slot 0.
    feed_and_handle(0, "GET /h HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "X-Custom: hello\r\n"));

    // Reuse the same slot for a plain handler; the header must be gone.
    conn_pool[0] = (TcpConn){0};
    conn_pool[0].id = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].proto = PROTO_HTTP; // dispatch requires an explicit protocol
    conn_pool[0].pcb = pc_net_host_pcb();
    http_reset(0);
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
    // The oversized header name must not appear...
    TEST_ASSERT_NULL(strstr(tcp_captured(), "X-Big"));
    // ...and the subsequent small header must still be emitted intact.
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "X-Small: ok\r\n"));
}

// PC_HTTP_EMIT_DATE: with a valid wall-clock time, every response carries the
// RFC 7231 IMF-fixdate Date header (epoch 784111777 = the RFC's example date).
void test_date_header_emitted_when_time_set()
{
    pc_ntp_set_test_epoch(784111777); // Sun, 06 Nov 1994 08:49:37 GMT
    on_http("/h", HTTP_GET, h_plain);
    feed_and_handle(0, "GET /h HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n"));
}

// Clock-less (no time source / NTP unsynced): the Date header is omitted rather
// than emitting a wrong date (RFC 7231 7.1.1.2).
void test_date_header_omitted_when_clockless()
{
    pc_ntp_set_test_epoch(0);
    on_http("/h", HTTP_GET, h_plain);
    feed_and_handle(0, "GET /h HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NULL(strstr(tcp_captured(), "Date:"));
}

void test_ntp_host_seam_accessors()
{
    // Host build: begin() is a no-op returning false; synced()/epoch() reflect the injected epoch.
    TEST_ASSERT_FALSE(pc_ntp_begin("UTC0", "a.pool.ntp.org", "b.pool.ntp.org"));
    pc_ntp_set_test_epoch(0);
    TEST_ASSERT_FALSE(pc_ntp_synced());
    TEST_ASSERT_EQUAL_INT(0, (long)pc_ntp_epoch());
    TEST_ASSERT_EQUAL_UINT32(0, pc_ntp_time_source()); // registry adapter: 0 when unsynced
    pc_ntp_set_test_epoch(784111777);
    TEST_ASSERT_TRUE(pc_ntp_synced());
    TEST_ASSERT_EQUAL_INT(784111777, (long)pc_ntp_epoch());
    TEST_ASSERT_EQUAL_UINT32(784111777, pc_ntp_time_source()); // registry adapter mirrors the epoch
    // http_date guards: null out / zero cap both return 0 without writing.
    char buf[40];
    TEST_ASSERT_EQUAL_UINT(0, pc_ntp_http_date(NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT(0, pc_ntp_http_date(buf, 0));
    // Valid IMF-fixdate for the injected epoch.
    TEST_ASSERT_TRUE(pc_ntp_http_date(buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("Sun, 06 Nov 1994 08:49:37 GMT", buf);
    // A pathologically large epoch overflows the broken-down year, so gmtime_r fails and http_date
    // fails closed (empty string, length 0). glibc returns EOVERFLOW here; the host test runs on glibc.
    pc_ntp_set_test_epoch((time_t)100000000000000000LL);
    buf[0] = 'x';
    TEST_ASSERT_EQUAL_UINT(0, pc_ntp_http_date(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_CHAR('\0', buf[0]);
    pc_ntp_set_test_epoch(0); // restore the clockless default for the other tests
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
