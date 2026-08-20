// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "mnt_mock.h"
#include "network_drivers/application/upload_service/upload_service.h"
#include "network_drivers/presentation/http/sse/sse.h"
#include "network_drivers/presentation/http/websocket/websocket.h"
#include "network_drivers/presentation/presentation.h"
#include "network_drivers/transport/tcp/common.h"
#include "protocore.h"
#include "server/storage/mnt/mnt.h"
#include <stdio.h>
#include <string.h>

#include "network_drivers/transport/tcp/tcp.h"
#include "rx_feed.h"
#include <unity.h>

static uint8_t mnt_work[16]; // the borrow an entry takes; Mnt never reads it

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
        HttpConn.slot = (uint8_t)i;
        HttpConn.reset(protocore_http_conn_span());
    }
    Ws.init(protocore_ws_span());
    Sse.init(protocore_sse_span());
    mock_mnt_reset();
    Mnt.args.backend = mock_mnt();
    Mnt.mount(mnt_work);
    mock_mnt_write_reset();
    tcp_capture_reset();
}

void tearDown()
{
    tcp_capture_disable();
}

void test_upload_streams_body_to_file()
{
    UploadServiceV.begin_args.path = "/upload";
    UploadServiceV.begin_args.dest_path = "/dest.bin";
    UploadService.begin(protocore_upload_service_span());

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
    HttpConn.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    handle();

    TEST_ASSERT_EQUAL_UINT(blen, mock_mnt_written());
    TEST_ASSERT_EQUAL_MEMORY(body, mock_mnt_wdata(), blen);
    UploadService.last_size(protocore_upload_service_span());
    TEST_ASSERT_EQUAL_UINT(blen, UploadServiceV.n);

    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "200 OK"));
    char expect[24];
    snprintf(expect, sizeof(expect), "%u bytes", (unsigned)blen);
    TEST_ASSERT_NOT_NULL(strstr(out, expect));
}

void test_small_body_single_chunk()
{
    UploadServiceV.begin_args.path = "/upload";
    UploadServiceV.begin_args.dest_path = "/dest.bin";
    UploadService.begin(protocore_upload_service_span());
    const char *body = "tiny";
    char req[128];
    int hn = snprintf(req, sizeof(req), "POST /upload HTTP/1.1\r\nContent-Length: 4\r\n\r\n%s", body);
    push_bytes(0, req, (size_t)hn);
    HttpConn.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    handle();
    TEST_ASSERT_EQUAL_UINT(4, mock_mnt_written());
    TEST_ASSERT_EQUAL_MEMORY("tiny", mock_mnt_wdata(), 4);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
}

void test_empty_body_not_streamed()
{
    UploadServiceV.begin_args.path = "/upload";
    UploadServiceV.begin_args.dest_path = "/dest.bin";
    UploadService.begin(protocore_upload_service_span());
    char req[128];
    int hn = snprintf(req, sizeof(req), "POST /upload HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    push_bytes(0, req, (size_t)hn);
    HttpConn.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    handle();

    TEST_ASSERT_EQUAL_UINT(0, mock_mnt_written());
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "400"));
}

void test_non_post_body_rejected_by_begin()
{
    UploadServiceV.begin_args.path = "/upload";
    UploadServiceV.begin_args.dest_path = "/dest.bin";
    UploadService.begin(protocore_upload_service_span());
    char req[128];
    int hn = snprintf(req, sizeof(req), "PUT /upload HTTP/1.1\r\nContent-Length: 4\r\n\r\ndata");
    push_bytes(0, req, (size_t)hn);
    HttpConn.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    handle();
    TEST_ASSERT_EQUAL_UINT(0, mock_mnt_written());
}

void test_wrong_path_rejected_by_begin()
{
    UploadServiceV.begin_args.path = "/upload";
    UploadServiceV.begin_args.dest_path = "/dest.bin";
    UploadService.begin(protocore_upload_service_span());
    char req[128];
    int hn = snprintf(req, sizeof(req), "POST /nope HTTP/1.1\r\nContent-Length: 4\r\n\r\ndata");
    push_bytes(0, req, (size_t)hn);
    HttpConn.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    handle();
    TEST_ASSERT_EQUAL_UINT(0, mock_mnt_written());
}

void test_open_failure_replies_500()
{
    UploadServiceV.begin_args.path = "/upload";
    UploadServiceV.begin_args.dest_path = "/dest.bin";
    UploadService.begin(protocore_upload_service_span());
    mock_mnt_fail_open("/dest.bin");
    char req[128];
    int hn = snprintf(req, sizeof(req), "POST /upload HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello");
    push_bytes(0, req, (size_t)hn);
    HttpConn.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    handle();
    TEST_ASSERT_EQUAL_UINT(0, mock_mnt_written());
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "500"));
    TEST_ASSERT_NOT_NULL(strstr(out, "upload failed"));
}

void test_null_dest_replies_500()
{
    UploadServiceV.begin_args.path = "/upload";
    UploadServiceV.begin_args.dest_path = NULL;
    UploadService.begin(protocore_upload_service_span());
    char req[128];
    int hn = snprintf(req, sizeof(req), "POST /upload HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello");
    push_bytes(0, req, (size_t)hn);
    HttpConn.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    handle();
    TEST_ASSERT_EQUAL_UINT(0, mock_mnt_written());
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "500"));
}

void test_write_failure_replies_500()
{
    UploadServiceV.begin_args.path = "/upload";
    UploadServiceV.begin_args.dest_path = "/dest.bin";
    UploadService.begin(protocore_upload_service_span());
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
    HttpConn.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    handle();

    UploadService.last_size(protocore_upload_service_span());
    TEST_ASSERT_EQUAL_UINT(0, UploadServiceV.n);
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "500"));
    TEST_ASSERT_NOT_NULL(strstr(out, "upload failed"));
}
