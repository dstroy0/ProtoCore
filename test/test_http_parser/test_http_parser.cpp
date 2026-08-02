// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Comprehensive unit tests for the standalone HTTP/1.1 parser.
//
// Tests call http_parser_feed() directly - no ring buffer, no transport layer.
// This verifies the parser in isolation: state transitions, field extraction,
// body handling, 413 detection, error cases, and boundary values.
//
// Sections:
//   RESET      - http_parser_reset() invariants
//   FEED API   - terminal-state guard, byte ordering
//   METHOD     - all supported methods, overflow → PARSE_ERROR
//   PATH       - extraction, truncation → PARSE_ERROR
//   QUERY      - single/multiple params, key/value split
//   HEADERS    - extraction, case-insensitive lookup, multi-header
//   BODY       - GET (no body), POST/PUT with body, boundary values
//   413        - Content-Length > BODY_BUF_SIZE → PARSE_ENTITY_TOO_LARGE
//   HELPERS    - http_get_header, http_get_query edge cases
//   STRESS     - large query, many headers, incremental feeds

#include "network_drivers/network/ip.h" // PC_IP_STR_MAX for the recovered-client buffer
#include "network_drivers/presentation/http/http_parser/http_parser.h"
#include <unity.h>

// ---- Helpers -----------------------------------------------------------

static void feed_str(HttpReq *req, const char *s)
{
    for (; *s; s++)
    {
        http_parser_feed(req, (uint8_t)*s);
    }
}

static void feed_request(uint8_t slot, const char *raw)
{
    http_parser_reset(&http_pool[slot]);
    feed_str(&http_pool[slot], raw);
}

// Builds "POST /f HTTP/1.1\r\n[Content-Type: <ct>\r\n]Content-Length: N\r\n\r\n<body>" with N computed
// from strlen(body), and feeds it. content_type == nullptr omits the Content-Type header entirely.
static void feed_form(uint8_t slot, const char *content_type, const char *body)
{
    char req[512];
    if (content_type)
    {
        snprintf(req, sizeof(req), "POST /f HTTP/1.1\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n%s", content_type,
                 (int)strlen(body), body);
    }
    else
    {
        snprintf(req, sizeof(req), "POST /f HTTP/1.1\r\nContent-Length: %d\r\n\r\n%s", (int)strlen(body), body);
    }
    feed_request(slot, req);
}

void setUp()
{
    for (int i = 0; i < MAX_CONNS; i++)
    {
        http_pool[i] = {};
        http_pool[i].slot_id = (uint8_t)i;
        http_parser_reset(&http_pool[i]);
    }
}

void tearDown()
{
}

// ====================================================================
// RESET TESTS
// ====================================================================

void test_reset_sets_parse_method_state()
{
    http_pool[0].parse_state = PARSE_COMPLETE;
    http_parser_reset(&http_pool[0]);
    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[0].parse_state);
}

void test_reset_preserves_slot_id()
{
    http_pool[2].slot_id = 2;
    http_parser_reset(&http_pool[2]);
    TEST_ASSERT_EQUAL(2, (int)http_pool[2].slot_id);
}

void test_reset_clears_method()
{
    feed_request(0, "GET / HTTP/1.1\r\n\r\n");
    http_parser_reset(&http_pool[0]);
    TEST_ASSERT_EQUAL('\0', http_pool[0].method[0]);
}

void test_reset_clears_path()
{
    feed_request(0, "GET /api/data HTTP/1.1\r\n\r\n");
    http_parser_reset(&http_pool[0]);
    TEST_ASSERT_EQUAL('\0', http_pool[0].path[0]);
}

void test_reset_clears_header_count()
{
    feed_request(0, "GET / HTTP/1.1\r\nX-Foo: bar\r\n\r\n");
    http_parser_reset(&http_pool[0]);
    TEST_ASSERT_EQUAL(0, (int)http_pool[0].header_count);
}

void test_reset_clears_body()
{
    feed_request(0, "POST / HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc");
    http_parser_reset(&http_pool[0]);
    TEST_ASSERT_EQUAL(0, (int)http_pool[0].body_len);
    TEST_ASSERT_EQUAL(0, (int)http_pool[0].content_length);
}

void test_reset_clears_query_count()
{
    feed_request(0, "GET /?a=1&b=2 HTTP/1.1\r\n\r\n");
    http_parser_reset(&http_pool[0]);
    TEST_ASSERT_EQUAL(0, (int)http_pool[0].query_count);
}

// ====================================================================
// FEED API - terminal-state guard
// ====================================================================

void test_feed_after_complete_does_not_change_state()
{
    feed_request(0, "GET / HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    http_parser_feed(&http_pool[0], 'X');
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
}

void test_feed_after_error_does_not_change_state()
{
    http_pool[0].parse_state = PARSE_ERROR;
    http_parser_feed(&http_pool[0], 'X');
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_feed_after_entity_too_large_does_not_change_state()
{
    http_pool[0].parse_state = PARSE_ENTITY_TOO_LARGE;
    http_parser_feed(&http_pool[0], 'X');
    TEST_ASSERT_EQUAL(PARSE_ENTITY_TOO_LARGE, http_pool[0].parse_state);
}

// ====================================================================
// METHOD TESTS
// ====================================================================

void test_method_get()
{
    feed_request(0, "GET / HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("GET", http_pool[0].method);
}

void test_method_post()
{
    feed_request(0, "POST / HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("POST", http_pool[0].method);
}

void test_method_put()
{
    feed_request(0, "PUT /r HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("PUT", http_pool[0].method);
}

void test_method_delete()
{
    feed_request(0, "DELETE /r HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("DELETE", http_pool[0].method);
}

void test_method_patch()
{
    feed_request(0, "PATCH /r HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("PATCH", http_pool[0].method);
}

void test_method_head()
{
    feed_request(0, "HEAD / HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("HEAD", http_pool[0].method);
}

void test_method_options()
{
    feed_request(0, "OPTIONS / HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("OPTIONS", http_pool[0].method);
}

void test_method_overflow_is_error()
{
    // More than 7 chars (sizeof method - 1) before a space → PARSE_ERROR
    feed_request(0, "TOOLONGM /path HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

// ====================================================================
// PATH TESTS
// ====================================================================

void test_path_root()
{
    feed_request(0, "GET / HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL_STRING("/", http_pool[0].path);
}

void test_path_segments()
{
    feed_request(0, "GET /api/users/42 HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL_STRING("/api/users/42", http_pool[0].path);
}

void test_path_without_query()
{
    feed_request(0, "GET /p?k=v HTTP/1.1\r\n\r\n");
    // path should NOT contain the query string
    TEST_ASSERT_EQUAL_STRING("/p", http_pool[0].path);
}

void test_path_overflow_is_414()
{
    // Build a path longer than MAX_PATH_LEN
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
    feed_request(0, req);
    TEST_ASSERT_EQUAL(PARSE_URI_TOO_LONG, http_pool[0].parse_state);
}

// ====================================================================
// QUERY STRING TESTS
// ====================================================================

void test_single_query_param()
{
    feed_request(0, "GET /p?id=42 HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(1, (int)http_pool[0].query_count);
    const char *v = http_get_query(&http_pool[0], "id");
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_STRING("42", v);
}

void test_two_query_params()
{
    feed_request(0, "GET /p?a=1&b=2 HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(2, (int)http_pool[0].query_count);
    TEST_ASSERT_EQUAL_STRING("1", http_get_query(&http_pool[0], "a"));
    TEST_ASSERT_EQUAL_STRING("2", http_get_query(&http_pool[0], "b"));
}

void test_query_key_not_found_returns_null()
{
    feed_request(0, "GET /p?a=1 HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NULL(http_get_query(&http_pool[0], "z"));
}

void test_query_empty_value()
{
    feed_request(0, "GET /p?key= HTTP/1.1\r\n\r\n");
    const char *v = http_get_query(&http_pool[0], "key");
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_STRING("", v);
}

// ====================================================================
// HEADER TESTS
// ====================================================================

void test_single_header_stored()
{
    feed_request(0, "GET / HTTP/1.1\r\nX-Custom: hello\r\n\r\n");
    TEST_ASSERT_EQUAL(1, (int)http_pool[0].header_count);
    const char *v = http_get_header(&http_pool[0], "X-Custom");
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_STRING("hello", v);
}

void test_header_lookup_case_insensitive()
{
    feed_request(0, "GET / HTTP/1.1\r\nContent-Type: application/json\r\n\r\n");
    TEST_ASSERT_NOT_NULL(http_get_header(&http_pool[0], "content-type"));
    TEST_ASSERT_NOT_NULL(http_get_header(&http_pool[0], "CONTENT-TYPE"));
    TEST_ASSERT_EQUAL_STRING("application/json", http_get_header(&http_pool[0], "Content-Type"));
}

// --- http_get_cookie (RFC 6265 request Cookie parsing) ---------------------

void test_cookie_basic_and_positions()
{
    feed_request(0, "GET / HTTP/1.1\r\nCookie: a=1; b=2; c=3\r\n\r\n");
    char v[32];
    TEST_ASSERT_TRUE(http_get_cookie(&http_pool[0], "a", v, sizeof(v))); // first
    TEST_ASSERT_EQUAL_STRING("1", v);
    TEST_ASSERT_TRUE(http_get_cookie(&http_pool[0], "b", v, sizeof(v))); // middle
    TEST_ASSERT_EQUAL_STRING("2", v);
    TEST_ASSERT_TRUE(http_get_cookie(&http_pool[0], "c", v, sizeof(v))); // last
    TEST_ASSERT_EQUAL_STRING("3", v);
}

void test_cookie_missing_and_no_header()
{
    char v[32];
    feed_request(0, "GET / HTTP/1.1\r\nCookie: a=1; b=2\r\n\r\n");
    TEST_ASSERT_FALSE(http_get_cookie(&http_pool[0], "z", v, sizeof(v))); // absent name
    TEST_ASSERT_EQUAL_STRING("", v);
    feed_request(1, "GET / HTTP/1.1\r\n\r\n");
    TEST_ASSERT_FALSE(http_get_cookie(&http_pool[1], "a", v, sizeof(v))); // no Cookie header
}

void test_cookie_exact_name_not_substring()
{
    feed_request(0, "GET / HTTP/1.1\r\nCookie: session=x; sess=y\r\n\r\n");
    char v[32];
    TEST_ASSERT_TRUE(http_get_cookie(&http_pool[0], "sess", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("y", v); // not "x" (no prefix/substring match)
    TEST_ASSERT_TRUE(http_get_cookie(&http_pool[0], "session", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("x", v);
}

void test_cookie_quoted_and_value_with_equals()
{
    feed_request(0, "GET / HTTP/1.1\r\nCookie: q=\"hello world\"; t=YWJj===\r\n\r\n");
    char v[32];
    TEST_ASSERT_TRUE(http_get_cookie(&http_pool[0], "q", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("hello world", v); // surrounding quotes stripped
    TEST_ASSERT_TRUE(http_get_cookie(&http_pool[0], "t", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("YWJj===", v); // '=' inside a value preserved (base64 padding)
}

// A literal HTAB (not SP) between cookie pairs must be skipped by the separator scan too.
void test_cookie_htab_separator_skipped()
{
    feed_request(0, "GET / HTTP/1.1\r\nCookie: a=1;\tb=2\r\n\r\n");
    char v[32];
    TEST_ASSERT_TRUE(http_get_cookie(&http_pool[0], "b", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("2", v);
}

// A malformed cookie-pair with no '=' at all is skipped without matching any name, whether it
// runs to the end of the header value or is cut short by the next ';'.
void test_cookie_malformed_pair_without_equals()
{
    feed_request(0, "GET / HTTP/1.1\r\nCookie: bogus\r\n\r\n"); // no '=' anywhere, ends at '\0'
    char v[32];
    TEST_ASSERT_FALSE(http_get_cookie(&http_pool[0], "a", v, sizeof(v)));

    feed_request(1, "GET / HTTP/1.1\r\nCookie: bogus;a=1\r\n\r\n"); // no '=' before the ';'
    TEST_ASSERT_TRUE(http_get_cookie(&http_pool[1], "a", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("1", v);
}

// An empty value ("a=;...") and a value with trailing HTAB OWS both exercise the trailing-OWS
// trim loop's boundary (value already at v, and a '\t' specifically, not just ' ').
void test_cookie_empty_value_and_htab_trim()
{
    feed_request(0, "GET / HTTP/1.1\r\nCookie: a=;b=2\r\n\r\n");
    char v[32];
    TEST_ASSERT_TRUE(http_get_cookie(&http_pool[0], "a", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("", v);

    feed_request(1, "GET / HTTP/1.1\r\nCookie: a=1\t;b=2\r\n\r\n");
    TEST_ASSERT_TRUE(http_get_cookie(&http_pool[1], "a", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("1", v); // trailing HTAB trimmed
}

// A value that opens with a DQUOTE but never closes one is left exactly as-is (not stripped) -
// the quote-pair check's "closing quote present" arm must be false, not just true.
void test_cookie_unterminated_quote_not_stripped()
{
    feed_request(0, "GET / HTTP/1.1\r\nCookie: a=\"abc;b=2\r\n\r\n");
    char v[32];
    TEST_ASSERT_TRUE(http_get_cookie(&http_pool[0], "a", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("\"abc", v); // opening quote kept, nothing stripped
}

// --- http_forwarded_client (RFC 7239 Forwarded / X-Forwarded-For) ----------

void test_forwarded_rfc7239()
{
    feed_request(0, "GET / HTTP/1.1\r\nForwarded: for=192.0.2.60;proto=https;by=203.0.113.1\r\n\r\n");
    char ip[24];
    bool https = false;
    TEST_ASSERT_TRUE(http_forwarded_client(&http_pool[0], ip, sizeof(ip), &https));
    TEST_ASSERT_EQUAL_STRING("192.0.2.60", ip);
    TEST_ASSERT_TRUE(https);
}

void test_forwarded_leftmost_client()
{
    // Both header forms list the original client leftmost.
    feed_request(0, "GET / HTTP/1.1\r\nForwarded: for=198.51.100.7, for=203.0.113.9\r\n\r\n");
    char ip[24];
    TEST_ASSERT_TRUE(http_forwarded_client(&http_pool[0], ip, sizeof(ip), nullptr));
    TEST_ASSERT_EQUAL_STRING("198.51.100.7", ip);

    feed_request(1, "GET / HTTP/1.1\r\nX-Forwarded-For: 198.51.100.23, 10.0.0.1, 10.0.0.2\r\n"
                    "X-Forwarded-Proto: https\r\n\r\n");
    bool https = false;
    TEST_ASSERT_TRUE(http_forwarded_client(&http_pool[1], ip, sizeof(ip), &https));
    TEST_ASSERT_EQUAL_STRING("198.51.100.23", ip);
    TEST_ASSERT_TRUE(https);
}

void test_forwarded_strips_quotes_and_port()
{
    feed_request(0, "GET / HTTP/1.1\r\nForwarded: for=\"192.0.2.43:47011\"\r\n\r\n");
    char ip[24];
    TEST_ASSERT_TRUE(http_forwarded_client(&http_pool[0], ip, sizeof(ip), nullptr));
    TEST_ASSERT_EQUAL_STRING("192.0.2.43", ip); // DQUOTE + :port removed
}

void test_forwarded_ipv6_recovered_unknown_rejected()
{
    char ip[PC_IP_STR_MAX];
    // RFC 7239 §6: an IPv6 for= value is DQUOTE-wrapped + bracketed, optional :port.
    feed_request(0, "GET / HTTP/1.1\r\nForwarded: for=\"[2001:db8::1]:8080\"\r\n\r\n");
    TEST_ASSERT_TRUE(http_forwarded_client(&http_pool[0], ip, sizeof(ip), nullptr));
    TEST_ASSERT_EQUAL_STRING("2001:db8::1", ip); // brackets + :port stripped
    // Bare IPv6 in the de-facto X-Forwarded-For (leftmost client), canonicalized (RFC 5952).
    feed_request(1, "GET / HTTP/1.1\r\nX-Forwarded-For: 2001:DB8:0:0:0:0:0:2, 198.51.100.1\r\n\r\n");
    TEST_ASSERT_TRUE(http_forwarded_client(&http_pool[1], ip, sizeof(ip), nullptr));
    TEST_ASSERT_EQUAL_STRING("2001:db8::2", ip);
    // "unknown" and obfuscated identifiers (RFC 7239 §6.3) are not addresses -> rejected.
    feed_request(2, "GET / HTTP/1.1\r\nX-Forwarded-For: unknown\r\n\r\n");
    TEST_ASSERT_FALSE(http_forwarded_client(&http_pool[2], ip, sizeof(ip), nullptr));
    feed_request(3, "GET / HTTP/1.1\r\nForwarded: for=_hidden\r\n\r\n");
    TEST_ASSERT_FALSE(http_forwarded_client(&http_pool[3], ip, sizeof(ip), nullptr));
    // No header at all.
    feed_request(0, "GET / HTTP/1.1\r\n\r\n");
    TEST_ASSERT_FALSE(http_forwarded_client(&http_pool[0], ip, sizeof(ip), nullptr));
}

void test_header_leading_space_stripped()
{
    feed_request(0, "GET / HTTP/1.1\r\nX-Val:   trimmed\r\n\r\n");
    const char *v = http_get_header(&http_pool[0], "X-Val");
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_STRING("trimmed", v);
}

void test_content_length_header_parsed()
{
    feed_request(0, "POST / HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello");
    TEST_ASSERT_EQUAL(5, (int)http_pool[0].content_length);
}

void test_content_length_in_headers_array()
{
    feed_request(0, "POST / HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc");
    const char *cl = http_get_header(&http_pool[0], "Content-Length");
    TEST_ASSERT_NOT_NULL(cl);
    TEST_ASSERT_EQUAL_STRING("3", cl);
}

void test_multiple_headers_stored()
{
    feed_request(0, "GET / HTTP/1.1\r\n"
                    "X-A: one\r\n"
                    "X-B: two\r\n"
                    "X-C: three\r\n"
                    "\r\n");
    TEST_ASSERT_EQUAL(3, (int)http_pool[0].header_count);
    TEST_ASSERT_EQUAL_STRING("one", http_get_header(&http_pool[0], "X-A"));
    TEST_ASSERT_EQUAL_STRING("two", http_get_header(&http_pool[0], "X-B"));
    TEST_ASSERT_EQUAL_STRING("three", http_get_header(&http_pool[0], "X-C"));
}

void test_missing_header_returns_null()
{
    feed_request(0, "GET / HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NULL(http_get_header(&http_pool[0], "X-Missing"));
}

void test_long_standard_header_key_accepted()
{
    // Regression: "Sec-WebSocket-Extensions" (24 chars) is a standard header that
    // must parse and be retrievable - the permessage-deflate handshake needs it.
    // Previously MAX_KEY_LEN was 24 and the over-long-key path returned 400.
    feed_request(0, "GET /ws HTTP/1.1\r\n"
                    "Host: x\r\n"
                    "Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits\r\n"
                    "\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("permessage-deflate; client_max_window_bits",
                             http_get_header(&http_pool[0], "Sec-WebSocket-Extensions"));
}

void test_overlong_header_key_truncated_not_error()
{
    // A header name longer than MAX_KEY_LEN is capped (capacity), not rejected:
    // the request still parses and the other headers remain readable.
    feed_request(0, "GET / HTTP/1.1\r\n"
                    "This-Header-Name-Is-Far-Longer-Than-The-Key-Limit: ignored\r\n"
                    "X-Real: kept\r\n"
                    "\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("kept", http_get_header(&http_pool[0], "X-Real"));
}

// ====================================================================
// BODY TESTS
// ====================================================================

void test_get_no_body_completes()
{
    feed_request(0, "GET / HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(0, (int)http_pool[0].body_len);
    TEST_ASSERT_EQUAL('\0', (char)http_pool[0].body[0]);
}

void test_post_with_body()
{
    feed_request(0, "POST /r HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(5, (int)http_pool[0].body_len);
    TEST_ASSERT_EQUAL_STRING("hello", (const char *)http_pool[0].body);
}

void test_put_with_body()
{
    feed_request(0, "PUT /r HTTP/1.1\r\nContent-Length: 7\r\n\r\nupdated");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(7, (int)http_pool[0].body_len);
    TEST_ASSERT_EQUAL_STRING("updated", (const char *)http_pool[0].body);
}

void test_body_starting_with_newline()
{
    feed_request(0, "POST / HTTP/1.1\r\nContent-Length: 5\r\n\r\n\nabcd");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(5, (int)http_pool[0].body_len);
    TEST_ASSERT_EQUAL('\n', (char)http_pool[0].body[0]);
    TEST_ASSERT_EQUAL_STRING("\nabcd", (const char *)http_pool[0].body);
}

void test_post_content_length_zero()
{
    feed_request(0, "POST / HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(0, (int)http_pool[0].body_len);
}

void test_body_exactly_at_buffer_limit()
{
    // Body of exactly BODY_BUF_SIZE bytes - should succeed
    char req[32 + BODY_BUF_SIZE + 64];
    int off = 0;
    off +=
        snprintf(req + off, sizeof(req) - (size_t)off, "POST / HTTP/1.1\r\nContent-Length: %d\r\n\r\n", BODY_BUF_SIZE);
    memset(req + off, 'X', BODY_BUF_SIZE);
    off += BODY_BUF_SIZE;
    req[off] = '\0';

    feed_request(0, req);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(BODY_BUF_SIZE, (int)http_pool[0].body_len);
}

void test_body_null_terminated_after_complete()
{
    feed_request(0, "POST / HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc");
    TEST_ASSERT_EQUAL('\0', (char)http_pool[0].body[3]);
}

// ====================================================================
// 413 - PARSE_ENTITY_TOO_LARGE
// ====================================================================

void test_body_one_over_limit_is_413()
{
    // Content-Length == BODY_BUF_SIZE + 1 → PARSE_ENTITY_TOO_LARGE
    char req[128];
    snprintf(req, sizeof(req), "POST / HTTP/1.1\r\nContent-Length: %d\r\n\r\n", BODY_BUF_SIZE + 1);
    feed_request(0, req);
    TEST_ASSERT_EQUAL(PARSE_ENTITY_TOO_LARGE, http_pool[0].parse_state);
}

void test_body_far_over_limit_is_413()
{
    char req[128];
    snprintf(req, sizeof(req), "POST / HTTP/1.1\r\nContent-Length: 65535\r\n\r\n");
    feed_request(0, req);
    TEST_ASSERT_EQUAL(PARSE_ENTITY_TOO_LARGE, http_pool[0].parse_state);
}

void test_413_no_body_bytes_fed()
{
    // Even though we detected 413, no body bytes should have been stored
    char req[128];
    snprintf(req, sizeof(req), "POST / HTTP/1.1\r\nContent-Length: %d\r\n\r\n", BODY_BUF_SIZE + 1);
    feed_request(0, req);
    TEST_ASSERT_EQUAL(0, (int)http_pool[0].body_len);
}

void test_413_header_still_stored()
{
    // Headers before the blank line must be accessible even when 413
    char req[128];
    snprintf(req, sizeof(req), "POST / HTTP/1.1\r\nX-Tag: test\r\nContent-Length: %d\r\n\r\n", BODY_BUF_SIZE + 1);
    feed_request(0, req);
    TEST_ASSERT_EQUAL(PARSE_ENTITY_TOO_LARGE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("test", http_get_header(&http_pool[0], "X-Tag"));
}

void test_body_exactly_at_limit_is_not_413()
{
    // BODY_BUF_SIZE is the max that fits - should NOT trigger 413
    char req[128];
    snprintf(req, sizeof(req), "POST / HTTP/1.1\r\nContent-Length: %d\r\n\r\n", BODY_BUF_SIZE);
    feed_request(0, req);
    // Parser enters PARSE_BODY, not PARSE_ENTITY_TOO_LARGE
    // (we don't send the body bytes here - just check it didn't go 413)
    TEST_ASSERT_NOT_EQUAL(PARSE_ENTITY_TOO_LARGE, http_pool[0].parse_state);
}

// ====================================================================
// 414 - PARSE_URI_TOO_LONG
// ====================================================================

void test_path_overflow_stops_feeding()
{
    // Bytes fed after URI_TOO_LONG are ignored - state must not change
    char req[MAX_PATH_LEN + 64];
    int idx = 0;
    memcpy(req + idx, "GET /", 5);
    idx += 5;
    for (int i = 0; i < MAX_PATH_LEN; i++)
    {
        req[idx++] = 'a';
    }
    req[idx] = '\0';
    http_parser_reset(&http_pool[0]);
    feed_str(&http_pool[0], req);
    TEST_ASSERT_EQUAL(PARSE_URI_TOO_LONG, http_pool[0].parse_state);
    // Feed more bytes - state must stay PARSE_URI_TOO_LONG
    http_parser_feed(&http_pool[0], 'X');
    TEST_ASSERT_EQUAL(PARSE_URI_TOO_LONG, http_pool[0].parse_state);
}

void test_414_path_filled_to_capacity()
{
    // Buffer fills to MAX_PATH_LEN-1 chars before overflow is detected
    char req[MAX_PATH_LEN + 64];
    int idx = 0;
    memcpy(req + idx, "GET /api", 8);
    idx += 8;
    for (int i = 0; i < MAX_PATH_LEN; i++)
    {
        req[idx++] = 'x';
    }
    req[idx] = '\0';
    http_parser_reset(&http_pool[0]);
    feed_str(&http_pool[0], req);
    TEST_ASSERT_EQUAL(PARSE_URI_TOO_LONG, http_pool[0].parse_state);
    // Prefix intact; buffer filled to exactly MAX_PATH_LEN-1 chars
    TEST_ASSERT_EQUAL('/', http_pool[0].path[0]);
    TEST_ASSERT_EQUAL('a', http_pool[0].path[1]);
    TEST_ASSERT_EQUAL(MAX_PATH_LEN - 1, (int)strlen(http_pool[0].path));
}

// ====================================================================
// RFC 7230 COMPLIANCE - method (tchar only)
// ====================================================================

void test_method_nul_byte_is_error()
{
    http_parser_reset(&http_pool[0]);
    http_parser_feed(&http_pool[0], 0x00);
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_method_control_char_is_error()
{
    http_parser_reset(&http_pool[0]);
    http_parser_feed(&http_pool[0], 0x01);
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_method_del_byte_is_error()
{
    http_parser_reset(&http_pool[0]);
    http_parser_feed(&http_pool[0], 0x7F);
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_method_non_tchar_symbol_is_error()
{
    // '(' is VCHAR but not tchar
    http_parser_reset(&http_pool[0]);
    http_parser_feed(&http_pool[0], (uint8_t)'(');
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_method_tchar_symbols_accepted()
{
    // '-' is a valid tchar; a custom method like "X-CMD" is valid per RFC 7230
    feed_request(0, "X-CMD / HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("X-CMD", http_pool[0].method);
}

// ====================================================================
// RFC 7230 COMPLIANCE - path / query (VCHAR only)
// ====================================================================

void test_path_nul_byte_is_error()
{
    http_parser_reset(&http_pool[0]);
    feed_str(&http_pool[0], "GET /");
    http_parser_feed(&http_pool[0], 0x00); // NUL in path
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_path_control_char_is_error()
{
    http_parser_reset(&http_pool[0]);
    feed_str(&http_pool[0], "GET /");
    http_parser_feed(&http_pool[0], 0x01);
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_path_del_byte_is_error()
{
    http_parser_reset(&http_pool[0]);
    feed_str(&http_pool[0], "GET /");
    http_parser_feed(&http_pool[0], 0x7F);
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_query_nul_byte_is_error()
{
    http_parser_reset(&http_pool[0]);
    feed_str(&http_pool[0], "GET /p?k=");
    http_parser_feed(&http_pool[0], 0x00);
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_query_control_char_is_error()
{
    http_parser_reset(&http_pool[0]);
    feed_str(&http_pool[0], "GET /p?k=");
    http_parser_feed(&http_pool[0], 0x02);
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

// ====================================================================
// RFC 7230 COMPLIANCE - header field-name (tchar only)
// ====================================================================

void test_header_key_space_is_error()
{
    // Space in a field-name is not a valid tchar
    feed_request(0, "GET / HTTP/1.1\r\nX Bad: v\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_header_key_nul_byte_is_error()
{
    http_parser_reset(&http_pool[0]);
    feed_str(&http_pool[0], "GET / HTTP/1.1\r\nX-");
    http_parser_feed(&http_pool[0], 0x00);
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_header_key_control_char_is_error()
{
    http_parser_reset(&http_pool[0]);
    feed_str(&http_pool[0], "GET / HTTP/1.1\r\nX-");
    http_parser_feed(&http_pool[0], 0x01);
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_header_key_mid_cr_is_error()
{
    // CR in the middle of a key name must be PARSE_ERROR, not blank-line detection
    http_parser_reset(&http_pool[0]);
    feed_str(&http_pool[0], "GET / HTTP/1.1\r\nX-Foo");
    http_parser_feed(&http_pool[0], '\r'); // CR mid-key
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_header_key_colon_at_start_skips_header()
{
    // Empty key name (colon immediately after CRLF): transition to val with empty key
    // This is unusual but not explicitly rejected - the header just has an empty key name
    feed_request(0, "GET / HTTP/1.1\r\n: empty-key\r\n\r\n");
    // Parser enters header-val with empty key - should complete without error
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
}

// ====================================================================
// RFC 7230 COMPLIANCE - header field-value (field-value chars only)
// ====================================================================

void test_header_val_nul_byte_is_error()
{
    http_parser_reset(&http_pool[0]);
    feed_str(&http_pool[0], "GET / HTTP/1.1\r\nX-A: ");
    http_parser_feed(&http_pool[0], 0x00);
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_header_val_control_char_is_error()
{
    http_parser_reset(&http_pool[0]);
    feed_str(&http_pool[0], "GET / HTTP/1.1\r\nX-A: ");
    http_parser_feed(&http_pool[0], 0x01);
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_header_val_del_byte_is_error()
{
    http_parser_reset(&http_pool[0]);
    feed_str(&http_pool[0], "GET / HTTP/1.1\r\nX-A: ");
    http_parser_feed(&http_pool[0], 0x7F);
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_header_val_htab_mid_value_allowed()
{
    // HTAB is valid mid-value (RFC 7230 §3.2)
    http_parser_reset(&http_pool[0]);
    feed_str(&http_pool[0], "GET / HTTP/1.1\r\nX-A: foo");
    http_parser_feed(&http_pool[0], '\t');
    feed_str(&http_pool[0], "bar\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    const char *v = http_get_header(&http_pool[0], "X-A");
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL('f', v[0]); // value starts with 'f', not tab
}

void test_header_val_leading_htab_stripped()
{
    // Leading HTAB (OWS) is stripped just like leading SP
    feed_request(0, "GET / HTTP/1.1\r\nX-B:\tvalue\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    const char *v = http_get_header(&http_pool[0], "X-B");
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_STRING("value", v);
}

void test_header_val_obs_text_allowed()
{
    // obs-text bytes (%x80-FF) are allowed for legacy compatibility (RFC 7230 §3.2.6)
    http_parser_reset(&http_pool[0]);
    feed_str(&http_pool[0], "GET / HTTP/1.1\r\nX-C: ");
    http_parser_feed(&http_pool[0], 0x80); // obs-text
    http_parser_feed(&http_pool[0], 0xFF); // obs-text
    feed_str(&http_pool[0], "\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
}

// ====================================================================
// VERSION VALIDATION
// ====================================================================

void test_version_http11_recognized()
{
    feed_request(0, "GET / HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(HTTP_11, http_pool[0].version);
}

void test_version_http10_recognized()
{
    feed_request(0, "GET / HTTP/1.0\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(HTTP_10, http_pool[0].version);
}

void test_version_unknown_is_http_unknown()
{
    feed_request(0, "GET / HTTP/2.0\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(HTTP_UNKNOWN, http_pool[0].version);
}

void test_version_reset_to_unknown()
{
    feed_request(0, "GET / HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(HTTP_11, http_pool[0].version);
    http_parser_reset(&http_pool[0]);
    TEST_ASSERT_EQUAL(HTTP_UNKNOWN, http_pool[0].version);
}

// ====================================================================
// PARSE_ERROR CASES
// ====================================================================

void test_bad_expect_lf_is_error()
{
    // CRLF in version line replaced by CR + X (no LF)
    http_parser_reset(&http_pool[0]);
    feed_str(&http_pool[0], "GET / HTTP/1.1\rX"); // CR then non-LF
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_blank_line_non_lf_is_error()
{
    // Header block ends with CR + non-LF in the blank line
    http_parser_reset(&http_pool[0]);
    feed_str(&http_pool[0], "GET / HTTP/1.1\r\n\rX");
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

// ====================================================================
// MULTI-SLOT INDEPENDENCE
// ====================================================================

void test_slots_are_independent()
{
    feed_request(0, "GET /slot0 HTTP/1.1\r\n\r\n");
    feed_request(1, "POST /slot1 HTTP/1.1\r\nContent-Length: 4\r\n\r\ndata");

    TEST_ASSERT_EQUAL_STRING("GET", http_pool[0].method);
    TEST_ASSERT_EQUAL_STRING("/slot0", http_pool[0].path);
    TEST_ASSERT_EQUAL_STRING("POST", http_pool[1].method);
    TEST_ASSERT_EQUAL_STRING("/slot1", http_pool[1].path);
    TEST_ASSERT_EQUAL_STRING("data", (const char *)http_pool[1].body);
}

// ====================================================================
// INCREMENTAL FEED
// ====================================================================

void test_incremental_byte_by_byte()
{
    const char *raw = "GET /inc HTTP/1.1\r\n\r\n";
    http_parser_reset(&http_pool[0]);
    for (; *raw; raw++)
    {
        http_parser_feed(&http_pool[0], (uint8_t)*raw);
    }
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("/inc", http_pool[0].path);
}

void test_incremental_two_chunks()
{
    http_parser_reset(&http_pool[0]);
    feed_str(&http_pool[0], "POST /c HTTP/1.1\r\nContent-Length: 4\r\n\r\n");
    TEST_ASSERT_NOT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    feed_str(&http_pool[0], "body");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("body", (const char *)http_pool[0].body);
}

// ====================================================================
// STRESS
// ====================================================================

void stress_many_requests_same_slot()
{
    for (int i = 0; i < 100; i++)
    {
        feed_request(0, "GET /stress HTTP/1.1\r\n\r\n");
        TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    }
}

void stress_max_headers()
{
    // Build a request with MAX_HEADERS header lines
    char req[MAX_HEADERS * 32 + 64];
    int off = 0;
    off += snprintf(req + off, sizeof(req) - (size_t)off, "GET / HTTP/1.1\r\n");
    for (int i = 0; i < MAX_HEADERS; i++)
    {
        off += snprintf(req + off, sizeof(req) - (size_t)off, "H%d: v%d\r\n", i, i);
    }
    off += snprintf(req + off, sizeof(req) - (size_t)off, "\r\n");
    req[off] = '\0';

    feed_request(0, req);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(MAX_HEADERS, (int)http_pool[0].header_count);
}

void stress_max_query_params()
{
    // Build a query string with MAX_QUERY_PARAMS parameters
    char req[MAX_QUERY_PARAMS * 16 + 64];
    int off = 0;
    off += snprintf(req + off, sizeof(req) - (size_t)off, "GET /p?");
    for (int i = 0; i < MAX_QUERY_PARAMS; i++)
    {
        if (i > 0)
        {
            req[off++] = '&';
        }
        off += snprintf(req + off, sizeof(req) - (size_t)off, "k%d=%d", i, i);
    }
    off += snprintf(req + off, sizeof(req) - (size_t)off, " HTTP/1.1\r\n\r\n");
    req[off] = '\0';

    feed_request(0, req);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(MAX_QUERY_PARAMS, (int)http_pool[0].query_count);
}

// The request accessors reject null / zero-size arguments.
void test_accessor_null_guards()
{
    feed_request(0, "GET /?a=1 HTTP/1.1\r\nCookie: a=1\r\n\r\n");
    char v[16];
    bool https = false;
    TEST_ASSERT_FALSE(http_get_cookie(&http_pool[0], "a", nullptr, sizeof(v)));
    TEST_ASSERT_FALSE(http_get_cookie(&http_pool[0], "a", v, 0));
    TEST_ASSERT_FALSE(http_get_cookie(nullptr, "a", v, sizeof(v)));
    TEST_ASSERT_FALSE(http_get_cookie(&http_pool[0], nullptr, v, sizeof(v)));
    TEST_ASSERT_FALSE(http_get_cookie(&http_pool[0], "", v, sizeof(v))); // empty name

    TEST_ASSERT_FALSE(http_forwarded_client(nullptr, v, sizeof(v), &https));
    TEST_ASSERT_FALSE(http_forwarded_client(&http_pool[0], nullptr, sizeof(v), &https));
    TEST_ASSERT_FALSE(http_forwarded_client(&http_pool[0], v, 0, &https));

    TEST_ASSERT_FALSE(http_get_form(&http_pool[0], "a", nullptr, sizeof(v)));
    TEST_ASSERT_FALSE(http_get_form(&http_pool[0], "a", v, 0));
    TEST_ASSERT_FALSE(http_get_form(nullptr, "a", v, sizeof(v)));
    TEST_ASSERT_FALSE(http_get_form(&http_pool[0], nullptr, v, sizeof(v)));

    TEST_ASSERT_NULL(http_get_param(nullptr, "a"));
    TEST_ASSERT_NULL(http_get_param(&http_pool[0], nullptr));
}

// Cookie parsing edge cases: a trailing separator with no more pairs, a value with
// trailing whitespace, and a value longer than the output buffer.
void test_cookie_parse_edges()
{
    char v[16];
    feed_request(0, "GET / HTTP/1.1\r\nCookie: a=1; \r\n\r\n");             // trailing "; "
    TEST_ASSERT_FALSE(http_get_cookie(&http_pool[0], "zzz", v, sizeof(v))); // scans to end past separators

    feed_request(0, "GET / HTTP/1.1\r\nCookie: a=1  ;b=2\r\n\r\n"); // value "1  " before ';'
    TEST_ASSERT_TRUE(http_get_cookie(&http_pool[0], "a", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("1", v); // trailing OWS trimmed

    feed_request(0, "GET / HTTP/1.1\r\nCookie: a=0123456789ABCDEF\r\n\r\n");
    char small[4];
    TEST_ASSERT_TRUE(http_get_cookie(&http_pool[0], "a", small, sizeof(small)));
    TEST_ASSERT_EQUAL_UINT(3, (unsigned)strlen(small)); // truncated to out_size-1
}

// Forwarded-for IPv4 extraction trims surrounding whitespace and rejects a token that
// is not dotted IPv4.
void test_forwarded_ip_whitespace_and_invalid()
{
    char ip[24];
    feed_request(0, "GET / HTTP/1.1\r\nForwarded: for= 1.2.3.4 \r\n\r\n"); // OWS around the token
    TEST_ASSERT_TRUE(http_forwarded_client(&http_pool[0], ip, sizeof(ip), nullptr));
    TEST_ASSERT_EQUAL_STRING("1.2.3.4", ip);

    feed_request(0, "GET / HTTP/1.1\r\nX-Forwarded-For: 1.2.3.4  , 5.6.7.8\r\n\r\n"); // trailing OWS on token
    TEST_ASSERT_TRUE(http_forwarded_client(&http_pool[0], ip, sizeof(ip), nullptr));
    TEST_ASSERT_EQUAL_STRING("1.2.3.4", ip);

    feed_request(0, "GET / HTTP/1.1\r\nForwarded: for=1.2.3\r\n\r\n"); // only two dots -> not IPv4
    TEST_ASSERT_FALSE(http_forwarded_client(&http_pool[0], ip, sizeof(ip), nullptr));
}

// --- Content-Length framing (RFC 7230 §3.3.2 / request-smuggling guards) ---------

void test_content_length_non_numeric_is_error()
{
    feed_request(0, "POST / HTTP/1.1\r\nContent-Length: abc\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

// A leading '+' (below '0' in the ASCII range) is not a digit either - covers the digit
// scan's *q < '0' arm, which "abc" above (all chars > '9') never exercises.
void test_content_length_leading_symbol_is_error()
{
    feed_request(0, "POST / HTTP/1.1\r\nContent-Length: +5\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_content_length_conflicting_duplicate_is_error()
{
    feed_request(0, "POST / HTTP/1.1\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\nhello");
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_content_length_matching_duplicate_is_not_error()
{
    // Two identical Content-Length headers agree, so this is not a smuggling vector.
    feed_request(0, "POST / HTTP/1.1\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\nhello");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("hello", (const char *)http_pool[0].body);
}

void test_transfer_encoding_is_rejected()
{
    // RFC 9112 §6.1/§6.3: this server never decodes chunked bodies - fail closed.
    feed_request(0, "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_duplicate_host_header_is_error()
{
    // RFC 7230 §5.4: a request MUST NOT carry more than one Host header.
    feed_request(0, "GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_ERROR, http_pool[0].parse_state);
}

void test_header_value_overflow_truncated_not_error()
{
    // A header value longer than MAX_VAL_LEN is capped (capacity limit), not rejected.
    char req[256];
    snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nX-Big: %060d\r\n\r\n", 0);
    feed_request(0, req);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    const char *v = http_get_header(&http_pool[0], "X-Big");
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL(MAX_VAL_LEN - 1, (int)strlen(v));
}

// --- Authorization header capture (PC_CAPTURE_AUTH_HEADER) -----------------------

#if PC_CAPTURE_AUTH_HEADER
void test_authorization_header_captured()
{
    feed_request(0, "GET / HTTP/1.1\r\nAuthorization: Bearer abc.def.ghi\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("Bearer abc.def.ghi", http_pool[0].authorization);
    TEST_ASSERT_FALSE(http_pool[0].cur_is_auth);
}

void test_authorization_header_capped_at_capacity()
{
    // A value longer than PC_AUTH_HDR_CAP must be truncated rather than overrun the buffer.
    char req[PC_AUTH_HDR_CAP + 64];
    int off = snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nAuthorization: ");
    for (int i = 0; i < PC_AUTH_HDR_CAP + 20; i++)
    {
        req[off++] = 'A';
    }
    off += snprintf(req + off, sizeof(req) - (size_t)off, "\r\n\r\n");
    req[off] = '\0';

    feed_request(0, req);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(PC_AUTH_HDR_CAP - 1, (int)strlen(http_pool[0].authorization));
}
#endif

// --- Query-string parsing edge cases (parse_query_params) -------------------------

void test_query_key_overflow_truncated()
{
    char req[128];
    int off = snprintf(req, sizeof(req), "GET /p?");
    for (int i = 0; i < QUERY_KEY_LEN + 4; i++)
    {
        req[off++] = 'k';
    }
    off += snprintf(req + off, sizeof(req) - (size_t)off, "=v HTTP/1.1\r\n\r\n");
    req[off] = '\0';
    feed_request(0, req);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(1, (int)http_pool[0].query_count);
    TEST_ASSERT_EQUAL(QUERY_KEY_LEN - 1, (int)strlen(http_pool[0].query_params[0].key));
    TEST_ASSERT_EQUAL_STRING("v", http_pool[0].query_params[0].val);
}

void test_query_value_overflow_truncated()
{
    char req[128];
    int off = snprintf(req, sizeof(req), "GET /p?k=");
    for (int i = 0; i < QUERY_VAL_LEN + 10; i++)
    {
        req[off++] = 'v';
    }
    off += snprintf(req + off, sizeof(req) - (size_t)off, " HTTP/1.1\r\n\r\n");
    req[off] = '\0';
    feed_request(0, req);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(QUERY_VAL_LEN - 1, (int)strlen(http_pool[0].query_params[0].val));
}

void test_query_embedded_equals_in_value()
{
    feed_request(0, "GET /p?k=a=b HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(1, (int)http_pool[0].query_count);
    TEST_ASSERT_EQUAL_STRING("a=b", http_get_query(&http_pool[0], "k"));
}

void test_query_empty_key_not_counted()
{
    feed_request(0, "GET /p?=orphan HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(0, (int)http_pool[0].query_count);
}

void test_query_raw_string_overflow_truncated()
{
    // Raw query text longer than MAX_QUERY_LEN is silently capped - a capacity limit,
    // not a protocol error.
    char req[MAX_QUERY_LEN + 64];
    int off = snprintf(req, sizeof(req), "GET /p?");
    for (int i = 0; i < MAX_QUERY_LEN + 20; i++)
    {
        req[off++] = 'a';
    }
    off += snprintf(req + off, sizeof(req) - (size_t)off, " HTTP/1.1\r\n\r\n");
    req[off] = '\0';
    feed_request(0, req);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(MAX_QUERY_LEN - 1, (int)strlen(http_pool[0].query));
}

// --- http_forwarded_client edge cases (fwd_extract_client) -------------------------

void test_forwarded_htab_whitespace_trimmed()
{
    char ip[24];
    feed_request(0, "GET / HTTP/1.1\r\nForwarded: for=\t9.9.9.9\t\r\n\r\n"); // HTAB OWS, not SP
    TEST_ASSERT_TRUE(http_forwarded_client(&http_pool[0], ip, sizeof(ip), nullptr));
    TEST_ASSERT_EQUAL_STRING("9.9.9.9", ip);
}

void test_forwarded_short_and_unterminated_quote_rejected()
{
    char ip[24];
    feed_request(0, "GET / HTTP/1.1\r\nForwarded: for=1\r\n\r\n"); // 1-char token, not an IP
    TEST_ASSERT_FALSE(http_forwarded_client(&http_pool[0], ip, sizeof(ip), nullptr));

    feed_request(1, "GET / HTTP/1.1\r\nForwarded: for=\"1.2.3.4\r\n\r\n"); // opening quote, no closing quote
    TEST_ASSERT_FALSE(http_forwarded_client(&http_pool[1], ip, sizeof(ip), nullptr));
}

void test_forwarded_bracketed_ipv6_overflow_and_unterminated()
{
    char req[128];
    char ip[24];

    // Bracket content longer than the PC_IP_STR_MAX scratch buffer.
    int off = snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nForwarded: for=[");
    for (int i = 0; i < PC_IP_STR_MAX + 10; i++)
    {
        req[off++] = 'f';
    }
    off += snprintf(req + off, sizeof(req) - (size_t)off, "]\r\n\r\n");
    req[off] = '\0';
    feed_request(0, req);
    TEST_ASSERT_FALSE(http_forwarded_client(&http_pool[0], ip, sizeof(ip), nullptr));

    // Opening '[' with no closing ']' at all.
    feed_request(1, "GET / HTTP/1.1\r\nForwarded: for=[2001:db8::1\r\n\r\n");
    TEST_ASSERT_FALSE(http_forwarded_client(&http_pool[1], ip, sizeof(ip), nullptr));
}

void test_forwarded_bare_colon_port_edge_cases()
{
    char ip[24];
    // Token starting with ':' - the single-colon "IPv4:port" split leaves a zero-length address.
    feed_request(0, "GET / HTTP/1.1\r\nForwarded: for=:80\r\n\r\n");
    TEST_ASSERT_FALSE(http_forwarded_client(&http_pool[0], ip, sizeof(ip), nullptr));

    // Two-or-more-colon token (treated as a bare IPv6 literal, no port stripped) longer than
    // the PC_IP_STR_MAX scratch buffer.
    feed_request(1, "GET / HTTP/1.1\r\n"
                    "X-Forwarded-For: 1234:1234:1234:1234:1234:1234:1234:1234:1234:1234\r\n\r\n");
    TEST_ASSERT_FALSE(http_forwarded_client(&http_pool[1], ip, sizeof(ip), nullptr));
}

void test_forwarded_proto_missing_or_in_later_element()
{
    bool https;

    // No "proto=" substring anywhere in the header.
    https = false;
    feed_request(0, "GET / HTTP/1.1\r\nForwarded: for=9.9.9.9\r\n\r\n");
    char ip[24];
    TEST_ASSERT_TRUE(http_forwarded_client(&http_pool[0], ip, sizeof(ip), &https));
    TEST_ASSERT_FALSE(https);

    // "proto=" present, but only in the second (comma-separated) element, and "for=" present
    // only in the first - so proto= is out of range and must not be picked up.
    https = false;
    feed_request(1, "GET / HTTP/1.1\r\nForwarded: for=9.9.9.9, for=1.1.1.1;proto=https\r\n\r\n");
    TEST_ASSERT_TRUE(http_forwarded_client(&http_pool[1], ip, sizeof(ip), &https));
    TEST_ASSERT_FALSE(https);

    // "proto=" in the first element, but "for=" only in the second - for= is out of range.
    https = false;
    feed_request(2, "GET / HTTP/1.1\r\nForwarded: proto=https, for=9.9.9.9\r\n\r\n");
    TEST_ASSERT_FALSE(http_forwarded_client(&http_pool[2], ip, sizeof(ip), &https));
    TEST_ASSERT_TRUE(https); // the first element's proto= still applies
}

// A "Forwarded" header present with no "for=" substring anywhere falls through to the
// X-Forwarded-For check (f == nullptr in fwd's "for=" search) instead of extracting a client.
void test_forwarded_header_present_without_for()
{
    char ip[24];
    feed_request(0, "GET / HTTP/1.1\r\nForwarded: proto=https\r\n\r\n");
    TEST_ASSERT_FALSE(http_forwarded_client(&http_pool[0], ip, sizeof(ip), nullptr));
}

// An empty for= token ("for=;...", token length 0) must be rejected by fwd_extract_client's
// n==0 guard rather than treated as a valid (empty) address.
void test_forwarded_empty_for_token_rejected()
{
    char ip[24];
    feed_request(0, "GET / HTTP/1.1\r\nForwarded: for=;proto=https\r\n\r\n");
    TEST_ASSERT_FALSE(http_forwarded_client(&http_pool[0], ip, sizeof(ip), nullptr));
}

void test_xff_proto_missing_or_mismatched()
{
    char ip[24];
    bool https;

    // X-Forwarded-For present, no X-Forwarded-Proto header at all.
    https = false;
    feed_request(0, "GET / HTTP/1.1\r\nX-Forwarded-For: 1.2.3.4\r\n\r\n");
    TEST_ASSERT_TRUE(http_forwarded_client(&http_pool[0], ip, sizeof(ip), &https));
    TEST_ASSERT_FALSE(https);

    // X-Forwarded-Proto present but not "https".
    https = false;
    feed_request(1, "GET / HTTP/1.1\r\nX-Forwarded-For: 1.2.3.4\r\nX-Forwarded-Proto: http\r\n\r\n");
    TEST_ASSERT_TRUE(http_forwarded_client(&http_pool[1], ip, sizeof(ip), &https));
    TEST_ASSERT_FALSE(https);
}

// --- http_get_form (application/x-www-form-urlencoded bodies) ---------------------

void test_form_basic_lookup_first_middle_last()
{
    feed_form(0, "application/x-www-form-urlencoded", "a=1&b=2&c=3");
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    char v[16];
    TEST_ASSERT_TRUE(http_get_form(&http_pool[0], "a", v, sizeof(v))); // first
    TEST_ASSERT_EQUAL_STRING("1", v);
    TEST_ASSERT_TRUE(http_get_form(&http_pool[0], "b", v, sizeof(v))); // middle
    TEST_ASSERT_EQUAL_STRING("2", v);
    TEST_ASSERT_TRUE(http_get_form(&http_pool[0], "c", v, sizeof(v))); // last, no trailing '&'
    TEST_ASSERT_EQUAL_STRING("3", v);
    TEST_ASSERT_FALSE(http_get_form(&http_pool[0], "z", v, sizeof(v))); // not found
}

void test_form_missing_or_wrong_content_type()
{
    char v[16];
    feed_form(0, nullptr, "a=1"); // no Content-Type header at all
    TEST_ASSERT_FALSE(http_get_form(&http_pool[0], "a", v, sizeof(v)));

    feed_form(1, "application/json", "a=1"); // wrong Content-Type
    TEST_ASSERT_FALSE(http_get_form(&http_pool[1], "a", v, sizeof(v)));
}

void test_form_content_type_with_charset_suffix()
{
    feed_form(0, "application/x-www-form-urlencoded; charset=utf-8", "x=y");
    char v[8];
    TEST_ASSERT_TRUE(http_get_form(&http_pool[0], "x", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("y", v);
}

void test_form_value_truncated_by_out_size()
{
    feed_form(0, "application/x-www-form-urlencoded", "k=0123456789ABCDEF");
    char small[4];
    TEST_ASSERT_TRUE(http_get_form(&http_pool[0], "k", small, sizeof(small)));
    TEST_ASSERT_EQUAL_UINT(3, (unsigned)strlen(small));
}

void test_form_key_exact_match_not_prefix()
{
    feed_form(0, "application/x-www-form-urlencoded", "ab=1&a=2");
    char v[8];
    TEST_ASSERT_TRUE(http_get_form(&http_pool[0], "a", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("2", v); // "a" must not match the stored "ab" pair
}

void test_form_key_without_equals_has_empty_value()
{
    feed_form(0, "application/x-www-form-urlencoded", "flag&x=1");
    char v[8];
    TEST_ASSERT_TRUE(http_get_form(&http_pool[0], "flag", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("", v);
}

// A trailing key with neither '=' nor '&' runs the key scan all the way to the end of the
// body (i == len), unlike test_form_key_without_equals_has_empty_value's mid-body "flag&...".
void test_form_trailing_key_without_equals_or_ampersand()
{
    feed_form(0, "application/x-www-form-urlencoded", "x=1&flag");
    char v[8];
    TEST_ASSERT_TRUE(http_get_form(&http_pool[0], "flag", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("", v);
}

// --- http_get_param (route :name captures) -----------------------------------------

void test_get_param_lookup()
{
    http_parser_reset(&http_pool[0]);
    http_pool[0].path_param_count = 2;
    strcpy(http_pool[0].path_params[0].key, "id");
    strcpy(http_pool[0].path_params[0].val, "42");
    strcpy(http_pool[0].path_params[1].key, "name");
    strcpy(http_pool[0].path_params[1].val, "bob");

    TEST_ASSERT_EQUAL_STRING("42", http_get_param(&http_pool[0], "id"));
    TEST_ASSERT_EQUAL_STRING("bob", http_get_param(&http_pool[0], "name"));
    TEST_ASSERT_NULL(http_get_param(&http_pool[0], "missing"));
}

// --- PARSE_BODY defensive capacity guard --------------------------------------------

void test_body_len_capacity_guard_direct()
{
    // The normal Content-Length gate (PARSE_EXPECT_BODY_LF) never lets content_length exceed
    // BODY_BUF_SIZE before entering PARSE_BODY, and the state flips to
    // PARSE_COMPLETE the instant body_bytes_read==content_length - so body_len can
    // never legitimately reach BODY_BUF_SIZE with a byte still incoming. Drive the state directly
    // to exercise the guard's defensive false arm without corrupting memory.
    http_parser_reset(&http_pool[0]);
    http_pool[0].parse_state = PARSE_BODY;
    http_pool[0].content_length = BODY_BUF_SIZE + 10;
    http_pool[0].body_len = BODY_BUF_SIZE;
    http_pool[0].body_bytes_read = BODY_BUF_SIZE;
    http_parser_feed(&http_pool[0], 'X');
    TEST_ASSERT_EQUAL(BODY_BUF_SIZE, (int)http_pool[0].body_len); // byte silently dropped, no overrun
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_accessor_null_guards);
    RUN_TEST(test_cookie_parse_edges);
    RUN_TEST(test_forwarded_ip_whitespace_and_invalid);

    // Content-Length / Transfer-Encoding / Host framing guards
    RUN_TEST(test_content_length_non_numeric_is_error);
    RUN_TEST(test_content_length_leading_symbol_is_error);
    RUN_TEST(test_content_length_conflicting_duplicate_is_error);
    RUN_TEST(test_content_length_matching_duplicate_is_not_error);
    RUN_TEST(test_transfer_encoding_is_rejected);
    RUN_TEST(test_duplicate_host_header_is_error);
    RUN_TEST(test_header_value_overflow_truncated_not_error);

#if PC_CAPTURE_AUTH_HEADER
    RUN_TEST(test_authorization_header_captured);
    RUN_TEST(test_authorization_header_capped_at_capacity);
#endif

    // Query-string edge cases
    RUN_TEST(test_query_key_overflow_truncated);
    RUN_TEST(test_query_value_overflow_truncated);
    RUN_TEST(test_query_embedded_equals_in_value);
    RUN_TEST(test_query_empty_key_not_counted);
    RUN_TEST(test_query_raw_string_overflow_truncated);

    // http_forwarded_client edge cases
    RUN_TEST(test_forwarded_htab_whitespace_trimmed);
    RUN_TEST(test_forwarded_short_and_unterminated_quote_rejected);
    RUN_TEST(test_forwarded_bracketed_ipv6_overflow_and_unterminated);
    RUN_TEST(test_forwarded_bare_colon_port_edge_cases);
    RUN_TEST(test_forwarded_proto_missing_or_in_later_element);
    RUN_TEST(test_forwarded_header_present_without_for);
    RUN_TEST(test_forwarded_empty_for_token_rejected);
    RUN_TEST(test_xff_proto_missing_or_mismatched);

    // http_get_form
    RUN_TEST(test_form_basic_lookup_first_middle_last);
    RUN_TEST(test_form_missing_or_wrong_content_type);
    RUN_TEST(test_form_content_type_with_charset_suffix);
    RUN_TEST(test_form_value_truncated_by_out_size);
    RUN_TEST(test_form_key_exact_match_not_prefix);
    RUN_TEST(test_form_key_without_equals_has_empty_value);
    RUN_TEST(test_form_trailing_key_without_equals_or_ampersand);

    // http_get_param
    RUN_TEST(test_get_param_lookup);

    // PARSE_BODY defensive guard
    RUN_TEST(test_body_len_capacity_guard_direct);

    // Reset invariants
    RUN_TEST(test_reset_sets_parse_method_state);
    RUN_TEST(test_reset_preserves_slot_id);
    RUN_TEST(test_reset_clears_method);
    RUN_TEST(test_reset_clears_path);
    RUN_TEST(test_reset_clears_header_count);
    RUN_TEST(test_reset_clears_body);
    RUN_TEST(test_reset_clears_query_count);

    // Terminal-state guard
    RUN_TEST(test_feed_after_complete_does_not_change_state);
    RUN_TEST(test_feed_after_error_does_not_change_state);
    RUN_TEST(test_feed_after_entity_too_large_does_not_change_state);

    // Method
    RUN_TEST(test_method_get);
    RUN_TEST(test_method_post);
    RUN_TEST(test_method_put);
    RUN_TEST(test_method_delete);
    RUN_TEST(test_method_patch);
    RUN_TEST(test_method_head);
    RUN_TEST(test_method_options);
    RUN_TEST(test_method_overflow_is_error);

    // Path
    RUN_TEST(test_path_root);
    RUN_TEST(test_path_segments);
    RUN_TEST(test_path_without_query);
    RUN_TEST(test_path_overflow_is_414);

    // Query
    RUN_TEST(test_single_query_param);
    RUN_TEST(test_two_query_params);
    RUN_TEST(test_query_key_not_found_returns_null);
    RUN_TEST(test_query_empty_value);

    // Headers
    RUN_TEST(test_single_header_stored);
    RUN_TEST(test_header_lookup_case_insensitive);
    RUN_TEST(test_cookie_basic_and_positions);
    RUN_TEST(test_cookie_missing_and_no_header);
    RUN_TEST(test_cookie_exact_name_not_substring);
    RUN_TEST(test_cookie_quoted_and_value_with_equals);
    RUN_TEST(test_cookie_htab_separator_skipped);
    RUN_TEST(test_cookie_malformed_pair_without_equals);
    RUN_TEST(test_cookie_empty_value_and_htab_trim);
    RUN_TEST(test_cookie_unterminated_quote_not_stripped);
    RUN_TEST(test_forwarded_rfc7239);
    RUN_TEST(test_forwarded_leftmost_client);
    RUN_TEST(test_forwarded_strips_quotes_and_port);
    RUN_TEST(test_forwarded_ipv6_recovered_unknown_rejected);
    RUN_TEST(test_header_leading_space_stripped);
    RUN_TEST(test_content_length_header_parsed);
    RUN_TEST(test_content_length_in_headers_array);
    RUN_TEST(test_multiple_headers_stored);
    RUN_TEST(test_missing_header_returns_null);

    // Body
    RUN_TEST(test_get_no_body_completes);
    RUN_TEST(test_post_with_body);
    RUN_TEST(test_put_with_body);
    RUN_TEST(test_body_starting_with_newline);
    RUN_TEST(test_post_content_length_zero);
    RUN_TEST(test_body_exactly_at_buffer_limit);
    RUN_TEST(test_body_null_terminated_after_complete);

    // 413
    RUN_TEST(test_body_one_over_limit_is_413);
    RUN_TEST(test_body_far_over_limit_is_413);
    RUN_TEST(test_413_no_body_bytes_fed);
    RUN_TEST(test_413_header_still_stored);
    RUN_TEST(test_body_exactly_at_limit_is_not_413);

    // 414
    RUN_TEST(test_path_overflow_stops_feeding);
    RUN_TEST(test_414_path_filled_to_capacity);

    // RFC 7230 - method (tchar)
    RUN_TEST(test_method_nul_byte_is_error);
    RUN_TEST(test_method_control_char_is_error);
    RUN_TEST(test_method_del_byte_is_error);
    RUN_TEST(test_method_non_tchar_symbol_is_error);
    RUN_TEST(test_method_tchar_symbols_accepted);

    // RFC 7230 - path / query (VCHAR)
    RUN_TEST(test_path_nul_byte_is_error);
    RUN_TEST(test_path_control_char_is_error);
    RUN_TEST(test_path_del_byte_is_error);
    RUN_TEST(test_query_nul_byte_is_error);
    RUN_TEST(test_query_control_char_is_error);

    // RFC 7230 - header field-name (tchar)
    RUN_TEST(test_header_key_space_is_error);
    RUN_TEST(test_header_key_nul_byte_is_error);
    RUN_TEST(test_header_key_control_char_is_error);
    RUN_TEST(test_header_key_mid_cr_is_error);
    RUN_TEST(test_header_key_colon_at_start_skips_header);
    RUN_TEST(test_long_standard_header_key_accepted);
    RUN_TEST(test_overlong_header_key_truncated_not_error);

    // RFC 7230 - header field-value
    RUN_TEST(test_header_val_nul_byte_is_error);
    RUN_TEST(test_header_val_control_char_is_error);
    RUN_TEST(test_header_val_del_byte_is_error);
    RUN_TEST(test_header_val_htab_mid_value_allowed);
    RUN_TEST(test_header_val_leading_htab_stripped);
    RUN_TEST(test_header_val_obs_text_allowed);

    // Version
    RUN_TEST(test_version_http11_recognized);
    RUN_TEST(test_version_http10_recognized);
    RUN_TEST(test_version_unknown_is_http_unknown);
    RUN_TEST(test_version_reset_to_unknown);

    // Errors
    RUN_TEST(test_bad_expect_lf_is_error);
    RUN_TEST(test_blank_line_non_lf_is_error);

    // Multi-slot
    RUN_TEST(test_slots_are_independent);

    // Incremental
    RUN_TEST(test_incremental_byte_by_byte);
    RUN_TEST(test_incremental_two_chunks);

    // Stress
    RUN_TEST(stress_many_requests_same_slot);
    RUN_TEST(stress_max_headers);
    RUN_TEST(stress_max_query_params);

    return UNITY_END();
}
