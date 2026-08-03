// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit, stress, and race-condition tests for Layer 6 (Presentation).
//
// Sections:
//   FUNCTION I/O  - one test per observable input→output path for each function
//   UNIT          - correctness of the state machine and parser logic
//   STRESS        - sustained-load and boundary-value coverage
//   RACE SIM      - simulated concurrent-access scenarios

#include "network_drivers/presentation/presentation.h"
#include "network_drivers/session/proto_handler.h" // ProtoHandler: full type needed to call ->on_poll()
#if PC_ENABLE_WEBSOCKET
#include "network_drivers/presentation/http/websocket/websocket.h" // ws_alloc(): upgraded-slot guard in http_parse()
#endif
#include <unity.h>

// tcp.cpp + presentation.cpp are compiled into the native env.
// No stubs or redefinitions needed.

static void push(uint8_t slot, const char *data)
{
    TcpConn *s = &conn_pool[slot];
    for (size_t i = 0; data[i]; i++)
    {
        size_t next = (s->rx_head + 1) % RX_BUF_SIZE;
        s->rx_buffer[s->rx_head] = (uint8_t)data[i];
        s->rx_head = next;
    }
}

void setUp()
{
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = i;
        conn_pool[i].state = CONN_ACTIVE;
        http_reset(i);
    }
}

void tearDown()
{
}

// ====================================================================
// FUNCTION I/O TESTS - http_reset()
// ====================================================================

void test_fn_reset_sets_parse_state_to_method()
{
    http_pool[0].parse_state = PARSE_COMPLETE;
    http_reset(0);
    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[0].parse_state);
}

void test_fn_reset_sets_slot_id()
{
    http_pool[2].slot_id = 99;
    http_reset(2);
    TEST_ASSERT_EQUAL(2, http_pool[2].slot_id);
}

void test_fn_reset_clears_method()
{
    memcpy(http_pool[0].method, "DELETE", 7);
    http_reset(0);
    TEST_ASSERT_EQUAL('\0', http_pool[0].method[0]);
}

void test_fn_reset_clears_path_and_idx()
{
    memcpy(http_pool[0].path, "/data", 6);
    http_pool[0].path_idx = 5;
    http_reset(0);
    TEST_ASSERT_EQUAL('\0', http_pool[0].path[0]);
    TEST_ASSERT_EQUAL(0, (int)http_pool[0].path_idx);
}

void test_fn_reset_clears_query_raw_and_params()
{
    memcpy(http_pool[0].query, "a=1&b=2", 8);
    http_pool[0].query_idx = 7;
    http_pool[0].query_count = 2;
    memcpy(http_pool[0].query_params[0].key, "a", 2);
    http_reset(0);
    TEST_ASSERT_EQUAL('\0', http_pool[0].query[0]);
    TEST_ASSERT_EQUAL(0, (int)http_pool[0].query_idx);
    TEST_ASSERT_EQUAL(0, http_pool[0].query_count);
    TEST_ASSERT_EQUAL('\0', http_pool[0].query_params[0].key[0]);
}

void test_fn_reset_clears_all_header_slots()
{
    http_pool[0].header_count = 3;
    memcpy(http_pool[0].headers[0].key, "Host", 5);
    memcpy(http_pool[0].headers[2].val, "val", 4);
    http_reset(0);
    TEST_ASSERT_EQUAL(0, http_pool[0].header_count);
    TEST_ASSERT_EQUAL('\0', http_pool[0].headers[0].key[0]);
    TEST_ASSERT_EQUAL('\0', http_pool[0].headers[2].val[0]);
}

void test_fn_reset_clears_body_fields()
{
    http_pool[0].body[0] = 'X';
    http_pool[0].body_len = 1;
    http_pool[0].content_length = 5;
    http_pool[0].body_bytes_read = 5;
    http_reset(0);
    TEST_ASSERT_EQUAL('\0', http_pool[0].body[0]);
    TEST_ASSERT_EQUAL(0, (int)http_pool[0].body_len);
    TEST_ASSERT_EQUAL(0, (int)http_pool[0].content_length);
    TEST_ASSERT_EQUAL(0, (int)http_pool[0].body_bytes_read);
}

void test_fn_reset_out_of_range_is_nop()
{
    http_reset(MAX_CONNS);
    http_reset(255);
    TEST_PASS();
}

void test_fn_reset_is_idempotent()
{
    push(0, "POST /x HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc");
    http_parse(0);
    http_reset(0);
    http_reset(0);
    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(0, http_pool[0].header_count);
    TEST_ASSERT_EQUAL(0, (int)http_pool[0].body_len);
}

// ====================================================================
// FUNCTION I/O TESTS - http_get_header()
// ====================================================================

void test_fn_get_header_null_when_no_headers()
{
    // setUp already reset all slots - header_count is 0
    TEST_ASSERT_NULL(http_get_header(&http_pool[0], "Host"));
}

void test_fn_get_header_finds_single_header()
{
    push(0, "GET / HTTP/1.1\r\nHost: esp32\r\n\r\n");
    http_parse(0);
    const char *v = http_get_header(&http_pool[0], "Host");
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_STRING("esp32", v);
}

void test_fn_get_header_finds_first_of_many()
{
    push(0, "GET / HTTP/1.1\r\nA: first\r\nB: second\r\nC: third\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL_STRING("first", http_get_header(&http_pool[0], "A"));
}

void test_fn_get_header_finds_middle_of_many()
{
    push(0, "GET / HTTP/1.1\r\nA: one\r\nB: mid\r\nC: three\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL_STRING("mid", http_get_header(&http_pool[0], "B"));
}

void test_fn_get_header_finds_last_of_many()
{
    push(0, "GET / HTTP/1.1\r\nA: one\r\nB: two\r\nC: last\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL_STRING("last", http_get_header(&http_pool[0], "C"));
}

void test_fn_get_header_case_insensitive_lowercase()
{
    push(0, "GET / HTTP/1.1\r\nContent-Type: application/json\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_NOT_NULL(http_get_header(&http_pool[0], "content-type"));
}

void test_fn_get_header_case_insensitive_uppercase()
{
    push(0, "GET / HTTP/1.1\r\nContent-Type: text/plain\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_NOT_NULL(http_get_header(&http_pool[0], "CONTENT-TYPE"));
}

void test_fn_get_header_returns_null_for_absent_key()
{
    push(0, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_NULL(http_get_header(&http_pool[0], "Authorization"));
}

void test_fn_get_header_does_not_bleed_across_slots()
{
    push(0, "GET / HTTP/1.1\r\nHost: alpha\r\n\r\n");
    push(1, "GET / HTTP/1.1\r\nHost: beta\r\n\r\n");
    http_parse(0);
    http_parse(1);
    TEST_ASSERT_EQUAL_STRING("alpha", http_get_header(&http_pool[0], "Host"));
    TEST_ASSERT_EQUAL_STRING("beta", http_get_header(&http_pool[1], "Host"));
}

// ====================================================================
// FUNCTION I/O TESTS - http_get_query()
// ====================================================================

void test_fn_get_query_null_when_no_params()
{
    push(0, "GET /path HTTP/1.1\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_NULL(http_get_query(&http_pool[0], "key"));
}

void test_fn_get_query_finds_single_param()
{
    push(0, "GET /s?foo=bar HTTP/1.1\r\n\r\n");
    http_parse(0);
    const char *v = http_get_query(&http_pool[0], "foo");
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_STRING("bar", v);
}

void test_fn_get_query_finds_first_param()
{
    push(0, "GET /s?a=1&b=2&c=3 HTTP/1.1\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL_STRING("1", http_get_query(&http_pool[0], "a"));
}

void test_fn_get_query_finds_middle_param()
{
    push(0, "GET /s?a=1&b=mid&c=3 HTTP/1.1\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL_STRING("mid", http_get_query(&http_pool[0], "b"));
}

void test_fn_get_query_finds_last_param()
{
    push(0, "GET /s?a=1&b=2&c=end HTTP/1.1\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL_STRING("end", http_get_query(&http_pool[0], "c"));
}

void test_fn_get_query_returns_null_for_absent_key()
{
    push(0, "GET /s?a=1 HTTP/1.1\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_NULL(http_get_query(&http_pool[0], "z"));
}

void test_fn_get_query_empty_value()
{
    push(0, "GET /s?key= HTTP/1.1\r\n\r\n");
    http_parse(0);
    const char *v = http_get_query(&http_pool[0], "key");
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_STRING("", v);
}

void test_fn_get_query_does_not_bleed_across_slots()
{
    push(0, "GET /s?x=slot0 HTTP/1.1\r\n\r\n");
    push(1, "GET /s?x=slot1 HTTP/1.1\r\n\r\n");
    http_parse(0);
    http_parse(1);
    TEST_ASSERT_EQUAL_STRING("slot0", http_get_query(&http_pool[0], "x"));
    TEST_ASSERT_EQUAL_STRING("slot1", http_get_query(&http_pool[1], "x"));
}

// ====================================================================
// UNIT TESTS - parser state machine
// ====================================================================

void test_get_parses_complete()
{
    push(0, "GET /api/status HTTP/1.1\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL_STRING("GET", http_pool[0].method);
    TEST_ASSERT_EQUAL_STRING("/api/status", http_pool[0].path);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
}

void test_post_body_stored()
{
    push(1, "POST /data HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello");
    http_parse(1);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[1].parse_state);
    TEST_ASSERT_EQUAL_STRING("hello", (const char *)http_pool[1].body);
    TEST_ASSERT_EQUAL(5, (int)http_pool[1].body_len);
}

void test_put_parses_complete()
{
    push(0, "PUT /res/1 HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL_STRING("PUT", http_pool[0].method);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
}

void test_delete_parses_complete()
{
    push(0, "DELETE /res/1 HTTP/1.1\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL_STRING("DELETE", http_pool[0].method);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
}

void test_patch_parses_complete()
{
    push(0, "PATCH /res/1 HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL_STRING("PATCH", http_pool[0].method);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
}

void test_head_parses_complete()
{
    push(0, "HEAD /ping HTTP/1.1\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL_STRING("HEAD", http_pool[0].method);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
}

void test_query_single_param()
{
    push(0, "GET /s?key=val HTTP/1.1\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL(1, http_pool[0].query_count);
    TEST_ASSERT_EQUAL_STRING("key", http_pool[0].query_params[0].key);
    TEST_ASSERT_EQUAL_STRING("val", http_pool[0].query_params[0].val);
}

void test_query_multiple_params()
{
    push(0, "GET /s?a=1&b=2&c=3 HTTP/1.1\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL(3, http_pool[0].query_count);
}

void test_body_null_terminated()
{
    push(0, "POST /t HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc");
    http_parse(0);
    TEST_ASSERT_EQUAL('\0', http_pool[0].body[3]);
}

void test_body_over_buf_size_is_413()
{
    // Content-Length > BODY_BUF_SIZE → PARSE_ENTITY_TOO_LARGE before any body is read.
    char req[RX_BUF_SIZE];
    int big = BODY_BUF_SIZE + 10;
    snprintf(req, sizeof(req), "POST /big HTTP/1.1\r\nContent-Length: %d\r\n\r\n", big);
    push(0, req);
    http_parse(0);
    TEST_ASSERT_EQUAL(PARSE_ENTITY_TOO_LARGE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(0, (int)http_pool[0].body_len);
}

void test_overflow_method_sets_error()
{
    push(3, "TOOLONGMETHODNAME /path HTTP/1.1\r\n\r\n");
    http_parse(3);
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[3].parse_state);
}

void test_overflow_path_sets_414()
{
    char req[256] = "GET /";
    for (int i = 5; i < MAX_PATH_LEN + 10; i++)
    {
        req[i] = 'x';
    }
    req[MAX_PATH_LEN + 10] = '\0';
    strcat(req, " HTTP/1.1\r\n\r\n");
    push(0, req);
    http_parse(0);
    TEST_ASSERT_EQUAL(PARSE_URI_TOO_LONG, http_pool[0].parse_state);
}

void test_bad_lf_after_cr_sets_error()
{
    // Null byte would terminate the C-string in push(), so use a visible non-LF byte.
    // The parser is in PARSE_EXPECT_LF after the \r; seeing 'X' instead of '\n'
    // must produce PARSE_ERROR.
    push(0, "GET / HTTP/1.1\rX\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_headers_beyond_max_are_dropped()
{
    push(0, "GET / HTTP/1.1\r\n"
            "H1: v1\r\nH2: v2\r\nH3: v3\r\nH4: v4\r\n"
            "H5: v5\r\nH6: v6\r\nH7: v7\r\nH8: v8\r\n"
            "H9: v9\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL(MAX_HEADERS, http_pool[0].header_count);
}

void test_query_params_beyond_max_are_dropped()
{
    push(0, "GET /?a=1&b=2&c=3&d=4&e=5&f=6&g=7&h=8&i=9 HTTP/1.1\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL(MAX_QUERY_PARAMS, http_pool[0].query_count);
}

void test_incremental_two_pushes_completes()
{
    push(0, "GET /inc HTTP/1.1\r\n");
    http_parse(0);
    TEST_ASSERT_NOT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    push(0, "\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
}

// Regression: PARSE_EXPECT_BODY_LF was added to prevent the old PARSE_BODY
// LF-skip from discarding a first body byte that happens to be '\n'.
// A body of "\nabcd" (Content-Length: 5) must be stored verbatim.
void test_body_starting_with_newline_stored()
{
    push(0, "POST /nl HTTP/1.1\r\nContent-Length: 5\r\n\r\n\nabcd");
    http_parse(0);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(5, (int)http_pool[0].body_len);
    TEST_ASSERT_EQUAL('\n', (char)http_pool[0].body[0]);
    TEST_ASSERT_EQUAL_STRING("\nabcd", (const char *)http_pool[0].body);
}

// PUT with an actual body (not just Content-Length: 0)
void test_put_body_stored()
{
    push(0, "PUT /r/1 HTTP/1.1\r\nContent-Length: 7\r\n\r\nupdated");
    http_parse(0);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("PUT", http_pool[0].method);
    TEST_ASSERT_EQUAL(7, (int)http_pool[0].body_len);
    TEST_ASSERT_EQUAL_STRING("updated", (const char *)http_pool[0].body);
}

// Content-Length is stored both as content_length (int) and in headers[]
// so that http_get_header("Content-Length") returns the value as a string.
void test_content_length_header_stored_in_headers_array()
{
    push(0, "POST /x HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc");
    http_parse(0);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(3, (int)http_pool[0].content_length);
    const char *cl = http_get_header(&http_pool[0], "Content-Length");
    TEST_ASSERT_NOT_NULL(cl);
    TEST_ASSERT_EQUAL_STRING("3", cl);
}

// ====================================================================
// STRESS TESTS
// ====================================================================

// 100 parse+reset cycles on one slot - no state accumulation allowed.
void stress_parse_reset_100_cycles()
{
    for (int iter = 0; iter < 100; iter++)
    {
        push(0, "GET /test HTTP/1.1\r\nHost: x\r\n\r\n");
        http_parse(0);
        TEST_ASSERT_EQUAL_MESSAGE(PARSE_COMPLETE, http_pool[0].parse_state, "unexpected parse state mid-cycle");
        http_reset(0);
        TEST_ASSERT_EQUAL_MESSAGE(PARSE_METHOD, http_pool[0].parse_state, "state not reset");
        TEST_ASSERT_EQUAL_MESSAGE(0, http_pool[0].header_count, "headers not reset");
        TEST_ASSERT_EQUAL_MESSAGE('\0', http_pool[0].method[0], "method not reset");
    }
}

// All four slots parse completely different requests simultaneously;
// each slot's result must be independent of the others.
void stress_all_slots_parse_simultaneously()
{
    push(0, "GET /zero HTTP/1.1\r\n\r\n");
    push(1, "POST /one HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc");
    push(2, "PUT /two HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    push(3, "DELETE /three HTTP/1.1\r\n\r\n");

    http_parse(0);
    http_parse(1);
    http_parse(2);
    http_parse(3);

    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("GET", http_pool[0].method);
    TEST_ASSERT_EQUAL_STRING("/zero", http_pool[0].path);

    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[1].parse_state);
    TEST_ASSERT_EQUAL_STRING("POST", http_pool[1].method);
    TEST_ASSERT_EQUAL_STRING("abc", (const char *)http_pool[1].body);

    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[2].parse_state);
    TEST_ASSERT_EQUAL_STRING("PUT", http_pool[2].method);

    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[3].parse_state);
    TEST_ASSERT_EQUAL_STRING("DELETE", http_pool[3].method);
    TEST_ASSERT_EQUAL_STRING("/three", http_pool[3].path);
}

// "OPTIONS" is the longest valid 7-character method - exactly fills method[0..6]
// with the null terminator at method[7]; must not trigger overflow.
void stress_method_at_max_7_chars_no_error()
{
    push(0, "OPTIONS /x HTTP/1.1\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL_STRING("OPTIONS", http_pool[0].method);
    TEST_ASSERT_NOT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

// Path exactly MAX_PATH_LEN-1 characters - must parse without error.
void stress_path_at_exact_limit_no_error()
{
    char req[MAX_PATH_LEN + 32];
    req[0] = 'G';
    req[1] = 'E';
    req[2] = 'T';
    req[3] = ' ';
    req[4] = '/';
    for (int i = 5; i < MAX_PATH_LEN - 1 + 4; i++)
    {
        req[i] = 'a'; // 4 = "GET "
    }
    // req now contains "GET /" followed by (MAX_PATH_LEN-2) 'a's = MAX_PATH_LEN-1 total path chars
    // That's exactly MAX_PATH_LEN-1 path characters (including leading '/').
    // Wait, let me re-think: path starts at index 4 with '/', so path ends at index 4+(MAX_PATH_LEN-1)-1 =
    // MAX_PATH_LEN+2. Let me recalculate: req = "GET " + "/" + (MAX_PATH_LEN-2 'a's) = path length = 1 +
    // (MAX_PATH_LEN-2) = MAX_PATH_LEN-1 req[4] = '/', req[5..MAX_PATH_LEN+2] = 'a', total path = MAX_PATH_LEN-1 chars ✓
    int end = 4 + MAX_PATH_LEN - 1; // index after last path char
    req[end] = ' ';
    req[end + 1] = 'H';
    req[end + 2] = 'T';
    req[end + 3] = 'T';
    req[end + 4] = 'P';
    req[end + 5] = '/';
    req[end + 6] = '1';
    req[end + 7] = '.';
    req[end + 8] = '1';
    req[end + 9] = '\r';
    req[end + 10] = '\n';
    req[end + 11] = '\r';
    req[end + 12] = '\n';
    req[end + 13] = '\0';
    push(0, req);
    http_parse(0);
    TEST_ASSERT_NOT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(MAX_PATH_LEN - 1, (int)strlen(http_pool[0].path));
}

// Body exactly BODY_BUF_SIZE bytes - all bytes stored, null terminator at index BODY_BUF_SIZE.
void stress_body_exactly_buf_size_all_stored()
{
    char hdr[128];
    snprintf(hdr, sizeof(hdr), "POST /b HTTP/1.1\r\nContent-Length: %d\r\n\r\n", BODY_BUF_SIZE);
    push(0, hdr);
    TcpConn *s = &conn_pool[0];
    for (int i = 0; i < BODY_BUF_SIZE; i++)
    {
        size_t next = (s->rx_head + 1) % RX_BUF_SIZE;
        s->rx_buffer[s->rx_head] = (uint8_t)('A' + (i % 26));
        s->rx_head = next;
    }
    http_parse(0);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(BODY_BUF_SIZE, (int)http_pool[0].body_len);
    TEST_ASSERT_EQUAL('\0', http_pool[0].body[BODY_BUF_SIZE]);
    // Spot-check: first, 26th, and 27th body bytes
    TEST_ASSERT_EQUAL('A', http_pool[0].body[0]);
    TEST_ASSERT_EQUAL('Z', http_pool[0].body[25]);
    TEST_ASSERT_EQUAL('A', http_pool[0].body[26]); // pattern wraps
}

// Exactly MAX_HEADERS headers - all must be stored; none silently dropped.
void stress_exactly_max_headers_all_stored()
{
    push(0, "GET / HTTP/1.1\r\n"
            "H1: v1\r\nH2: v2\r\nH3: v3\r\nH4: v4\r\n"
            "H5: v5\r\nH6: v6\r\nH7: v7\r\nH8: v8\r\n"
            "\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL(MAX_HEADERS, http_pool[0].header_count);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("H8", http_pool[0].headers[7].key);
    TEST_ASSERT_EQUAL_STRING("v8", http_pool[0].headers[7].val);
}

// Exactly MAX_QUERY_PARAMS query params - all stored; none dropped.
void stress_exactly_max_query_params_all_stored()
{
    push(0, "GET /?a=1&b=2&c=3&d=4&e=5&f=6&g=7&h=8 HTTP/1.1\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL(MAX_QUERY_PARAMS, http_pool[0].query_count);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("h", http_pool[0].query_params[MAX_QUERY_PARAMS - 1].key);
    TEST_ASSERT_EQUAL_STRING("8", http_pool[0].query_params[MAX_QUERY_PARAMS - 1].val);
}

// Byte-by-byte incremental parse - worst-case for a streaming environment.
// Each byte is pushed individually; the parser must never enter PARSE_ERROR
// while consuming a valid request.
void stress_incremental_byte_by_byte_no_error()
{
    const char *req = "GET /stream HTTP/1.1\r\nHost: test\r\n\r\n";
    TcpConn *s = &conn_pool[0];

    for (size_t i = 0; req[i]; i++)
    {
        size_t next = (s->rx_head + 1) % RX_BUF_SIZE;
        s->rx_buffer[s->rx_head] = (uint8_t)req[i];
        s->rx_head = next;
        http_parse(0);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(PARSE_ERROR, http_pool[0].parse_state,
                                      "unexpected error during incremental parse");
    }
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
}

// 50 sequential requests, alternating GET/POST - verify slot is clean after each.
void stress_sequential_requests_no_state_leak()
{
    for (int i = 0; i < 50; i++)
    {
        if (i % 2 == 0)
        {
            push(0, "GET /ping HTTP/1.1\r\n\r\n");
            http_parse(0);
            TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
            TEST_ASSERT_EQUAL_STRING("GET", http_pool[0].method);
            TEST_ASSERT_EQUAL(0, http_pool[0].header_count);
        }
        else
        {
            push(0, "POST /data HTTP/1.1\r\nContent-Length: 2\r\n\r\nhi");
            http_parse(0);
            TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
            TEST_ASSERT_EQUAL_STRING("POST", http_pool[0].method);
            TEST_ASSERT_EQUAL_STRING("hi", (const char *)http_pool[0].body);
        }
        http_reset(0);
    }
}

// ====================================================================
// RACE CONDITION SIMULATIONS
//
// The library uses a single-producer (lwIP tcpip_thread) /
// single-consumer (Arduino loop) ring buffer with volatile head/tail.
// These tests simulate the ordering hazards that volatile + careful
// sequencing are designed to prevent.
// ====================================================================

// Interleaved write/read: producer writes a batch, consumer reads half,
// producer writes another batch, consumer finishes.  Verifies that no byte
// is lost or reordered across an interleaved boundary.
void race_interleaved_producer_consumer_ring_buffer()
{
    TcpConn *s = &conn_pool[0];
    s->rx_head = 0;
    s->rx_tail = 0;

    // Producer writes first 100 bytes
    for (int i = 0; i < 100; i++)
    {
        size_t next = (s->rx_head + 1) % RX_BUF_SIZE;
        s->rx_buffer[s->rx_head] = (uint8_t)i;
        s->rx_head = next;
    }

    // Consumer reads 50 bytes
    for (int i = 0; i < 50; i++)
    {
        TEST_ASSERT_EQUAL((uint8_t)i, s->rx_buffer[s->rx_tail]);
        s->rx_tail = (s->rx_tail + 1) % RX_BUF_SIZE;
    }

    // Producer writes 50 more bytes (values 100..149)
    for (int i = 100; i < 150; i++)
    {
        size_t next = (s->rx_head + 1) % RX_BUF_SIZE;
        s->rx_buffer[s->rx_head] = (uint8_t)i;
        s->rx_head = next;
    }

    // Consumer reads remaining 100 bytes (50..149)
    for (int i = 50; i < 150; i++)
    {
        TEST_ASSERT_EQUAL((uint8_t)i, s->rx_buffer[s->rx_tail]);
        s->rx_tail = (s->rx_tail + 1) % RX_BUF_SIZE;
    }

    TEST_ASSERT_EQUAL(s->rx_head, s->rx_tail); // empty
}

// Write-past-full protection: reaching the sentinel must prevent writes
// - the full condition must be enforced before any write succeeds.
void race_ring_buffer_full_prevents_write()
{
    TcpConn *s = &conn_pool[0];
    s->rx_head = 0;
    s->rx_tail = 0;

    int written = 0;
    while (1)
    {
        size_t next = (s->rx_head + 1) % RX_BUF_SIZE;
        if (next == s->rx_tail)
        {
            break; // full - stop before overwriting
        }
        s->rx_buffer[s->rx_head] = (uint8_t)(written & 0xFF);
        s->rx_head = next;
        written++;
    }

    // Attempting to write one more must be blocked (next_head == tail)
    TEST_ASSERT_EQUAL(s->rx_tail, (s->rx_head + 1) % RX_BUF_SIZE);
    TEST_ASSERT_EQUAL(RX_BUF_SIZE - 1, written);
}

// ABA slot reuse: free a slot via timeout, immediately re-arm it, verify
// that last_activity_ms reflects the new connection - no stale timestamp.
void race_aba_slot_reuse_fresh_timestamp()
{
    const uint32_t T_OLD = 0;
    const uint32_t T_DEAD = CONN_TIMEOUT_MS;
    const uint32_t T_NEW = CONN_TIMEOUT_MS + 1;

    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = NULL;
    conn_pool[0].last_activity_ms = T_OLD;

    set_millis(T_DEAD);
    proto_tcp_check_timeouts(0);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state); // timed out

    // Simulate a new accept: re-arm the slot
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].last_activity_ms = T_NEW;

    set_millis(T_NEW); // now millis == last_activity → diff = 0, no timeout
    proto_tcp_check_timeouts(0);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state); // must NOT time out
}

// Double-free: call proto_tcp_check_timeouts() on a slot that is already CONN_FREE.
// Must be a no-op - no state corruption and no crash.
void race_double_free_is_nop()
{
    conn_pool[0].state = CONN_FREE;
    set_millis(CONN_TIMEOUT_MS * 10);
    proto_tcp_check_timeouts(0);
    proto_tcp_check_timeouts(0);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

// Slot isolation: parsing on slot 0 must not affect slot 1's state,
// even if both are mid-parse at different stages simultaneously.
void race_concurrent_slot_parse_isolation()
{
    // Slot 0: push a full request
    push(0, "GET /a HTTP/1.1\r\n\r\n");
    // Slot 1: push only the first line (mid-parse, no blank line yet)
    push(1, "POST /b HTTP/1.1\r\n");

    http_parse(0);
    http_parse(1);

    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    // Slot 1 is not complete - it's still awaiting headers/blank line
    TEST_ASSERT_NOT_EQUAL(PARSE_COMPLETE, http_pool[1].parse_state);
    TEST_ASSERT_NOT_EQUAL(PARSE_ERROR, http_pool[1].parse_state);

    // Finish slot 1 with the blank line
    push(1, "Content-Length: 0\r\n\r\n");
    http_parse(1);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[1].parse_state);
    TEST_ASSERT_EQUAL_STRING("POST", http_pool[1].method);
}

// Reset-during-parse: simulates a disconnect arriving (http_reset) while
// the parser is in the middle of a header value.  The resulting state
// must be fully clean.
void race_reset_during_parse_header_val()
{
    push(0, "GET / HTTP/1.1\r\nContent-Type: appli"); // truncated value
    http_parse(0);
    TEST_ASSERT_EQUAL(PARSE_HEADER_VAL, http_pool[0].parse_state);

    http_reset(0); // disconnect arrives mid-parse

    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(0, http_pool[0].header_count);
    TEST_ASSERT_EQUAL('\0', http_pool[0].method[0]);
}

// Reset-during-query-parse: simulates a disconnect while accumulating
// a query string.
void race_reset_during_parse_query()
{
    push(0, "GET /s?key=val&partia"); // truncated query
    http_parse(0);
    TEST_ASSERT_EQUAL(PARSE_QUERY, http_pool[0].parse_state);

    http_reset(0);

    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(0, (int)http_pool[0].query_idx);
    TEST_ASSERT_EQUAL(0, http_pool[0].query_count);
}

// Reset-during-body-parse: simulates a disconnect mid-body.
void race_reset_during_parse_body()
{
    push(0, "POST /x HTTP/1.1\r\nContent-Length: 10\r\n\r\nhalf");
    http_parse(0);
    TEST_ASSERT_EQUAL(PARSE_BODY, http_pool[0].parse_state);

    http_reset(0);

    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(0, (int)http_pool[0].body_len);
    TEST_ASSERT_EQUAL(0, (int)http_pool[0].body_bytes_read);
    TEST_ASSERT_EQUAL('\0', http_pool[0].body[0]);
}

// After http_parse hits PARSE_COMPLETE, calling it again must be a no-op
// - bytes pushed after completion must stay in the buffer unconsumed.
void race_parse_after_complete_is_nop()
{
    push(0, "GET / HTTP/1.1\r\n\r\n");
    http_parse(0);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);

    // Push extra bytes (simulates garbage/next request arriving)
    push(0, "EXTRA");
    http_parse(0); // must not alter any field

    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("GET", http_pool[0].method);
}

// ====================================================================
// FUNCTION I/O TESTS - http_conn_open() / http_parse() guard clauses
// ====================================================================

void test_fn_conn_open_out_of_range_is_nop()
{
    http_conn_open(MAX_CONNS);
    http_conn_open(255);
    TEST_PASS();
}

void test_fn_parse_out_of_range_is_nop()
{
    http_parse(MAX_CONNS);
    http_parse(255);
    TEST_PASS();
}

#if PC_ENABLE_WEBSOCKET
// Once a slot has upgraded to WebSocket, http_parse() must not touch its rx ring
// (those bytes are WS frame data, not HTTP) - it returns immediately.
void test_fn_parse_is_nop_on_ws_upgraded_slot()
{
    TEST_ASSERT_NOT_NULL(ws_alloc(0));
    push(0, "GET / HTTP/1.1\r\n\r\n");
    size_t before = pc_conn_available(0);
    http_parse(0);
    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[0].parse_state); // untouched
    TEST_ASSERT_EQUAL(before, pc_conn_available(0));           // bytes left in the ring
    ws_free(0);
}
#endif

// ====================================================================
// FUNCTION I/O TESTS - HTTP ProtoHandler poll seam
// ====================================================================

static uint8_t s_poll_seen_slot = 0xFF;
static void test_poll_fn(uint8_t slot)
{
    s_poll_seen_slot = slot;
}

// Before any application installs a pump, on_poll() must be a silent no-op.
// (Must run before test_fn_poll_trampoline_calls_installed_fn below - the installed
// pump is process-lifetime state in presentation.cpp, not reset between tests.)
void test_fn_poll_trampoline_noop_before_install()
{
    s_poll_seen_slot = 0xFF;
    http_proto_handler()->on_poll(2);
    TEST_ASSERT_EQUAL(0xFF, s_poll_seen_slot); // nothing ran
}

void test_fn_poll_trampoline_calls_installed_fn()
{
    http_proto_set_poll(test_poll_fn);
    http_proto_handler()->on_poll(3);
    TEST_ASSERT_EQUAL(3, s_poll_seen_slot);
}

int main()
{
    UNITY_BEGIN();

    // Function I/O: http_reset()
    RUN_TEST(test_fn_reset_sets_parse_state_to_method);
    RUN_TEST(test_fn_reset_sets_slot_id);
    RUN_TEST(test_fn_reset_clears_method);
    RUN_TEST(test_fn_reset_clears_path_and_idx);
    RUN_TEST(test_fn_reset_clears_query_raw_and_params);
    RUN_TEST(test_fn_reset_clears_all_header_slots);
    RUN_TEST(test_fn_reset_clears_body_fields);
    RUN_TEST(test_fn_reset_out_of_range_is_nop);
    RUN_TEST(test_fn_reset_is_idempotent);

    // Function I/O: http_conn_open() / http_parse() guard clauses
    RUN_TEST(test_fn_conn_open_out_of_range_is_nop);
    RUN_TEST(test_fn_parse_out_of_range_is_nop);
#if PC_ENABLE_WEBSOCKET
    RUN_TEST(test_fn_parse_is_nop_on_ws_upgraded_slot);
#endif

    // Function I/O: HTTP ProtoHandler poll seam (order matters - see comment above)
    RUN_TEST(test_fn_poll_trampoline_noop_before_install);
    RUN_TEST(test_fn_poll_trampoline_calls_installed_fn);

    // Function I/O: http_get_header()
    RUN_TEST(test_fn_get_header_null_when_no_headers);
    RUN_TEST(test_fn_get_header_finds_single_header);
    RUN_TEST(test_fn_get_header_finds_first_of_many);
    RUN_TEST(test_fn_get_header_finds_middle_of_many);
    RUN_TEST(test_fn_get_header_finds_last_of_many);
    RUN_TEST(test_fn_get_header_case_insensitive_lowercase);
    RUN_TEST(test_fn_get_header_case_insensitive_uppercase);
    RUN_TEST(test_fn_get_header_returns_null_for_absent_key);
    RUN_TEST(test_fn_get_header_does_not_bleed_across_slots);

    // Function I/O: http_get_query()
    RUN_TEST(test_fn_get_query_null_when_no_params);
    RUN_TEST(test_fn_get_query_finds_single_param);
    RUN_TEST(test_fn_get_query_finds_first_param);
    RUN_TEST(test_fn_get_query_finds_middle_param);
    RUN_TEST(test_fn_get_query_finds_last_param);
    RUN_TEST(test_fn_get_query_returns_null_for_absent_key);
    RUN_TEST(test_fn_get_query_empty_value);
    RUN_TEST(test_fn_get_query_does_not_bleed_across_slots);

    // Unit tests
    RUN_TEST(test_get_parses_complete);
    RUN_TEST(test_post_body_stored);
    RUN_TEST(test_put_parses_complete);
    RUN_TEST(test_delete_parses_complete);
    RUN_TEST(test_patch_parses_complete);
    RUN_TEST(test_head_parses_complete);
    RUN_TEST(test_query_single_param);
    RUN_TEST(test_query_multiple_params);
    RUN_TEST(test_body_null_terminated);
    RUN_TEST(test_body_over_buf_size_is_413);
    RUN_TEST(test_overflow_method_sets_error);
    RUN_TEST(test_overflow_path_sets_414);
    RUN_TEST(test_bad_lf_after_cr_sets_error);
    RUN_TEST(test_headers_beyond_max_are_dropped);
    RUN_TEST(test_query_params_beyond_max_are_dropped);
    RUN_TEST(test_incremental_two_pushes_completes);
    RUN_TEST(test_body_starting_with_newline_stored);
    RUN_TEST(test_put_body_stored);
    RUN_TEST(test_content_length_header_stored_in_headers_array);

    // Stress tests
    RUN_TEST(stress_parse_reset_100_cycles);
    RUN_TEST(stress_all_slots_parse_simultaneously);
    RUN_TEST(stress_method_at_max_7_chars_no_error);
    RUN_TEST(stress_path_at_exact_limit_no_error);
    RUN_TEST(stress_body_exactly_buf_size_all_stored);
    RUN_TEST(stress_exactly_max_headers_all_stored);
    RUN_TEST(stress_exactly_max_query_params_all_stored);
    RUN_TEST(stress_incremental_byte_by_byte_no_error);
    RUN_TEST(stress_sequential_requests_no_state_leak);

    // Race condition simulations
    RUN_TEST(race_interleaved_producer_consumer_ring_buffer);
    RUN_TEST(race_ring_buffer_full_prevents_write);
    RUN_TEST(race_aba_slot_reuse_fresh_timestamp);
    RUN_TEST(race_double_free_is_nop);
    RUN_TEST(race_concurrent_slot_parse_isolation);
    RUN_TEST(race_reset_during_parse_header_val);
    RUN_TEST(race_reset_during_parse_query);
    RUN_TEST(race_reset_during_parse_body);
    RUN_TEST(race_parse_after_complete_is_nop);

    return UNITY_END();
}
