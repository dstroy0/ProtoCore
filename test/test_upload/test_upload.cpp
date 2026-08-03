// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Streaming file upload (PC_ENABLE_UPLOAD): a POST body is streamed straight
// into an FS file via the parser's streaming-body hook. Built with
// BODY_BUF_SIZE=64 so a larger body exercises multi-chunk streaming; the mock FS
// captures the written bytes for verification.

#include "FS.h"
#include "network_drivers/application/upload_service/upload_service.h"
#include "protocore.h"
#include <stdio.h>
#include <string.h>
#include <unity.h>

static fs::FS g_fs;

static void push_bytes(uint8_t slot, const char *data, size_t n)
{
    TcpConn *c = &conn_pool[slot];
    for (size_t i = 0; i < n; i++)
    {
        c->rx_buffer[c->rx_head] = (uint8_t)data[i];
        c->rx_head = (c->rx_head + 1) % RX_BUF_SIZE;
    }
}

void setUp()
{
    pc_server_reset();
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = {};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = ConnState::CONN_ACTIVE;
        conn_pool[i].proto = ConnProto::PROTO_HTTP; // dispatch requires an explicit protocol
        conn_pool[i].pcb = &_mock_pcb;
        http_reset(i);
    }
    ws_init();
    pc_sse_init();
    fs::mock_fs_reset();
    fs::mock_fs_write_reset();
    tcp_capture_reset();
}

void tearDown()
{
    tcp_capture_disable();
}

void test_upload_streams_body_to_file()
{
    pc_upload_begin(server, "/upload", g_fs, "/dest.bin");

    // 200-byte body (> BODY_BUF_SIZE=64) -> several streamed chunks.
    char body[200];
    for (int i = 0; i < (int)sizeof(body); i++)
    {
        body[i] = (char)('A' + (i % 26));
    }
    size_t blen = sizeof(body);

    char req[400];
    int hn = snprintf(req, sizeof(req), "POST /upload HTTP/1.1\r\nContent-Length: %u\r\n\r\n", (unsigned)blen);
    push_bytes(0, req, (size_t)hn);
    push_bytes(0, body, blen);
    http_parse(0);
    handle();

    TEST_ASSERT_EQUAL_UINT(blen, fs::mock_fs_written());
    TEST_ASSERT_EQUAL_MEMORY(body, fs::mock_fs_wdata(), blen);
    TEST_ASSERT_EQUAL_UINT(blen, pc_upload_last_size());

    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "200 OK"));
    char expect[24];
    snprintf(expect, sizeof(expect), "%u bytes", (unsigned)blen);
    TEST_ASSERT_NOT_NULL(strstr(out, expect));
}

void test_small_body_single_chunk()
{
    pc_upload_begin(server, "/upload", g_fs, "/dest.bin");
    const char *body = "tiny";
    char req[128];
    int hn = snprintf(req, sizeof(req), "POST /upload HTTP/1.1\r\nContent-Length: 4\r\n\r\n%s", body);
    push_bytes(0, req, (size_t)hn);
    http_parse(0);
    handle();
    TEST_ASSERT_EQUAL_UINT(4, fs::mock_fs_written());
    TEST_ASSERT_EQUAL_MEMORY("tiny", fs::mock_fs_wdata(), 4);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
}

void test_empty_body_not_streamed()
{
    pc_upload_begin(server, "/upload", g_fs, "/dest.bin");
    char req[128];
    int hn = snprintf(req, sizeof(req), "POST /upload HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    push_bytes(0, req, (size_t)hn);
    http_parse(0);
    handle();
    // No body -> not streamed -> handler replies 400, nothing written.
    TEST_ASSERT_EQUAL_UINT(0, fs::mock_fs_written());
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "400"));
}

// A body request whose method is not POST: the stream-begin hook runs (there is a body) but rejects,
// so nothing is opened or written.
void test_non_post_body_rejected_by_begin()
{
    pc_upload_begin(server, "/upload", g_fs, "/dest.bin");
    char req[128];
    int hn = snprintf(req, sizeof(req), "PUT /upload HTTP/1.1\r\nContent-Length: 4\r\n\r\ndata");
    push_bytes(0, req, (size_t)hn);
    http_parse(0);
    handle();
    TEST_ASSERT_EQUAL_UINT(0, fs::mock_fs_written()); // begin rejected the non-POST -> no file write
}

// A POST with a body to a different path: the begin hook rejects on the path mismatch.
void test_wrong_path_rejected_by_begin()
{
    pc_upload_begin(server, "/upload", g_fs, "/dest.bin");
    char req[128];
    int hn = snprintf(req, sizeof(req), "POST /nope HTTP/1.1\r\nContent-Length: 4\r\n\r\ndata");
    push_bytes(0, req, (size_t)hn);
    http_parse(0);
    handle();
    TEST_ASSERT_EQUAL_UINT(0, fs::mock_fs_written());
}

// The destination file cannot be opened: begin flags an error, the body is still consumed, and the
// route handler replies 500.
void test_open_failure_replies_500()
{
    pc_upload_begin(server, "/upload", g_fs, "/dest.bin");
    fs::_mock_open_fail_path() = "/dest.bin"; // FS::open() returns an invalid File for this path
    char req[128];
    int hn = snprintf(req, sizeof(req), "POST /upload HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello");
    push_bytes(0, req, (size_t)hn);
    http_parse(0);
    handle();
    TEST_ASSERT_EQUAL_UINT(0, fs::mock_fs_written()); // open failed -> nothing written
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "500"));
    TEST_ASSERT_NOT_NULL(strstr(out, "upload failed"));
}

// No destination path configured: begin skips the open (fs && dest is false), so the upload reports 500.
void test_null_dest_replies_500()
{
    pc_upload_begin(server, "/upload", g_fs, nullptr);
    char req[128];
    int hn = snprintf(req, sizeof(req), "POST /upload HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello");
    push_bytes(0, req, (size_t)hn);
    http_parse(0);
    handle();
    TEST_ASSERT_EQUAL_UINT(0, fs::mock_fs_written());
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "500"));
}

// A short write mid-stream: the mock write-capture buffer is pre-filled so the first chunk write is
// short, which flags the error and makes the handler reply 500.
void test_write_failure_replies_500()
{
    pc_upload_begin(server, "/upload", g_fs, "/dest.bin");
    fs::_mock_wlen() = 8192 - 32; // only 32 bytes of write capacity left -> a 64-byte chunk is short

    char body[128];
    for (int i = 0; i < (int)sizeof(body); i++)
    {
        body[i] = (char)('A' + (i % 26));
    }
    char req[128];
    int hn = snprintf(req, sizeof(req), "POST /upload HTTP/1.1\r\nContent-Length: 128\r\n\r\n");
    push_bytes(0, req, (size_t)hn);
    push_bytes(0, body, sizeof(body));
    http_parse(0);
    handle();

    TEST_ASSERT_EQUAL_UINT(0, pc_upload_last_size()); // no full chunk landed before the short write
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "500"));
    TEST_ASSERT_NOT_NULL(strstr(out, "upload failed"));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_upload_streams_body_to_file);
    RUN_TEST(test_small_body_single_chunk);
    RUN_TEST(test_empty_body_not_streamed);
    RUN_TEST(test_non_post_body_rejected_by_begin);
    RUN_TEST(test_wrong_path_rejected_by_begin);
    RUN_TEST(test_open_failure_replies_500);
    RUN_TEST(test_null_dest_replies_500);
    RUN_TEST(test_write_failure_replies_500);
    return UNITY_END();
}
