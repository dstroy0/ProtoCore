// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "lfs_mock.h"
#include "network_drivers/transport/tcp/common.h"
#include "network_drivers/transport/tcp/protocol/protocol.h"
#include "network_drivers/transport/tcp/server/server.h"
#include "network_drivers/transport/tcp/tcp.h"
#include "protocore.h"
#include "rx_feed.h"
#include "server/core/proto_handler.h"
#include "server/storage/mnt/mnt.h"
#include <string.h>
#include <unity.h>

static uint8_t mnt_work[16]; // the borrow an entry takes; Mnt never reads it

// The SSE pool, reached through its namespace: set the members a call takes, invoke it, read the
// outcome off the same handle.
static SseConn *sse_alloc(uint8_t slot, const char *path)
{
    SseV.slot = slot;
    SseV.route.path = path;
    Sse.alloc(protocore_sse_span());
    return SseV.conn;
}

static SseConn *sse_find(uint8_t slot)
{
    SseV.slot = slot;
    Sse.find(protocore_sse_span());
    return SseV.conn;
}

static void sse_free(uint8_t slot)
{
    SseV.slot = slot;
    Sse.free(protocore_sse_span());
}

static int sse_format(char *buf, size_t cap, const char *data, const char *event, const char *event_id)
{
    SseV.out.buf = buf;
    SseV.out.cap = cap;
    SseV.event_args.data = data;
    SseV.event_args.event = event;
    SseV.event_args.event_id = event_id;
    Sse.format(protocore_sse_span());
    return SseV.n;
}

static proto_bool sse_write(SseConn *stream, const char *data, const char *event, const char *event_id)
{
    SseV.stream = stream;
    SseV.event_args.data = data;
    SseV.event_args.event = event;
    SseV.event_args.event_id = event_id;
    Sse.write(protocore_sse_span());
    return SseV.ok;
}

static proto_bool handler_called;
static uint8_t handler_slot;

static void record_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    handler_called = PROTO_TRUE;
    handler_slot = slot_id;
}

static void arm_slot(uint8_t slot, const char *raw)
{
    conn_pool[slot] = (TcpConn){0};
    conn_pool[slot].id = slot;
    conn_pool[slot].state = CONN_ACTIVE;
    conn_pool[slot].proto = PROTO_HTTP;
    conn_pool[slot].pcb = NULL;

    TcpConn *s = &conn_pool[slot];
    for (size_t i = 0; raw[i]; i++)
    {
        size_t next = (s->rx_head + 1) % RX_BUF_SIZE;
        s->rx_buffer[s->rx_head] = (uint8_t)raw[i];
        s->rx_head = next;
    }
    HttpConnV.slot = slot;
    HttpConn.reset(protocore_http_conn_span());
    HttpConnV.slot = slot;
    HttpConn.parse(protocore_http_conn_span());
}

#define MARK_MAX 8
static int g_mark[MARK_MAX];

#define MARK_HANDLER(n)                                                                                                \
    static void mark##n(uint8_t id, HttpReq *req)                                                                      \
    {                                                                                                                  \
        (void)id;                                                                                                      \
        (void)req;                                                                                                     \
        g_mark[n]++;                                                                                                   \
    }
MARK_HANDLER(0)
MARK_HANDLER(1)
MARK_HANDLER(2)
MARK_HANDLER(3)
MARK_HANDLER(4)
MARK_HANDLER(5)
MARK_HANDLER(6)
MARK_HANDLER(7)

static void fail_handler(uint8_t id, HttpReq *req)
{
    (void)id;
    (void)req;
    TEST_FAIL_MESSAGE("handler must not be called for Transfer-Encoding request");
}

static void reset_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    g_mark[0]++;
    HttpConnV.slot = slot_id;
    HttpConn.reset(protocore_http_conn_span());
}

static char g_body_seen[32];
static void body_handler(uint8_t id, HttpReq *req)
{
    (void)id;
    strncpy(g_body_seen, (const char *)req->body, sizeof(g_body_seen) - 1);
}

static char g_query_seen[48];
static void query_handler(uint8_t id, HttpReq *req)
{
    (void)id;
    HttpParserV.get_query_args.req = req;
    HttpParserV.get_query_args.key = "id";
    HttpParser.get_query(protocore_http_parser_span());
    const char *v = HttpParserV.text;
    if (v)
    {
        strncpy(g_query_seen, v, sizeof(g_query_seen) - 1);
    }
}

static char g_header_seen[48];
static void header_handler(uint8_t id, HttpReq *req)
{
    (void)id;
    HttpParserV.get_header_args.req = req;
    HttpParserV.get_header_args.key = "X-Token";
    HttpParser.get_header(protocore_http_parser_span());
    const char *v = HttpParserV.text;
    if (v)
    {
        strncpy(g_header_seen, v, sizeof(g_header_seen) - 1);
    }
}

static void hello_handler(uint8_t id, HttpReq *req)
{
    (void)req;
    send_text(id, 200, "text/plain", "hello");
}

static void stats_handler(uint8_t id, HttpReq *req)
{
    (void)req;
    stats(id);
}

void setUp(void)
{
    memset(g_mark, 0, sizeof(g_mark));
    set_millis(0);
    ConnPoolV.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(protocore_conn_pool_span());
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].proto = PROTO_HTTP;
        HttpConnV.slot = i;
        HttpConn.reset(protocore_http_conn_span());
    }
    handler_called = PROTO_FALSE;
    handler_slot = 255;
    Ws.init(protocore_ws_span());
    Sse.init(protocore_sse_span());
    protocore_server_reset();
}

void tearDown(void)
{
}

void test_fn_on_registers_and_dispatches(void)
{
    on_http("/ping", HTTP_GET, record_handler);
    arm_slot(0, "GET /ping HTTP/1.1\r\n\r\n");
    handle();
    TEST_ASSERT_TRUE(handler_called);
}

void test_fn_on_path_copied_null_terminated(void)
{

    char path[MAX_PATH_LEN + 4];
    path[0] = '/';
    for (int i = 1; i < MAX_PATH_LEN - 1; i++)
    {
        path[i] = 'a';
    }
    path[MAX_PATH_LEN - 1] = '\0';
    on_http(path, HTTP_GET, record_handler);
    arm_slot(0, "GET /a HTTP/1.1\r\n\r\n");
    handle();
    TEST_PASS();
}

void test_fn_on_table_full_extra_routes_dropped(void)
{

    for (int i = 0; i < MAX_ROUTES + 5; i++)
    {
        on_http("/x", HTTP_GET, record_handler);
    }
    arm_slot(0, "GET /x HTTP/1.1\r\n\r\n");
    handle();
    TEST_ASSERT_TRUE(handler_called);
}

void test_fn_on_same_path_different_methods_are_distinct(void)
{
    on_http("/r", HTTP_GET, mark0);
    on_http("/r", HTTP_POST, mark1);

    arm_slot(0, "GET /r HTTP/1.1\r\n\r\n");
    handle();
    TEST_ASSERT_EQUAL_INT(1, g_mark[0]);
    TEST_ASSERT_EQUAL_INT(0, g_mark[1]);

    arm_slot(0, "POST /r HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    handle();
    TEST_ASSERT_EQUAL_INT(1, g_mark[1]);
}

void test_fn_on_not_found_called_when_no_match(void)
{
    on_not_found(record_handler);
    arm_slot(0, "GET /nowhere HTTP/1.1\r\n\r\n");
    handle();
    TEST_ASSERT_TRUE(handler_called);
}

void test_fn_on_not_found_not_called_when_match_exists(void)
{
    on_http("/here", HTTP_GET, record_handler);
    on_not_found(mark0);
    arm_slot(0, "GET /here HTTP/1.1\r\n\r\n");
    handle();
    TEST_ASSERT_TRUE(handler_called);
    TEST_ASSERT_EQUAL_INT(0, g_mark[0]);
}

void test_fn_set_cors_options_preflight_clears_slot(void)
{
    set_cors("*");
    arm_slot(0, "OPTIONS /x HTTP/1.1\r\n\r\n");
    handle();
    TEST_ASSERT_NOT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
}

void test_fn_set_cors_empty_string_disables(void)
{
    set_cors("*");
    set_cors("");
    on_http("/x", HTTP_OPTIONS, record_handler);
    arm_slot(0, "OPTIONS /x HTTP/1.1\r\n\r\n");
    handle();
    TEST_ASSERT_TRUE(handler_called);
}

void test_wrong_method_does_not_match(void)
{
    on_http("/r", HTTP_POST, record_handler);
    arm_slot(0, "GET /r HTTP/1.1\r\n\r\n");
    handle();
    TEST_ASSERT_FALSE(handler_called);
}

void test_wrong_path_does_not_match(void)
{
    on_http("/right", HTTP_GET, record_handler);
    arm_slot(0, "GET /wrong HTTP/1.1\r\n\r\n");
    handle();
    TEST_ASSERT_FALSE(handler_called);
}

void test_all_http_methods_dispatched(void)
{
    on_http("/get", HTTP_GET, mark0);
    on_http("/post", HTTP_POST, mark1);
    on_http("/put", HTTP_PUT, mark2);
    on_http("/delete", HTTP_DELETE, mark3);
    on_http("/patch", HTTP_PATCH, mark4);
    on_http("/head", HTTP_HEAD, mark5);
    on_http("/options", HTTP_OPTIONS, mark6);

    arm_slot(0, "GET /get HTTP/1.1\r\n\r\n");
    handle();
    arm_slot(0, "POST /post HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    handle();
    arm_slot(0, "PUT /put HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    handle();
    arm_slot(0, "DELETE /delete HTTP/1.1\r\n\r\n");
    handle();
    arm_slot(0, "PATCH /patch HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    handle();
    arm_slot(0, "HEAD /head HTTP/1.1\r\n\r\n");
    handle();
    arm_slot(0, "OPTIONS /options HTTP/1.1\r\n\r\n");
    handle();

    for (int i = 0; i < 7; i++)
    {
        TEST_ASSERT_EQUAL_MESSAGE(1, g_mark[i], "method not dispatched");
    }
}

void test_root_path_matches_exactly(void)
{
    on_http("/", HTTP_GET, record_handler);
    arm_slot(0, "GET / HTTP/1.1\r\n\r\n");
    handle();
    TEST_ASSERT_TRUE(handler_called);
}

void test_root_path_does_not_match_subpath(void)
{
    on_http("/", HTTP_GET, record_handler);
    arm_slot(0, "GET /other HTTP/1.1\r\n\r\n");
    handle();
    TEST_ASSERT_FALSE(handler_called);
}

void test_wildcard_matches_any_suffix(void)
{
    on_http("/api/*", HTTP_GET, record_handler);
    arm_slot(0, "GET /api/users/42 HTTP/1.1\r\n\r\n");
    handle();
    TEST_ASSERT_TRUE(handler_called);
}

void test_wildcard_does_not_match_unrelated_prefix(void)
{
    on_http("/api/*", HTTP_GET, record_handler);
    arm_slot(0, "GET /other/path HTTP/1.1\r\n\r\n");
    handle();
    TEST_ASSERT_FALSE(handler_called);
}

void test_exact_route_wins_when_registered_first(void)
{
    on_http("/api/status", HTTP_GET, mark0);
    on_http("/api/*", HTTP_GET, record_handler);
    arm_slot(0, "GET /api/status HTTP/1.1\r\n\r\n");
    handle();
    TEST_ASSERT_EQUAL_INT(1, g_mark[0]);
    TEST_ASSERT_FALSE(handler_called);
}

void test_slot_not_stuck_in_complete_after_handle(void)
{
    on_http("/free", HTTP_GET, record_handler);
    arm_slot(0, "GET /free HTTP/1.1\r\n\r\n");
    handle();
    TEST_ASSERT_NOT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
}

void test_parse_error_slot_auto_reset(void)
{
    push_str(0, "TOOLONGMETHODNAME /path HTTP/1.1\r\n\r\n");
    HttpConnV.slot = 0;
    HttpConn.reset(protocore_http_conn_span());
    HttpConnV.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
    handle();
    TEST_ASSERT_NOT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_handler_reads_body(void)
{
    g_body_seen[0] = '\0';
    on_http("/body", HTTP_POST, body_handler);
    arm_slot(0, "POST /body HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello");
    handle();
    TEST_ASSERT_EQUAL_STRING("hello", g_body_seen);
}

void test_handler_reads_query_param(void)
{
    g_query_seen[0] = '\0';
    on_http("/q", HTTP_GET, query_handler);
    arm_slot(0, "GET /q?id=42 HTTP/1.1\r\n\r\n");
    handle();
    TEST_ASSERT_EQUAL_STRING("42", g_query_seen);
}

void test_handler_reads_header(void)
{
    g_header_seen[0] = '\0';
    on_http("/h", HTTP_GET, header_handler);
    arm_slot(0, "GET /h HTTP/1.1\r\nX-Token: secret\r\n\r\n");
    handle();
    TEST_ASSERT_EQUAL_STRING("secret", g_header_seen);
}

void test_wildcard_before_exact_wildcard_wins(void)
{
    on_http("/api/*", HTTP_GET, mark0);
    on_http("/api/status", HTTP_GET, mark1);
    arm_slot(0, "GET /api/status HTTP/1.1\r\n\r\n");
    handle();
    TEST_ASSERT_EQUAL_INT(1, g_mark[0]);
    TEST_ASSERT_EQUAL_INT(0, g_mark[1]);
}

void stress_last_route_dispatched_in_full_table(void)
{
    for (int i = 0; i < MAX_ROUTES - 1; i++)
    {
        char path[16];
        snprintf(path, sizeof(path), "/r%d", i);
        on_http(path, HTTP_GET, mark7);
    }
    on_http("/last", HTTP_GET, mark0);

    arm_slot(0, "GET /last HTTP/1.1\r\n\r\n");
    handle();
    TEST_ASSERT_EQUAL(1, g_mark[0]);
}

void stress_sequential_requests_no_state_leak(void)
{
    on_http("/seq", HTTP_GET, mark0);

    for (int i = 0; i < 50; i++)
    {
        arm_slot(0, "GET /seq HTTP/1.1\r\n\r\n");
        handle();
    }
    TEST_ASSERT_EQUAL(50, g_mark[0]);
}

void stress_all_slots_dispatched_simultaneously(void)
{
    on_http("/s0", HTTP_GET, mark0);
    on_http("/s1", HTTP_GET, mark1);
    on_http("/s2", HTTP_GET, mark2);
    on_http("/s3", HTTP_GET, mark3);

    arm_slot(0, "GET /s0 HTTP/1.1\r\n\r\n");
    arm_slot(1, "GET /s1 HTTP/1.1\r\n\r\n");
    arm_slot(2, "GET /s2 HTTP/1.1\r\n\r\n");
    arm_slot(3, "GET /s3 HTTP/1.1\r\n\r\n");

    handle();

    for (int i = 0; i < 4; i++)
    {
        TEST_ASSERT_EQUAL_MESSAGE(1, g_mark[i], "slot not dispatched");
    }
}

void stress_wildcard_matches_many_paths(void)
{
    on_http("/api/*", HTTP_GET, mark0);

    const char *paths[] = {
        "GET /api/users HTTP/1.1\r\n\r\n",
        "GET /api/devices HTTP/1.1\r\n\r\n",
        "GET /api/status/health HTTP/1.1\r\n\r\n",
        "GET /api/ HTTP/1.1\r\n\r\n",
        "GET /api/a HTTP/1.1\r\n\r\n",
        "GET /api/b/c/d HTTP/1.1\r\n\r\n",
        "GET /api/1 HTTP/1.1\r\n\r\n",
        "GET /api/2 HTTP/1.1\r\n\r\n",
        "GET /api/3 HTTP/1.1\r\n\r\n",
        "GET /api/long/nested/path HTTP/1.1\r\n\r\n",
    };
    for (int i = 0; i < 10; i++)
    {
        arm_slot(0, paths[i]);
        handle();
    }
    TEST_ASSERT_EQUAL(10, g_mark[0]);
}

void stress_handle_with_no_complete_slots_is_nop(void)
{
    on_http("/x", HTTP_GET, record_handler);

    for (int i = 0; i < 20; i++)
    {
        handle();
    }
    TEST_ASSERT_FALSE(handler_called);
}

void race_slot_complete_between_handle_calls(void)
{
    on_http("/late", HTTP_GET, mark0);

    handle();
    TEST_ASSERT_EQUAL_INT(0, g_mark[0]);

    arm_slot(0, "GET /late HTTP/1.1\r\n\r\n");
    handle();
    TEST_ASSERT_EQUAL_INT(1, g_mark[0]);
}

void race_conn_freed_after_parse_complete(void)
{
    on_http("/r", HTTP_GET, record_handler);

    arm_slot(0, "GET /r HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);

    conn_pool[0].state = CONN_FREE;
    conn_pool[0].pcb = NULL;

    handle();
    TEST_ASSERT_NOT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
}

void race_double_handle_no_double_dispatch(void)
{
    on_http("/dd", HTTP_GET, mark0);

    arm_slot(0, "GET /dd HTTP/1.1\r\n\r\n");
    handle();
    handle();

    TEST_ASSERT_EQUAL(1, g_mark[0]);
}

void race_error_and_valid_slot_in_same_handle(void)
{
    on_http("/ok", HTTP_GET, mark0);

    push_str(0, "TOOLONGMETHODNAME /path HTTP/1.1\r\n\r\n");
    HttpConnV.slot = 0;
    HttpConn.reset(protocore_http_conn_span());
    HttpConnV.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);

    arm_slot(1, "GET /ok HTTP/1.1\r\n\r\n");

    handle();

    TEST_ASSERT_NOT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_INT(1, g_mark[0]);
}

void race_callback_manually_resets_slot(void)
{
    on_http("/mr", HTTP_GET, reset_handler);

    arm_slot(0, "GET /mr HTTP/1.1\r\n\r\n");
    handle();

    TEST_ASSERT_EQUAL_INT(1, g_mark[0]);
    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[0].parse_state);
}

void test_uri_too_long_auto_resets_slot(void)
{

    char req[MAX_PATH_LEN + 64];
    int idx = 0;
    memcpy(req + idx, "GET /", 5);
    idx += 5;
    for (int i = 0; i < MAX_PATH_LEN; i++)
    {
        req[idx++] = 'a';
    }
    memcpy(req + idx, " HTTP/1.1\r\n\r\n", 13);
    idx += 13;
    req[idx] = '\0';

    push_str(0, req);
    HttpConnV.slot = 0;
    HttpConn.reset(protocore_http_conn_span());
    HttpConnV.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    TEST_ASSERT_EQUAL(PARSE_URI_TOO_LONG, http_pool[0].parse_state);

    handle();
    TEST_ASSERT_NOT_EQUAL(PARSE_URI_TOO_LONG, http_pool[0].parse_state);
}

void test_transfer_encoding_chunked_is_501(void)
{

    arm_slot(0, "POST /data HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n");
    on_http("/data", HTTP_POST, fail_handler);
    handle();
    TEST_ASSERT_NOT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
}

void test_transfer_encoding_identity_is_501(void)
{

    arm_slot(0, "GET / HTTP/1.1\r\nTransfer-Encoding: identity\r\n\r\n");
    handle();
    TEST_ASSERT_NOT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
}

void test_redirect_emits_location_and_status(void)
{
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].proto = PROTO_HTTP;
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    redirect(0, 301, "/index.html");
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "HTTP/1.1 301 Moved Permanently"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Location: /index.html\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Content-Length: 0\r\n"));
    tcp_capture_disable();
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

void test_redirect_invalid_code_defaults_to_302(void)
{
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].proto = PROTO_HTTP;
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    redirect(0, 200, "/elsewhere");
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "HTTP/1.1 302 Found"));
    tcp_capture_disable();
}

void test_mime_type_detection(void)
{
    TEST_ASSERT_EQUAL_STRING("text/html", mime_type("/index.html"));
    TEST_ASSERT_EQUAL_STRING("text/css", mime_type("/css/site.css"));
    TEST_ASSERT_EQUAL_STRING("application/javascript", mime_type("/app.JS"));
    TEST_ASSERT_EQUAL_STRING("application/json", mime_type("/api/data.json"));
    TEST_ASSERT_EQUAL_STRING("image/svg+xml", mime_type("logo.svg"));
    TEST_ASSERT_EQUAL_STRING("image/png", mime_type("a.b.c.png"));

    TEST_ASSERT_EQUAL_STRING("application/octet-stream", mime_type("/file.unknownext"));
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", mime_type("/noext"));
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", mime_type("/dir.with.dot/file"));
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", mime_type(NULL));
}

void test_serve_static_file_and_mime(void)
{
    lfsm_format();
    MntV.args.backend = lfsm();
    Mnt.mount(mnt_work);
    static const char css[] = "body{color:red}";
    TEST_ASSERT_TRUE(lfsm_write_text("/www/style.css", css));
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = lfsm();
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());
    arm_slot(0, "GET /style.css HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Content-Type: text/css"));
    TEST_ASSERT_NOT_NULL(strstr(out, "body{color:red}"));
}

void test_serve_static_cache_control(void)
{
    lfsm_format();
    MntV.args.backend = lfsm();
    Mnt.mount(mnt_work);
    static const char css[] = "body{color:red}";
    TEST_ASSERT_TRUE(lfsm_write_text("/www/style.css", css));
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = lfsm();
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());

    set_cache_control("max-age=3600");
    arm_slot(0, "GET /style.css HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Cache-Control: max-age=3600"));

    set_cache_control("");
    arm_slot(0, "GET /style.css HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NULL(strstr(out, "Cache-Control:"));
}

void test_serve_static_index_fallback(void)
{
    lfsm_format();
    MntV.args.backend = lfsm();
    Mnt.mount(mnt_work);
    static const char html[] = "<h1>home</h1>";
    TEST_ASSERT_TRUE(lfsm_write_text("/www/index.html", html));
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = lfsm();
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());
    arm_slot(0, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Content-Type: text/html"));
    TEST_ASSERT_NOT_NULL(strstr(out, "<h1>home</h1>"));
}

void test_serve_static_gzip_when_accepted(void)
{
    lfsm_format();
    MntV.args.backend = lfsm();
    Mnt.mount(mnt_work);
    static const char gzbody[] = "\x1f\x8b"
                                 "FAKEGZIP";
    TEST_ASSERT_TRUE(lfsm_write_file("/www/app.js.gz", gzbody, sizeof(gzbody) - 1));
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = lfsm();
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());
    arm_slot(0, "GET /app.js HTTP/1.1\r\nHost: x\r\nAccept-Encoding: gzip, deflate\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Content-Type: application/javascript"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Content-Encoding: gzip"));
}

void test_serve_static_wildcard_and_route_full(void)
{
    lfsm_format();
    MntV.args.backend = lfsm();
    Mnt.mount(mnt_work);
    static const char js[] = "x=1;";
    TEST_ASSERT_TRUE(lfsm_write_text("/www/app.js", js));
    FileServingV.serve_static_args.url_prefix = "/assets*";
    FileServingV.serve_static_args.file_sys = lfsm();
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());
    arm_slot(0, "GET /assets/app.js HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out, "HTTP/1.1 200 OK"));

    for (int i = 0; i < MAX_ROUTES + 3; i++)
    {
        FileServingV.serve_static_args.url_prefix = "/s";
        FileServingV.serve_static_args.file_sys = lfsm();
        FileServingV.serve_static_args.fs_root = "/www";
        FileServing.serve_static(protocore_file_serving_span());
    }
}

static void hdr_guard_handler(uint8_t id, HttpReq *req)
{
    (void)req;
    static char big[512];
    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    set_cookie(id, "toobig", big, NULL);
    proto_add_response_header(id, "X-Ok", "1");
    send_text(id, 200, "text/plain", "ok");
}

void test_response_header_cookie_guards(void)
{
    proto_add_response_header(MAX_CONNS, "X", "y");
    proto_add_response_header(0, NULL, "y");
    set_cookie(MAX_CONNS, "s", "1", NULL);
    set_cookie(0, NULL, "1", NULL);
    clear_response_headers(MAX_CONNS);

    on_http("/hdrtest", HTTP_GET, hdr_guard_handler);
    arm_slot(0, "GET /hdrtest HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out, "X-Ok: 1"));
    TEST_ASSERT_NULL(strstr(out, "toobig"));
}

void test_serve_static_no_gzip_when_not_accepted(void)
{
    lfsm_format();
    MntV.args.backend = lfsm();
    Mnt.mount(mnt_work);
    static const char js[] = "console.log(1)";
    TEST_ASSERT_TRUE(lfsm_write_text("/www/app.js", js));
    TEST_ASSERT_TRUE(lfsm_write_text("/www/app.js.gz", "GZIPPED"));
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = lfsm();
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());
    arm_slot(0, "GET /app.js HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NULL(strstr(out, "Content-Encoding: gzip"));
    TEST_ASSERT_NOT_NULL(strstr(out, "console.log(1)"));
}

void test_serve_static_traversal_not_leaked(void)
{
    lfsm_format();
    MntV.args.backend = lfsm();
    Mnt.mount(mnt_work);
    TEST_ASSERT_TRUE(lfsm_write_text("/secret", "topsecret"));
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = lfsm();
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());
    arm_slot(0, "GET /../secret HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NULL(strstr(out, "topsecret"));
}

void test_serve_static_missing_is_404(void)
{
    lfsm_format();
    MntV.args.backend = lfsm();
    Mnt.mount(mnt_work);
    TEST_ASSERT_TRUE(lfsm_write_text("/www/exists.txt", "hi"));
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = lfsm();
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());
    arm_slot(0, "GET /nope.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out, "404"));
}

void test_serve_static_etag_conditional_get(void)
{
    lfsm_format();
    MntV.args.backend = lfsm();
    Mnt.mount(mnt_work);
    TEST_ASSERT_TRUE(lfsm_write_text_at("/www/page.html", "<html>hi</html>", 1000));
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = lfsm();
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());

    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    const char *out1 = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out1, "HTTP/1.1 200 OK"));
    const char *etp = strstr(out1, "ETag: ");
    TEST_ASSERT_NOT_NULL(etp);
    char etag[40];
    etp += 6;
    size_t i = 0;
    while (etp[i] && etp[i] != '\r' && i < sizeof(etag) - 1)
    {
        etag[i] = etp[i];
        i++;
    }
    etag[i] = '\0';

    char req[160];
    snprintf(req, sizeof(req), "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: %s\r\n\r\n", etag);
    arm_slot(0, req);
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    const char *out2 = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out2, "304 Not Modified"));
    TEST_ASSERT_NOT_NULL(strstr(out2, etag));
    TEST_ASSERT_NULL(strstr(out2, "<html>hi</html>"));
}

void test_serve_static_inm_star_list_weak(void)
{
    lfsm_format();
    MntV.args.backend = lfsm();
    Mnt.mount(mnt_work);
    TEST_ASSERT_TRUE(lfsm_write_text_at("/www/page.html", "<html>hi</html>", 1000));
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = lfsm();
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());

    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    const char *out1 = tcp_captured();
    tcp_capture_disable();
    const char *etp = strstr(out1, "ETag: ");
    TEST_ASSERT_NOT_NULL(etp);
    char etag[40];
    etp += 6;
    size_t i = 0;
    while (etp[i] && etp[i] != '\r' && i < sizeof(etag) - 1)
    {
        etag[i] = etp[i];
        i++;
    }
    etag[i] = '\0';

    char req[200];

    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: *\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "304 Not Modified"));
    tcp_capture_disable();

    snprintf(req, sizeof(req), "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: W/%s\r\n\r\n", etag);
    arm_slot(0, req);
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "304 Not Modified"));
    tcp_capture_disable();

    snprintf(req, sizeof(req), "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: \"nope\", %s\r\n\r\n", etag);
    arm_slot(0, req);
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "304 Not Modified"));
    tcp_capture_disable();

    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: \"a\", \"b\"\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "HTTP/1.1 200 OK"));
    tcp_capture_disable();

    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: \"nope\", \r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "HTTP/1.1 200 OK"));
    tcp_capture_disable();
}

void test_serve_static_last_modified_conditional_get(void)
{
    lfsm_format();
    MntV.args.backend = lfsm();
    Mnt.mount(mnt_work);
    TEST_ASSERT_TRUE(lfsm_write_text_at("/www/page.html", "<html>hi</html>", 1000));
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = lfsm();
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());
    const char *LM = "Thu, 01 Jan 1970 00:16:40 GMT";
    char req[200];
    const char *o;

    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    o = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(o, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(o, "Last-Modified: Thu, 01 Jan 1970 00:16:40 GMT\r\n"));

    snprintf(req, sizeof(req), "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-Modified-Since: %s\r\n\r\n", LM);
    arm_slot(0, req);
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    o = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(o, "304 Not Modified"));
    TEST_ASSERT_NULL(strstr(o, "<html>hi</html>"));

    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-Modified-Since: Thu, 01 Jan 1970 00:16:39 GMT\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    o = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(o, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(o, "<html>hi</html>"));

    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-Modified-Since: Fri, 02 Jan 1970 00:00:00 GMT\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    o = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(o, "304 Not Modified"));

    snprintf(req, sizeof(req),
             "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: \"deadbeef\"\r\nIf-Modified-Since: %s\r\n\r\n", LM);
    arm_slot(0, req);
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    o = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(o, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(o, "<html>hi</html>"));
}

void test_serve_static_ims_field_comparisons(void)
{
    lfsm_format();
    MntV.args.backend = lfsm();
    Mnt.mount(mnt_work);
    TEST_ASSERT_TRUE(lfsm_write_text_at("/www/page.html", "<html>hi</html>", 1000));
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = lfsm();
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());
    const char *ims[] = {
        "Fri, 01 Jan 1971 00:16:40 GMT",
        "Sun, 01 Feb 1970 00:16:40 GMT",
        "Thu, 01 Jan 1970 01:16:40 GMT",
        "Thu, 01 Jan 1970 00:17:40 GMT",
    };
    char req[200];
    for (size_t i = 0; i < sizeof(ims) / sizeof(ims[0]); i++)
    {
        snprintf(req, sizeof(req), "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-Modified-Since: %s\r\n\r\n", ims[i]);
        arm_slot(0, req);
        conn_pool[0].pcb = protocore_net_host_pcb();
        tcp_capture_reset();
        handle();
        const char *o = tcp_captured();
        tcp_capture_disable();
        TEST_ASSERT_NOT_NULL(strstr(o, "304 Not Modified"));
        TEST_ASSERT_NULL(strstr(o, "<html>hi</html>"));
    }

    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-Modified-Since: Wed, 01 Jan 1969 00:16:40 GMT\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    const char *o = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(o, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(o, "<html>hi</html>"));
}

void test_serve_static_no_timestamp(void)
{
    lfsm_format();
    MntV.args.backend = lfsm();
    Mnt.mount(mnt_work);
    TEST_ASSERT_TRUE(lfsm_write_text("/www/page.html", "<html>hi</html>"));
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = lfsm();
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());

    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    const char *o = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(o, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NULL(strstr(o, "Last-Modified:"));

    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-Modified-Since: Thu, 01 Jan 2099 00:00:00 GMT\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    o = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(o, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(o, "<html>hi</html>"));
}

void test_serve_static_if_modified_since_malformed(void)
{
    lfsm_format();
    MntV.args.backend = lfsm();
    Mnt.mount(mnt_work);
    TEST_ASSERT_TRUE(lfsm_write_text_at("/www/page.html", "<html>hi</html>", 1000));
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = lfsm();
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());
    const char *bad[] = {
        "not a date",
        "Thu, 01",
        "Thu, 01 ebM 1970 00:00:00 GMT",
        "Thu, 01 Xyz 1970 00:00:00 GMT",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        char req[200];
        snprintf(req, sizeof(req), "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-Modified-Since: %s\r\n\r\n", bad[i]);
        arm_slot(0, req);
        conn_pool[0].pcb = protocore_net_host_pcb();
        tcp_capture_reset();
        handle();
        const char *o = tcp_captured();
        tcp_capture_disable();
        TEST_ASSERT_NOT_NULL(strstr(o, "HTTP/1.1 200 OK"));
        TEST_ASSERT_NOT_NULL(strstr(o, "<html>hi</html>"));
    }
}

static char g_log_method[8];
static char g_log_path[64];
static int g_log_status;
static int g_log_bytes;
static int g_log_calls;
static void capture_log(const char *m, const char *p, int s, int b)
{
    strncpy(g_log_method, m, sizeof(g_log_method) - 1);
    g_log_method[sizeof(g_log_method) - 1] = '\0';
    strncpy(g_log_path, p, sizeof(g_log_path) - 1);
    g_log_path[sizeof(g_log_path) - 1] = '\0';
    g_log_status = s;
    g_log_bytes = b;
    g_log_calls++;
}

void test_request_log_hook_fires(void)
{
    g_log_calls = 0;
    on_request_log(capture_log);
    on_http("/hi", HTTP_GET, hello_handler);
    arm_slot(0, "GET /hi HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    handle();
    TEST_ASSERT_EQUAL_INT(1, g_log_calls);
    TEST_ASSERT_EQUAL_STRING("GET", g_log_method);
    TEST_ASSERT_EQUAL_STRING("/hi", g_log_path);
    TEST_ASSERT_EQUAL_INT(200, g_log_status);
    TEST_ASSERT_EQUAL_INT(5, g_log_bytes);
    on_request_log(NULL);
}

void test_stats_endpoint_emits_json(void)
{
    on_http("/stats", HTTP_GET, stats_handler);
    arm_slot(0, "GET /stats HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out, "application/json"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"uptime_ms\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"requests\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"http_2xx\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"http_4xx\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"active_conns\""));
}

#if PROTOCORE_ENABLE_METRICS

void test_metrics_emits_prometheus(void)
{
    conn_pool[0] = (TcpConn){0};
    conn_pool[0].id = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].proto = PROTO_HTTP;
    conn_pool[0].pcb = protocore_net_host_pcb();
    HttpConnV.slot = 0;
    HttpConn.reset(protocore_http_conn_span());
    tcp_capture_reset();
    metrics(0);
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "text/plain; version=0.0.4"));
    TEST_ASSERT_NOT_NULL(strstr(out, "# TYPE protocore_http_requests_total counter"));
    TEST_ASSERT_NOT_NULL(strstr(out, "protocore_http_responses_total{class=\"2xx\"}"));
    TEST_ASSERT_NOT_NULL(strstr(out, "protocore_free_heap_bytes"));
    TEST_ASSERT_NOT_NULL(strstr(out, "protocore_uptime_seconds"));

    const char *body = strstr(out, "\r\n\r\n");
    TEST_ASSERT_NOT_NULL(body);
    body += 4;
    int samples = 0;
    for (const char *ln = body; *ln;)
    {
        const char *eol = strchr(ln, '\n');
        size_t len = eol ? (size_t)(eol - ln) : strlen(ln);
        while (len && (ln[len - 1] == '\r' || ln[len - 1] == ' '))
        {
            len--;
        }
        if (len && ln[0] != '#')
        {

            const char *sp = NULL;
            for (size_t i = 0; i < len; i++)
            {
                if (ln[i] == ' ')
                {
                    sp = ln + i;
                }
            }
            TEST_ASSERT_NOT_NULL_MESSAGE(sp, "metric sample line has no value separator");
            TEST_ASSERT_TRUE_MESSAGE((size_t)(sp - ln) + 1 < len, "metric sample line has an empty value");
            samples++;
        }
        if (!eol)
        {
            break;
        }
        ln = eol + 1;
    }
    TEST_ASSERT_TRUE_MESSAGE(samples >= 11, "expected every metric placeholder to emit a sample");
    tcp_capture_disable();
}
#endif

#if PROTOCORE_ENABLE_SSE

void test_sse_broadcast_after_upgrade_matches_path(void)
{
    on_sse("/events", NULL);

    conn_pool[0] = (TcpConn){0};
    conn_pool[0].id = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].proto = PROTO_HTTP;
    conn_pool[0].pcb = protocore_net_host_pcb();
    push_str(0, "GET /events HTTP/1.1\r\n\r\n");
    HttpConnV.slot = 0;
    HttpConn.reset(protocore_http_conn_span());
    HttpConnV.slot = 0;
    HttpConn.parse(protocore_http_conn_span());

    tcp_capture_reset();
    handle();
    protocore_sse_broadcast("/events", "hello", "msg", NULL);
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "text/event-stream"));
    TEST_ASSERT_NOT_NULL(strstr(out, "data: hello"));
    tcp_capture_disable();
}
#endif

#if PROTOCORE_ENABLE_WEBSOCKET

void test_ws_send_api(void)
{
    Ws.init(protocore_ws_span());
    conn_pool[0] = (TcpConn){0};
    conn_pool[0].id = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].proto = PROTO_HTTP;
    conn_pool[0].pcb = protocore_net_host_pcb();
    WsV.slot = 0;
    Ws.alloc(protocore_ws_span());
    WsConn *ws = WsV.found;
    TEST_ASSERT_NOT_NULL(ws);

    tcp_capture_reset();
    ws_send_text(MAX_WS_CONNS, "x");
    ws_send_text(1, "x");
    ws_send_binary(MAX_WS_CONNS, (const uint8_t *)"x", 1);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());

    tcp_capture_reset();
    ws_send_text(0, "hello");
    TEST_ASSERT_TRUE(tcp_captured_len() >= 2);
    TEST_ASSERT_EQUAL_HEX8(0x81, (uint8_t)tcp_captured()[0]);

    tcp_capture_reset();
    const uint8_t payload[3] = {1, 2, 3};
    ws_send_binary(0, payload, sizeof(payload));
    TEST_ASSERT_TRUE(tcp_captured_len() >= 2);
    TEST_ASSERT_EQUAL_HEX8(0x82, (uint8_t)tcp_captured()[0]);

    ws->parse_state = WS_CLOSED;
    tcp_capture_reset();
    ws_send_text(0, "nope");
    ws_send_binary(0, payload, sizeof(payload));
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
    ws->parse_state = WS_HEADER1;

    tcp_capture_reset();
    ws_disconnect(MAX_WS_CONNS);
    ws_disconnect(0);
    TEST_ASSERT_TRUE(tcp_captured_len() >= 2);
    TEST_ASSERT_EQUAL_HEX8(0x88, (uint8_t)tcp_captured()[0]);
    tcp_capture_disable();
}
#endif

#if PROTOCORE_ENABLE_SSE

void test_sse_send_api(void)
{
    Sse.init(protocore_sse_span());
    conn_pool[0] = (TcpConn){0};
    conn_pool[0].id = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].proto = PROTO_HTTP;
    conn_pool[0].pcb = protocore_net_host_pcb();
    SseConn *sse = sse_alloc(0, "/events");
    TEST_ASSERT_NOT_NULL(sse);

    tcp_capture_reset();
    protocore_sse_send(MAX_SSE_CONNS, "x", NULL, NULL);
    protocore_sse_send(1, "x", NULL, NULL);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());

    tcp_capture_reset();
    protocore_sse_send(0, "hi", "msg", "42");
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "event: msg"));
    TEST_ASSERT_NOT_NULL(strstr(out, "id: 42"));
    TEST_ASSERT_NOT_NULL(strstr(out, "data: hi"));

    tcp_capture_reset();
    protocore_sse_broadcast("/other", "skip", NULL, NULL);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
    tcp_capture_disable();
}
#endif

typedef struct
{
    int code;
    const char *reason;
} StatusCase;

void test_status_text_reason_phrases(void)
{
    static const StatusCase cases[] = {
        {201, "Created"},
        {206, "Partial Content"},
        {303, "See Other"},
        {304, "Not Modified"},
        {307, "Temporary Redirect"},
        {308, "Permanent Redirect"},
        {401, "Unauthorized"},
        {405, "Method Not Allowed"},
        {408, "Request Timeout"},
        {409, "Conflict"},
        {413, "Payload Too Large"},
        {414, "URI Too Long"},
        {416, "Range Not Satisfiable"},
        {429, "Too Many Requests"},
        {500, "Internal Server Error"},
        {501, "Not Implemented"},
        {503, "Service Unavailable"},
        {999, "Unknown"},
#if PROTOCORE_ENABLE_WEBDAV
        {207, "Multi-Status"},
        {412, "Precondition Failed"},
        {423, "Locked"},
        {502, "Bad Gateway"},
#endif
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        conn_pool[0] = (TcpConn){0};
        conn_pool[0].id = 0;
        conn_pool[0].state = CONN_ACTIVE;
        conn_pool[0].proto = PROTO_HTTP;
        conn_pool[0].pcb = protocore_net_host_pcb();
        HttpConnV.slot = 0;
        HttpConn.reset(protocore_http_conn_span());
        tcp_capture_reset();
        send_text(0, cases[i].code, "text/plain", "x");
        char want[48];
        snprintf(want, sizeof(want), "HTTP/1.1 %d %s", cases[i].code, cases[i].reason);
        TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), want));
    }
    tcp_capture_disable();
}

void test_send_binary_body_with_nul(void)
{
    conn_pool[0].id = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].proto = PROTO_HTTP;
    conn_pool[0].pcb = protocore_net_host_pcb();
    HttpConnV.slot = 0;
    HttpConn.reset(protocore_http_conn_span());
    const uint8_t body[] = {0x00, 0x00, 0x00, 0x00, 0x05, 'h', 'e', 0x00, 'l', 'o'};
    tcp_capture_reset();
    send_bin(0, 200, "application/grpc-web+proto", body, sizeof(body));
    const char *out = tcp_captured();
    size_t out_len = tcp_captured_len();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Content-Type: application/grpc-web+proto\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Content-Length: 10\r\n"));
    size_t hdr_end = 0;
    for (size_t i = 0; i + 4 <= out_len; i++)
    {
        if (memcmp(out + i, "\r\n\r\n", 4) == 0)
        {
            hdr_end = i + 4;
            break;
        }
    }
    TEST_ASSERT_TRUE(hdr_end > 0);
    TEST_ASSERT_EQUAL_UINT(sizeof(body), (unsigned)(out_len - hdr_end));
    TEST_ASSERT_EQUAL_INT(0, memcmp(out + hdr_end, body, sizeof(body)));
}

void test_allow_header_lists_methods(void)
{
    on_http("/m", HTTP_PATCH, record_handler);
    on_http("/m", HTTP_OPTIONS, record_handler);
    on_http("/m", HTTP_HEAD, record_handler);
    on_http("/m", HTTP_PUT, record_handler);
    on_http("/m", HTTP_METHOD_UNKNOWN, record_handler);
    arm_slot(0, "DELETE /m HTTP/1.1\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "405"));
    TEST_ASSERT_NOT_NULL(strstr(out, "PATCH"));
    TEST_ASSERT_NOT_NULL(strstr(out, "OPTIONS"));
    TEST_ASSERT_NOT_NULL(strstr(out, "HEAD"));
    TEST_ASSERT_NOT_NULL(strstr(out, "PUT"));
    tcp_capture_disable();
}

void test_listen_and_begin(void)
{

    TEST_ASSERT_EQUAL_INT32(PROTOCORE_ERR_NO_LISTENERS, proto_begin(NULL));

    for (int i = 0; i < MAX_LISTENERS; i++)
    {
        TEST_ASSERT_EQUAL_INT32(i, listen((uint16_t)(9100 + i), PROTO_HTTP));
    }
    TEST_ASSERT_EQUAL_INT32(PROTOCORE_ERR_LISTENER_FULL, listen(9999, PROTO_HTTP));

    TEST_ASSERT_EQUAL_INT32(PROTOCORE_OK, proto_begin(NULL));
    TcpListener.stop_all(protocore_tcp_listener_span());
}

void test_begin_port_convenience(void)
{
    TEST_ASSERT_EQUAL_INT32(PROTOCORE_OK, begin_http((uint16_t)8080, NULL));
    TcpListener.stop_all(protocore_tcp_listener_span());

    for (int i = 0; i < MAX_LISTENERS; i++)
    {
        listen((uint16_t)(9300 + i), PROTO_HTTP);
    }
    TEST_ASSERT_EQUAL_INT32(PROTOCORE_ERR_LISTENER_FULL, begin_http((uint16_t)9999, NULL));
}

void test_restart_and_stop(void)
{

    TEST_ASSERT_EQUAL_INT32(PROTOCORE_ERR_NO_LISTENERS, restart(NULL));

    TEST_ASSERT_EQUAL_INT32(0, listen((uint16_t)9500, PROTO_HTTP));
    TEST_ASSERT_EQUAL_INT32(PROTOCORE_OK, proto_begin(NULL));
    TEST_ASSERT_EQUAL_INT32(PROTOCORE_OK, restart(NULL));

    stop();
    stop();
    TcpListener.stop_all(protocore_tcp_listener_span());
}

void test_route_registration_variants_table_full(void)
{
    for (int i = 0; i < MAX_ROUTES; i++)
    {
        on_http("/x", HTTP_GET, record_handler);
    }

    on_http_iface("/i", HTTP_GET, record_handler, PROTOCORE_IF_WIFI_STA);
    on_regex("/re.*", HTTP_GET, record_handler);
#if PROTOCORE_ENABLE_AUTH
    on_http_auth("/a", HTTP_GET, record_handler, "realm", "u", "p", PROTO_FALSE);
#endif
#if PROTOCORE_ENABLE_WEBSOCKET
    on_ws("/ws", NULL, NULL, NULL);
#endif
#if PROTOCORE_ENABLE_SSE
    on_sse("/sse", NULL);
#endif

    arm_slot(0, "GET /i HTTP/1.1\r\n\r\n");
    handler_called = PROTO_FALSE;
    handle();
    TEST_ASSERT_FALSE(handler_called);
}

void test_send_family_slot_and_conn_gone_guards(void)
{
    redirect(MAX_CONNS, 302, "/x");
    send_template(MAX_CONNS, 200, "text/html", "hi", NULL);
    send_chunked(MAX_CONNS, 200, "text/plain", NULL, NULL);

    conn_pool[0].state = CONN_FREE;
    conn_pool[0].pcb = NULL;
    redirect(0, 302, "/x");
    send_template(0, 200, "text/html", "hi", NULL);
    send_chunked(0, 200, "text/plain", NULL, NULL);
    TEST_PASS();
}

void test_redirect_response_and_code_normalization(void)
{
    conn_pool[0] = (TcpConn){0};
    conn_pool[0].id = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].proto = PROTO_HTTP;
    conn_pool[0].pcb = protocore_net_host_pcb();
    HttpConnV.slot = 0;
    HttpConn.reset(protocore_http_conn_span());
    tcp_capture_reset();
    redirect(0, 307, "/new");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "307 Temporary Redirect"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Location: /new"));

    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = protocore_net_host_pcb();
    HttpConnV.slot = 0;
    HttpConn.reset(protocore_http_conn_span());
    tcp_capture_reset();
    redirect(0, 200, "/z");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "302 Found"));
    tcp_capture_disable();
}

void test_request_error_paths_te_method_ws(void)
{
    on_http("/only-get", HTTP_GET, record_handler);
#if PROTOCORE_ENABLE_WEBSOCKET
    on_ws("/ws", NULL, NULL, NULL);
#endif

    arm_slot(0, "POST /only-get HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "405"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Allow:"));

#if PROTOCORE_ENABLE_WEBSOCKET

    arm_slot(0, "GET /ws HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "400"));

    arm_slot(0, "GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 12\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "426"));
#endif
    tcp_capture_disable();
}

void test_ws_sse_upgrade_failure_paths(void)
{
#if PROTOCORE_ENABLE_WEBSOCKET
    on_ws("/ws", NULL, NULL, NULL);

    arm_slot(0, "GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGVzdA==\r\nSec-WebSocket-Version: 13\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "400"));

    arm_slot(0, "GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Version: 13\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "400"));

    WsV.slot = 1;
    Ws.alloc(protocore_ws_span());
    WsV.slot = 2;
    Ws.alloc(protocore_ws_span());
    arm_slot(0, "GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "101"));
    Ws.init(protocore_ws_span());
    tcp_capture_disable();
#endif
}

#if PROTOCORE_ENABLE_SSE

void test_sse_upgrade_pool_exhausted(void)
{
    on_sse("/events", NULL);
    sse_alloc(1, "/a");
    sse_alloc(2, "/b");
    arm_slot(0, "GET /events HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "text/event-stream"));
    Sse.init(protocore_sse_span());
    tcp_capture_disable();
}
#endif

void test_response_headers_that_do_not_fit_are_refused(void)
{

    char bigct[800];
    memset(bigct, 'a', 750);
    bigct[750] = '\0';
    arm_slot(0, "GET /x HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    send_text(0, 200, bigct, "ok");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "HTTP/1.1 500"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Connection: close"));

    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "\r\n\r\n"));
    TEST_ASSERT_NULL(strstr(tcp_captured(), "aaaa"));
    tcp_capture_disable();

    char midct[600];
    memset(midct, 'b', 500);
    midct[500] = '\0';
    char hv[250];
    memset(hv, 'c', 240);
    hv[240] = '\0';
    arm_slot(0, "GET /y HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    proto_add_response_header(0, "X-Big", hv);
    tcp_capture_reset();
    send_text(0, 200, midct, "ok");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "HTTP/1.1 500"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "\r\n\r\n"));
    tcp_capture_disable();
}

static void live_slot(uint8_t slot)
{
    conn_pool[slot] = (TcpConn){0};
    conn_pool[slot].id = slot;
    conn_pool[slot].state = CONN_ACTIVE;
    conn_pool[slot].proto = PROTO_HTTP;
    conn_pool[slot].pcb = protocore_net_host_pcb();
    HttpConnV.slot = slot;
    HttpConn.reset(protocore_http_conn_span());
    http_pool[slot].version = HTTP_11;
}

static QueryParam g_seen_params[MAX_PATH_PARAMS];
static uint8_t g_seen_param_count;
static void capture_params_handler(uint8_t id, HttpReq *req)
{
    (void)id;
    handler_called = PROTO_TRUE;
    g_seen_param_count = req->path_param_count;
    memcpy(g_seen_params, req->path_params, sizeof(g_seen_params));
}

void test_stats_counters_ignore_sub_200_status(void)
{
    live_slot(0);
    send_text(0, 100, "text/plain", "x");

    live_slot(1);
    tcp_capture_reset();
    stats(1);
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out, "\"requests\":1"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"http_2xx\":0"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"http_4xx\":0"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"http_5xx\":0"));
}

void test_response_trailer_cors_block_and_null_disable(void)
{
    set_cors("https://a.example");
    live_slot(0);
    tcp_capture_reset();
    send_text(0, 200, "text/plain", "ok");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Access-Control-Allow-Origin: https://a.example\r\n"));

    set_cors(NULL);
    live_slot(0);
    tcp_capture_reset();
    send_text(0, 200, "text/plain", "ok");
    TEST_ASSERT_NULL(strstr(tcp_captured(), "Access-Control-Allow-Origin"));
    tcp_capture_disable();
}

void test_cache_control_null_clears_header(void)
{
    lfsm_format();
    MntV.args.backend = lfsm();
    Mnt.mount(mnt_work);
    static const char body[] = "x";
    TEST_ASSERT_TRUE(lfsm_write_text("/www/c.txt", body));
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = lfsm();
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());

    set_cache_control("max-age=60");
    set_cache_control(NULL);
    arm_slot(0, "GET /c.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NULL(strstr(tcp_captured(), "Cache-Control"));
    tcp_capture_disable();
    lfsm_format();
    MntV.args.backend = lfsm();
    Mnt.mount(mnt_work);
}

void test_empty_route_pattern_matches_nothing(void)
{
    on_http("", HTTP_GET, record_handler);
    arm_slot(0, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "404"));
    tcp_capture_disable();
}

void test_path_param_capture_limits(void)
{
    on_http("/q/:a/:b/:c/:d/:e", HTTP_GET, capture_params_handler);
    arm_slot(0, "GET /q/1/2/3/4/5 HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    handle();
    TEST_ASSERT_TRUE(handler_called);
    TEST_ASSERT_EQUAL_UINT8(MAX_PATH_PARAMS, g_seen_param_count);
    TEST_ASSERT_EQUAL_STRING("4", g_seen_params[3].val);

    handler_called = PROTO_FALSE;
    on_http("/k/:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", HTTP_GET, capture_params_handler);
    char req[160];
    char big[60];
    memset(big, 'v', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    snprintf(req, sizeof(req), "GET /k/%s HTTP/1.1\r\nHost: x\r\n\r\n", big);
    arm_slot(0, req);
    conn_pool[0].pcb = protocore_net_host_pcb();
    handle();
    TEST_ASSERT_TRUE(handler_called);
    TEST_ASSERT_EQUAL_UINT(QUERY_KEY_LEN - 1, (unsigned)strlen(g_seen_params[0].key));
    TEST_ASSERT_EQUAL_UINT(QUERY_VAL_LEN - 1, (unsigned)strlen(g_seen_params[0].val));
}

void test_path_param_segment_mismatches(void)
{
    on_http("/p1/:a", HTTP_GET, record_handler);
    on_http("/p2/:a", HTTP_GET, record_handler);
    on_http("/p3/:a", HTTP_GET, record_handler);
    on_http("//:a", HTTP_GET, capture_params_handler);

    const char *misses[] = {
        "GET /p1 HTTP/1.1\r\nHost: x\r\n\r\n",
        "GET /p2/x/y HTTP/1.1\r\nHost: x\r\n\r\n",
        "GET /p9/x HTTP/1.1\r\nHost: x\r\n\r\n",
    };
    for (size_t i = 0; i < sizeof(misses) / sizeof(misses[0]); i++)
    {
        handler_called = PROTO_FALSE;
        arm_slot(0, misses[i]);
        conn_pool[0].pcb = protocore_net_host_pcb();
        tcp_capture_reset();
        handle();
        TEST_ASSERT_FALSE_MESSAGE(handler_called, misses[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(tcp_captured(), "404"), misses[i]);
    }

    handler_called = PROTO_FALSE;
    arm_slot(0, "GET //v HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    handle();
    TEST_ASSERT_TRUE(handler_called);
    TEST_ASSERT_EQUAL_STRING("v", g_seen_params[0].val);
    tcp_capture_disable();
}

void test_worker_owner_filter_skips_foreign_slot(void)
{
    on_http("/own", HTTP_GET, record_handler);
    arm_slot(1, "GET /own HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[1].owner = 1;
    handle();
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[1].parse_state);

    conn_pool[1].owner = 0;
    handle();
    TEST_ASSERT_TRUE(handler_called);
}

void test_slot_poll_requires_registered_handler_with_poll(void)
{
    on_http("/pp", HTTP_GET, record_handler);
    arm_slot(0, "GET /pp HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].proto = PROTO_TELNET;
    handle();
    TEST_ASSERT_FALSE(handler_called);

    static const ProtoHandler no_poll = {NULL, NULL, NULL, NULL};
    Protocols.proto = PROTO_TELNET;
    Protocols.h = &no_poll;
    Protocols.add(protocore_session_span());
    handle();
    TEST_ASSERT_FALSE(handler_called);

    Protocols.proto = PROTO_TELNET;
    Protocols.h = NULL;
    Protocols.add(protocore_session_span());
    conn_pool[0].proto = PROTO_HTTP;
    handle();
    TEST_ASSERT_TRUE(handler_called);
}

void test_entity_too_large_auto_413(void)
{
    on_http("/big", HTTP_POST, record_handler);
    arm_slot(0, "POST /big HTTP/1.1\r\nHost: x\r\nContent-Length: 100000\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    TEST_ASSERT_EQUAL(PARSE_ENTITY_TOO_LARGE, http_pool[0].parse_state);
    tcp_capture_reset();
    handle();
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "413 Payload Too Large"));
    tcp_capture_disable();
}

void test_allow_header_dedupes_repeated_method(void)
{
    on_http("/dup", HTTP_POST, record_handler);
    on_http("/dup", HTTP_POST, record_handler);
    arm_slot(0, "GET /dup HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "Allow: POST\r\n"));
    TEST_ASSERT_NULL(strstr(out, "POST, POST"));
    tcp_capture_disable();
}

void test_error_close_head_and_dead_connection(void)
{
    on_http("/po", HTTP_POST, record_handler);

    arm_slot(0, "HEAD /po HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "405 Method Not Allowed"));
    TEST_ASSERT_NULL(strstr(out, "\r\n\r\nMethod Not Allowed"));

    arm_slot(0, "GET /po HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    conn_pool[0].state = CONN_CLOSING;
    tcp_capture_reset();
    handle();
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());

    arm_slot(0, "GET /po HTTP/1.1\r\nHost: x\r\n\r\n");
    tcp_capture_reset();
    handle();
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
    tcp_capture_disable();
}

void test_transfer_encoding_on_semantic_ingress_is_501(void)
{
    on_http("/te", HTTP_POST, record_handler);
    live_slot(0);
    HttpReq *r = &http_pool[0];
    snprintf(r->method, sizeof(r->method), "POST");
    snprintf(r->path, sizeof(r->path), "/te");
    r->path_idx = strlen(r->path);
    r->version = HTTP_11;
    snprintf(r->headers[0].key, sizeof(r->headers[0].key), "transfer-encoding");
    snprintf(r->headers[0].val, sizeof(r->headers[0].val), "chunked");
    r->header_count = 1;
    r->parse_state = PARSE_COMPLETE;

    tcp_capture_reset();
    handle();
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "501 Not Implemented"));
    tcp_capture_disable();
}

void test_static_mount_rejects_non_get_methods(void)
{
    lfsm_format();
    MntV.args.backend = lfsm();
    Mnt.mount(mnt_work);
    static const char body[] = "hi";
    TEST_ASSERT_TRUE(lfsm_write_text("/www/a.txt", body));
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = lfsm();
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());
    arm_slot(0, "POST /a.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "405"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Allow: GET, HEAD\r\n"));
    tcp_capture_disable();
    lfsm_format();
    MntV.args.backend = lfsm();
    Mnt.mount(mnt_work);
}

void test_send_null_payload_and_slot_bounds(void)
{
    live_slot(0);
    tcp_capture_reset();
    send_text(CONN_POOL_SLOTS, 200, "text/plain", "x");
    send_empty(CONN_POOL_SLOTS, 204);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());

    tcp_capture_reset();
    send_text(0, 200, "text/plain", (const char *)NULL);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Content-Length: 0\r\n"));
    tcp_capture_disable();
}

void test_send_body_framing_paths(void)
{

    live_slot(0);
    snprintf(http_pool[0].method, sizeof(http_pool[0].method), "HEAD");
    tcp_capture_reset();
    send_text(0, 200, "text/plain", "abcdef");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Content-Length: 6\r\n"));
    TEST_ASSERT_NULL(strstr(tcp_captured(), "abcdef"));

    live_slot(0);
    tcp_capture_reset();
    send_text(0, 200, "text/plain", "");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Content-Length: 0\r\n"));

    static char big[RESP_HDR_BUF_SIZE + 64];
    memset(big, 'B', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    live_slot(0);
    tcp_capture_reset();
    send_text(0, 200, "text/plain", big);
    const char *out = tcp_captured();
    char want[40];
    snprintf(want, sizeof(want), "Content-Length: %u\r\n", (unsigned)(sizeof(big) - 1));
    TEST_ASSERT_NOT_NULL(strstr(out, want));
    TEST_ASSERT_EQUAL_UINT(sizeof(big) - 1, (unsigned)strlen(strstr(out, "\r\n\r\n") + 4));
    tcp_capture_disable();
}

void test_send_empty_and_redirect_dead_connection_guards(void)
{
    live_slot(0);
    conn_pool[0].state = CONN_CLOSING;
    tcp_capture_reset();
    send_empty(0, 204);
    redirect(0, 302, "/x");
    send_text(0, 200, "text/plain", "x");
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());

    live_slot(0);
    conn_pool[0].pcb = NULL;
    tcp_capture_reset();
    send_empty(0, 204);
    redirect(0, 302, "/x");
    send_text(0, 200, "text/plain", "x");
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[0].parse_state);
    tcp_capture_disable();
}

static const char *tmpl_resolver(const char *name)
{
    return strcmp(name, "who") == 0 ? "world" : NULL;
}

void test_send_template_placeholder_edges(void)
{
    live_slot(0);
    tcp_capture_reset();
    send_template(0, 200, "text/plain", "a{{0123456789012345678901234567890123}}b", tmpl_resolver);
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "a{{0123456789012345678901234567890123}}b"));

    live_slot(0);
    tcp_capture_reset();
    send_template(0, 204, "text/plain", "", tmpl_resolver);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Content-Length: 0\r\n"));
    tcp_capture_disable();
}

void test_send_chunked_without_source(void)
{
    live_slot(0);
    tcp_capture_reset();
    send_chunked(0, 200, "text/plain", NULL, NULL);
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "Transfer-Encoding: chunked\r\n"));
    TEST_ASSERT_NULL(strstr(out, "0\r\n\r\n"));
    TEST_ASSERT_FALSE(protocore_resp_holds_slot(0));
    tcp_capture_disable();
}

static int g_chunk_calls;
static size_t chunk_src_fill(uint8_t *buf, size_t cap, void *ctx)
{
    (void)ctx;
    if (g_chunk_calls++ >= 2)
    {
        return 0;
    }
    size_t n = cap < 40 ? cap : 40;
    memset(buf, 'q', n);
    return n;
}

void test_chunked_pump_small_window_and_connection_lost(void)
{
    g_chunk_calls = 0;
    mock_sndbuf_set(64);
    live_slot(0);
    tcp_capture_reset();
    send_chunked(0, 200, "text/plain", chunk_src_fill, NULL);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Transfer-Encoding: chunked\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "28\r\nqqqq"));
    TEST_ASSERT_FALSE(protocore_resp_holds_slot(0));

    g_chunk_calls = 0;
    mock_sndbuf_set(0);
    live_slot(0);
    tcp_capture_reset();
    send_chunked(0, 200, "text/plain", chunk_src_fill, NULL);
    TEST_ASSERT_TRUE(protocore_resp_holds_slot(0));

    conn_pool[0].pcb = NULL;
    handle();
    TEST_ASSERT_FALSE(protocore_resp_holds_slot(0));
    TEST_ASSERT_NULL(strstr(tcp_captured(), "qqqq"));

    mock_sndbuf_set(MOCK_SNDBUF_DEFAULT);
    tcp_capture_disable();
}

void test_response_header_null_value_empty_attrs_and_overflow(void)
{
    live_slot(0);
    clear_response_headers(0);
    proto_add_response_header(0, "X-Keep", "1");
    proto_add_response_header(0, "X-Null", NULL);
    set_cookie(0, "c-null", NULL, NULL);
    set_cookie(0, "sid", "abc", "");

    char filler[EXTRA_HDR_BUF_SIZE];
    memset(filler, 'f', sizeof(filler) - 1);
    filler[sizeof(filler) - 1] = '\0';
    proto_add_response_header(0, "X-Too-Big", filler);
    set_cookie(0, "big", filler, NULL);

    tcp_capture_reset();
    send_text(0, 200, "text/plain", "ok");
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "X-Keep: 1\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Set-Cookie: sid=abc\r\n"));
    TEST_ASSERT_NULL(strstr(out, "X-Null"));
    TEST_ASSERT_NULL(strstr(out, "c-null"));
    TEST_ASSERT_NULL(strstr(out, "X-Too-Big"));
    TEST_ASSERT_NULL(strstr(out, "ffff"));
    tcp_capture_disable();
}

void test_mime_type_extension_edges(void)
{
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", mime_type("/file."));
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", mime_type("/a.7z"));
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", mime_type("/a.jsx"));
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", mime_type("/a.h"));
    TEST_ASSERT_EQUAL_STRING("font/woff2", mime_type("/a.WOFF2"));
}

#if PROTOCORE_ENABLE_WEBSOCKET

static void push_ws_text_frame(uint8_t slot, const char *text)
{
    const uint8_t mask[4] = {0x11, 0x22, 0x33, 0x44};
    size_t n = strlen(text);
    uint8_t hdr[6] = {0x81, (uint8_t)(0x80 | n), mask[0], mask[1], mask[2], mask[3]};
    TcpConn *c = &conn_pool[slot];
    for (size_t i = 0; i < sizeof(hdr); i++)
    {
        c->rx_buffer[c->rx_head] = hdr[i];
        c->rx_head = (c->rx_head + 1) % RX_BUF_SIZE;
    }
    for (size_t i = 0; i < n; i++)
    {
        c->rx_buffer[c->rx_head] = (uint8_t)(text[i] ^ mask[i % 4]);
        c->rx_head = (c->rx_head + 1) % RX_BUF_SIZE;
    }
}

static void ws_upgrade_slot0(const char *path)
{
    char req[220];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n",
             path);
    arm_slot(0, req);
    conn_pool[0].pcb = protocore_net_host_pcb();
    handle();
}

void test_ws_upgrade_without_connect_handler(void)
{
    Ws.init(protocore_ws_span());
    on_ws("/wsn", NULL, NULL, NULL);
    tcp_capture_reset();
    ws_upgrade_slot0("/wsn");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "101 Switching Protocols"));
    WsV.slot = 0;
    Ws.find(protocore_ws_span());
    TEST_ASSERT_NOT_NULL(WsV.found);
    tcp_capture_disable();
    Ws.init(protocore_ws_span());
}

void test_ws_dispatch_without_message_or_close_handler(void)
{
    Ws.init(protocore_ws_span());
    on_http("/plain", HTTP_GET, record_handler);
    on_ws("/wsq", NULL, NULL, NULL);
    ws_upgrade_slot0("/wsq");
    WsV.slot = 0;
    Ws.find(protocore_ws_span());
    WsConn *ws = WsV.found;
    TEST_ASSERT_NOT_NULL(ws);

    push_ws_text_frame(0, "hi");
    handle();
    WsV.slot = 0;
    Ws.find(protocore_ws_span());
    TEST_ASSERT_NOT_NULL(WsV.found);
    TEST_ASSERT_NOT_EQUAL(WS_FRAME_READY, ws->parse_state);

    ws->parse_state = WS_ERROR;
    handle();
    WsV.slot = 0;
    Ws.find(protocore_ws_span());
    TEST_ASSERT_NULL(WsV.found);
    Ws.init(protocore_ws_span());
}

void test_ws_upgrade_handshake_gate(void)
{
    on_ws("/wsg", NULL, NULL, NULL);
    const char *bad[] = {
        "POST /wsg HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n",
        "GET /wsg HTTP/1.1\r\nHost: x\r\nConnection: Upgrade\r\n\r\n",
        "GET /wsg HTTP/1.1\r\nHost: x\r\nUpgrade: h2c\r\nConnection: Upgrade\r\n\r\n",
        "GET /wsg HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: keep-alive\r\n\r\n",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        arm_slot(0, bad[i]);
        conn_pool[0].pcb = protocore_net_host_pcb();
        tcp_capture_reset();
        handle();
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(tcp_captured(), "400"), bad[i]);
    }

    arm_slot(0, "GET /wsg HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "426 Upgrade Required"));
    tcp_capture_disable();
}
#endif

void test_upgrade_entry_points_on_dead_slot(void)
{
#if PROTOCORE_ENABLE_WEBSOCKET
    arm_slot(0, "GET /w HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n");
    conn_pool[0].pcb = NULL;
    tcp_capture_reset();
    ws_send_version_required(0);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[0].parse_state);

    arm_slot(0, "GET /w HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n");
    conn_pool[0].pcb = NULL;
    tcp_capture_reset();
    TEST_ASSERT_FALSE(ws_do_upgrade(0, &http_pool[0], 0));
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
#endif
#if PROTOCORE_ENABLE_SSE
    arm_slot(0, "GET /e HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = NULL;
    tcp_capture_reset();
    TEST_ASSERT_FALSE(protocore_sse_do_upgrade(0, &http_pool[0], 0));
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
#endif
    tcp_capture_disable();
}

#if PROTOCORE_ENABLE_SSE
static uint8_t g_sse_connected_id;
static int g_sse_connect_calls;
static void sse_on_connect(uint8_t id)
{
    g_sse_connected_id = id;
    g_sse_connect_calls++;
}

void test_sse_upgrade_fires_connect_handler(void)
{
    Sse.init(protocore_sse_span());
    g_sse_connect_calls = 0;
    g_sse_connected_id = 0xFF;
    on_sse("/evh", sse_on_connect);
    arm_slot(0, "GET /evh HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = protocore_net_host_pcb();
    tcp_capture_reset();
    handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "text/event-stream"));
    TEST_ASSERT_EQUAL_INT(1, g_sse_connect_calls);
    TEST_ASSERT_NOT_NULL(sse_find(0));
    TEST_ASSERT_EQUAL_UINT8(sse_find(0)->protocore_sse_id, g_sse_connected_id);
    tcp_capture_disable();
    Sse.init(protocore_sse_span());
}

void test_sse_send_on_dead_slot_writes_nothing(void)
{
    Sse.init(protocore_sse_span());
    live_slot(0);
    SseConn *sse = sse_alloc(0, "/events");
    TEST_ASSERT_NOT_NULL(sse);
    conn_pool[0].pcb = NULL;

    tcp_capture_reset();
    protocore_sse_send(sse->protocore_sse_id, "x", NULL, NULL);
    protocore_sse_broadcast("/events", "x", NULL, NULL);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
    tcp_capture_disable();
    Sse.init(protocore_sse_span());
}
#endif

#if PROTOCORE_ENABLE_WEBSOCKET

void test_ws_send_api_inactive_error_state_and_dead_slot(void)
{
    Ws.init(protocore_ws_span());
    live_slot(0);
    WsV.slot = 0;
    Ws.alloc(protocore_ws_span());
    WsConn *ws = WsV.found;
    TEST_ASSERT_NOT_NULL(ws);

    tcp_capture_reset();
    ws_send_binary(1, (const uint8_t *)"x", 1);
    ws_disconnect(1);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());

    ws->parse_state = WS_ERROR;
    tcp_capture_reset();
    ws_send_text(ws->ws_id, "nope");
    ws_send_binary(ws->ws_id, (const uint8_t *)"x", 1);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());

    ws->parse_state = WS_HEADER1;
    conn_pool[0].pcb = NULL;
    tcp_capture_reset();
    ws_send_text(ws->ws_id, "nope");
    ws_send_binary(ws->ws_id, (const uint8_t *)"x", 1);
    ws_disconnect(ws->ws_id);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
    tcp_capture_disable();
    Ws.init(protocore_ws_span());
}
#endif

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_response_headers_that_do_not_fit_are_refused);
    RUN_TEST(test_restart_and_stop);
    RUN_TEST(test_route_registration_variants_table_full);
    RUN_TEST(test_send_family_slot_and_conn_gone_guards);
    RUN_TEST(test_send_binary_body_with_nul);
    RUN_TEST(test_redirect_response_and_code_normalization);
    RUN_TEST(test_request_error_paths_te_method_ws);
    RUN_TEST(test_ws_sse_upgrade_failure_paths);
#if PROTOCORE_ENABLE_SSE
    RUN_TEST(test_sse_upgrade_pool_exhausted);
#endif

    RUN_TEST(test_handler_reads_body);
    RUN_TEST(test_handler_reads_query_param);
    RUN_TEST(test_handler_reads_header);
    RUN_TEST(test_wildcard_before_exact_wildcard_wins);

    RUN_TEST(test_fn_on_registers_and_dispatches);
    RUN_TEST(test_fn_on_path_copied_null_terminated);
    RUN_TEST(test_fn_on_table_full_extra_routes_dropped);
    RUN_TEST(test_fn_on_same_path_different_methods_are_distinct);

    RUN_TEST(test_fn_on_not_found_called_when_no_match);
    RUN_TEST(test_fn_on_not_found_not_called_when_match_exists);

    RUN_TEST(test_fn_set_cors_options_preflight_clears_slot);
    RUN_TEST(test_fn_set_cors_empty_string_disables);

    RUN_TEST(test_wrong_method_does_not_match);
    RUN_TEST(test_wrong_path_does_not_match);
    RUN_TEST(test_all_http_methods_dispatched);
    RUN_TEST(test_root_path_matches_exactly);
    RUN_TEST(test_root_path_does_not_match_subpath);
    RUN_TEST(test_wildcard_matches_any_suffix);
    RUN_TEST(test_wildcard_does_not_match_unrelated_prefix);
    RUN_TEST(test_exact_route_wins_when_registered_first);
    RUN_TEST(test_slot_not_stuck_in_complete_after_handle);
    RUN_TEST(test_parse_error_slot_auto_reset);

    RUN_TEST(stress_last_route_dispatched_in_full_table);
    RUN_TEST(stress_sequential_requests_no_state_leak);
    RUN_TEST(stress_all_slots_dispatched_simultaneously);
    RUN_TEST(stress_wildcard_matches_many_paths);
    RUN_TEST(stress_handle_with_no_complete_slots_is_nop);

    RUN_TEST(race_slot_complete_between_handle_calls);
    RUN_TEST(race_conn_freed_after_parse_complete);
    RUN_TEST(race_double_handle_no_double_dispatch);
    RUN_TEST(race_error_and_valid_slot_in_same_handle);
    RUN_TEST(race_callback_manually_resets_slot);

    RUN_TEST(test_uri_too_long_auto_resets_slot);

    RUN_TEST(test_transfer_encoding_chunked_is_501);
    RUN_TEST(test_transfer_encoding_identity_is_501);

    RUN_TEST(test_redirect_emits_location_and_status);
    RUN_TEST(test_redirect_invalid_code_defaults_to_302);
    RUN_TEST(test_mime_type_detection);

    RUN_TEST(test_serve_static_file_and_mime);
    RUN_TEST(test_serve_static_wildcard_and_route_full);
    RUN_TEST(test_response_header_cookie_guards);
    RUN_TEST(test_serve_static_index_fallback);
    RUN_TEST(test_serve_static_gzip_when_accepted);
    RUN_TEST(test_serve_static_no_gzip_when_not_accepted);
    RUN_TEST(test_serve_static_traversal_not_leaked);
    RUN_TEST(test_serve_static_missing_is_404);
    RUN_TEST(test_serve_static_etag_conditional_get);
    RUN_TEST(test_serve_static_inm_star_list_weak);
    RUN_TEST(test_serve_static_last_modified_conditional_get);
    RUN_TEST(test_serve_static_ims_field_comparisons);
    RUN_TEST(test_serve_static_no_timestamp);
    RUN_TEST(test_serve_static_if_modified_since_malformed);
    RUN_TEST(test_serve_static_cache_control);

    RUN_TEST(test_request_log_hook_fires);
    RUN_TEST(test_stats_endpoint_emits_json);
    RUN_TEST(test_status_text_reason_phrases);
    RUN_TEST(test_allow_header_lists_methods);
    RUN_TEST(test_listen_and_begin);
    RUN_TEST(test_begin_port_convenience);

#if PROTOCORE_ENABLE_WEBSOCKET
    RUN_TEST(test_ws_send_api);
#endif
#if PROTOCORE_ENABLE_SSE
    RUN_TEST(test_sse_broadcast_after_upgrade_matches_path);
    RUN_TEST(test_sse_send_api);
#endif
#if PROTOCORE_ENABLE_METRICS
    RUN_TEST(test_metrics_emits_prometheus);
#endif

    RUN_TEST(test_stats_counters_ignore_sub_200_status);
    RUN_TEST(test_response_trailer_cors_block_and_null_disable);
    RUN_TEST(test_cache_control_null_clears_header);
    RUN_TEST(test_empty_route_pattern_matches_nothing);
    RUN_TEST(test_path_param_capture_limits);
    RUN_TEST(test_path_param_segment_mismatches);
    RUN_TEST(test_worker_owner_filter_skips_foreign_slot);
    RUN_TEST(test_slot_poll_requires_registered_handler_with_poll);
    RUN_TEST(test_entity_too_large_auto_413);
    RUN_TEST(test_allow_header_dedupes_repeated_method);
    RUN_TEST(test_error_close_head_and_dead_connection);
    RUN_TEST(test_transfer_encoding_on_semantic_ingress_is_501);
    RUN_TEST(test_static_mount_rejects_non_get_methods);
    RUN_TEST(test_send_null_payload_and_slot_bounds);
    RUN_TEST(test_send_body_framing_paths);
    RUN_TEST(test_send_empty_and_redirect_dead_connection_guards);

    RUN_TEST(test_send_template_placeholder_edges);
    RUN_TEST(test_send_chunked_without_source);
    RUN_TEST(test_chunked_pump_small_window_and_connection_lost);
    RUN_TEST(test_response_header_null_value_empty_attrs_and_overflow);
    RUN_TEST(test_mime_type_extension_edges);

#if PROTOCORE_ENABLE_WEBSOCKET
    RUN_TEST(test_ws_upgrade_without_connect_handler);
    RUN_TEST(test_ws_dispatch_without_message_or_close_handler);
    RUN_TEST(test_ws_upgrade_handshake_gate);
    RUN_TEST(test_ws_send_api_inactive_error_state_and_dead_slot);
#endif
    RUN_TEST(test_upgrade_entry_points_on_dead_slot);
#if PROTOCORE_ENABLE_SSE
    RUN_TEST(test_sse_upgrade_fires_connect_handler);
    RUN_TEST(test_sse_send_on_dead_slot_writes_nothing);
#endif

    return UNITY_END();
}
