// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "mnt_mock.h"
#include "network_drivers/transport/tcp/common.h"
#include "protocore.h"
#include "server/storage/mnt/mnt.h"
#include <stdio.h>
#include <string.h>

#include "network_drivers/presentation/http/websocket/websocket.h"
#include "network_drivers/transport/tcp/tcp.h"
#include "rx_feed.h"
#include <unity.h>

static uint8_t mnt_work[16]; // the borrow an entry takes; Mnt never reads it

static const protocore_mnt_backend *g_fs;
static proto_bool handler_called = PROTO_FALSE;

static void handle_html(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    handler_called = PROTO_TRUE;
    const protocore_mnt_backend *fs = mock_mnt();
    FileServingV.serve_file_args.slot_id = slot_id;
    FileServingV.serve_file_args.file_sys = fs;
    FileServingV.serve_file_args.fs_path = "/index.html";
    FileServingV.serve_file_args.content_type = "text/html";
    FileServing.serve_file(protocore_file_serving_span());
}

static void handle_js(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    handler_called = PROTO_TRUE;
    const protocore_mnt_backend *fs = mock_mnt();
    FileServingV.serve_file_args.slot_id = slot_id;
    FileServingV.serve_file_args.file_sys = fs;
    FileServingV.serve_file_args.fs_path = "/app.js";
    FileServingV.serve_file_args.content_type = "application/javascript";
    FileServing.serve_file(protocore_file_serving_span());
}

static void handle_missing(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    handler_called = PROTO_TRUE;
    const protocore_mnt_backend *fs = mock_mnt();
    FileServingV.serve_file_args.slot_id = slot_id;
    FileServingV.serve_file_args.file_sys = fs;
    FileServingV.serve_file_args.fs_path = "/missing.txt";
    FileServingV.serve_file_args.content_type = "text/plain";
    FileServing.serve_file(protocore_file_serving_span());
}

void setUp()
{
    protocore_server_reset();
    handler_called = PROTO_FALSE;

    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].proto = PROTO_HTTP;
        conn_pool[i].pcb = protocore_net_host_pcb();
        HttpConnV.slot = i;
        HttpConn.reset(protocore_http_conn_span());
    }
    Ws.init(protocore_ws_span());
    Sse.init(protocore_sse_span());

    mock_mnt_reset();

    MntV.args.backend = mock_mnt();
    Mnt.mount(mnt_work);
    tcp_capture_reset();
}

void tearDown()
{
    tcp_capture_disable();
    mock_mnt_clear();
}

static void feed_and_handle(uint8_t slot, const char *req_str)
{
    push_str(slot, req_str);
    HttpConnV.slot = slot;
    HttpConn.parse(protocore_http_conn_span());
    handle();
}

void test_missing_file_returns_404()
{
    on_http("/page", HTTP_GET, handle_missing);
    mock_mnt_clear();
    feed_and_handle(0, "GET /page HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "404"));
}

void test_existing_file_returns_200()
{
    on_http("/page", HTTP_GET, handle_html);
    mock_mnt_set_text("<html><body>Hello</body></html>");
    feed_and_handle(0, "GET /page HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
}

void test_response_includes_content_type_html()
{
    on_http("/page", HTTP_GET, handle_html);
    mock_mnt_set_text("<html></html>");
    feed_and_handle(0, "GET /page HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Content-Type: text/html"));
}

void test_response_includes_content_type_js()
{
    on_http("/app", HTTP_GET, handle_js);
    mock_mnt_set_text("console.log('hello');");
    feed_and_handle(0, "GET /app HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Content-Type: application/javascript"));
}

void test_content_length_matches_file_size()
{
    on_http("/page", HTTP_GET, handle_html);
    const char *body = "Hello, World!";
    mock_mnt_set_text(body);
    size_t expected_len = strlen(body);

    feed_and_handle(0, "GET /page HTTP/1.1\r\n\r\n");

    char expected_cl[64];
    snprintf(expected_cl, sizeof(expected_cl), "Content-Length: %u", (unsigned)expected_len);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), expected_cl));
}

void test_file_body_is_sent()
{
    on_http("/page", HTTP_GET, handle_html);
    const char *body = "<h1>Test Page</h1>";
    mock_mnt_set_text(body);
    feed_and_handle(0, "GET /page HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), body));
}

static proto_bool other_called = PROTO_FALSE;
static const char *cur_ctype = NULL;
static const char *cur_path = NULL;

static void h_empty(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    const protocore_mnt_backend *fs = mock_mnt();
    FileServingV.serve_file_args.slot_id = slot_id;
    FileServingV.serve_file_args.file_sys = fs;
    FileServingV.serve_file_args.fs_path = "/empty.txt";
    FileServingV.serve_file_args.content_type = "text/plain";
    FileServing.serve_file(protocore_file_serving_span());
}

static void h_big(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    const protocore_mnt_backend *fs = mock_mnt();
    FileServingV.serve_file_args.slot_id = slot_id;
    FileServingV.serve_file_args.file_sys = fs;
    FileServingV.serve_file_args.fs_path = "/big.bin";
    FileServingV.serve_file_args.content_type = "application/octet-stream";
    FileServing.serve_file(protocore_file_serving_span());
}

static void h_other(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    other_called = PROTO_TRUE;
    send_text(slot_id, 200, "text/plain", "other");
}

static void h_case(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    const protocore_mnt_backend *fs = mock_mnt();
    FileServingV.serve_file_args.slot_id = slot_id;
    FileServingV.serve_file_args.file_sys = fs;
    FileServingV.serve_file_args.fs_path = cur_path;
    FileServingV.serve_file_args.content_type = cur_ctype;
    FileServing.serve_file(protocore_file_serving_span());
}

static void h_f(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    const protocore_mnt_backend *fs = mock_mnt();
    FileServingV.serve_file_args.slot_id = slot_id;
    FileServingV.serve_file_args.file_sys = fs;
    FileServingV.serve_file_args.fs_path = "/f.txt";
    FileServingV.serve_file_args.content_type = "text/plain";
    FileServing.serve_file(protocore_file_serving_span());
}

void test_empty_file_returns_200_with_zero_length()
{
    on_http("/empty", HTTP_GET, h_empty);
    uint8_t zero_data[] = {0};
    mock_mnt_set(zero_data, 0);

    feed_and_handle(0, "GET /empty HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Content-Length: 0"));
}

void test_large_file_body_fully_sent()
{

#define BIG_N 16000
    static uint8_t big[BIG_N];
    for (size_t i = 0; i < BIG_N; i++)
    {
        big[i] = (uint8_t)('A' + (i % 26));
    }

    on_http("/big", HTTP_GET, h_big);
    mock_mnt_set(big, BIG_N);

    feed_and_handle(0, "GET /big HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));

    char expected_cl[64];
    snprintf(expected_cl, sizeof(expected_cl), "Content-Length: %u", (unsigned)BIG_N);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), expected_cl));

    const char *cap = tcp_captured();
    const char *body = strstr(cap, "\r\n\r\n");
    TEST_ASSERT_NOT_NULL(body);
    body += 4;
    size_t body_len = tcp_captured_len() - (size_t)(body - cap);
    TEST_ASSERT_EQUAL_size_t(BIG_N, body_len);
    for (size_t i = 0; i < BIG_N; i++)
    {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)('A' + (i % 26)), (uint8_t)body[i]);
    }
}

void test_serve_file_does_not_affect_other_routes()
{
    on_http("/other", HTTP_GET, h_other);
    on_http("/file", HTTP_GET, handle_html);

    mock_mnt_set_text("<html/>");
    feed_and_handle(0, "GET /other HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(other_called);
    TEST_ASSERT_FALSE(handler_called);
}

void test_multiple_content_types()
{
    static const struct
    {
        const char *path;
        const char *ctype;
        const char *body;
    } cases[] = {
        {"/page.html", "text/html", "<html/>"},
        {"/style.css", "text/css", "body{}"},
        {"/data.json", "application/json", "{}"},
        {"/app.js", "text/javascript", "var x=1;"},
    };

    for (size_t i = 0; i < 4; i++)
    {
        cur_ctype = cases[i].ctype;
        cur_path = cases[i].path;

        protocore_server_reset();
        conn_pool[0] = (TcpConn){0};
        conn_pool[0].id = 0;
        conn_pool[0].state = CONN_ACTIVE;
        conn_pool[0].proto = PROTO_HTTP;
        conn_pool[0].pcb = protocore_net_host_pcb();
        HttpConnV.slot = 0;
        HttpConn.reset(protocore_http_conn_span());
        tcp_capture_reset();

        on_http(cur_path, HTTP_GET, h_case);

        mock_mnt_set_text(cases[i].body);
        char req_str[128];
        snprintf(req_str, sizeof(req_str), "GET %s HTTP/1.1\r\n\r\n", cases[i].path);
        feed_and_handle(0, req_str);

        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(tcp_captured(), "200 OK"), "expected 200 OK");
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(tcp_captured(), cases[i].ctype), "expected content-type in response");
    }
}

static void rearm(uint8_t slot)
{
    conn_pool[slot] = (TcpConn){0};
    conn_pool[slot].id = slot;
    conn_pool[slot].state = CONN_ACTIVE;
    conn_pool[slot].proto = PROTO_HTTP;
    conn_pool[slot].pcb = protocore_net_host_pcb();
    HttpConnV.slot = slot;
    HttpConn.reset(protocore_http_conn_span());
    tcp_capture_reset();
}

void test_serve_static_root_join_variants()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/a.txt", "AAA", 0);
    mock_mnt_add_text("/b.txt", "BBB", 0);
    mock_mnt_add_text("/www/c.txt", "CCC", 0);

    FileServingV.serve_static_args.url_prefix = "/ts";
    FileServingV.serve_static_args.file_sys = g_fs;
    FileServingV.serve_static_args.fs_root = "/www/";
    FileServing.serve_static(protocore_file_serving_span());
    feed_and_handle(0, "GET /ts/a.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "AAA"));

    rearm(0);
    FileServingV.serve_static_args.url_prefix = "/nr";
    FileServingV.serve_static_args.file_sys = g_fs;
    FileServingV.serve_static_args.fs_root = NULL;
    FileServing.serve_static(protocore_file_serving_span());
    feed_and_handle(0, "GET /nr/b.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "BBB"));

    rearm(0);
    FileServingV.serve_static_args.url_prefix = "/ns";
    FileServingV.serve_static_args.file_sys = g_fs;
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());
    feed_and_handle(0, "GET /ns/c.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "CCC"));

    mock_mnt_reset();
}

void test_serve_static_empty_prefix_mount()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/any.txt", "anything", 0);
    FileServingV.serve_static_args.url_prefix = "";
    FileServingV.serve_static_args.file_sys = g_fs;
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());
    feed_and_handle(0, "GET /any.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "anything"));
    mock_mnt_reset();
}

void test_serve_static_directory_and_overlong_path()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/docs/index.html", "<i>docs</i>", 0);
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = g_fs;
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());
    feed_and_handle(0, "GET /docs/ HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "<i>docs</i>"));

    rearm(0);
    static char longroot[255];
    memset(longroot, 'r', sizeof(longroot) - 1);
    longroot[0] = '/';
    longroot[sizeof(longroot) - 1] = '\0';
    FileServingV.serve_static_args.url_prefix = "/lp";
    FileServingV.serve_static_args.file_sys = g_fs;
    FileServingV.serve_static_args.fs_root = longroot;
    FileServing.serve_static(protocore_file_serving_span());
    feed_and_handle(0, "GET /lp/x HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "404"));
    mock_mnt_reset();
}

void test_serve_static_gzip_negotiation_misses()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/app.js", "console.log(2)", 0);
    mock_mnt_add_text("/www/app.js.gz", "GZ", 0);
    mock_mnt_add_text("/www/plain.txt", "plain body", 0);
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = g_fs;
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());

    feed_and_handle(0, "GET /app.js HTTP/1.1\r\nHost: x\r\nAccept-Encoding: deflate, br\r\n\r\n");
    TEST_ASSERT_NULL(strstr(tcp_captured(), "Content-Encoding: gzip"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "console.log(2)"));

    rearm(0);
    feed_and_handle(0, "GET /plain.txt HTTP/1.1\r\nHost: x\r\nAccept-Encoding: gzip\r\n\r\n");
    TEST_ASSERT_NULL(strstr(tcp_captured(), "Content-Encoding: gzip"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "plain body"));
    mock_mnt_reset();
}

void test_serve_static_head_and_cors_headers()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/page.html", "<html>body</html>", 0);
    set_cors("*");
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = g_fs;
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());

    feed_and_handle(0, "HEAD /page.html HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Content-Length: 17"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Access-Control-Allow-Origin: *"));
    TEST_ASSERT_NULL(strstr(tcp_captured(), "<html>body</html>"));
    size_t n = tcp_captured_len();
    TEST_ASSERT_TRUE(n > 4);
    TEST_ASSERT_EQUAL_STRING("\r\n\r\n", tcp_captured() + n - 4);

    rearm(0);
    feed_and_handle(0, "GET /page.html HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Access-Control-Allow-Origin: *"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "<html>body</html>"));

    set_cors("");
    mock_mnt_reset();
}

void test_serve_static_inm_non_matching_forms()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/p.html", "123456789012345", (time_t)1000);
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = g_fs;
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());

    feed_and_handle(0, "GET /p.html HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "ETag: \"f-3e8\""));

    static const char *misses[] = {
        "\"nope\",", "\"a\",\t\"b\"", "Wxyz", "bare-token", "\"unterminated", "\"f-3e9\"",
    };
    for (size_t i = 0; i < sizeof(misses) / sizeof(misses[0]); i++)
    {
        rearm(0);
        char req[200];
        snprintf(req, sizeof(req), "GET /p.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: %s\r\n\r\n", misses[i]);
        feed_and_handle(0, req);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(tcp_captured(), "HTTP/1.1 200 OK"), misses[i]);
    }

    rearm(0);
    feed_and_handle(0, "GET /p.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: W/\"f-3e8\"\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "304 Not Modified"));
    mock_mnt_reset();
}

void test_file_send_pump_connection_lost_midtransfer()
{
    mock_mnt_reset();
    static const size_t N = 9000;
    static uint8_t big[BIG_N];
    memset(big, 'Z', BIG_N);
    mock_mnt_add_text("/www/big.bin", big, BIG_N);
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = g_fs;
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());

    mock_sndbuf_set(0);
    feed_and_handle(0, "GET /big.bin HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    FileServingV.holds_slot_args.slot = 0;
    FileServing.holds_slot(protocore_file_serving_span());
    TEST_ASSERT_TRUE(FileServingV.ok);

    conn_pool[0].pcb = NULL;
    handle();
    FileServingV.holds_slot_args.slot = 0;
    FileServing.holds_slot(protocore_file_serving_span());
    TEST_ASSERT_FALSE(FileServingV.ok);
    TEST_ASSERT_NULL(strstr(tcp_captured(), "ZZZZ"));

    mock_sndbuf_set(MOCK_SNDBUF_DEFAULT);
    mock_mnt_reset();
}

void stress_serve_file_50_requests()
{
    const char *body = "stress body";
    mock_mnt_set_text(body);
    on_http("/f", HTTP_GET, handle_html);

    for (int i = 0; i < 50; i++)
    {
        uint8_t slot = (uint8_t)(i % MAX_CONNS);
        conn_pool[slot] = (TcpConn){0};
        conn_pool[slot].id = slot;
        conn_pool[slot].state = CONN_ACTIVE;
        conn_pool[slot].proto = PROTO_HTTP;
        conn_pool[slot].pcb = protocore_net_host_pcb();
        HttpConnV.slot = slot;
        HttpConn.reset(protocore_http_conn_span());
        tcp_capture_reset();
        handler_called = PROTO_FALSE;

        push_str(slot, "GET /f HTTP/1.1\r\n\r\n");
        HttpConnV.slot = slot;
        HttpConn.parse(protocore_http_conn_span());
        handle();

        TEST_ASSERT_TRUE_MESSAGE(handler_called, "handler not called");
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(tcp_captured(), "200 OK"), "not 200");
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(tcp_captured(), body), "body missing");
    }
}

void stress_alternate_missing_and_found()
{
    on_http("/f", HTTP_GET, h_f);

    for (int i = 0; i < 40; i++)
    {
        uint8_t slot = (uint8_t)(i % MAX_CONNS);
        conn_pool[slot] = (TcpConn){0};
        conn_pool[slot].id = slot;
        conn_pool[slot].state = CONN_ACTIVE;
        conn_pool[slot].proto = PROTO_HTTP;
        conn_pool[slot].pcb = protocore_net_host_pcb();
        HttpConnV.slot = slot;
        HttpConn.reset(protocore_http_conn_span());
        tcp_capture_reset();

        if (i % 2 == 0)
        {
            mock_mnt_set_text("content");
        }
        else
        {
            mock_mnt_clear();
        }

        push_str(slot, "GET /f HTTP/1.1\r\n\r\n");
        HttpConnV.slot = slot;
        HttpConn.parse(protocore_http_conn_span());
        handle();

        if (i % 2 == 0)
        {
            TEST_ASSERT_NOT_NULL_MESSAGE(strstr(tcp_captured(), "200"), "expected 200");
        }
        else
        {
            TEST_ASSERT_NOT_NULL_MESSAGE(strstr(tcp_captured(), "404"), "expected 404");
        }
    }
}

static void inject_header(uint8_t slot, const char *key, const char *val)
{
    HttpReq *r = &http_pool[slot];
    TEST_ASSERT_TRUE(r->header_count < MAX_HEADERS);
    Header *h = &r->headers[r->header_count++];
    snprintf(h->key, sizeof(h->key), "%s", key);
    snprintf(h->val, sizeof(h->val), "%s", val);
}

void test_inm_leading_ows_still_matches()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/p.html", "123456789012345", (time_t)1000);
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = g_fs;
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());

    push_str(0, "GET /p.html HTTP/1.1\r\nHost: x\r\n\r\n");
    HttpConnV.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    inject_header(0, "If-None-Match", " \t\"f-3e8\"");
    handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "304 Not Modified"));
    mock_mnt_reset();
}

void test_inm_list_separators_reach_later_tag()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/p.html", "123456789012345", (time_t)1000);
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = g_fs;
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());
    feed_and_handle(0, "GET /p.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: , \"a\" , \"f-3e8\"\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "304 Not Modified"));
    mock_mnt_reset();
}

void test_conditional_304_carries_cors_block()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/p.html", "123456789012345", (time_t)1000);
    set_cors("*");
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = g_fs;
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());

    feed_and_handle(0, "GET /p.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: \"f-3e8\"\r\n\r\n");
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "304 Not Modified"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Access-Control-Allow-Origin: *\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(out, "ETag: \"f-3e8\""));
    TEST_ASSERT_NULL(strstr(out, "123456789012345"));

    set_cors("");
    mock_mnt_reset();
}

void test_serve_static_overlong_prefix_registers_nothing()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/index.html", "<i>root</i>", 0);

    char prefix[MAX_PATH_LEN + 8];
    prefix[0] = '/';
    memset(prefix + 1, 'p', sizeof(prefix) - 2);
    prefix[sizeof(prefix) - 1] = '\0';
    FileServingV.serve_static_args.url_prefix = prefix;
    FileServingV.serve_static_args.file_sys = g_fs;
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());

    char req[MAX_PATH_LEN + 64];
    char path[MAX_PATH_LEN];
    memcpy(path, prefix, MAX_PATH_LEN - 1);
    path[MAX_PATH_LEN - 1] = '\0';
    snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: x\r\n\r\n", path);
    feed_and_handle(0, req);
    TEST_ASSERT_NULL(strstr(tcp_captured(), "<i>root</i>"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "404"));
    mock_mnt_reset();
}

void test_serve_static_param_mount_shorter_than_pattern()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/index.html", "<i>idx</i>", 0);
    FileServingV.serve_static_args.url_prefix = "/a/:b";
    FileServingV.serve_static_args.file_sys = g_fs;
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());
    feed_and_handle(0, "GET /a/x HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "<i>idx</i>"));
    mock_mnt_reset();
}

void test_serve_static_trailing_slash_root_bare_prefix()
{
    mock_mnt_reset();
    mock_mnt_add_text("/root/index.html", "<i>bare</i>", 0);
    FileServingV.serve_static_args.url_prefix = "/s";
    FileServingV.serve_static_args.file_sys = g_fs;
    FileServingV.serve_static_args.fs_root = "/root/";
    FileServing.serve_static(protocore_file_serving_span());
    feed_and_handle(0, "GET /s HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "<i>bare</i>"));
    mock_mnt_reset();
}

void test_serve_static_joined_path_overflow_is_404()
{
    mock_mnt_reset();
    static char longroot[201];
    memset(longroot, 'r', sizeof(longroot) - 1);
    longroot[0] = '/';
    longroot[sizeof(longroot) - 1] = '\0';
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = g_fs;
    FileServingV.serve_static_args.fs_root = longroot;
    FileServing.serve_static(protocore_file_serving_span());

    char req[128];
    char sub[60];
    memset(sub, 's', sizeof(sub) - 1);
    sub[sizeof(sub) - 1] = '\0';
    snprintf(req, sizeof(req), "GET /%s HTTP/1.1\r\nHost: x\r\n\r\n", sub);
    feed_and_handle(0, req);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "404"));
    mock_mnt_reset();
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_missing_file_returns_404);
    RUN_TEST(test_existing_file_returns_200);
    RUN_TEST(test_response_includes_content_type_html);
    RUN_TEST(test_response_includes_content_type_js);
    RUN_TEST(test_content_length_matches_file_size);
    RUN_TEST(test_file_body_is_sent);
    RUN_TEST(test_empty_file_returns_200_with_zero_length);
    RUN_TEST(test_large_file_body_fully_sent);
    RUN_TEST(test_serve_file_does_not_affect_other_routes);
    RUN_TEST(test_multiple_content_types);
    RUN_TEST(test_serve_static_root_join_variants);
    RUN_TEST(test_serve_static_empty_prefix_mount);
    RUN_TEST(test_serve_static_directory_and_overlong_path);
    RUN_TEST(test_serve_static_gzip_negotiation_misses);
    RUN_TEST(test_serve_static_head_and_cors_headers);
    RUN_TEST(test_serve_static_inm_non_matching_forms);
    RUN_TEST(test_file_send_pump_connection_lost_midtransfer);

    RUN_TEST(test_inm_leading_ows_still_matches);
    RUN_TEST(test_inm_list_separators_reach_later_tag);
    RUN_TEST(test_conditional_304_carries_cors_block);
    RUN_TEST(test_serve_static_overlong_prefix_registers_nothing);
    RUN_TEST(test_serve_static_param_mount_shorter_than_pattern);
    RUN_TEST(test_serve_static_trailing_slash_root_bare_prefix);
    RUN_TEST(test_serve_static_joined_path_overflow_is_404);

    RUN_TEST(stress_serve_file_50_requests);
    RUN_TEST(stress_alternate_missing_and_found);

    return UNITY_END();
}
