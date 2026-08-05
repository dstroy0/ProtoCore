// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for serve_file().
//
// Tests verify that:
//   - Missing file → 404
//   - Existing file → 200 with correct Content-Type and Content-Length
//   - File body is streamed to tcp_write
//   - Content-Length matches file size exactly
//   - Multiple content types are handled correctly
//   - Empty file → 200 with Content-Length: 0

#include "mnt_mock.h"
#include "protocore.h"             // pc_file_holds_slot: does the file pump hold this slot
#include "server/filesystem/mnt.h" // pc_mnt_mount: the fixture is the mounted store
#include <stdio.h>

#include <unity.h>

static const pc_mnt_backend *g_fs; // the mock store the serve_static mounts read through
static proto_bool handler_called = PROTO_FALSE;

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

static void handle_html(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    handler_called = PROTO_TRUE;
    const pc_mnt_backend *fs = mock_mnt();
    serve_file(slot_id, fs, "/index.html", "text/html");
}

static void handle_js(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    handler_called = PROTO_TRUE;
    const pc_mnt_backend *fs = mock_mnt();
    serve_file(slot_id, fs, "/app.js", "application/javascript");
}

static void handle_missing(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    handler_called = PROTO_TRUE;
    const pc_mnt_backend *fs = mock_mnt();
    serve_file(slot_id, fs, "/missing.txt", "text/plain");
}

void setUp()
{
    pc_server_reset();
    handler_called = PROTO_FALSE;

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

    mock_mnt_reset();
    // serve_file_internal resolves through the accessor, not through the backend handed to
    // serve_file(), so the fixture has to be the mounted store rather than just a pointer passed in.
    pc_mnt_mount(mock_mnt());
    tcp_capture_reset();
}

void tearDown()
{
    tcp_capture_disable();
    mock_mnt_clear();
}

// ---------------------------------------------------------------------------
// Helper: feed a complete HTTP request and drive handle()
// ---------------------------------------------------------------------------
static void feed_and_handle(uint8_t slot, const char *req_str)
{
    push_str(slot, req_str);
    http_parse(slot);
    handle();
}

// ====================================================================
// UNIT TESTS
// ====================================================================

void test_missing_file_returns_404()
{
    on_http("/page", HTTP_GET, handle_missing);
    mock_mnt_clear(); // no file set
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

// Read by the hoisted handlers below, which a lambda used to close over.
static proto_bool other_called = PROTO_FALSE;
static const char *cur_ctype = NULL;
static const char *cur_path = NULL;

static void h_empty(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    const pc_mnt_backend *fs = mock_mnt();
    serve_file(slot_id, fs, "/empty.txt", "text/plain");
}

static void h_big(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    const pc_mnt_backend *fs = mock_mnt();
    serve_file(slot_id, fs, "/big.bin", "application/octet-stream");
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
    const pc_mnt_backend *fs = mock_mnt();
    serve_file(slot_id, fs, cur_path, cur_ctype);
}

static void h_f(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    const pc_mnt_backend *fs = mock_mnt();
    serve_file(slot_id, fs, "/f.txt", "text/plain");
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
    // A body far larger than one send-buffer window: the cross-loop file pump must
    // deliver every byte, not truncate at the window. (The host mock never returns
    // ERR_MEM, so this guards the pump's body-length accounting; the real TCP
    // send-window paging is verified on hardware.)
#define BIG_N 16000
    static uint8_t big[BIG_N];
    for (size_t i = 0; i < BIG_N; i++)
    {
        big[i] = (uint8_t)('A' + (i % 26)); // printable, no NUL, position-dependent
    }

    on_http("/big", HTTP_GET, h_big);
    mock_mnt_set(big, BIG_N);

    feed_and_handle(0, "GET /big HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));

    char expected_cl[64];
    snprintf(expected_cl, sizeof(expected_cl), "Content-Length: %u", (unsigned)BIG_N);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), expected_cl));

    // The whole body must be present after the header boundary, byte-exact.
    const char *cap = tcp_captured();
    const char *body = strstr(cap, "\r\n\r\n");
    TEST_ASSERT_NOT_NULL(body);
    body += 4;
    size_t body_len = tcp_captured_len() - (size_t)(body - cap);
    TEST_ASSERT_EQUAL_size_t(BIG_N, body_len); // no truncation
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

        pc_server_reset();
        conn_pool[0] = (TcpConn){0};
        conn_pool[0].id = 0;
        conn_pool[0].state = CONN_ACTIVE;
        conn_pool[0].proto = PROTO_HTTP; // dispatch requires an explicit protocol
        conn_pool[0].pcb = pc_net_host_pcb();
        http_reset(0);
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

// ====================================================================
// SERVE_STATIC PATH JOINING / NEGOTIATION EDGES
// ====================================================================

// Re-arm slot 0 for another request within one test (a file response closes the slot).
static void rearm(uint8_t slot)
{
    conn_pool[slot] = (TcpConn){0};
    conn_pool[slot].id = slot;
    conn_pool[slot].state = CONN_ACTIVE;
    conn_pool[slot].proto = PROTO_HTTP;
    conn_pool[slot].pcb = pc_net_host_pcb();
    http_reset(slot);
    tcp_capture_reset();
}

// The mount root's three shapes must all join to the same on-disk path: a root that
// already ends in '/' (the sub-path's leading '/' is skipped so the separator is not
// doubled), a root without one (no separator inserted when the sub-path supplies it),
// and a null root (treated as empty).
void test_serve_static_root_join_variants()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/a.txt", "AAA", 0);
    mock_mnt_add_text("/b.txt", "BBB", 0);
    mock_mnt_add_text("/www/c.txt", "CCC", 0);

    serve_static("/ts", g_fs, "/www/"); // root ends in '/'
    feed_and_handle(0, "GET /ts/a.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "AAA"));

    rearm(0);
    serve_static("/nr", g_fs, NULL); // null root
    feed_and_handle(0, "GET /nr/b.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "BBB"));

    rearm(0);
    serve_static("/ns", g_fs, "/www"); // root without '/', sub-path supplies it
    feed_and_handle(0, "GET /ns/c.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "CCC"));

    mock_mnt_reset();
}

// An empty url_prefix mounts the bare wildcard "*": the prefix length is zero, so the whole
// request path is the sub-path.
void test_serve_static_empty_prefix_mount()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/any.txt", "anything", 0);
    serve_static("", g_fs, "/www");
    feed_and_handle(0, "GET /any.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "anything"));
    mock_mnt_reset();
}

// A sub-path ending in '/' is a directory request (index.html), and a mount whose root plus
// the request sub-path would overflow the 256-byte path buffer is refused with a 404 rather
// than served from a truncated path.
void test_serve_static_directory_and_overlong_path()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/docs/index.html", "<i>docs</i>", 0);
    serve_static("/", g_fs, "/www");
    feed_and_handle(0, "GET /docs/ HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "<i>docs</i>"));

    rearm(0);
    static char longroot[255];
    memset(longroot, 'r', sizeof(longroot) - 1);
    longroot[0] = '/';
    longroot[sizeof(longroot) - 1] = '\0'; // 254-char root: root + "/x" == 256, the join fails
    serve_static("/lp", g_fs, longroot);
    feed_and_handle(0, "GET /lp/x HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "404"));
    mock_mnt_reset();
}

// Pre-compressed negotiation: an Accept-Encoding that does not list gzip, and one that does
// but for a resource with no .gz variant, both serve the identity file.
void test_serve_static_gzip_negotiation_misses()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/app.js", "console.log(2)", 0);
    mock_mnt_add_text("/www/app.js.gz", "GZ", 0);
    mock_mnt_add_text("/www/plain.txt", "plain body", 0);
    serve_static("/", g_fs, "/www");

    feed_and_handle(0, "GET /app.js HTTP/1.1\r\nHost: x\r\nAccept-Encoding: deflate, br\r\n\r\n");
    TEST_ASSERT_NULL(strstr(tcp_captured(), "Content-Encoding: gzip"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "console.log(2)"));

    rearm(0);
    feed_and_handle(0, "GET /plain.txt HTTP/1.1\r\nHost: x\r\nAccept-Encoding: gzip\r\n\r\n");
    TEST_ASSERT_NULL(strstr(tcp_captured(), "Content-Encoding: gzip")); // no .gz variant exists
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "plain body"));
    mock_mnt_reset();
}

// A HEAD of a static file carries the full GET headers (including the configured CORS block)
// with no body; the matching GET carries the same headers plus the body.
void test_serve_static_head_and_cors_headers()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/page.html", "<html>body</html>", 0); // 17 bytes
    set_cors("*");
    serve_static("/", g_fs, "/www");

    feed_and_handle(0, "HEAD /page.html HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Content-Length: 17"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Access-Control-Allow-Origin: *"));
    TEST_ASSERT_NULL(strstr(tcp_captured(), "<html>body</html>")); // headers only
    size_t n = tcp_captured_len();
    TEST_ASSERT_TRUE(n > 4);
    TEST_ASSERT_EQUAL_STRING("\r\n\r\n", tcp_captured() + n - 4); // response ends at the headers

    rearm(0);
    feed_and_handle(0, "GET /page.html HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Access-Control-Allow-Origin: *"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "<html>body</html>"));

    set_cors(""); // restore the default for later tests
    mock_mnt_reset();
}

// RFC 9110 13.1.2 If-None-Match forms that must NOT match our tag: a list ending in a
// separator, tab-separated entries, a 'W' that does not begin a weak validator, a bare
// (unquoted) token, an unterminated quoted-string, and a same-length-but-different tag.
// Each must serve the full 200; only the real tag (weak or strong) yields 304.
void test_serve_static_inm_non_matching_forms()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/p.html", "123456789012345", (time_t)1000); // 15 bytes, mtime 1000
    serve_static("/", g_fs, "/www");

    // Pin the tag these cases are compared against: "<size hex>-<mtime hex>".
    feed_and_handle(0, "GET /p.html HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "ETag: \"f-3e8\""));

    static const char *misses[] = {
        "\"nope\",",      // list ends on the separator: the scan runs off the end
        "\"a\",\t\"b\"",  // tab between entries
        "Wxyz",           // 'W' not followed by '/': not a weak validator
        "bare-token",     // unquoted entity-tag
        "\"unterminated", // no closing quote
        "\"f-3e9\"",      // same length as our tag, one byte different
    };
    for (size_t i = 0; i < sizeof(misses) / sizeof(misses[0]); i++)
    {
        rearm(0);
        char req[200];
        snprintf(req, sizeof(req), "GET /p.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: %s\r\n\r\n", misses[i]);
        feed_and_handle(0, req);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(tcp_captured(), "HTTP/1.1 200 OK"), misses[i]);
    }

    // The weak form of the real tag still matches -> 304.
    rearm(0);
    feed_and_handle(0, "GET /p.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: W/\"f-3e8\"\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "304 Not Modified"));
    mock_mnt_reset();
}

// A body larger than the send window parks in the cross-loop pump. If the peer disappears
// before the window reopens, the next pump must drop the source and the continuation instead
// of writing into a dead connection.
void test_file_send_pump_connection_lost_midtransfer()
{
    mock_mnt_reset();
    static const size_t N = 9000;
    static uint8_t big[BIG_N];
    memset(big, 'Z', BIG_N);
    mock_mnt_add_text("/www/big.bin", big, BIG_N);
    serve_static("/", g_fs, "/www");

    mock_sndbuf_set(0); // no window: the headers queue, then the body transfer parks
    feed_and_handle(0, "GET /big.bin HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_TRUE(pc_file_holds_slot(0)); // parked, waiting for the window to reopen

    conn_pool[0].pcb = NULL; // peer went away mid-transfer
    handle();
    TEST_ASSERT_FALSE(pc_file_holds_slot(0));         // continuation dropped
    TEST_ASSERT_NULL(strstr(tcp_captured(), "ZZZZ")); // no body bytes were ever written

    mock_sndbuf_set(MOCK_SNDBUF_DEFAULT); // restore the window for the remaining tests
    mock_mnt_reset();
}

// ====================================================================
// STRESS TESTS
// ====================================================================

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
        conn_pool[slot].proto = PROTO_HTTP; // dispatch requires an explicit protocol
        conn_pool[slot].pcb = pc_net_host_pcb();
        http_reset(slot);
        tcp_capture_reset();
        handler_called = PROTO_FALSE;

        push_str(slot, "GET /f HTTP/1.1\r\n\r\n");
        http_parse(slot);
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
        conn_pool[slot].proto = PROTO_HTTP; // dispatch requires an explicit protocol
        conn_pool[slot].pcb = pc_net_host_pcb();
        http_reset(slot);
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
        http_parse(slot);
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

// ====================================================================
// CONDITIONAL-GET AND MOUNT-RESOLUTION EDGES
// ====================================================================

// Append a header to an already-parsed request slot. This is what a semantic ingress
// (HTTP/2 / HTTP/3) does: the HPACK/QPACK-decoded value is copied in verbatim, without
// the HTTP/1.x byte parser's leading-OWS strip.
static void inject_header(uint8_t slot, const char *key, const char *val)
{
    HttpReq *r = &http_pool[slot];
    TEST_ASSERT_TRUE(r->header_count < MAX_HEADERS);
    Header *h = &r->headers[r->header_count++];
    snprintf(h->key, sizeof(h->key), "%s", key);
    snprintf(h->val, sizeof(h->val), "%s", val);
}

// If-None-Match comparison skips leading OWS before the first entity-tag. The HTTP/1.x
// parser strips it, but a semantic ingress does not, so the tag must still match when the
// value arrives with the whitespace attached.
void test_inm_leading_ows_still_matches()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/p.html", "123456789012345", (time_t)1000); // 15 bytes, mtime 1000 -> "f-3e8"
    serve_static("/", g_fs, "/www");

    push_str(0, "GET /p.html HTTP/1.1\r\nHost: x\r\n\r\n");
    http_parse(0);
    inject_header(0, "If-None-Match", " \t\"f-3e8\""); // SP + HTAB ahead of the tag
    handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "304 Not Modified"));
    mock_mnt_reset();
}

// A comma-and-space delimited If-None-Match list is walked entry by entry: a leading
// separator, spaces around the commas, and a non-matching first entry must not stop the
// scan finding the real tag further along.
void test_inm_list_separators_reach_later_tag()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/p.html", "123456789012345", (time_t)1000);
    serve_static("/", g_fs, "/www");
    feed_and_handle(0, "GET /p.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: , \"a\" , \"f-3e8\"\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "304 Not Modified"));
    mock_mnt_reset();
}

// A 304 is a full response in its own right: it carries the configured CORS block, the
// validators, and no body.
void test_conditional_304_carries_cors_block()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/p.html", "123456789012345", (time_t)1000);
    set_cors("*");
    serve_static("/", g_fs, "/www");

    feed_and_handle(0, "GET /p.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: \"f-3e8\"\r\n\r\n");
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "304 Not Modified"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Access-Control-Allow-Origin: *\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(out, "ETag: \"f-3e8\""));
    TEST_ASSERT_NULL(strstr(out, "123456789012345")); // no body on a 304

    set_cors(""); // restore for later tests
    mock_mnt_reset();
}

// A url_prefix too long to hold its own wildcard registers NO route.
//
// It used to be stored truncated to MAX_PATH_LEN-1, which dropped the trailing '*' along with the
// overflow and quietly converted a subtree mount into an exact-match route for a path the caller
// never wrote. A mount that serves something other than what was asked for is worse than one that
// does not exist, so the prefix is built before a route slot is taken and an over-long one is
// refused outright.
void test_serve_static_overlong_prefix_registers_nothing()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/index.html", "<i>root</i>", 0);

    char prefix[MAX_PATH_LEN + 8];
    prefix[0] = '/';
    memset(prefix + 1, 'p', sizeof(prefix) - 2);
    prefix[sizeof(prefix) - 1] = '\0';
    serve_static(prefix, g_fs, "/www");

    // the truncated form the old code would have registered must NOT resolve
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

// A mount whose prefix carries a `:name` segment is matched segment-wise, so a request can
// legitimately be shorter than the stored pattern. The sub-path is then empty rather than a
// read past the end of the request path, and the mount's index.html is served.
void test_serve_static_param_mount_shorter_than_pattern()
{
    mock_mnt_reset();
    mock_mnt_add_text("/www/index.html", "<i>idx</i>", 0);
    serve_static("/a/:b", g_fs, "/www");                        // pattern "/a/:b*" - 5 chars before the '*'
    feed_and_handle(0, "GET /a/x HTTP/1.1\r\nHost: x\r\n\r\n"); // 4 chars: shorter than the prefix
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "<i>idx</i>"));
    mock_mnt_reset();
}

// A root that already ends in '/' and a bare-prefix request (empty sub-path) must not
// produce a doubled separator: the join is root + "index.html".
void test_serve_static_trailing_slash_root_bare_prefix()
{
    mock_mnt_reset();
    mock_mnt_add_text("/root/index.html", "<i>bare</i>", 0);
    serve_static("/s", g_fs, "/root/");
    feed_and_handle(0, "GET /s HTTP/1.1\r\nHost: x\r\n\r\n"); // sub-path is empty
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "<i>bare</i>"));
    mock_mnt_reset();
}

// A mount root long enough that root + sub-path overflows the 256-byte filesystem-path
// buffer is refused with a 404 rather than served from a silently truncated path.
void test_serve_static_joined_path_overflow_is_404()
{
    mock_mnt_reset();
    static char longroot[201];
    memset(longroot, 'r', sizeof(longroot) - 1);
    longroot[0] = '/';
    longroot[sizeof(longroot) - 1] = '\0'; // 200-char root
    serve_static("/", g_fs, longroot);

    char req[128];
    char sub[60];
    memset(sub, 's', sizeof(sub) - 1);
    sub[sizeof(sub) - 1] = '\0';
    snprintf(req, sizeof(req), "GET /%s HTTP/1.1\r\nHost: x\r\n\r\n", sub); // 200 + 1 + 59 > 256
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

    // Conditional-GET and mount-resolution edges
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
