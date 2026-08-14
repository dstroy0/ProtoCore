// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "lfs_mock.h"
#include "network_drivers/transport/tcp/common.h"
#include "protocore.h"
#include <string.h>

#include "network_drivers/transport/tcp/tcp.h"
#include "rx_feed.h"
#include <unity.h>

static const char FILE_DATA[] = "0123456789ABCDEFGHIJ";

static void serve_data(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    serve_file(slot_id, lfsm(), "/data.bin", "application/octet-stream");
}

static void serve_data_conn_gone(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    conn_pool[slot_id].pcb = NULL;
    serve_file(slot_id, lfsm(), "/data.bin", "application/octet-stream");
}

void setUp()
{
    protocore_server_reset();
    on_http("/data", HTTP_GET, serve_data);
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].proto = PROTO_HTTP;
        conn_pool[i].pcb = protocore_net_host_pcb();
        http_reset(i);
    }
    ws_init();
    protocore_sse_init();
    lfsm_format();
    protocore_mnt_mount(lfsm());
    TEST_ASSERT_TRUE(lfsm_write_text("/data.bin", FILE_DATA));
    tcp_capture_reset();
    mock_sndbuf_set(MOCK_SNDBUF_DEFAULT);
}

void tearDown()
{
    tcp_capture_disable();
}

static const char *body_ptr()
{
    const char *sep = strstr(tcp_captured(), "\r\n\r\n");
    return sep ? sep + 4 : NULL;
}

static size_t body_len()
{
    const char *b = body_ptr();
    if (!b)
    {
        return 0;
    }
    return tcp_captured_len() - (size_t)(b - tcp_captured());
}

static void request(const char *range_hdr)
{
    char req[128];
    if (range_hdr)
    {
        snprintf(req, sizeof(req), "GET /data HTTP/1.1\r\nRange: %s\r\n\r\n", range_hdr);
    }
    else
    {
        snprintf(req, sizeof(req), "GET /data HTTP/1.1\r\n\r\n");
    }
    push_str(0, req);
    http_parse(0);
    handle();
}

void test_no_range_full_200()
{
    request(NULL);
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Accept-Ranges: bytes"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Length: 20"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Connection: keep-alive"));
    TEST_ASSERT_NULL(strstr(r, "Content-Range"));
    TEST_ASSERT_EQUAL_UINT(20, body_len());
    TEST_ASSERT_EQUAL_MEMORY(FILE_DATA, body_ptr(), 20);
}

void test_range_prefix()
{
    request("bytes=0-3");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "206 Partial Content"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Range: bytes 0-3/20"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Length: 4"));
    TEST_ASSERT_EQUAL_UINT(4, body_len());
    TEST_ASSERT_EQUAL_MEMORY("0123", body_ptr(), 4);
}

void test_range_open_ended()
{
    request("bytes=5-");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "206 Partial Content"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Range: bytes 5-19/20"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Length: 15"));
    TEST_ASSERT_EQUAL_UINT(15, body_len());
    TEST_ASSERT_EQUAL_MEMORY("56789ABCDEFGHIJ", body_ptr(), 15);
}

void test_range_suffix()
{
    request("bytes=-4");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "206 Partial Content"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Range: bytes 16-19/20"));
    TEST_ASSERT_EQUAL_UINT(4, body_len());
    TEST_ASSERT_EQUAL_MEMORY("GHIJ", body_ptr(), 4);
}

void test_range_single_byte()
{
    request("bytes=2-2");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Content-Range: bytes 2-2/20"));
    TEST_ASSERT_EQUAL_UINT(1, body_len());
    TEST_ASSERT_EQUAL_MEMORY("2", body_ptr(), 1);
}

void test_range_clamped_to_eof()
{
    request("bytes=10-999");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "206 Partial Content"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Content-Range: bytes 10-19/20"));
    TEST_ASSERT_EQUAL_UINT(10, body_len());
    TEST_ASSERT_EQUAL_MEMORY("ABCDEFGHIJ", body_ptr(), 10);
}

void test_range_unsatisfiable_416()
{
    request("bytes=100-200");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "416 Range Not Satisfiable"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Range: bytes */20"));
    TEST_ASSERT_NULL(strstr(r, "206"));
}

void test_malformed_range_ignored()
{
    request("bytes=abc");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
    TEST_ASSERT_EQUAL_UINT(20, body_len());
}

void test_multirange_falls_back_to_200()
{
    request("bytes=0-1,5-6");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
    TEST_ASSERT_NULL(strstr(r, "206"));
    TEST_ASSERT_EQUAL_UINT(20, body_len());
}

void test_range_overflow_start_unsatisfiable()
{
    request("bytes=99999999999999999999999-");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "416 Range Not Satisfiable"));
    TEST_ASSERT_NULL(strstr(r, "206"));
}

void test_range_overflow_end_clamps()
{
    request("bytes=0-99999999999999999999999");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "206 Partial Content"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Range: bytes 0-19/20"));
}

void test_range_suffix_zero_unsatisfiable()
{
    request("bytes=-0");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "416 Range Not Satisfiable"));
    TEST_ASSERT_NULL(strstr(r, "206"));
}

void test_head_with_range_no_body()
{
    push_str(0, "HEAD /data HTTP/1.1\r\nRange: bytes=0-3\r\n\r\n");
    http_parse(0);
    handle();
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "206 Partial Content"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Range: bytes 0-3/20"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Length: 4"));
    TEST_ASSERT_EQUAL_UINT(0, body_len());
}

void test_file_send_backpressure_resumes_across_polls()
{
    mock_sndbuf_set(0);
    request(NULL);
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Length: 20"));
    TEST_ASSERT_EQUAL_UINT(0, body_len());

    mock_sndbuf_set(8);
    handle();
    TEST_ASSERT_EQUAL_UINT(20, body_len());
    TEST_ASSERT_EQUAL_MEMORY(FILE_DATA, body_ptr(), 20);
}

void test_file_send_write_fails_then_retries()
{
    mock_send_fail_after(1);
    request(NULL);
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Length: 20"));
    TEST_ASSERT_EQUAL_UINT(0, body_len());

    mock_send_fail_after(-1);
    handle();
    TEST_ASSERT_EQUAL_UINT(20, body_len());
    TEST_ASSERT_EQUAL_MEMORY(FILE_DATA, body_ptr(), 20);
}

void test_file_send_short_read_stops()
{
    lfsm_read_budget(8);
    request(NULL);
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Length: 20"));
    TEST_ASSERT_EQUAL_UINT(8, body_len());
    TEST_ASSERT_EQUAL_MEMORY(FILE_DATA, body_ptr(), 8);
}

void test_range_trailing_garbage_ignored()
{
    request("bytes=0-3 x");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
    TEST_ASSERT_NULL(strstr(r, "206"));
    TEST_ASSERT_EQUAL_UINT(20, body_len());
}

void test_range_start_after_end_unsatisfiable()
{
    request("bytes=5-2");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "416 Range Not Satisfiable"));
    TEST_ASSERT_NULL(strstr(r, "206"));
}

void test_range_suffix_on_empty_file()
{
    TEST_ASSERT_TRUE(lfsm_write_text("/data.bin", ""));
    request("bytes=-4");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "416 Range Not Satisfiable"));
    TEST_ASSERT_NULL(strstr(r, "206"));
}

void test_serve_file_connection_gone()
{
    on_http("/gone", HTTP_GET, serve_data_conn_gone);
    push_str(0, "GET /gone HTTP/1.1\r\n\r\n");
    http_parse(0);
    handle();
    TEST_ASSERT_EQUAL_UINT(0, tcp_captured_len());
    TEST_ASSERT_NULL(strstr(tcp_captured(), "200 OK"));
}

void test_unsatisfiable_range_416_carries_cors()
{
    set_cors("*");
    push_str(0, "GET /data HTTP/1.1\r\nHost: x\r\nRange: bytes=100-200\r\n\r\n");
    http_parse(0);
    handle();
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "416 Range Not Satisfiable"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Content-Range: bytes */20\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Access-Control-Allow-Origin: *\r\n"));
    set_cors("");
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_unsatisfiable_range_416_carries_cors);
    RUN_TEST(test_file_send_backpressure_resumes_across_polls);
    RUN_TEST(test_file_send_write_fails_then_retries);
    RUN_TEST(test_file_send_short_read_stops);
    RUN_TEST(test_range_trailing_garbage_ignored);
    RUN_TEST(test_range_start_after_end_unsatisfiable);
    RUN_TEST(test_range_suffix_on_empty_file);
    RUN_TEST(test_serve_file_connection_gone);
    RUN_TEST(test_no_range_full_200);
    RUN_TEST(test_range_prefix);
    RUN_TEST(test_range_open_ended);
    RUN_TEST(test_range_suffix);
    RUN_TEST(test_range_single_byte);
    RUN_TEST(test_range_clamped_to_eof);
    RUN_TEST(test_range_unsatisfiable_416);
    RUN_TEST(test_malformed_range_ignored);
    RUN_TEST(test_range_overflow_start_unsatisfiable);
    RUN_TEST(test_range_overflow_end_clamps);
    RUN_TEST(test_range_suffix_zero_unsatisfiable);
    RUN_TEST(test_multirange_falls_back_to_200);
    RUN_TEST(test_head_with_range_no_body);
    return UNITY_END();
}
