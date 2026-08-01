// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit, stress, and race-condition tests for Layer 7 (Application).
//
// Sections:
//   FUNCTION I/O  - one test per PC method behavior
//   UNIT          - routing, wildcard, not-found, CORS dispatch
//   STRESS        - route-table full scan, sequential requests, all-slots
//   RACE SIM      - slot state hazards visible to handle()

#include "network_drivers/session/proto_handler.h" // proto_register/proto_get: the slot-poll dispatch table
#include "network_drivers/transport/listener.h"    // listener_stop_all() for begin() test cleanup
#include "protocore.h"                             // ws/sse upgrade entry points, pc_resp_holds_slot
#include <unity.h>

// All source layers compiled via native_app env - no stubs needed.

// ---- Shared helpers ------------------------------------------------

static bool handler_called;
static uint8_t handler_slot;

static void record_handler(uint8_t slot_id, HttpReq *)
{
    handler_called = true;
    handler_slot = slot_id;
}

// Push a raw HTTP request into a slot's ring buffer, reset the parser,
// and run http_parse so the slot is ready for handle().
static void arm_slot(uint8_t slot, const char *raw)
{
    conn_pool[slot] = {};
    conn_pool[slot].id = slot;
    conn_pool[slot].state = ConnState::CONN_ACTIVE;
    conn_pool[slot].proto = ConnProto::PROTO_HTTP; // dispatch requires an explicit protocol
    conn_pool[slot].pcb = nullptr;

    TcpConn *s = &conn_pool[slot];
    for (size_t i = 0; raw[i]; i++)
    {
        size_t next = (s->rx_head + 1) % RX_BUF_SIZE;
        s->rx_buffer[s->rx_head] = (uint8_t)raw[i];
        s->rx_head = next;
    }
    http_reset(slot);
    http_parse(slot);
}

// Push raw bytes into a ring buffer without resetting or parsing.
static void push_bytes(uint8_t slot, const char *data)
{
    TcpConn *s = &conn_pool[slot];
    for (size_t i = 0; data[i]; i++)
    {
        size_t next = (s->rx_head + 1) % RX_BUF_SIZE;
        s->rx_buffer[s->rx_head] = (uint8_t)data[i];
        s->rx_head = next;
    }
}

static PC *g_server;

void setUp()
{
    set_millis(0);
    DeterministicAsyncTCP::pool_init();
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = {};
        conn_pool[i].id = i;
        conn_pool[i].state = ConnState::CONN_ACTIVE;
        conn_pool[i].proto = ConnProto::PROTO_HTTP; // dispatch requires an explicit protocol
        http_reset(i);
    }
    handler_called = false;
    handler_slot = 255;
#if PC_ENABLE_WEBSOCKET
    ws_init(); // isolate ws_pool[] between tests (a leftover WS slot makes http_parse skip it)
#endif
#if PC_ENABLE_SSE
    pc_sse_init(); // isolate pc_sse_pool[] between tests
#endif
    g_server = new PC();
}

void tearDown()
{
    delete g_server;
    g_server = nullptr;
}

// ====================================================================
// FUNCTION I/O TESTS - PC::on()
// ====================================================================

void test_fn_on_registers_and_dispatches()
{
    g_server->on("/ping", HttpMethod::HTTP_GET, record_handler);
    arm_slot(0, "GET /ping HTTP/1.1\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_TRUE(handler_called);
}

void test_fn_on_path_copied_null_terminated()
{
    // A path of exactly MAX_PATH_LEN-1 chars must not overflow the route buffer.
    char path[MAX_PATH_LEN + 4];
    path[0] = '/';
    for (int i = 1; i < MAX_PATH_LEN - 1; i++)
    {
        path[i] = 'a';
    }
    path[MAX_PATH_LEN - 1] = '\0';
    g_server->on(path, HttpMethod::HTTP_GET, record_handler);
    arm_slot(0, "GET /a HTTP/1.1\r\n\r\n"); // won't match long path - just must not crash
    g_server->handle();
    TEST_PASS();
}

void test_fn_on_table_full_extra_routes_dropped()
{
    // Fill the table; on() beyond MAX_ROUTES must silently drop
    for (int i = 0; i < MAX_ROUTES + 5; i++)
    {
        g_server->on("/x", HttpMethod::HTTP_GET, record_handler);
    }
    arm_slot(0, "GET /x HTTP/1.1\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_TRUE(handler_called);
}

void test_fn_on_same_path_different_methods_are_distinct()
{
    static bool get_called = false;
    static bool post_called = false;
    g_server->on("/r", HttpMethod::HTTP_GET, [](uint8_t, HttpReq *) { get_called = true; });
    g_server->on("/r", HttpMethod::HTTP_POST, [](uint8_t, HttpReq *) { post_called = true; });

    arm_slot(0, "GET /r HTTP/1.1\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_TRUE(get_called);
    TEST_ASSERT_FALSE(post_called);

    arm_slot(0, "POST /r HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_TRUE(post_called);
}

// ====================================================================
// FUNCTION I/O TESTS - PC::on_not_found()
// ====================================================================

void test_fn_on_not_found_called_when_no_match()
{
    g_server->on_not_found(record_handler);
    arm_slot(0, "GET /nowhere HTTP/1.1\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_TRUE(handler_called);
}

void test_fn_on_not_found_not_called_when_match_exists()
{
    static bool nf = false;
    g_server->on("/here", HttpMethod::HTTP_GET, record_handler);
    g_server->on_not_found([](uint8_t, HttpReq *) { nf = true; });
    arm_slot(0, "GET /here HTTP/1.1\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_TRUE(handler_called);
    TEST_ASSERT_FALSE(nf);
}

// ====================================================================
// FUNCTION I/O TESTS - PC::set_cors()
// ====================================================================

void test_fn_set_cors_options_preflight_clears_slot()
{
    g_server->set_cors("*");
    arm_slot(0, "OPTIONS /x HTTP/1.1\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_NOT_EQUAL(ParseState::PARSE_COMPLETE, http_pool[0].parse_state);
}

void test_fn_set_cors_empty_string_disables()
{
    g_server->set_cors("*");
    g_server->set_cors(""); // disable
    g_server->on("/x", HttpMethod::HTTP_OPTIONS, record_handler);
    arm_slot(0, "OPTIONS /x HTTP/1.1\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_TRUE(handler_called); // routed normally, not intercepted as preflight
}

// ====================================================================
// UNIT TESTS - routing
// ====================================================================

void test_wrong_method_does_not_match()
{
    g_server->on("/r", HttpMethod::HTTP_POST, record_handler);
    arm_slot(0, "GET /r HTTP/1.1\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_FALSE(handler_called);
}

void test_wrong_path_does_not_match()
{
    g_server->on("/right", HttpMethod::HTTP_GET, record_handler);
    arm_slot(0, "GET /wrong HTTP/1.1\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_FALSE(handler_called);
}

void test_all_http_methods_dispatched()
{
    static int counts[7] = {};
    g_server->on("/get", HttpMethod::HTTP_GET, [](uint8_t, HttpReq *) { counts[0]++; });
    g_server->on("/post", HttpMethod::HTTP_POST, [](uint8_t, HttpReq *) { counts[1]++; });
    g_server->on("/put", HttpMethod::HTTP_PUT, [](uint8_t, HttpReq *) { counts[2]++; });
    g_server->on("/delete", HttpMethod::HTTP_DELETE, [](uint8_t, HttpReq *) { counts[3]++; });
    g_server->on("/patch", HttpMethod::HTTP_PATCH, [](uint8_t, HttpReq *) { counts[4]++; });
    g_server->on("/head", HttpMethod::HTTP_HEAD, [](uint8_t, HttpReq *) { counts[5]++; });
    g_server->on("/options", HttpMethod::HTTP_OPTIONS, [](uint8_t, HttpReq *) { counts[6]++; });

    arm_slot(0, "GET /get HTTP/1.1\r\n\r\n");
    g_server->handle();
    arm_slot(0, "POST /post HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    g_server->handle();
    arm_slot(0, "PUT /put HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    g_server->handle();
    arm_slot(0, "DELETE /delete HTTP/1.1\r\n\r\n");
    g_server->handle();
    arm_slot(0, "PATCH /patch HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    g_server->handle();
    arm_slot(0, "HEAD /head HTTP/1.1\r\n\r\n");
    g_server->handle();
    arm_slot(0, "OPTIONS /options HTTP/1.1\r\n\r\n");
    g_server->handle();

    for (int i = 0; i < 7; i++)
    {
        TEST_ASSERT_EQUAL_MESSAGE(1, counts[i], "method not dispatched");
    }
}

void test_root_path_matches_exactly()
{
    g_server->on("/", HttpMethod::HTTP_GET, record_handler);
    arm_slot(0, "GET / HTTP/1.1\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_TRUE(handler_called);
}

void test_root_path_does_not_match_subpath()
{
    g_server->on("/", HttpMethod::HTTP_GET, record_handler);
    arm_slot(0, "GET /other HTTP/1.1\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_FALSE(handler_called);
}

void test_wildcard_matches_any_suffix()
{
    g_server->on("/api/*", HttpMethod::HTTP_GET, record_handler);
    arm_slot(0, "GET /api/users/42 HTTP/1.1\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_TRUE(handler_called);
}

void test_wildcard_does_not_match_unrelated_prefix()
{
    g_server->on("/api/*", HttpMethod::HTTP_GET, record_handler);
    arm_slot(0, "GET /other/path HTTP/1.1\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_FALSE(handler_called);
}

void test_exact_route_wins_when_registered_first()
{
    static bool exact_called = false;
    g_server->on("/api/status", HttpMethod::HTTP_GET, [](uint8_t, HttpReq *) { exact_called = true; });
    g_server->on("/api/*", HttpMethod::HTTP_GET, record_handler);
    arm_slot(0, "GET /api/status HTTP/1.1\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_TRUE(exact_called);
    TEST_ASSERT_FALSE(handler_called);
}

void test_slot_not_stuck_in_complete_after_handle()
{
    g_server->on("/free", HttpMethod::HTTP_GET, record_handler);
    arm_slot(0, "GET /free HTTP/1.1\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_NOT_EQUAL(ParseState::PARSE_COMPLETE, http_pool[0].parse_state);
}

void test_parse_error_slot_auto_reset()
{
    push_bytes(0, "TOOLONGMETHODNAME /path HTTP/1.1\r\n\r\n");
    http_reset(0);
    http_parse(0);
    TEST_ASSERT_EQUAL(ParseState::PARSE_ERROR, http_pool[0].parse_state);
    g_server->handle();
    TEST_ASSERT_NOT_EQUAL(ParseState::PARSE_ERROR, http_pool[0].parse_state);
}

// Handler reads req->body from a POST request
void test_handler_reads_body()
{
    static char body_seen[32] = {};
    g_server->on("/body", HttpMethod::HTTP_POST,
                 [](uint8_t, HttpReq *req) { strncpy(body_seen, (const char *)req->body, sizeof(body_seen) - 1); });
    arm_slot(0, "POST /body HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello");
    g_server->handle();
    TEST_ASSERT_EQUAL_STRING("hello", body_seen);
}

// Handler calls http_get_query() to read a URL query parameter.
// Value is copied inside the handler because http_reset() clears
// http_pool[] before handle() returns to the test.
void test_handler_reads_query_param()
{
    static char q_seen[48] = {};
    g_server->on("/q", HttpMethod::HTTP_GET, [](uint8_t, HttpReq *req) {
        const char *v = http_get_query(req, "id");
        if (v)
        {
            strncpy(q_seen, v, sizeof(q_seen) - 1);
        }
    });
    arm_slot(0, "GET /q?id=42 HTTP/1.1\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_EQUAL_STRING("42", q_seen);
}

// Handler calls http_get_header() to read a custom request header.
// Same copy-inside-handler pattern as test_handler_reads_query_param.
void test_handler_reads_header()
{
    static char h_seen[48] = {};
    g_server->on("/h", HttpMethod::HTTP_GET, [](uint8_t, HttpReq *req) {
        const char *v = http_get_header(req, "X-Token");
        if (v)
        {
            strncpy(h_seen, v, sizeof(h_seen) - 1);
        }
    });
    arm_slot(0, "GET /h HTTP/1.1\r\nX-Token: secret\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_EQUAL_STRING("secret", h_seen);
}

// Wildcard registered BEFORE exact: first-match means wildcard wins.
// (Complement of test_exact_route_wins_when_registered_first.)
void test_wildcard_before_exact_wildcard_wins()
{
    static bool wildcard_called = false;
    static bool exact_called = false;
    g_server->on("/api/*", HttpMethod::HTTP_GET, [](uint8_t, HttpReq *) { wildcard_called = true; });
    g_server->on("/api/status", HttpMethod::HTTP_GET, [](uint8_t, HttpReq *) { exact_called = true; });
    arm_slot(0, "GET /api/status HTTP/1.1\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_TRUE(wildcard_called);
    TEST_ASSERT_FALSE(exact_called);
}

// ====================================================================
// STRESS TESTS
// ====================================================================

// Route table full (MAX_ROUTES entries); request matches the LAST route -
// worst-case O(N) linear scan must not corrupt any route or crash.
void stress_last_route_dispatched_in_full_table()
{
    static int last_count = 0;
    for (int i = 0; i < MAX_ROUTES - 1; i++)
    {
        char path[16];
        snprintf(path, sizeof(path), "/r%d", i);
        g_server->on(path, HttpMethod::HTTP_GET, [](uint8_t, HttpReq *) {});
    }
    g_server->on("/last", HttpMethod::HTTP_GET, [](uint8_t, HttpReq *) { last_count++; });

    arm_slot(0, "GET /last HTTP/1.1\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_EQUAL(1, last_count);
}

// 50 sequential requests on slot 0; handler records each dispatch.
// Verifies zero state leakage between requests.
void stress_sequential_requests_no_state_leak()
{
    static int seq_count = 0;
    g_server->on("/seq", HttpMethod::HTTP_GET, [](uint8_t, HttpReq *) { seq_count++; });

    for (int i = 0; i < 50; i++)
    {
        arm_slot(0, "GET /seq HTTP/1.1\r\n\r\n");
        g_server->handle();
    }
    TEST_ASSERT_EQUAL(50, seq_count);
}

// All four slots serve different routes simultaneously in a single handle() call.
void stress_all_slots_dispatched_simultaneously()
{
    static int counts[4] = {};
    g_server->on("/s0", HttpMethod::HTTP_GET, [](uint8_t, HttpReq *) { counts[0]++; });
    g_server->on("/s1", HttpMethod::HTTP_GET, [](uint8_t, HttpReq *) { counts[1]++; });
    g_server->on("/s2", HttpMethod::HTTP_GET, [](uint8_t, HttpReq *) { counts[2]++; });
    g_server->on("/s3", HttpMethod::HTTP_GET, [](uint8_t, HttpReq *) { counts[3]++; });

    arm_slot(0, "GET /s0 HTTP/1.1\r\n\r\n");
    arm_slot(1, "GET /s1 HTTP/1.1\r\n\r\n");
    arm_slot(2, "GET /s2 HTTP/1.1\r\n\r\n");
    arm_slot(3, "GET /s3 HTTP/1.1\r\n\r\n");

    g_server->handle();

    for (int i = 0; i < 4; i++)
    {
        TEST_ASSERT_EQUAL_MESSAGE(1, counts[i], "slot not dispatched");
    }
}

// Single wildcard route matches 10 different path suffixes; each dispatches exactly once.
void stress_wildcard_matches_many_paths()
{
    static int wc_count = 0;
    g_server->on("/api/*", HttpMethod::HTTP_GET, [](uint8_t, HttpReq *) { wc_count++; });

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
        g_server->handle();
    }
    TEST_ASSERT_EQUAL(10, wc_count);
}

// 20 sequential handle() calls with NO complete parse slots - must be idle no-ops.
void stress_handle_with_no_complete_slots_is_nop()
{
    g_server->on("/x", HttpMethod::HTTP_GET, record_handler);
    // All slots in ParseState::PARSE_METHOD (setUp resets them) - nothing to dispatch
    for (int i = 0; i < 20; i++)
    {
        g_server->handle();
    }
    TEST_ASSERT_FALSE(handler_called);
}

// ====================================================================
// RACE CONDITION SIMULATIONS
// ====================================================================

// Slot transitions to ParseState::PARSE_COMPLETE between tick and handle() slot scan -
// already covered by the normal flow; here we verify handle() dispatches
// a slot that became complete since the last call.
void race_slot_complete_between_handle_calls()
{
    static bool dispatched = false;
    g_server->on("/late", HttpMethod::HTTP_GET, [](uint8_t, HttpReq *) { dispatched = true; });

    g_server->handle(); // no complete slots yet
    TEST_ASSERT_FALSE(dispatched);

    arm_slot(0, "GET /late HTTP/1.1\r\n\r\n"); // becomes complete NOW
    g_server->handle();
    TEST_ASSERT_TRUE(dispatched);
}

// A slot is in ParseState::PARSE_COMPLETE but its conn state is ConnState::CONN_FREE (connection
// already dropped by a timeout between parse completion and handle()).
// send() must detect pcb==nullptr/ConnState::CONN_FREE and call http_reset() cleanly.
void race_conn_freed_after_parse_complete()
{
    g_server->on("/r", HttpMethod::HTTP_GET, record_handler);

    arm_slot(0, "GET /r HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(ParseState::PARSE_COMPLETE, http_pool[0].parse_state);

    // Simulate connection drop between parse and dispatch
    conn_pool[0].state = ConnState::CONN_FREE;
    conn_pool[0].pcb = nullptr;

    g_server->handle(); // must not crash; slot must be cleaned up
    TEST_ASSERT_NOT_EQUAL(ParseState::PARSE_COMPLETE, http_pool[0].parse_state);
}

// handle() is called twice without any new input - the second call must
// see no ParseState::PARSE_COMPLETE slots and dispatch nothing.
void race_double_handle_no_double_dispatch()
{
    static int dispatch_count = 0;
    g_server->on("/dd", HttpMethod::HTTP_GET, [](uint8_t, HttpReq *) { dispatch_count++; });

    arm_slot(0, "GET /dd HTTP/1.1\r\n\r\n");
    g_server->handle(); // dispatches once, resets slot
    g_server->handle(); // slot is ParseState::PARSE_METHOD - must dispatch 0 times

    TEST_ASSERT_EQUAL(1, dispatch_count);
}

// A ParseState::PARSE_ERROR slot is followed immediately by a valid slot; handle() must
// process the error slot (send 400) and also dispatch the valid slot.
void race_error_and_valid_slot_in_same_handle()
{
    static bool valid_dispatched = false;
    g_server->on("/ok", HttpMethod::HTTP_GET, [](uint8_t, HttpReq *) { valid_dispatched = true; });

    // Slot 0: inject a parse error
    push_bytes(0, "TOOLONGMETHODNAME /path HTTP/1.1\r\n\r\n");
    http_reset(0);
    http_parse(0);
    TEST_ASSERT_EQUAL(ParseState::PARSE_ERROR, http_pool[0].parse_state);

    // Slot 1: valid request
    arm_slot(1, "GET /ok HTTP/1.1\r\n\r\n");

    g_server->handle();

    TEST_ASSERT_NOT_EQUAL(ParseState::PARSE_ERROR, http_pool[0].parse_state); // 400 sent, reset
    TEST_ASSERT_TRUE(valid_dispatched);                                       // slot 1 dispatched
}

// A callback that calls http_reset() directly (instead of via send()) must
// not confuse handle()'s post-dispatch guard.
void race_callback_manually_resets_slot()
{
    static bool manual_reset_called = false;
    g_server->on("/mr", HttpMethod::HTTP_GET, [](uint8_t slot_id, HttpReq *) {
        manual_reset_called = true;
        http_reset(slot_id); // reset without sending a response
    });

    arm_slot(0, "GET /mr HTTP/1.1\r\n\r\n");
    g_server->handle(); // must not double-reset or crash

    TEST_ASSERT_TRUE(manual_reset_called);
    TEST_ASSERT_EQUAL(ParseState::PARSE_METHOD, http_pool[0].parse_state);
}

// ====================================================================
// 414 URI TOO LONG
// ====================================================================

void test_uri_too_long_auto_resets_slot()
{
    // Overflow the path buffer - handle() should send 414 and free the slot
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

    push_bytes(0, req);
    http_reset(0);
    http_parse(0);
    TEST_ASSERT_EQUAL(ParseState::PARSE_URI_TOO_LONG, http_pool[0].parse_state);

    g_server->handle(); // must send 414 and reset the slot
    TEST_ASSERT_NOT_EQUAL(ParseState::PARSE_URI_TOO_LONG, http_pool[0].parse_state);
}

// ====================================================================
// TRANSFER-ENCODING REJECTION
// ====================================================================

void test_transfer_encoding_chunked_is_501()
{
    // A request advertising Transfer-Encoding must be rejected with 501
    arm_slot(0, "POST /data HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n");
    g_server->on("/data", HttpMethod::HTTP_POST, [](uint8_t, HttpReq *) {
        TEST_FAIL_MESSAGE("handler must not be called for Transfer-Encoding request");
    });
    g_server->handle(); // must send 501, not dispatch the route
    TEST_ASSERT_NOT_EQUAL(ParseState::PARSE_COMPLETE, http_pool[0].parse_state);
}

void test_transfer_encoding_identity_is_501()
{
    // Even "identity" is rejected - we advertise no TE support at all
    arm_slot(0, "GET / HTTP/1.1\r\nTransfer-Encoding: identity\r\n\r\n");
    g_server->handle();
    TEST_ASSERT_NOT_EQUAL(ParseState::PARSE_COMPLETE, http_pool[0].parse_state);
}

// ====================================================================
// REDIRECT + MIME
// ====================================================================

void test_redirect_emits_location_and_status()
{
    conn_pool[0].state = ConnState::CONN_ACTIVE;
    conn_pool[0].proto = ConnProto::PROTO_HTTP; // dispatch requires an explicit protocol
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->redirect(0, 301, "/index.html");
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "HTTP/1.1 301 Moved Permanently"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Location: /index.html\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Content-Length: 0\r\n"));
    tcp_capture_disable();
    TEST_ASSERT_EQUAL(ConnState::CONN_FREE, (ConnState)conn_pool[0].state); // slot released
}

void test_redirect_invalid_code_defaults_to_302()
{
    conn_pool[0].state = ConnState::CONN_ACTIVE;
    conn_pool[0].proto = ConnProto::PROTO_HTTP; // dispatch requires an explicit protocol
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->redirect(0, 200, "/elsewhere"); // 200 is not a redirect code
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "HTTP/1.1 302 Found"));
    tcp_capture_disable();
}

void test_mime_type_detection()
{
    TEST_ASSERT_EQUAL_STRING("text/html", PC::mime_type("/index.html"));
    TEST_ASSERT_EQUAL_STRING("text/css", PC::mime_type("/css/site.css"));
    TEST_ASSERT_EQUAL_STRING("application/javascript", PC::mime_type("/app.JS")); // case-insensitive
    TEST_ASSERT_EQUAL_STRING("application/json", PC::mime_type("/api/data.json"));
    TEST_ASSERT_EQUAL_STRING("image/svg+xml", PC::mime_type("logo.svg"));
    TEST_ASSERT_EQUAL_STRING("image/png", PC::mime_type("a.b.c.png")); // last extension wins
    // Unknown / missing extension and dotfiles fall back.
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", PC::mime_type("/file.unknownext"));
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", PC::mime_type("/noext"));
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", PC::mime_type("/dir.with.dot/file"));
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", PC::mime_type(nullptr));
}

// ====================================================================
// SERVE_STATIC (mount a filesystem subtree)
// ====================================================================

static fs::FS g_static_fs; // mock FS instance (state lives in the global registry)

void test_serve_static_file_and_mime()
{
    fs::mock_fs_reset();
    static const char css[] = "body{color:red}";
    fs::mock_fs_add("/www/style.css", css);
    g_server->serve_static("/", g_static_fs, "/www");
    arm_slot(0, "GET /style.css HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Content-Type: text/css"));
    TEST_ASSERT_NOT_NULL(strstr(out, "body{color:red}"));
}

void test_serve_static_cache_control()
{
    fs::mock_fs_reset();
    static const char css[] = "body{color:red}";
    fs::mock_fs_add("/www/style.css", css);
    g_server->serve_static("/", g_static_fs, "/www");

    g_server->set_cache_control("max-age=3600");
    arm_slot(0, "GET /style.css HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Cache-Control: max-age=3600"));

    // Clearing it removes the header (and restores the default for later tests).
    g_server->set_cache_control("");
    arm_slot(0, "GET /style.css HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NULL(strstr(out, "Cache-Control:"));
}

void test_serve_static_index_fallback()
{
    fs::mock_fs_reset();
    static const char html[] = "<h1>home</h1>";
    fs::mock_fs_add("/www/index.html", html);
    g_server->serve_static("/", g_static_fs, "/www");
    arm_slot(0, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Content-Type: text/html"));
    TEST_ASSERT_NOT_NULL(strstr(out, "<h1>home</h1>"));
}

void test_serve_static_gzip_when_accepted()
{
    fs::mock_fs_reset();
    static const char gzbody[] = "\x1f\x8b"
                                 "FAKEGZIP"; // split avoids \x8bF hex-escape merge
    fs::mock_fs_add("/www/app.js.gz", (const uint8_t *)gzbody, sizeof(gzbody) - 1);
    g_server->serve_static("/", g_static_fs, "/www");
    arm_slot(0, "GET /app.js HTTP/1.1\r\nHost: x\r\nAccept-Encoding: gzip, deflate\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Content-Type: application/javascript")); // original type
    TEST_ASSERT_NOT_NULL(strstr(out, "Content-Encoding: gzip"));
}

// serve_static with a prefix already ending in '*' is stored as-is (no second wildcard); once the
// route table is full, further serve_static() calls are dropped (fail closed).
void test_serve_static_wildcard_and_route_full()
{
    fs::mock_fs_reset();
    static const char js[] = "x=1;";
    fs::mock_fs_add("/www/app.js", js);
    g_server->serve_static("/assets*", g_static_fs, "/www");
    arm_slot(0, "GET /assets/app.js HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out, "HTTP/1.1 200 OK")); // wildcard route served the file

    for (int i = 0; i < MAX_ROUTES + 3; i++) // fill + overflow the route table
    {
        g_server->serve_static("/s", g_static_fs, "/www");
    }
}

// The public header/cookie API rejects a bad slot / null args, and drops a cookie too large for the
// 256-byte per-slot buffer while a small custom header still fits.
static void hdr_guard_handler(uint8_t id, HttpReq *)
{
    static char big[512];
    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    g_server->set_cookie(id, "toobig", big, nullptr); // overflow -> dropped
    g_server->add_response_header(id, "X-Ok", "1");   // fits
    g_server->send(id, 200, "text/plain", "ok");
}

void test_response_header_cookie_guards()
{
    g_server->add_response_header(MAX_CONNS, "X", "y"); // out-of-range slot
    g_server->add_response_header(0, nullptr, "y");     // null name
    g_server->set_cookie(MAX_CONNS, "s", "1", nullptr); // out-of-range slot
    g_server->set_cookie(0, nullptr, "1", nullptr);     // null name
    g_server->clear_response_headers(MAX_CONNS);        // out-of-range slot

    g_server->on("/hdrtest", HttpMethod::HTTP_GET, hdr_guard_handler);
    arm_slot(0, "GET /hdrtest HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out, "X-Ok: 1")); // the small header was emitted
    TEST_ASSERT_NULL(strstr(out, "toobig"));      // the oversized cookie was dropped
}

void test_serve_static_no_gzip_when_not_accepted()
{
    fs::mock_fs_reset();
    static const char js[] = "console.log(1)";
    fs::mock_fs_add("/www/app.js", js);
    fs::mock_fs_add("/www/app.js.gz", "GZIPPED");
    g_server->serve_static("/", g_static_fs, "/www");
    arm_slot(0, "GET /app.js HTTP/1.1\r\nHost: x\r\n\r\n"); // no Accept-Encoding
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NULL(strstr(out, "Content-Encoding: gzip"));
    TEST_ASSERT_NOT_NULL(strstr(out, "console.log(1)"));
}

void test_serve_static_traversal_not_leaked()
{
    fs::mock_fs_reset();
    fs::mock_fs_add("/secret", "topsecret");
    g_server->serve_static("/", g_static_fs, "/www");
    arm_slot(0, "GET /../secret HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NULL(strstr(out, "topsecret")); // traversal must not leak the file
}

void test_serve_static_missing_is_404()
{
    fs::mock_fs_reset();
    fs::mock_fs_add("/www/exists.txt", "hi");
    g_server->serve_static("/", g_static_fs, "/www");
    arm_slot(0, "GET /nope.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out, "404"));
}

// A served file carries an ETag; a matching If-None-Match yields 304 (no body).
void test_serve_static_etag_conditional_get()
{
    fs::mock_fs_reset();
    fs::mock_fs_add("/www/page.html", "<html>hi</html>", (time_t)1000);
    g_server->serve_static("/", g_static_fs, "/www");

    // First GET: 200 with an ETag header.
    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
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

    // Second GET with that ETag in If-None-Match: 304, no body.
    char req[160];
    snprintf(req, sizeof(req), "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: %s\r\n\r\n", etag);
    arm_slot(0, req);
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    const char *out2 = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out2, "304 Not Modified"));
    TEST_ASSERT_NOT_NULL(strstr(out2, etag));          // ETag echoed
    TEST_ASSERT_NULL(strstr(out2, "<html>hi</html>")); // body suppressed
}

// RFC 9110 13.1.2: If-None-Match supports "*", a comma-separated list, and weak
// comparison (W/"x" matches our strong "x"). All three must yield 304.
void test_serve_static_inm_star_list_weak()
{
    fs::mock_fs_reset();
    fs::mock_fs_add("/www/page.html", "<html>hi</html>", (time_t)1000);
    g_server->serve_static("/", g_static_fs, "/www");

    // First GET to capture the strong ETag (with quotes).
    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
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
    // (a) "*" matches any current representation -> 304.
    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: *\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "304 Not Modified"));
    tcp_capture_disable();

    // (b) weak validator W/"x" matches our strong "x" -> 304.
    snprintf(req, sizeof(req), "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: W/%s\r\n\r\n", etag);
    arm_slot(0, req);
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "304 Not Modified"));
    tcp_capture_disable();

    // (c) a list containing the tag (after a non-matching one) -> 304.
    snprintf(req, sizeof(req), "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: \"nope\", %s\r\n\r\n", etag);
    arm_slot(0, req);
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "304 Not Modified"));
    tcp_capture_disable();

    // (d) a list with only non-matching tags -> 200 (full response).
    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: \"a\", \"b\"\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "HTTP/1.1 200 OK"));
    tcp_capture_disable();

    // (e) a non-matching tag followed by a trailing comma+space: the scan reaches end
    // of string after the separator and stops -> 200 (exercises the empty-remainder break).
    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: \"nope\", \r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "HTTP/1.1 200 OK"));
    tcp_capture_disable();
}

// A served file carries Last-Modified; If-Modified-Since drives a conditional GET
// (304 when not newer than the client's date), with If-None-Match taking precedence.
void test_serve_static_last_modified_conditional_get()
{
    fs::mock_fs_reset();
    fs::mock_fs_add("/www/page.html", "<html>hi</html>", (time_t)1000); // 1970-01-01 00:16:40 GMT
    g_server->serve_static("/", g_static_fs, "/www");
    const char *LM = "Thu, 01 Jan 1970 00:16:40 GMT";
    char req[200];
    const char *o;

    // (1) plain GET: 200 carries the Last-Modified header.
    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    o = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(o, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(o, "Last-Modified: Thu, 01 Jan 1970 00:16:40 GMT\r\n"));

    // (2) If-Modified-Since == mtime -> not modified -> 304, no body.
    snprintf(req, sizeof(req), "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-Modified-Since: %s\r\n\r\n", LM);
    arm_slot(0, req);
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    o = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(o, "304 Not Modified"));
    TEST_ASSERT_NULL(strstr(o, "<html>hi</html>"));

    // (3) If-Modified-Since one second older than mtime -> file IS newer -> 200 + body.
    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-Modified-Since: Thu, 01 Jan 1970 00:16:39 GMT\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    o = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(o, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(o, "<html>hi</html>"));

    // (4) If-Modified-Since newer than mtime -> 304.
    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-Modified-Since: Fri, 02 Jan 1970 00:00:00 GMT\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    o = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(o, "304 Not Modified"));

    // (5) If-None-Match (non-matching) takes precedence: If-Modified-Since ignored -> 200.
    snprintf(req, sizeof(req),
             "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: \"deadbeef\"\r\nIf-Modified-Since: %s\r\n\r\n", LM);
    arm_slot(0, req);
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    o = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(o, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(o, "<html>hi</html>"));
}

// http_not_modified_since compares fields most-significant first and returns as soon as
// one differs. mtime 1000 == Thu, 01 Jan 1970 00:16:40 GMT; each If-Modified-Since below
// differs in exactly one field (year/month/hour/minute) and is later than the file, so
// each drives the corresponding early return -> 304. (Day and second are covered by the
// preceding test's cases 4 and 3.)
void test_serve_static_ims_field_comparisons()
{
    fs::mock_fs_reset();
    fs::mock_fs_add("/www/page.html", "<html>hi</html>", (time_t)1000);
    g_server->serve_static("/", g_static_fs, "/www");
    const char *ims[] = {
        "Fri, 01 Jan 1971 00:16:40 GMT", // year differs (file older) -> 304
        "Sun, 01 Feb 1970 00:16:40 GMT", // month differs -> 304
        "Thu, 01 Jan 1970 01:16:40 GMT", // hour differs -> 304
        "Thu, 01 Jan 1970 00:17:40 GMT", // minute differs -> 304
    };
    char req[200];
    for (size_t i = 0; i < sizeof(ims) / sizeof(ims[0]); i++)
    {
        snprintf(req, sizeof(req), "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-Modified-Since: %s\r\n\r\n", ims[i]);
        arm_slot(0, req);
        conn_pool[0].pcb = &_mock_pcb;
        tcp_capture_reset();
        g_server->handle();
        const char *o = tcp_captured();
        tcp_capture_disable();
        TEST_ASSERT_NOT_NULL(strstr(o, "304 Not Modified"));
        TEST_ASSERT_NULL(strstr(o, "<html>hi</html>"));
    }
    // The year-younger direction takes the other branch of the same compare -> 200.
    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-Modified-Since: Wed, 01 Jan 1969 00:16:40 GMT\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    const char *o = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(o, "HTTP/1.1 200 OK")); // file is newer than 1969 -> full body
    TEST_ASSERT_NOT_NULL(strstr(o, "<html>hi</html>"));
}

// A stat timestamp that gmtime_r() cannot represent (garbage/overflowing clock value):
// the server must omit Last-Modified and treat the resource as always-modified - never
// a stale 304 - and still serve the body.
void test_serve_static_unrepresentable_mtime()
{
    fs::mock_fs_reset();
    fs::mock_fs_add("/www/page.html", "<html>hi</html>", (time_t)1 << 60); // year far past what tm_year holds
    g_server->serve_static("/", g_static_fs, "/www");

    // (a) plain GET: 200 with no Last-Modified line (http_rfc1123 bailed).
    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    const char *o = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(o, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NULL(strstr(o, "Last-Modified:")); // date omitted

    // (b) If-Modified-Since: gmtime_r on the file mtime fails -> not-modified false -> 200 + body.
    arm_slot(0, "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-Modified-Since: Thu, 01 Jan 2099 00:00:00 GMT\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    o = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(o, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(o, "<html>hi</html>"));
}

// A malformed If-Modified-Since must fail safe: serve 200 (the body), never a stale
// 304. Includes the off-by-alignment month token ("ebM" lives inside "FebMar") that
// must NOT mis-parse as a real month.
void test_serve_static_if_modified_since_malformed()
{
    fs::mock_fs_reset();
    fs::mock_fs_add("/www/page.html", "<html>hi</html>", (time_t)1000); // Jan 1970
    g_server->serve_static("/", g_static_fs, "/www");
    const char *bad[] = {
        "not a date",                    // sscanf field count != 6
        "Thu, 01",                       // truncated
        "Thu, 01 ebM 1970 00:00:00 GMT", // bogus month token at a non-3 offset
        "Thu, 01 Xyz 1970 00:00:00 GMT", // unknown month
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        char req[200];
        snprintf(req, sizeof(req), "GET /page.html HTTP/1.1\r\nHost: x\r\nIf-Modified-Since: %s\r\n\r\n", bad[i]);
        arm_slot(0, req);
        conn_pool[0].pcb = &_mock_pcb;
        tcp_capture_reset();
        g_server->handle();
        const char *o = tcp_captured();
        tcp_capture_disable();
        TEST_ASSERT_NOT_NULL(strstr(o, "HTTP/1.1 200 OK")); // not 304
        TEST_ASSERT_NOT_NULL(strstr(o, "<html>hi</html>")); // body served
    }
}

// ====================================================================
// ACCESS-LOG HOOK + RUNTIME STATS
// ====================================================================

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

void test_request_log_hook_fires()
{
    g_log_calls = 0;
    g_server->on_request_log(capture_log);
    g_server->on("/hi", HttpMethod::HTTP_GET,
                 [](uint8_t id, HttpReq *) { g_server->send(id, 200, "text/plain", "hello"); });
    arm_slot(0, "GET /hi HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    g_server->handle();
    TEST_ASSERT_EQUAL_INT(1, g_log_calls);
    TEST_ASSERT_EQUAL_STRING("GET", g_log_method);
    TEST_ASSERT_EQUAL_STRING("/hi", g_log_path);
    TEST_ASSERT_EQUAL_INT(200, g_log_status);
    TEST_ASSERT_EQUAL_INT(5, g_log_bytes); // "hello"
    g_server->on_request_log(nullptr);
}

void test_stats_endpoint_emits_json()
{
    g_server->on("/stats", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { g_server->stats(id); });
    arm_slot(0, "GET /stats HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out, "application/json"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"uptime_ms\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"requests\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"http_2xx\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"http_4xx\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"active_conns\""));
}

#if PC_ENABLE_METRICS
// Prometheus /metrics emits the stats counters in text exposition format.
void test_metrics_emits_prometheus()
{
    conn_pool[0] = {};
    conn_pool[0].id = 0;
    conn_pool[0].state = ConnState::CONN_ACTIVE;
    conn_pool[0].proto = ConnProto::PROTO_HTTP; // dispatch requires an explicit protocol
    conn_pool[0].pcb = &_mock_pcb;
    http_reset(0);
    tcp_capture_reset();
    g_server->metrics(0);
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "text/plain; version=0.0.4"));
    TEST_ASSERT_NOT_NULL(strstr(out, "# TYPE pc_http_requests_total counter"));
    TEST_ASSERT_NOT_NULL(strstr(out, "pc_http_responses_total{class=\"2xx\"}"));
    TEST_ASSERT_NOT_NULL(strstr(out, "pc_free_heap_bytes"));
    TEST_ASSERT_NOT_NULL(strstr(out, "pc_uptime_seconds"));

    // Every sample line must actually carry a VALUE. Asserting only that the metric NAME appears
    // is what let a template/resolver name mismatch ship: three {{resp_Nxx}} placeholders resolved
    // to nothing, so the body held `pc_http_responses_total{class="2xx"} ` with an empty value -
    // which is not valid Prometheus text format, and makes a scrape of the WHOLE endpoint fail.
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
            len--; // a value-less line is exactly what a trailing-space trim exposes
        }
        if (len && ln[0] != '#')
        {
            // "name{labels} value" - the last space must be followed by at least one character.
            const char *sp = nullptr;
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

#if PC_ENABLE_SSE
// Regression: pc_sse_do_upgrade() must store the request path by VALUE before
// http_reset() zeroes the parser buffer, so a later path-matched pc_sse_broadcast()
// reaches the client. (A dangling path pointer made broadcasts silently miss.)
void test_sse_broadcast_after_upgrade_matches_path()
{
    g_server->on_sse("/events", nullptr);

    conn_pool[0] = {};
    conn_pool[0].id = 0;
    conn_pool[0].state = ConnState::CONN_ACTIVE;
    conn_pool[0].proto = ConnProto::PROTO_HTTP; // dispatch requires an explicit protocol
    conn_pool[0].pcb = &_mock_pcb;
    push_bytes(0, "GET /events HTTP/1.1\r\n\r\n");
    http_reset(0);
    http_parse(0);

    tcp_capture_reset();
    g_server->handle(); // dispatch -> pc_sse_do_upgrade (200 text/event-stream)
    g_server->pc_sse_broadcast("/events", "hello", "msg");
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "text/event-stream")); // upgrade happened
    TEST_ASSERT_NOT_NULL(strstr(out, "data: hello"));       // broadcast matched the stored path
    tcp_capture_disable();
}
#endif

#if PC_ENABLE_WEBSOCKET
// The WebSocket send API: bad-id / inactive / terminal-state guards send
// nothing; a live connection frames text (0x81) and binary (0x82) payloads and
// flushes, and ws_disconnect queues a Close frame (0x88).
void test_ws_send_api()
{
    ws_init();
    conn_pool[0] = {};
    conn_pool[0].id = 0;
    conn_pool[0].state = ConnState::CONN_ACTIVE;
    conn_pool[0].proto = ConnProto::PROTO_HTTP;
    conn_pool[0].pcb = &_mock_pcb;
    WsConn *ws = ws_alloc(0);
    TEST_ASSERT_NOT_NULL(ws);

    // Guards: out-of-range id and an id that is in range but inactive.
    tcp_capture_reset();
    g_server->ws_send_text(MAX_WS_CONNS, "x");                       // id >= MAX
    g_server->ws_send_text(1, "x");                                  // in range, inactive
    g_server->ws_send_binary(MAX_WS_CONNS, (const uint8_t *)"x", 1); // id >= MAX
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());

    // Text frame -> FIN|TEXT opcode.
    tcp_capture_reset();
    g_server->ws_send_text(0, "hello");
    TEST_ASSERT_TRUE(tcp_captured_len() >= 2);
    TEST_ASSERT_EQUAL_HEX8(0x81, (uint8_t)tcp_captured()[0]);

    // Binary frame -> FIN|BINARY opcode.
    tcp_capture_reset();
    const uint8_t payload[3] = {1, 2, 3};
    g_server->ws_send_binary(0, payload, sizeof(payload));
    TEST_ASSERT_TRUE(tcp_captured_len() >= 2);
    TEST_ASSERT_EQUAL_HEX8(0x82, (uint8_t)tcp_captured()[0]);

    // A terminal parse state suppresses further sends.
    ws->parse_state = WsParseState::WS_CLOSED;
    tcp_capture_reset();
    g_server->ws_send_text(0, "nope");
    g_server->ws_send_binary(0, payload, sizeof(payload));
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
    ws->parse_state = WsParseState::WS_HEADER1; // reopen for disconnect

    // Disconnect: Close frame (opcode 0x88); the out-of-range id is a no-op.
    tcp_capture_reset();
    g_server->ws_disconnect(MAX_WS_CONNS);
    g_server->ws_disconnect(0);
    TEST_ASSERT_TRUE(tcp_captured_len() >= 2);
    TEST_ASSERT_EQUAL_HEX8(0x88, (uint8_t)tcp_captured()[0]);
    tcp_capture_disable();
}
#endif

#if PC_ENABLE_SSE
// The SSE send API: pc_sse_send writes an event/id/data block to the bound slot;
// bad-id / inactive guards send nothing; pc_sse_broadcast skips connections whose
// stored path does not match.
void test_sse_send_api()
{
    pc_sse_init();
    conn_pool[0] = {};
    conn_pool[0].id = 0;
    conn_pool[0].state = ConnState::CONN_ACTIVE;
    conn_pool[0].proto = ConnProto::PROTO_HTTP;
    conn_pool[0].pcb = &_mock_pcb;
    SseConn *sse = pc_sse_alloc(0, "/events");
    TEST_ASSERT_NOT_NULL(sse);

    // Guards send nothing.
    tcp_capture_reset();
    g_server->pc_sse_send(MAX_SSE_CONNS, "x"); // id >= MAX
    g_server->pc_sse_send(1, "x");             // in range, inactive
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());

    // A live send emits the event, id, and data fields (RFC-style SSE block).
    tcp_capture_reset();
    g_server->pc_sse_send(0, "hi", "msg", "42");
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "event: msg"));
    TEST_ASSERT_NOT_NULL(strstr(out, "id: 42"));
    TEST_ASSERT_NOT_NULL(strstr(out, "data: hi"));

    // Broadcast to a non-matching path skips the connection (no output).
    tcp_capture_reset();
    g_server->pc_sse_broadcast("/other", "skip");
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
    tcp_capture_disable();
}
#endif

// status_text() renders the reason phrase for every code the server emits.
// send() formats "HTTP/1.1 <code> <reason>", so drive it across the codes that
// higher-level tests do not already exercise (plus an unknown code -> default).
void test_status_text_reason_phrases()
{
    struct StatusCase
    {
        int code;
        const char *reason;
    };
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
        {999, "Unknown"}, // 999 -> default
#if PC_ENABLE_WEBDAV
        {207, "Multi-Status"},
        {412, "Precondition Failed"},
        {423, "Locked"},
        {502, "Bad Gateway"},
#endif
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        conn_pool[0] = {};
        conn_pool[0].id = 0;
        conn_pool[0].state = ConnState::CONN_ACTIVE;
        conn_pool[0].proto = ConnProto::PROTO_HTTP;
        conn_pool[0].pcb = &_mock_pcb;
        http_reset(0);
        tcp_capture_reset();
        g_server->send(0, cases[i].code, "text/plain", "x");
        char want[48];
        snprintf(want, sizeof(want), "HTTP/1.1 %d %s", cases[i].code, cases[i].reason);
        TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), want));
    }
    tcp_capture_disable();
}

// The length-aware send() overload carries a binary body (embedded NUL bytes) intact: Content-Length counts
// every octet and the bytes after the header terminator match the source byte-for-byte. This is what a
// gRPC-web frame / protobuf / octet-stream response needs (the const char* overload would strnlen-truncate).
void test_send_binary_body_with_nul()
{
    conn_pool[0].id = 0;
    conn_pool[0].state = ConnState::CONN_ACTIVE;
    conn_pool[0].proto = ConnProto::PROTO_HTTP;
    conn_pool[0].pcb = &_mock_pcb;
    http_reset(0);
    const uint8_t body[] = {0x00, 0x00, 0x00, 0x00, 0x05, 'h', 'e', 0x00, 'l', 'o'}; // NUL-laden, 10 octets
    tcp_capture_reset();
    g_server->send(0, 200, "application/grpc-web+proto", body, sizeof(body));
    const char *out = tcp_captured();
    size_t out_len = tcp_captured_len();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Content-Type: application/grpc-web+proto\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Content-Length: 10\r\n")); // counts the NUL bytes, not strlen (=0)
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
    TEST_ASSERT_EQUAL_UINT(sizeof(body), (unsigned)(out_len - hdr_end)); // whole body present
    TEST_ASSERT_EQUAL_INT(0, memcmp(out + hdr_end, body, sizeof(body))); // byte-for-byte, NULs and all
}

// A 405 lists every method registered for the matched path in the Allow header;
// method_name() renders each token. Register PATCH/HEAD/OPTIONS on one path and
// request it with an unregistered method.
void test_allow_header_lists_methods()
{
    g_server->on("/m", HttpMethod::HTTP_PATCH, record_handler);
    g_server->on("/m", HttpMethod::HTTP_OPTIONS, record_handler);
    g_server->on("/m", HttpMethod::HTTP_HEAD, record_handler);
    g_server->on("/m", HttpMethod::HTTP_PUT, record_handler);
    g_server->on("/m", HttpMethod::HTTP_METHOD_UNKNOWN, record_handler); // -> method_name() default ""
    arm_slot(0, "DELETE /m HTTP/1.1\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb; // arm_slot leaves pcb null; the 405 must emit
    tcp_capture_reset();
    g_server->handle();
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "405"));
    TEST_ASSERT_NOT_NULL(strstr(out, "PATCH"));
    TEST_ASSERT_NOT_NULL(strstr(out, "OPTIONS"));
    TEST_ASSERT_NOT_NULL(strstr(out, "HEAD"));
    TEST_ASSERT_NOT_NULL(strstr(out, "PUT"));
    tcp_capture_disable();
}

// listen() registers listener slots and rejects once the table (MAX_LISTENERS)
// is full; begin() requires at least one listener, then brings up the pools and
// listeners. Uses a local server so the global listener slots are the only
// shared state (released with listener_stop_all()).
void test_listen_and_begin()
{
    PC srv;

    // begin() before any listen() -> no-listeners error, no side effects.
    TEST_ASSERT_EQUAL_INT32(pc_result::PC_ERR_NO_LISTENERS, srv.begin());

    // Fill the listener table, then the next listen() is rejected. listen() returns each
    // listener's id (its index), so the i-th call returns i.
    for (int i = 0; i < MAX_LISTENERS; i++)
    {
        TEST_ASSERT_EQUAL_INT32(i, srv.listen((uint16_t)(9100 + i)));
    }
    TEST_ASSERT_EQUAL_INT32(pc_result::PC_ERR_LISTENER_FULL, srv.listen(9999));

    // begin() now brings the registered listeners up.
    TEST_ASSERT_EQUAL_INT32(pc_result::PC_OK, srv.begin());
    listener_stop_all(); // release the global listener slots for later tests
}

// begin(port) is the one-call convenience: listen(port) then begin(). When the
// listener table is already full its listen() fails and begin(port) forwards the
// error without binding.
void test_begin_port_convenience()
{
    PC srv;
    TEST_ASSERT_EQUAL_INT32(pc_result::PC_OK, srv.begin((uint16_t)8080));
    listener_stop_all();

    PC full;
    for (int i = 0; i < MAX_LISTENERS; i++)
    {
        full.listen((uint16_t)(9300 + i));
    }
    TEST_ASSERT_EQUAL_INT32(pc_result::PC_ERR_LISTENER_FULL, full.begin((uint16_t)9999));
}

// restart() = stop() + begin(): it forwards the no-listeners error before any listen(), and
// otherwise cycles the listeners back up. stop() must be an idempotent teardown.
void test_restart_and_stop()
{
    PC srv;
    // Before any listener, restart() forwards the no-listeners error (no stop()/begin()).
    TEST_ASSERT_EQUAL_INT32(pc_result::PC_ERR_NO_LISTENERS, srv.restart());

    // Bring a listener up, then restart() tears down and re-binds it. The first listen() returns id 0.
    TEST_ASSERT_EQUAL_INT32(0, srv.listen((uint16_t)9500));
    TEST_ASSERT_EQUAL_INT32(pc_result::PC_OK, srv.begin());
    TEST_ASSERT_EQUAL_INT32(pc_result::PC_OK, srv.restart());

    // stop() tears everything down; a second stop() with nothing active is a safe no-op.
    srv.stop();
    srv.stop();
    listener_stop_all();
}

// Every route-registration variant guards `_route_count >= MAX_ROUTES` and silently drops the route.
// The plain on() path is covered elsewhere; this hits the iface / regex / auth / ws / sse variants.
void test_route_registration_variants_table_full()
{
    PC srv;
    for (int i = 0; i < MAX_ROUTES; i++)
    {
        srv.on("/x", HttpMethod::HTTP_GET, record_handler);
    }

    srv.on("/i", HttpMethod::HTTP_GET, record_handler, pc_iface::PC_IFACE_STA); // on(..., iface)
    srv.on_regex("/re.*", HttpMethod::HTTP_GET, record_handler);
#if PC_ENABLE_AUTH
    srv.on("/a", HttpMethod::HTTP_GET, record_handler, "realm", "u", "p", false);
#endif
#if PC_ENABLE_WEBSOCKET
    srv.on_ws("/ws", nullptr, nullptr, nullptr);
#endif
#if PC_ENABLE_SSE
    srv.on_sse("/sse", nullptr);
#endif

    // The dropped iface route does not dispatch: a request to it falls through (handler untouched).
    arm_slot(0, "GET /i HTTP/1.1\r\n\r\n");
    handler_called = false;
    srv.handle();
    TEST_ASSERT_FALSE(handler_called);
}

// redirect / send_template / send_chunked all guard slot_id >= MAX_CONNS and a gone connection
// (freed slot / null pcb -> http_reset + return). Neither guard was exercised.
void test_send_family_slot_and_conn_gone_guards()
{
    g_server->redirect(MAX_CONNS, 302, "/x"); // slot out of range -> no-op
    g_server->send_template(MAX_CONNS, 200, "text/html", "hi", nullptr);
    g_server->send_chunked(MAX_CONNS, 200, "text/plain", nullptr, nullptr);

    conn_pool[0].state = ConnState::CONN_FREE; // connection gone
    conn_pool[0].pcb = nullptr;
    g_server->redirect(0, 302, "/x");
    g_server->send_template(0, 200, "text/html", "hi", nullptr);
    g_server->send_chunked(0, 200, "text/plain", nullptr, nullptr);
    TEST_PASS(); // guards hit, nothing sent, no crash
}

void test_redirect_response_and_code_normalization()
{
    conn_pool[0] = {};
    conn_pool[0].id = 0;
    conn_pool[0].state = ConnState::CONN_ACTIVE;
    conn_pool[0].proto = ConnProto::PROTO_HTTP;
    conn_pool[0].pcb = &_mock_pcb;
    http_reset(0);
    tcp_capture_reset();
    g_server->redirect(0, 307, "/new");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "307 Temporary Redirect"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Location: /new"));

    // An out-of-range redirect code normalizes to 302.
    conn_pool[0].state = ConnState::CONN_ACTIVE;
    conn_pool[0].pcb = &_mock_pcb;
    http_reset(0);
    tcp_capture_reset();
    g_server->redirect(0, 200, "/z");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "302 Found"));
    tcp_capture_disable();
}

void test_request_error_paths_te_method_ws()
{
    g_server->on("/only-get", HttpMethod::HTTP_GET, record_handler);
#if PC_ENABLE_WEBSOCKET
    g_server->on_ws("/ws", nullptr, nullptr, nullptr);
#endif
    // Wrong method to a GET-only route -> 405 with an Allow header.
    arm_slot(0, "POST /only-get HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "405"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Allow:"));

#if PC_ENABLE_WEBSOCKET
    // A WS route hit without an upgrade -> 400.
    arm_slot(0, "GET /ws HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "400"));

    // A WS upgrade with an unsupported version -> 426 Upgrade Required.
    arm_slot(0, "GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 12\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "426"));
#endif
    tcp_capture_disable();
}

// WebSocket/SSE upgrade failure paths (all with a valid Upgrade + Version: 13 where noted):
// a malformed or missing Sec-WebSocket-Key is a client error (400 after ws_accept_key /
// the null-key guard), and an exhausted WS/SSE connection pool aborts the upgrade after its
// optimistic handshake header (101 for WS, the 200 event-stream header for SSE). Note:
// ws_accept_key's over-64-char length cap is only reachable when MAX_VAL_LEN is configured
// > 64 (the parser truncates header values to MAX_VAL_LEN, 48 by default), so here a bad key
// is rejected by the base64 length check instead.
void test_ws_sse_upgrade_failure_paths()
{
#if PC_ENABLE_WEBSOCKET
    g_server->on_ws("/ws", nullptr, nullptr, nullptr);

    // (a) A Sec-WebSocket-Key that does not base64-decode to 16 bytes -> ws_accept_key rejects -> 400.
    arm_slot(0, "GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGVzdA==\r\nSec-WebSocket-Version: 13\r\n\r\n"); // "test" -> 4 bytes, not 16
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "400"));

    // (b) Upgrade with Version: 13 but no Sec-WebSocket-Key -> 400.
    arm_slot(0, "GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Version: 13\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "400"));

    // (c) A valid upgrade with the WS pool exhausted -> ws_alloc fails, connection aborted
    // after the optimistic 101 header is emitted.
    ws_alloc(1);
    ws_alloc(2); // fill the 2-slot ws_pool (MAX_WS_CONNS)
    arm_slot(0, "GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "101")); // optimistic handshake sent before the pool check
    ws_init();                                           // release the pool for later tests
    tcp_capture_disable();
#endif
}

#if PC_ENABLE_SSE
// An SSE upgrade with the SSE pool exhausted -> pc_sse_alloc fails, connection aborted after
// the optimistic 200 event-stream header.
void test_sse_upgrade_pool_exhausted()
{
    g_server->on_sse("/events", nullptr);
    pc_sse_alloc(1, "/a");
    pc_sse_alloc(2, "/b"); // fill the 2-slot pc_sse_pool (MAX_SSE_CONNS)
    arm_slot(0, "GET /events HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "text/event-stream")); // header sent before the pool check
    pc_sse_init();                                                     // release the pool for later tests
    tcp_capture_disable();
}
#endif

// append_resp_trailer clamps the returned length in-bounds so a response whose status
// line and/or custom-header trailer would overflow the header buffer is emitted truncated
// rather than making the writer read past the buffer (a stack over-read). Driven with an
// over-long content_type (an unbounded caller string).
// A response whose own headers do not fit RESP_HDR_BUF_SIZE is refused, not truncated.
//
// This used to clamp: snprintf truncated the header block and the clamped result was sent. That
// emits a header block with no terminating CRLF, so the peer keeps reading the body as headers and
// the connection desynchronizes - a response-splitting shape, not a cosmetic truncation. The
// server now sends a fixed 500 that always fits and closes.
void test_response_headers_that_do_not_fit_are_refused()
{
    // (a) The status line alone overflows the header buffer.
    char bigct[800];
    memset(bigct, 'a', 750);
    bigct[750] = '\0';
    arm_slot(0, "GET /x HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->send(0, 200, bigct, "ok"); // public entry, no route needed
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "HTTP/1.1 500"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Connection: close"));
    // whatever goes out is a complete header block: it ends with the blank line
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "\r\n\r\n"));
    TEST_ASSERT_NULL(strstr(tcp_captured(), "aaaa")); // no fragment of the oversized value leaks
    tcp_capture_disable();

    // (b) The status line fits but the trailer (a full custom-header block + Connection) does not.
    char midct[600];
    memset(midct, 'b', 500);
    midct[500] = '\0';
    char hv[250];
    memset(hv, 'c', 240);
    hv[240] = '\0';
    arm_slot(0, "GET /y HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    g_server->add_response_header(0, "X-Big", hv); // fill the extra-header block (~249 bytes)
    tcp_capture_reset();
    g_server->send(0, 200, midct, "ok");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "HTTP/1.1 500"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "\r\n\r\n"));
    tcp_capture_disable();
}

// ====================================================================
// DISPATCH / RESPONSE EDGE CASES
// ====================================================================

// Reset a slot to a live, sendable HTTP connection. arm_slot() deliberately leaves
// pcb null (routing-only tests), which short-circuits every response path before a
// byte is written; these tests need the bytes.
static void live_slot(uint8_t slot)
{
    conn_pool[slot] = {};
    conn_pool[slot].id = slot;
    conn_pool[slot].state = ConnState::CONN_ACTIVE;
    conn_pool[slot].proto = ConnProto::PROTO_HTTP;
    conn_pool[slot].pcb = &_mock_pcb;
    http_reset(slot);
    http_pool[slot].version = HttpVersion::HTTP_11; // an HTTP/1.1 peer (chunked is 1.1-only)
}

// The dispatch loop resets a slot whose handler sent nothing, so a `:name` capture has
// to be read inside the handler to survive.
static QueryParam g_seen_params[MAX_PATH_PARAMS];
static uint8_t g_seen_param_count;
static void capture_params_handler(uint8_t, HttpReq *req)
{
    handler_called = true;
    g_seen_param_count = req->path_param_count;
    memcpy(g_seen_params, req->path_params, sizeof(g_seen_params));
}

// note_response() buckets a response by status class. A code below 200 belongs to
// none of the 2xx/4xx/5xx buckets but still counts as a request, so /stats reports
// requests=1 with every class counter still zero.
void test_stats_counters_ignore_sub_200_status()
{
    live_slot(0);
    g_server->send(0, 100, "text/plain", "x"); // below every class bucket

    live_slot(1);
    tcp_capture_reset();
    g_server->stats(1); // renders the counters as they stand before its own response
    const char *out = tcp_captured();
    tcp_capture_disable();
    TEST_ASSERT_NOT_NULL(strstr(out, "\"requests\":1")); // counted as a request
    TEST_ASSERT_NOT_NULL(strstr(out, "\"http_2xx\":0"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"http_4xx\":0"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"http_5xx\":0"));
}

// The shared response trailer injects the pre-built CORS block into every dynamic
// response once set_cors() is on, and set_cors(nullptr) turns it back off.
void test_response_trailer_cors_block_and_null_disable()
{
    g_server->set_cors("https://a.example");
    live_slot(0);
    tcp_capture_reset();
    g_server->send(0, 200, "text/plain", "ok");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Access-Control-Allow-Origin: https://a.example\r\n"));

    g_server->set_cors(nullptr); // null origin disables CORS (same as "")
    live_slot(0);
    tcp_capture_reset();
    g_server->send(0, 200, "text/plain", "ok");
    TEST_ASSERT_NULL(strstr(tcp_captured(), "Access-Control-Allow-Origin"));
    tcp_capture_disable();
}

// set_cache_control(nullptr) clears the pre-built header the same way the empty
// string does, so a later file response carries no Cache-Control.
void test_cache_control_null_clears_header()
{
    fs::mock_fs_reset();
    static const char body[] = "x";
    fs::mock_fs_add("/www/c.txt", body);
    g_server->serve_static("/", g_static_fs, "/www");

    g_server->set_cache_control("max-age=60");
    g_server->set_cache_control(nullptr); // cleared, not "Cache-Control: (null)"
    arm_slot(0, "GET /c.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NULL(strstr(tcp_captured(), "Cache-Control"));
    tcp_capture_disable();
    fs::mock_fs_reset();
}

// An empty route pattern is not a wildcard: is_wildcard is only set for a non-empty
// path ending in '*', so on("") matches nothing (not everything).
void test_empty_route_pattern_matches_nothing()
{
    g_server->on("", HttpMethod::HTTP_GET, record_handler);
    arm_slot(0, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "404"));
    tcp_capture_disable();
}

// `:name` capture limits: captures stop at MAX_PATH_PARAMS, an over-long parameter
// name is truncated to QUERY_KEY_LEN-1, and an over-long segment value to
// QUERY_VAL_LEN-1. All three are capacity caps, never overflows.
void test_path_param_capture_limits()
{
    g_server->on("/q/:a/:b/:c/:d/:e", HttpMethod::HTTP_GET, capture_params_handler);
    arm_slot(0, "GET /q/1/2/3/4/5 HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    g_server->handle();
    TEST_ASSERT_TRUE(handler_called);
    TEST_ASSERT_EQUAL_UINT8(MAX_PATH_PARAMS, g_seen_param_count); // 5th capture dropped
    TEST_ASSERT_EQUAL_STRING("4", g_seen_params[3].val);          // last stored capture

    // An over-long :name and an over-long value are both truncated, not overflowed.
    PC srv2;
    handler_called = false;
    srv2.on("/k/:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", HttpMethod::HTTP_GET, capture_params_handler);
    char req[160];
    char big[60];
    memset(big, 'v', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    snprintf(req, sizeof(req), "GET /k/%s HTTP/1.1\r\nHost: x\r\n\r\n", big);
    arm_slot(0, req);
    conn_pool[0].pcb = &_mock_pcb;
    srv2.handle();
    TEST_ASSERT_TRUE(handler_called);
    TEST_ASSERT_EQUAL_UINT(QUERY_KEY_LEN - 1, (unsigned)strlen(g_seen_params[0].key));
    TEST_ASSERT_EQUAL_UINT(QUERY_VAL_LEN - 1, (unsigned)strlen(g_seen_params[0].val));
}

// Segment-by-segment matching rejects every shape that is not an exact segment-count
// match: fewer path segments than the route, more path segments than the route, and a
// literal segment of equal length that differs. An empty ("//") segment on both sides
// still matches, so a `:name` after it is captured.
void test_path_param_segment_mismatches()
{
    g_server->on("/p1/:a", HttpMethod::HTTP_GET, record_handler);       // path runs out early
    g_server->on("/p2/:a", HttpMethod::HTTP_GET, record_handler);       // route runs out early
    g_server->on("/p3/:a", HttpMethod::HTTP_GET, record_handler);       // literal differs, same length
    g_server->on("//:a", HttpMethod::HTTP_GET, capture_params_handler); // empty first segment

    const char *misses[] = {
        "GET /p1 HTTP/1.1\r\nHost: x\r\n\r\n",     // route wants another segment
        "GET /p2/x/y HTTP/1.1\r\nHost: x\r\n\r\n", // path has one segment too many
        "GET /p9/x HTTP/1.1\r\nHost: x\r\n\r\n",   // "p9" != "p3" (equal length)
    };
    for (size_t i = 0; i < sizeof(misses) / sizeof(misses[0]); i++)
    {
        handler_called = false;
        arm_slot(0, misses[i]);
        conn_pool[0].pcb = &_mock_pcb;
        tcp_capture_reset();
        g_server->handle();
        TEST_ASSERT_FALSE_MESSAGE(handler_called, misses[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(tcp_captured(), "404"), misses[i]);
    }

    // "//v": both route and path carry an empty segment, then ":a" captures "v".
    handler_called = false;
    arm_slot(0, "GET //v HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    g_server->handle();
    TEST_ASSERT_TRUE(handler_called);
    TEST_ASSERT_EQUAL_STRING("v", g_seen_params[0].val);
    tcp_capture_disable();
}

// A worker services only the slots it owns: a slot stamped with another worker's id
// is skipped entirely, so its completed request stays pending instead of being
// dispatched twice.
void test_worker_owner_filter_skips_foreign_slot()
{
    g_server->on("/own", HttpMethod::HTTP_GET, record_handler);
    arm_slot(1, "GET /own HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[1].owner = 1; // owned by another worker
    g_server->handle();
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_EQUAL(ParseState::PARSE_COMPLETE, http_pool[1].parse_state); // still queued

    conn_pool[1].owner = 0; // hand it back; now it dispatches
    g_server->handle();
    TEST_ASSERT_TRUE(handler_called);
}

// The poll loop drives a slot only through a registered ProtoHandler that supplies an
// on_poll: an unregistered protocol resolves to no handler, and a handler without an
// on_poll is skipped. Either way the slot's completed request is never dispatched.
void test_slot_poll_requires_registered_handler_with_poll()
{
    g_server->on("/pp", HttpMethod::HTTP_GET, record_handler);
    arm_slot(0, "GET /pp HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].proto = ConnProto::PROTO_TELNET; // no handler registered in this build
    g_server->handle();
    TEST_ASSERT_FALSE(handler_called);

    static const ProtoHandler no_poll = {nullptr, nullptr, nullptr, nullptr};
    proto_register(ConnProto::PROTO_TELNET, &no_poll); // registered, but nothing to poll
    g_server->handle();
    TEST_ASSERT_FALSE(handler_called);

    proto_register(ConnProto::PROTO_TELNET, nullptr); // restore: telnet is unregistered here
    conn_pool[0].proto = ConnProto::PROTO_HTTP;
    g_server->handle();
    TEST_ASSERT_TRUE(handler_called); // same slot, now polled
}

// A Content-Length beyond BODY_BUF_SIZE puts the parser in ENTITY_TOO_LARGE and the
// dispatch loop answers 413 without invoking any route.
void test_entity_too_large_auto_413()
{
    g_server->on("/big", HttpMethod::HTTP_POST, record_handler);
    arm_slot(0, "POST /big HTTP/1.1\r\nHost: x\r\nContent-Length: 100000\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    TEST_ASSERT_EQUAL(ParseState::PARSE_ENTITY_TOO_LARGE, http_pool[0].parse_state);
    tcp_capture_reset();
    g_server->handle();
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "413 Payload Too Large"));
    tcp_capture_disable();
}

// The Allow list de-duplicates: two routes registered for the same path and method
// contribute one token, not two.
void test_allow_header_dedupes_repeated_method()
{
    g_server->on("/dup", HttpMethod::HTTP_POST, record_handler);
    g_server->on("/dup", HttpMethod::HTTP_POST, record_handler);
    arm_slot(0, "GET /dup HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "Allow: POST\r\n"));
    TEST_ASSERT_NULL(strstr(out, "POST, POST"));
    tcp_capture_disable();
}

// The error-and-close path (405 here) honours HEAD by sending headers only, and
// writes nothing at all once the connection is gone - whether the slot left
// CONN_ACTIVE or lost its pcb.
void test_error_close_head_and_dead_connection()
{
    g_server->on("/po", HttpMethod::HTTP_POST, record_handler);

    // HEAD on a POST-only route -> 405 headers, no body.
    arm_slot(0, "HEAD /po HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "405 Method Not Allowed"));
    TEST_ASSERT_NULL(strstr(out, "\r\n\r\nMethod Not Allowed")); // headers only

    // Slot no longer ACTIVE (pcb still attached) -> nothing written.
    arm_slot(0, "GET /po HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    conn_pool[0].state = ConnState::CONN_CLOSING;
    tcp_capture_reset();
    g_server->handle();
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());

    // Slot ACTIVE but the pcb is gone -> nothing written.
    arm_slot(0, "GET /po HTTP/1.1\r\nHost: x\r\n\r\n"); // arm_slot leaves pcb null
    tcp_capture_reset();
    g_server->handle();
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
    tcp_capture_disable();
}

// A Transfer-Encoding header reaching dispatch is answered 501. The HTTP/1.x byte
// parser already fails such a request closed (400), so this guard is the one that
// covers a semantic ingress (HTTP/2 / HTTP/3) whose headers are handed over already
// decoded - modelled here by populating the request slot directly.
void test_transfer_encoding_on_semantic_ingress_is_501()
{
    g_server->on("/te", HttpMethod::HTTP_POST, record_handler);
    live_slot(0);
    HttpReq *r = &http_pool[0];
    snprintf(r->method, sizeof(r->method), "POST");
    snprintf(r->path, sizeof(r->path), "/te");
    r->path_idx = strlen(r->path);
    r->version = HttpVersion::HTTP_11;
    snprintf(r->headers[0].key, sizeof(r->headers[0].key), "transfer-encoding");
    snprintf(r->headers[0].val, sizeof(r->headers[0].val), "chunked");
    r->header_count = 1;
    r->parse_state = ParseState::PARSE_COMPLETE;

    tcp_capture_reset();
    g_server->handle();
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "501 Not Implemented"));
    tcp_capture_disable();
}

// A static mount answers GET/HEAD only; any other method is a 405 that advertises
// both in Allow.
void test_static_mount_rejects_non_get_methods()
{
    fs::mock_fs_reset();
    static const char body[] = "hi";
    fs::mock_fs_add("/www/a.txt", body);
    g_server->serve_static("/", g_static_fs, "/www");
    arm_slot(0, "POST /a.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "405"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Allow: GET, HEAD\r\n"));
    tcp_capture_disable();
    fs::mock_fs_reset();
}

// send()/send_empty() guard their public entry against an out-of-range slot (the
// reserved dispatch slots sit above MAX_CONNS, so the bound is CONN_POOL_SLOTS), and
// a null payload is a zero-length body rather than a strnlen of nullptr.
void test_send_null_payload_and_slot_bounds()
{
    live_slot(0);
    tcp_capture_reset();
    g_server->send(CONN_POOL_SLOTS, 200, "text/plain", "x"); // out of range
    g_server->send_empty(CONN_POOL_SLOTS, 204);              // out of range
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());

    tcp_capture_reset();
    g_server->send(0, 200, "text/plain", (const char *)nullptr);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Content-Length: 0\r\n"));
    tcp_capture_disable();
}

// send() picks its write shape from the body: a HEAD response and a zero-length body
// emit headers only, a small body is coalesced into the header buffer, and a body too
// large to coalesce goes out as a second write. All three must arrive intact.
void test_send_body_framing_paths()
{
    // HEAD: headers only, but Content-Length still describes the would-be body.
    live_slot(0);
    snprintf(http_pool[0].method, sizeof(http_pool[0].method), "HEAD");
    tcp_capture_reset();
    g_server->send(0, 200, "text/plain", "abcdef");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Content-Length: 6\r\n"));
    TEST_ASSERT_NULL(strstr(tcp_captured(), "abcdef"));

    // Empty body: headers only, Content-Length: 0.
    live_slot(0);
    tcp_capture_reset();
    g_server->send(0, 200, "text/plain", "");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Content-Length: 0\r\n"));

    // A body larger than the header scratch cannot be coalesced -> separate write.
    static char big[RESP_HDR_BUF_SIZE + 64];
    memset(big, 'B', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    live_slot(0);
    tcp_capture_reset();
    g_server->send(0, 200, "text/plain", big);
    const char *out = tcp_captured();
    char want[40];
    snprintf(want, sizeof(want), "Content-Length: %u\r\n", (unsigned)(sizeof(big) - 1));
    TEST_ASSERT_NOT_NULL(strstr(out, want));
    TEST_ASSERT_EQUAL_UINT(sizeof(big) - 1, (unsigned)strlen(strstr(out, "\r\n\r\n") + 4));
    tcp_capture_disable();
}

// send_empty() and redirect() both refuse to write once the connection is gone,
// whether the slot left CONN_ACTIVE or lost its pcb, and reset the parser instead.
void test_send_empty_and_redirect_dead_connection_guards()
{
    live_slot(0);
    conn_pool[0].state = ConnState::CONN_CLOSING; // not ACTIVE, pcb still attached
    tcp_capture_reset();
    g_server->send_empty(0, 204);
    g_server->redirect(0, 302, "/x");
    g_server->send(0, 200, "text/plain", "x");
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());

    live_slot(0);
    conn_pool[0].pcb = nullptr; // ACTIVE, but the pcb is gone
    tcp_capture_reset();
    g_server->send_empty(0, 204);
    g_server->redirect(0, 302, "/x");
    g_server->send(0, 200, "text/plain", "x");
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
    TEST_ASSERT_EQUAL(ParseState::PARSE_METHOD, http_pool[0].parse_state); // parser reset
    tcp_capture_disable();
}

// ====================================================================
// TEMPLATE / CHUNKED / HEADER-BUFFER EDGES
// ====================================================================

static const char *tmpl_resolver(const char *name)
{
    return strcmp(name, "who") == 0 ? "world" : nullptr;
}

// The template walker emits a placeholder literally when its name exceeds the 32-char
// cap (there is no such variable), and an empty template renders a zero-length body.
void test_send_template_placeholder_edges()
{
    live_slot(0);
    tcp_capture_reset();
    g_server->send_template(0, 200, "text/plain", "a{{0123456789012345678901234567890123}}b", tmpl_resolver);
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "a{{0123456789012345678901234567890123}}b")); // emitted verbatim

    live_slot(0);
    tcp_capture_reset();
    g_server->send_template(0, 204, "text/plain", "", tmpl_resolver);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Content-Length: 0\r\n"));
    tcp_capture_disable();
}

// A chunked response with no source is a headers-only reply: the chunked framing is
// advertised but no chunk (and no terminator) follows.
void test_send_chunked_without_source()
{
    live_slot(0);
    tcp_capture_reset();
    g_server->send_chunked(0, 200, "text/plain", nullptr, nullptr);
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "Transfer-Encoding: chunked\r\n"));
    TEST_ASSERT_NULL(strstr(out, "0\r\n\r\n"));
    TEST_ASSERT_FALSE(pc_resp_holds_slot(0));
    tcp_capture_disable();
}

static int g_chunk_calls;
static size_t chunk_src_fill(uint8_t *buf, size_t cap, void *)
{
    if (g_chunk_calls++ >= 2)
    {
        return 0; // end of body
    }
    size_t n = cap < 40 ? cap : 40;
    memset(buf, 'q', n);
    return n;
}

// A send window smaller than one full CHUNK_BUF_SIZE caps each chunk at the window,
// and a window of zero parks the transfer; if the peer then disappears the next pump
// drops the continuation instead of writing into a dead connection.
void test_chunked_pump_small_window_and_connection_lost()
{
    g_chunk_calls = 0;
    mock_sndbuf() = 64; // smaller than CHUNK_BUF_SIZE: cap comes from the window
    live_slot(0);
    tcp_capture_reset();
    g_server->send_chunked(0, 200, "text/plain", chunk_src_fill, nullptr);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Transfer-Encoding: chunked\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "28\r\nqqqq")); // 0x28 == 40 bytes framed
    TEST_ASSERT_FALSE(pc_resp_holds_slot(0));                   // source drained, response finished

    // No window at all: the body parks in the pump, still active.
    g_chunk_calls = 0;
    mock_sndbuf() = 0;
    live_slot(0);
    tcp_capture_reset();
    g_server->send_chunked(0, 200, "text/plain", chunk_src_fill, nullptr);
    TEST_ASSERT_TRUE(pc_resp_holds_slot(0));

    conn_pool[0].pcb = nullptr; // peer went away before the window reopened
    g_server->handle();
    TEST_ASSERT_FALSE(pc_resp_holds_slot(0)); // continuation dropped
    TEST_ASSERT_NULL(strstr(tcp_captured(), "qqqq"));

    mock_sndbuf() = MOCK_SNDBUF_DEFAULT;
    tcp_capture_disable();
}

// The per-response header buffer rejects a null value like a null name, accepts a
// cookie with an empty attribute string (no trailing "; "), and drops - whole - any
// header or cookie that would not fit, leaving the earlier ones intact.
void test_response_header_null_value_empty_attrs_and_overflow()
{
    live_slot(0);
    g_server->clear_response_headers(0);
    g_server->add_response_header(0, "X-Keep", "1");
    g_server->add_response_header(0, "X-Null", nullptr); // null value -> ignored
    g_server->set_cookie(0, "c-null", nullptr, nullptr); // null value -> ignored
    g_server->set_cookie(0, "sid", "abc", "");           // empty attrs -> no "; " suffix

    char filler[EXTRA_HDR_BUF_SIZE];
    memset(filler, 'f', sizeof(filler) - 1);
    filler[sizeof(filler) - 1] = '\0';
    g_server->add_response_header(0, "X-Too-Big", filler); // would overflow -> dropped whole
    g_server->set_cookie(0, "big", filler, nullptr);       // would overflow -> dropped whole

    tcp_capture_reset();
    g_server->send(0, 200, "text/plain", "ok");
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "X-Keep: 1\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Set-Cookie: sid=abc\r\n")); // no attribute suffix
    TEST_ASSERT_NULL(strstr(out, "X-Null"));
    TEST_ASSERT_NULL(strstr(out, "c-null"));
    TEST_ASSERT_NULL(strstr(out, "X-Too-Big"));
    TEST_ASSERT_NULL(strstr(out, "ffff"));
    tcp_capture_disable();
}

// mime_type() extension edges: a trailing dot has no extension, an extension that is
// a strict prefix or extension of a table entry must not match, and a leading
// non-letter exercises the case-folding compare on a character outside 'A'..'Z'.
void test_mime_type_extension_edges()
{
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", PC::mime_type("/file.")); // dot, no extension
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", PC::mime_type("/a.7z"));  // digit first
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", PC::mime_type("/a.jsx")); // longer than "js"
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", PC::mime_type("/a.h"));   // shorter than "htm"
    TEST_ASSERT_EQUAL_STRING("font/woff2", PC::mime_type("/a.WOFF2"));             // upper-case folds
}

// ====================================================================
// WEBSOCKET / SSE UPGRADE + SEND-API EDGES
// ====================================================================

#if PC_ENABLE_WEBSOCKET
// Push one masked client text frame (RFC 6455 §5.3) into a slot's rx ring.
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

// Drive a complete RFC 6455 handshake on slot 0 for the route at @p path.
static void ws_upgrade_slot0(const char *path)
{
    char req[220];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n",
             path);
    arm_slot(0, req);
    conn_pool[0].pcb = &_mock_pcb;
    g_server->handle();
}

// A WS route registered without an on-connect handler still upgrades: the 101 goes
// out and the slot is handed to the frame parser, the callback is simply not fired.
void test_ws_upgrade_without_connect_handler()
{
    ws_init();
    g_server->on_ws("/wsn", nullptr, nullptr, nullptr);
    tcp_capture_reset();
    ws_upgrade_slot0("/wsn");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "101 Switching Protocols"));
    TEST_ASSERT_NOT_NULL(ws_find(0)); // slot promoted to WebSocket
    tcp_capture_disable();
    ws_init();
}

// With no ws_message / ws_close handler registered, a completed frame and a closed
// connection are still processed: the dispatch scan finds nothing to call, the frame
// is consumed, and the close releases the WS slot.
void test_ws_dispatch_without_message_or_close_handler()
{
    ws_init();
    g_server->on("/plain", HttpMethod::HTTP_GET, record_handler); // a non-WS route to scan past
    g_server->on_ws("/wsq", nullptr, nullptr, nullptr);
    ws_upgrade_slot0("/wsq");
    WsConn *ws = ws_find(0);
    TEST_ASSERT_NOT_NULL(ws);

    push_ws_text_frame(0, "hi");
    g_server->handle(); // ws_dispatch_message finds no handler; frame consumed
    TEST_ASSERT_NOT_NULL(ws_find(0));
    TEST_ASSERT_NOT_EQUAL(WsParseState::WS_FRAME_READY, ws->parse_state);

    ws->parse_state = WsParseState::WS_ERROR; // protocol error seen by the parser
    g_server->handle();                       // ws_dispatch_close finds no handler; slot freed
    TEST_ASSERT_NULL(ws_find(0));
    ws_init();
}

// The WS handshake gate: only a GET carrying both Upgrade: websocket and a Connection
// header listing the "upgrade" token is a handshake (everything else is 400), and a
// handshake with a missing or non-13 version is 426.
void test_ws_upgrade_handshake_gate()
{
    g_server->on_ws("/wsg", nullptr, nullptr, nullptr);
    const char *bad[] = {
        "POST /wsg HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n",
        "GET /wsg HTTP/1.1\r\nHost: x\r\nConnection: Upgrade\r\n\r\n",                          // no Upgrade header
        "GET /wsg HTTP/1.1\r\nHost: x\r\nUpgrade: h2c\r\nConnection: Upgrade\r\n\r\n",          // wrong protocol
        "GET /wsg HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: keep-alive\r\n\r\n", // no token
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        arm_slot(0, bad[i]);
        conn_pool[0].pcb = &_mock_pcb;
        tcp_capture_reset();
        g_server->handle();
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(tcp_captured(), "400"), bad[i]);
    }

    // A real handshake with no Sec-WebSocket-Version at all -> 426, same as a wrong one.
    arm_slot(0, "GET /wsg HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "426 Upgrade Required"));
    tcp_capture_disable();
}
#endif // PC_ENABLE_WEBSOCKET

// Every upgrade entry point re-checks that the slot is still sendable and bails
// without writing when it is not (the request may have been parsed a loop before the
// peer vanished). The 426 path resets the parser rather than emitting a challenge.
void test_upgrade_entry_points_on_dead_slot()
{
#if PC_ENABLE_WEBSOCKET
    arm_slot(0, "GET /w HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n");
    conn_pool[0].pcb = nullptr; // peer gone; the request is still parsed
    tcp_capture_reset();
    ws_send_version_required(0);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
    TEST_ASSERT_EQUAL(ParseState::PARSE_METHOD, http_pool[0].parse_state);

    arm_slot(0, "GET /w HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n");
    conn_pool[0].pcb = nullptr;
    tcp_capture_reset();
    TEST_ASSERT_FALSE(ws_do_upgrade(0, &http_pool[0], nullptr)); // valid key, dead slot
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
#endif
#if PC_ENABLE_SSE
    arm_slot(0, "GET /e HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = nullptr;
    tcp_capture_reset();
    TEST_ASSERT_FALSE(pc_sse_do_upgrade(0, &http_pool[0], nullptr));
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
#endif
    tcp_capture_disable();
}

#if PC_ENABLE_SSE
static uint8_t g_sse_connected_id;
static int g_sse_connect_calls;
static void sse_on_connect(uint8_t id)
{
    g_sse_connected_id = id;
    g_sse_connect_calls++;
}

// An SSE route registered with an on-connect handler fires it with the newly
// allocated stream id once the 200 event-stream header is out.
void test_sse_upgrade_fires_connect_handler()
{
    pc_sse_init();
    g_sse_connect_calls = 0;
    g_sse_connected_id = 0xFF;
    g_server->on_sse("/evh", sse_on_connect);
    arm_slot(0, "GET /evh HTTP/1.1\r\nHost: x\r\n\r\n");
    conn_pool[0].pcb = &_mock_pcb;
    tcp_capture_reset();
    g_server->handle();
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "text/event-stream"));
    TEST_ASSERT_EQUAL_INT(1, g_sse_connect_calls);
    TEST_ASSERT_NOT_NULL(pc_sse_find(0));
    TEST_ASSERT_EQUAL_UINT8(pc_sse_find(0)->pc_sse_id, g_sse_connected_id);
    tcp_capture_disable();
    pc_sse_init();
}

// pc_sse_send / pc_sse_broadcast write nothing once the bound slot's connection is
// gone: pc_sse_write() reports the failure and no flush follows.
void test_sse_send_on_dead_slot_writes_nothing()
{
    pc_sse_init();
    live_slot(0);
    SseConn *sse = pc_sse_alloc(0, "/events");
    TEST_ASSERT_NOT_NULL(sse);
    conn_pool[0].pcb = nullptr; // connection gone, pool entry still live

    tcp_capture_reset();
    g_server->pc_sse_send(sse->pc_sse_id, "x");
    g_server->pc_sse_broadcast("/events", "x");
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
    tcp_capture_disable();
    pc_sse_init();
}
#endif // PC_ENABLE_SSE

#if PC_ENABLE_WEBSOCKET
// The WS send API on an in-range-but-inactive id, on a connection in the WS_ERROR
// terminal state, and on a live pool entry whose TCP slot has gone away: all three
// must write nothing, and ws_disconnect must not flush a dead slot.
void test_ws_send_api_inactive_error_state_and_dead_slot()
{
    ws_init();
    live_slot(0);
    WsConn *ws = ws_alloc(0);
    TEST_ASSERT_NOT_NULL(ws);

    // In range but not allocated.
    tcp_capture_reset();
    g_server->ws_send_binary(1, (const uint8_t *)"x", 1);
    g_server->ws_disconnect(1);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());

    // WS_ERROR is terminal for sends, exactly like WS_CLOSED.
    ws->parse_state = WsParseState::WS_ERROR;
    tcp_capture_reset();
    g_server->ws_send_text(ws->ws_id, "nope");
    g_server->ws_send_binary(ws->ws_id, (const uint8_t *)"x", 1);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());

    // Live pool entry, dead TCP slot: the frame write fails, so nothing is flushed.
    ws->parse_state = WsParseState::WS_HEADER1;
    conn_pool[0].pcb = nullptr;
    tcp_capture_reset();
    g_server->ws_send_text(ws->ws_id, "nope");
    g_server->ws_send_binary(ws->ws_id, (const uint8_t *)"x", 1);
    g_server->ws_disconnect(ws->ws_id); // close frame cannot go out either
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
    tcp_capture_disable();
    ws_init();
}
#endif // PC_ENABLE_WEBSOCKET

int main()
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
#if PC_ENABLE_SSE
    RUN_TEST(test_sse_upgrade_pool_exhausted);
#endif

    // Function I/O: handler API access
    RUN_TEST(test_handler_reads_body);
    RUN_TEST(test_handler_reads_query_param);
    RUN_TEST(test_handler_reads_header);
    RUN_TEST(test_wildcard_before_exact_wildcard_wins);

    // Function I/O: on()
    RUN_TEST(test_fn_on_registers_and_dispatches);
    RUN_TEST(test_fn_on_path_copied_null_terminated);
    RUN_TEST(test_fn_on_table_full_extra_routes_dropped);
    RUN_TEST(test_fn_on_same_path_different_methods_are_distinct);

    // Function I/O: on_not_found()
    RUN_TEST(test_fn_on_not_found_called_when_no_match);
    RUN_TEST(test_fn_on_not_found_not_called_when_match_exists);

    // Function I/O: set_cors()
    RUN_TEST(test_fn_set_cors_options_preflight_clears_slot);
    RUN_TEST(test_fn_set_cors_empty_string_disables);

    // Unit tests
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

    // Stress tests
    RUN_TEST(stress_last_route_dispatched_in_full_table);
    RUN_TEST(stress_sequential_requests_no_state_leak);
    RUN_TEST(stress_all_slots_dispatched_simultaneously);
    RUN_TEST(stress_wildcard_matches_many_paths);
    RUN_TEST(stress_handle_with_no_complete_slots_is_nop);

    // Race condition simulations
    RUN_TEST(race_slot_complete_between_handle_calls);
    RUN_TEST(race_conn_freed_after_parse_complete);
    RUN_TEST(race_double_handle_no_double_dispatch);
    RUN_TEST(race_error_and_valid_slot_in_same_handle);
    RUN_TEST(race_callback_manually_resets_slot);

    // 414
    RUN_TEST(test_uri_too_long_auto_resets_slot);

    // Transfer-Encoding rejection
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
    RUN_TEST(test_serve_static_unrepresentable_mtime);
    RUN_TEST(test_serve_static_if_modified_since_malformed);
    RUN_TEST(test_serve_static_cache_control);

    RUN_TEST(test_request_log_hook_fires);
    RUN_TEST(test_stats_endpoint_emits_json);
    RUN_TEST(test_status_text_reason_phrases);
    RUN_TEST(test_allow_header_lists_methods);
    RUN_TEST(test_listen_and_begin);
    RUN_TEST(test_begin_port_convenience);

#if PC_ENABLE_WEBSOCKET
    RUN_TEST(test_ws_send_api);
#endif
#if PC_ENABLE_SSE
    RUN_TEST(test_sse_broadcast_after_upgrade_matches_path);
    RUN_TEST(test_sse_send_api);
#endif
#if PC_ENABLE_METRICS
    RUN_TEST(test_metrics_emits_prometheus);
#endif

    // Dispatch / response edge cases
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

    // Template / chunked / header-buffer edges
    RUN_TEST(test_send_template_placeholder_edges);
    RUN_TEST(test_send_chunked_without_source);
    RUN_TEST(test_chunked_pump_small_window_and_connection_lost);
    RUN_TEST(test_response_header_null_value_empty_attrs_and_overflow);
    RUN_TEST(test_mime_type_extension_edges);

    // WebSocket / SSE upgrade + send-API edges
#if PC_ENABLE_WEBSOCKET
    RUN_TEST(test_ws_upgrade_without_connect_handler);
    RUN_TEST(test_ws_dispatch_without_message_or_close_handler);
    RUN_TEST(test_ws_upgrade_handshake_gate);
    RUN_TEST(test_ws_send_api_inactive_error_state_and_dead_slot);
#endif
    RUN_TEST(test_upgrade_entry_points_on_dead_slot);
#if PC_ENABLE_SSE
    RUN_TEST(test_sse_upgrade_fires_connect_handler);
    RUN_TEST(test_sse_send_on_dead_slot_writes_nothing);
#endif

    return UNITY_END();
}
