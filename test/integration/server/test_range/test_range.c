// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// HTTP Range requests / 206 Partial Content (PC_ENABLE_RANGE). Each test
// serves a known 20-byte file via serve_file() with a Range header and checks
// the status line, Content-Range / Content-Length headers, and the exact bytes
// returned (via the tcp_write capture mock).

#include "lfs_mock.h"
#include "protocore.h"
#include <string.h>

#include "network_drivers/transport/tcp.h"
#include "rx_feed.h"
#include <unity.h>

static const char FILE_DATA[] = "0123456789ABCDEFGHIJ"; // 20 bytes

static void serve_data(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    serve_file(slot_id, lfsm(), "/data.bin", "application/octet-stream");
}

// Nulls the slot's pcb before serving, simulating a peer that vanished between accept
// and the handler running: serve_file must bail without sending.
static void serve_data_conn_gone(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    conn_pool[slot_id].pcb = NULL;
    serve_file(slot_id, lfsm(), "/data.bin", "application/octet-stream");
}

void setUp()
{
    pc_server_reset();
    on_http("/data", HTTP_GET, serve_data);
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
    lfsm_format();
    pc_mnt_mount(lfsm()); // the app hands us its filesystem; mnt is what everything reaches it through
    TEST_ASSERT_TRUE(lfsm_write_text("/data.bin", FILE_DATA)); // the 20 bytes every test serves
    tcp_capture_reset();
    mock_sndbuf_set(MOCK_SNDBUF_DEFAULT); // reopen the window a backpressure test may have shrunk
}

void tearDown()
{
    tcp_capture_disable();
}

// Return a pointer to the response body (after the header terminator), or nullptr.
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

// ---------------------------------------------------------------------------

void test_no_range_full_200()
{
    request(NULL);
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Accept-Ranges: bytes"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Length: 20"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Connection: keep-alive")); // HTTP/1.1 default is now persistent (keep-alive on)
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
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK")); // malformed -> full response
    TEST_ASSERT_EQUAL_UINT(20, body_len());
}

void test_multirange_falls_back_to_200()
{
    request("bytes=0-1,5-6");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK")); // multi-range unsupported -> full 200
    TEST_ASSERT_NULL(strstr(r, "206"));
    TEST_ASSERT_EQUAL_UINT(20, body_len());
}

// A start that overflows size_t must not wrap to a small in-range value -> 416, never
// a corrupt 206. (Saturating accumulator in parse_byte_range.)
void test_range_overflow_start_unsatisfiable()
{
    request("bytes=99999999999999999999999-");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "416 Range Not Satisfiable"));
    TEST_ASSERT_NULL(strstr(r, "206"));
}

// An overflowing end clamps to the last byte (full 206), not a wrapped window.
void test_range_overflow_end_clamps()
{
    request("bytes=0-99999999999999999999999");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "206 Partial Content"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Range: bytes 0-19/20"));
}

// Suffix "bytes=-0" requests the last zero bytes -> unsatisfiable (416).
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
    TEST_ASSERT_EQUAL_UINT(0, body_len()); // HEAD: headers only
}

// A zero send window forces file_send_pump onto its backpressure path: it flushes the
// queued headers and returns with the response still active, having sent no body. The
// next worker poll - after the window reopens - resumes the transfer through
// http_poll_slot()'s in-flight-file branch and pages the body out. Reopening to a window
// smaller than the remaining body also exercises the per-loop clamp (want = avail), so
// the 20-byte file pages out in several bounded reads without truncation - the
// paging-across-loops path a real file transfer under flow control relies on.
void test_file_send_backpressure_resumes_across_polls()
{
    mock_sndbuf_set(0); // window shut: no room to page any body this loop
    request(NULL);
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Length: 20")); // headers went out
    TEST_ASSERT_EQUAL_UINT(0, body_len());                 // body deferred: nothing after the header terminator

    mock_sndbuf_set(8); // window reopens, but narrower than the remaining 20 bytes
    handle();           // worker poll resumes and pages the body in 8-byte-capped reads
    TEST_ASSERT_EQUAL_UINT(20, body_len());
    TEST_ASSERT_EQUAL_MEMORY(FILE_DATA, body_ptr(), 20); // full body, no truncation
}

// The first body write fails (send buffer full): file_send_pump seeks back to un-read
// those bytes, flushes, and returns with the response still active. A later worker poll
// retries and, with the send buffer recovered, pages the whole body out - exactly the
// file bytes, none lost or duplicated by the seek-back.
void test_file_send_write_fails_then_retries()
{
    mock_send_fail_after(1); // header write succeeds; the next (first body) write fails
    request(NULL);
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Length: 20")); // header queued
    TEST_ASSERT_EQUAL_UINT(0, body_len());                 // body write failed: nothing after the header

    mock_send_fail_after(-1); // send buffer recovers
    handle();                 // worker poll retries the in-flight file response
    TEST_ASSERT_EQUAL_UINT(20, body_len());
    TEST_ASSERT_EQUAL_MEMORY(FILE_DATA, body_ptr(), 20); // exactly the file, no dup/loss
}

// A file that stats as 20 bytes but reads short (truncated / I/O error mid-body):
// read() returns 0 while bytes remain, so the pump stops with whatever paged out and
// finishes the response - it must not spin on the zero-length read.
void test_file_send_short_read_stops()
{
    lfsm_read_budget(8); // the medium yields only 8 of the file's 20 bytes
    request(NULL);
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(r, "Content-Length: 20")); // header still advertises the stat size
    TEST_ASSERT_EQUAL_UINT(8, body_len());                 // only the bytes that actually read out
    TEST_ASSERT_EQUAL_MEMORY(FILE_DATA, body_ptr(), 8);
}

// A Range header with a valid spec followed by trailing garbage is ignored (full 200),
// not misparsed: the parser skips trailing spaces then rejects any leftover byte.
void test_range_trailing_garbage_ignored()
{
    request("bytes=0-3 x"); // valid spec, then a stray token past a space
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK")); // header ignored -> full response
    TEST_ASSERT_NULL(strstr(r, "206"));
    TEST_ASSERT_EQUAL_UINT(20, body_len());
}

// A reversed range (start > end, both in bounds) is unsatisfiable -> 416.
void test_range_start_after_end_unsatisfiable()
{
    request("bytes=5-2");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "416 Range Not Satisfiable"));
    TEST_ASSERT_NULL(strstr(r, "206"));
}

// A suffix range against a zero-length file is unsatisfiable -> 416 (never a wrapped
// window from size - end underflow).
void test_range_suffix_on_empty_file()
{
    TEST_ASSERT_TRUE(lfsm_write_text("/data.bin", "")); // truncate to 0 bytes
    request("bytes=-4");
    const char *r = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(r, "416 Range Not Satisfiable"));
    TEST_ASSERT_NULL(strstr(r, "206"));
}

// The connection dropped after the file opened: serve_file resets the slot and sends
// nothing (no partial/garbage response to a gone peer).
void test_serve_file_connection_gone()
{
    on_http("/gone", HTTP_GET, serve_data_conn_gone);
    push_str(0, "GET /gone HTTP/1.1\r\n\r\n");
    http_parse(0);
    handle();
    TEST_ASSERT_EQUAL_UINT(0, tcp_captured_len()); // nothing queued
    TEST_ASSERT_NULL(strstr(tcp_captured(), "200 OK"));
}

// A 416 is a full response in its own right, so it carries the configured CORS block
// alongside the required "Content-Range: bytes * /<size>" (RFC 7233 4.4).
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
