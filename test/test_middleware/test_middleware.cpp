// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the middleware chain (use()) and the built-in rate limiter
// (enable_rate_limit()).

#include "protocore.h"
#include <stdio.h>
#include <string.h>
#include <unity.h>

static PC server;

// Cross-test observation state (middlewares are plain function pointers, so they
// communicate through file-static globals + the global server, just like the
// application handlers do). setUp() resets all of it.
static bool g_handler_called;
static int g_log_count;
static char g_order[16];
static size_t g_order_len;

static void order_push(char c)
{
    if (g_order_len + 1 < sizeof(g_order))
    {
        g_order[g_order_len++] = c;
        g_order[g_order_len] = '\0';
    }
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

// Re-arm a connection slot (send() frees it) and clear the response sink so each
// request in a test is observed in isolation.
static void arm_slot(uint8_t slot)
{
    conn_pool[slot] = {};
    conn_pool[slot].id = slot;
    conn_pool[slot].state = ConnState::CONN_ACTIVE;
    conn_pool[slot].proto = ConnProto::PROTO_HTTP; // dispatch requires an explicit protocol
    conn_pool[slot].pcb = &_mock_pcb;
    http_reset(slot);
    tcp_capture_reset();
}

static const char *do_req(uint8_t slot, const char *req_str)
{
    arm_slot(slot);
    push_str(slot, req_str);
    http_parse(slot);
    server.handle();
    return tcp_captured();
}

// ---- Handlers --------------------------------------------------------------

static void h_ok(uint8_t slot, HttpReq *req)
{
    (void)req;
    g_handler_called = true;
    server.send(slot, 200, "text/plain", "OK");
}

// ---- Middlewares -----------------------------------------------------------

static MwResult mw_pass(uint8_t slot, HttpReq *req)
{
    (void)slot;
    (void)req;
    g_log_count++; // a logging middleware: observe every request, fall through
    return MwResult::MW_NEXT;
}

static MwResult mw_inject_header(uint8_t slot, HttpReq *req)
{
    (void)req;
    server.add_response_header(slot, "X-MW", "1");
    return MwResult::MW_NEXT;
}

static MwResult mw_block(uint8_t slot, HttpReq *req)
{
    (void)req;
    server.send(slot, 403, "text/plain", "Forbidden");
    return MwResult::MW_HALT; // short-circuit: handler must not run
}

static MwResult mw_order_a(uint8_t slot, HttpReq *req)
{
    (void)slot;
    (void)req;
    order_push('A');
    return MwResult::MW_NEXT;
}
static MwResult mw_order_b(uint8_t slot, HttpReq *req)
{
    (void)slot;
    (void)req;
    order_push('B');
    return MwResult::MW_NEXT;
}

void setUp()
{
    server = PC();
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
    tcp_capture_reset();

    g_handler_called = false;
    g_log_count = 0;
    g_order[0] = '\0';
    g_order_len = 0;
    set_millis(0);
}

void tearDown()
{
    tcp_capture_disable();
}

// ====================================================================
// TESTS
// ====================================================================

void test_middleware_runs_then_handler()
{
    server.use(mw_pass);
    server.on("/t", HttpMethod::HTTP_GET, h_ok);
    const char *r = do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(1, g_log_count); // middleware saw the request
    TEST_ASSERT_TRUE(g_handler_called);    // and fell through to the handler
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
}

void test_middleware_runs_for_unmatched_route()
{
    // No route registered -> 404, but the middleware still observes the request.
    server.use(mw_pass);
    const char *r = do_req(0, "GET /missing HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(1, g_log_count);
    TEST_ASSERT_FALSE(g_handler_called);
    TEST_ASSERT_NOT_NULL(strstr(r, "404 Not Found"));
}

void test_middleware_can_inject_response_header()
{
    server.use(mw_inject_header);
    server.on("/t", HttpMethod::HTTP_GET, h_ok);
    const char *r = do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(r, "X-MW: 1\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
}

void test_middleware_halt_short_circuits_handler()
{
    server.use(mw_block);
    server.on("/t", HttpMethod::HTTP_GET, h_ok);
    const char *r = do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(r, "403 Forbidden"));
    TEST_ASSERT_FALSE(g_handler_called); // handler never reached
}

void test_middleware_runs_in_registration_order()
{
    server.use(mw_order_a);
    server.use(mw_order_b);
    server.on("/t", HttpMethod::HTTP_GET, h_ok);
    do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL_STRING("AB", g_order);
}

void test_use_respects_capacity_cap()
{
    // Register more than MAX_MIDDLEWARE; extras are dropped, none crash.
    for (int i = 0; i < MAX_MIDDLEWARE + 3; i++)
    {
        server.use(mw_pass);
    }
    server.on("/t", HttpMethod::HTTP_GET, h_ok);
    do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(MAX_MIDDLEWARE, g_log_count); // capped, not 7
    TEST_ASSERT_TRUE(g_handler_called);
}

void test_rate_limit_allows_then_rejects()
{
    server.enable_rate_limit(2, 1000);
    server.on("/t", HttpMethod::HTTP_GET, h_ok);

    const char *r1 = do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(r1, "200 OK"));
    const char *r2 = do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(r2, "200 OK"));

    // Third request inside the window is over budget -> 429 + Retry-After.
    const char *r3 = do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(r3, "429 Too Many Requests"));
    TEST_ASSERT_NOT_NULL(strstr(r3, "Retry-After: 1\r\n"));
}

void test_rate_limit_window_resets()
{
    server.enable_rate_limit(2, 1000);
    server.on("/t", HttpMethod::HTTP_GET, h_ok);

    do_req(0, "GET /t HTTP/1.1\r\n\r\n"); // 1
    do_req(0, "GET /t HTTP/1.1\r\n\r\n"); // 2
    const char *over = do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(over, "429"));

    // Advance past the window: budget refills.
    set_millis(1000);
    const char *after = do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(after, "200 OK"));
}

void test_rate_limit_disabled_by_default()
{
    server.on("/t", HttpMethod::HTTP_GET, h_ok);
    for (int i = 0; i < 5; i++)
    {
        const char *r = do_req(0, "GET /t HTTP/1.1\r\n\r\n");
        TEST_ASSERT_NOT_NULL(strstr(r, "200 OK"));
    }
}

// use(nullptr) is rejected without touching the chain (the other half of the capacity guard's
// OR, which test_use_respects_capacity_cap already covers).
void test_use_rejects_null_middleware()
{
    server.use(nullptr);
    server.use(mw_pass);
    server.on("/t", HttpMethod::HTTP_GET, h_ok);
    do_req(0, "GET /t HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(1, g_log_count); // only mw_pass ran; the null entry was dropped
    TEST_ASSERT_TRUE(g_handler_called);
}

// A rate limit enabled with a zero window is treated as disabled - the window_ms == 0 half of
// the guard, distinct from the max_requests == 0 (never-enabled) half every other test hits.
void test_rate_limit_zero_window_disables()
{
    server.enable_rate_limit(1, 0);
    server.on("/t", HttpMethod::HTTP_GET, h_ok);
    for (int i = 0; i < 5; i++)
    {
        const char *r = do_req(0, "GET /t HTTP/1.1\r\n\r\n");
        TEST_ASSERT_NOT_NULL(strstr(r, "200 OK")); // never rate-limited
    }
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_middleware_runs_then_handler);
    RUN_TEST(test_middleware_runs_for_unmatched_route);
    RUN_TEST(test_middleware_can_inject_response_header);
    RUN_TEST(test_middleware_halt_short_circuits_handler);
    RUN_TEST(test_middleware_runs_in_registration_order);
    RUN_TEST(test_use_respects_capacity_cap);
    RUN_TEST(test_rate_limit_allows_then_rejects);
    RUN_TEST(test_rate_limit_window_resets);
    RUN_TEST(test_rate_limit_disabled_by_default);
    RUN_TEST(test_use_rejects_null_middleware);
    RUN_TEST(test_rate_limit_zero_window_disables);
    return UNITY_END();
}
