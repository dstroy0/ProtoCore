// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "mnt_mock.h"
#include "network_drivers/transport/tcp/common.h"
#include "network_drivers/application/upload_service/upload_service.h"
#include "protocore.h"
#include "server/storage/mnt.h"
#include <stdio.h>
#include <string.h>

#include "network_drivers/transport/tcp/tcp.h"
#include "rx_feed.h"
#include <unity.h>

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
        http_reset(i);
    }
    ws_init();
    protocore_sse_init();
    mock_mnt_reset();
    protocore_mnt_mount(mock_mnt());
    mock_mnt_write_reset();
    tcp_capture_reset();
}

void tearDown()
{
    tcp_capture_disable();
}

void test_upload_streams_body_to_file()
{
    protocore_upload_begin("/upload", "/dest.bin");

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

    TEST_ASSERT_EQUAL_UINT(blen, mock_mnt_written());
    TEST_ASSERT_EQUAL_MEMORY(body, mock_mnt_wdata(), blen);
    TEST_ASSERT_EQUAL_UINT(blen, protocore_upload_last_size());

    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "200 OK"));
    char expect[24];
    snprintf(expect, sizeof(expect), "%u bytes", (unsigned)blen);
    TEST_ASSERT_NOT_NULL(strstr(out, expect));
}

void test_small_body_single_chunk()
{
    protocore_upload_begin("/upload", "/dest.bin");
    const char *body = "tiny";
    char req[128];
    int hn = snprintf(req, sizeof(req), "POST /upload HTTP/1.1\r\nContent-Length: 4\r\n\r\n%s", body);
    push_bytes(0, req, (size_t)hn);
    http_parse(0);
    handle();
    TEST_ASSERT_EQUAL_UINT(4, mock_mnt_written());
    TEST_ASSERT_EQUAL_MEMORY("tiny", mock_mnt_wdata(), 4);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
}

void test_empty_body_not_streamed()
{
    protocore_upload_begin("/upload", "/dest.bin");
    char req[128];
    int hn = snprintf(req, sizeof(req), "POST /upload HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    push_bytes(0, req, (size_t)hn);
    http_parse(0);
    handle();

    TEST_ASSERT_EQUAL_UINT(0, mock_mnt_written());
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "400"));
}

void test_non_post_body_rejected_by_begin()
{
    protocore_upload_begin("/upload", "/dest.bin");
    char req[128];
    int hn = snprintf(req, sizeof(req), "PUT /upload HTTP/1.1\r\nContent-Length: 4\r\n\r\ndata");
    push_bytes(0, req, (size_t)hn);
    http_parse(0);
    handle();
    TEST_ASSERT_EQUAL_UINT(0, mock_mnt_written());
}

void test_wrong_path_rejected_by_begin()
{
    protocore_upload_begin("/upload", "/dest.bin");
    char req[128];
    int hn = snprintf(req, sizeof(req), "POST /nope HTTP/1.1\r\nContent-Length: 4\r\n\r\ndata");
    push_bytes(0, req, (size_t)hn);
    http_parse(0);
    handle();
    TEST_ASSERT_EQUAL_UINT(0, mock_mnt_written());
}

void test_open_failure_replies_500()
{
    protocore_upload_begin("/upload", "/dest.bin");
    mock_mnt_fail_open("/dest.bin");
    char req[128];
    int hn = snprintf(req, sizeof(req), "POST /upload HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello");
    push_bytes(0, req, (size_t)hn);
    http_parse(0);
    handle();
    TEST_ASSERT_EQUAL_UINT(0, mock_mnt_written());
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "500"));
    TEST_ASSERT_NOT_NULL(strstr(out, "upload failed"));
}

void test_null_dest_replies_500()
{
    protocore_upload_begin("/upload", NULL);
    char req[128];
    int hn = snprintf(req, sizeof(req), "POST /upload HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello");
    push_bytes(0, req, (size_t)hn);
    http_parse(0);
    handle();
    TEST_ASSERT_EQUAL_UINT(0, mock_mnt_written());
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "500"));
}

void test_write_failure_replies_500()
{
    protocore_upload_begin("/upload", "/dest.bin");
    mock_mnt_write_fill(8192 - 32);

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

    TEST_ASSERT_EQUAL_UINT(0, protocore_upload_last_size());
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
