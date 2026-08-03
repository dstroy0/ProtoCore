// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for send_chunked() / ChunkedResponse streaming responses.

#include "protocore.h"
#include "shared_primitives/hex.h" // pc_hex_u32 (the chunk size-line writer)
#include <stdio.h>
#include <string.h>
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

// ---- Chunk sources (pull generators) ---------------------------------------
// Each returns the next body piece (one chunk per call) and 0 to end. A single
// global cursor is enough here: tests are sequential on slot 0, reset in setUp().

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
    return 0; // empty body: just the terminating chunk
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
// Large body: emit BIG_TOTAL bytes of a position-dependent pattern, cap bytes per
// call, to exercise paging a body far past one send window without truncation.
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

// Misbehaving source: writes exactly cap bytes but over-reports the count. The pump
// must clamp to cap (never frame or advance past the window it granted) so a buggy
// generator cannot overrun the send buffer.
static size_t src_overreport(uint8_t *buf, size_t cap, void *ctx)
{
    (void)ctx;
    if (g_step++ == 0)
    {
        memset(buf, 'Z', cap);
        return cap + 100; // lie: claim 100 more than actually written
    }
    return 0;
}

// ---- Handlers --------------------------------------------------------------

static void h_hello(uint8_t s, HttpReq *r)
{
    (void)r;
    send_chunked(s, 200, "text/plain", src_hello);
}
static void h_multi(uint8_t s, HttpReq *r)
{
    (void)r;
    send_chunked(s, 200, "text/plain", src_multi);
}
static void h_printf(uint8_t s, HttpReq *r)
{
    (void)r;
    send_chunked(s, 200, "text/plain", src_printf);
}
static void h_ok(uint8_t s, HttpReq *r)
{
    (void)r;
    send_chunked(s, 200, "text/plain", src_ok);
}
static void h_empty(uint8_t s, HttpReq *r)
{
    (void)r;
    send_chunked(s, 200, "text/plain", src_empty);
}
static void h_two5(uint8_t s, HttpReq *r)
{
    (void)r;
    send_chunked(s, 200, "text/plain", src_two5);
}
static void h_big(uint8_t s, HttpReq *r)
{
    (void)r;
    send_chunked(s, 200, "application/octet-stream", src_big);
}
static void h_overreport(uint8_t s, HttpReq *r)
{
    (void)r;
    send_chunked(s, 200, "application/octet-stream", src_overreport);
}
static void h_with_hdr(uint8_t s, HttpReq *r)
{
    (void)r;
    proto_add_response_header(s, "X-Stream", "1");
    send_chunked(s, 200, "text/plain", src_hello);
}

void setUp()
{
    pc_server_reset();
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = {};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].proto = PROTO_HTTP; // dispatch requires an explicit protocol
        conn_pool[i].pcb = &_mock_pcb;
        http_reset(i);
    }
    ws_init();
    pc_sse_init();
    tcp_capture_reset();
    mock_sndbuf() = MOCK_SNDBUF_DEFAULT; // reopen the window a backpressure test may have shrunk
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
    http_parse(slot);
    handle();
}

// ====================================================================
// TESTS
// ====================================================================

void test_headers_announce_chunked_no_content_length()
{
    on_http("/c", HTTP_GET, h_hello);
    feed_and_handle(0, "GET /c HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Transfer-Encoding: chunked\r\n"));
    TEST_ASSERT_NULL(strstr(r, "Content-Length")); // mutually exclusive with chunked
}

void test_single_chunk_framing()
{
    on_http("/c", HTTP_GET, h_hello);
    feed_and_handle(0, "GET /c HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();
    // "hello" = 5 bytes -> "5\r\nhello\r\n" then the terminating "0\r\n\r\n".
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
    // One piece "ok" then the terminator; nothing extra.
    TEST_ASSERT_NOT_NULL(strstr(r, "2\r\nok\r\n0\r\n\r\n"));
}

void test_empty_body_is_just_terminator()
{
    on_http("/c", HTTP_GET, h_empty);
    feed_and_handle(0, "GET /c HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();
    const char *body = strstr(r, "\r\n\r\n"); // end of headers
    TEST_ASSERT_NOT_NULL(body);
    // A source that returns 0 immediately yields only the terminating chunk.
    TEST_ASSERT_EQUAL_STRING("\r\n\r\n0\r\n\r\n", body);
}

void test_large_chunked_body_not_truncated()
{
    on_request_log(log_cb);
    on_http("/c", HTTP_GET, h_big);
    feed_and_handle(0, "GET /c HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();
    // The whole 16000-byte body must page out (paced across the window) and finish
    // with the terminator; the log hook reports the full body length, no truncation.
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
    TEST_ASSERT_NULL(strstr(r, "hello"));     // no body
    TEST_ASSERT_NULL(strstr(r, "0\r\n\r\n")); // no terminating chunk
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
    TEST_ASSERT_EQUAL_INT(10, g_log_len); // "hello" + "world", framing excluded
}

// RFC 7230 3.3.1: chunked must not be sent to an HTTP/1.0 client. The same
// streaming handler falls back to a close-delimited body: no Transfer-Encoding,
// Connection: close, and the raw bytes with no chunk framing or terminator.
void test_http10_falls_back_to_close_delimited()
{
    on_http("/c", HTTP_GET, h_multi);
    feed_and_handle(0, "GET /c HTTP/1.0\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
    TEST_ASSERT_NULL(strstr(r, "Transfer-Encoding")); // never chunked to a 1.0 client
    TEST_ASSERT_NOT_NULL(strstr(r, "Connection: close\r\n"));
    // Body bytes verbatim, no "2\r\n.."/"0\r\n\r\n" framing.
    const char *body = strstr(r, "\r\n\r\n");
    TEST_ASSERT_NOT_NULL(body);
    TEST_ASSERT_EQUAL_STRING("\r\n\r\nabcdef", body);
}

// The large-body pager must also page a close-delimited HTTP/1.0 stream in full.
void test_http10_large_body_not_truncated()
{
    on_request_log(log_cb);
    on_http("/c", HTTP_GET, h_big);
    feed_and_handle(0, "GET /c HTTP/1.0\r\n\r\n");
    TEST_ASSERT_NULL(strstr(tcp_captured(), "Transfer-Encoding"));
    TEST_ASSERT_EQUAL_INT(200, g_log_status);
    TEST_ASSERT_EQUAL_INT(BIG_TOTAL, g_log_len); // full body paged out, none dropped
}

// A send window too small for even a framed chunk (avail <= FRAME) forces the pager
// onto its backpressure path: chunk_send_pump flushes the queued headers and returns
// with the response still active, having emitted no body. A later worker poll - after
// the window reopens - resumes the in-flight response through http_poll_slot() and
// finishes the body + terminator. This is the multi-loop resume a large streamed
// response depends on under real flow control.
void test_chunked_backpressure_resumes_across_polls()
{
    on_http("/c", HTTP_GET, h_two5);
    mock_sndbuf() = 8; // below the 12-byte chunk framing reserve: no room for a useful chunk
    feed_and_handle(0, "GET /c HTTP/1.1\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "Transfer-Encoding: chunked\r\n")); // headers went out
    TEST_ASSERT_NULL(strstr(r, "hello"));                              // body deferred
    TEST_ASSERT_NULL(strstr(r, "0\r\n\r\n"));                          // no terminating chunk yet

    mock_sndbuf() = MOCK_SNDBUF_DEFAULT; // window reopens
    handle();                            // worker poll resumes the in-flight chunked response
    r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "5\r\nhello\r\n5\r\nworld\r\n0\r\n\r\n")); // full body + terminator
}

// A source that over-reports its byte count must not make the pump frame or send more
// than the window cap it was given: the returned length is clamped to cap, so exactly
// CHUNK_BUF_SIZE bytes are framed and the logged body length is the clamped value.
void test_chunked_source_overreport_clamped()
{
    on_request_log(log_cb);
    on_http("/c", HTTP_GET, h_overreport);
    feed_and_handle(0, "GET /c HTTP/1.1\r\n\r\n"); // default window: cap == CHUNK_BUF_SIZE
    const char *r = tcp_captured();
    // CHUNK_BUF_SIZE bytes framed, then the terminator - the over-report is dropped.
    char expect_sz[16];
    snprintf(expect_sz, sizeof(expect_sz), "%x\r\n", (unsigned)CHUNK_BUF_SIZE);
    TEST_ASSERT_NOT_NULL(strstr(r, expect_sz));
    TEST_ASSERT_NOT_NULL(strstr(r, "0\r\n\r\n"));
    TEST_ASSERT_EQUAL_INT(CHUNK_BUF_SIZE, g_log_len); // body length is the clamped count
}

// pc_hex_u32 (the chunk size-line writer that replaced snprintf("%x") in chunk_send_pump): the digits
// must match snprintf("%x") across the range, and 0 must write "0" (the branch the chunked path itself
// never hits - its terminating chunk is a literal "0\r\n\r\n").
void test_hex_u32_size_line()
{
    char out[8];
    size_t nd = pc_hex_u32(0, out);
    TEST_ASSERT_EQUAL_size_t(1, nd);
    TEST_ASSERT_EQUAL_HEX8('0', out[0]);

    const uint32_t vals[] = {1, 0xF, 0x10, 0x5A0 /* CHUNK_BUF_SIZE */, 0xFFFF, 0x12345, 0xFFFFFFFFu};
    for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
    {
        char ref[16];
        int rn = snprintf(ref, sizeof(ref), "%x", (unsigned)vals[i]);
        nd = pc_hex_u32(vals[i], out);
        TEST_ASSERT_EQUAL_size_t((size_t)rn, nd);
        TEST_ASSERT_EQUAL_MEMORY(ref, out, nd); // most-significant nibble first, matches %x
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
