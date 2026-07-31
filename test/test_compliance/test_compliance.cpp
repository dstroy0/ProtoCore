// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// RFC-compliance suite. Built with production enforcement defaults
// (PC_ENFORCE_HOST_HEADER=1) so the strict behaviors are exercised:
//
//   RFC 7230 §5.4 - Host header required for HTTP/1.1; at most one Host.
//   RFC 7230 §3.3.2 - Content-Length must be 1*DIGIT; conflicting duplicates
//                     are rejected.
//
// The parser is fed directly, byte by byte - no transport layer.

#include "network_drivers/presentation/http/http_parser/http_parser.h"
#include <stdio.h>
#include <unity.h>

#if PC_ENFORCE_HOST_HEADER != 1
#error "test_compliance must be built with PC_ENFORCE_HOST_HEADER=1"
#endif

static void feed_request(uint8_t slot, const char *raw)
{
    http_parser_reset(&http_pool[slot]);
    for (const char *s = raw; *s; s++)
    {
        http_parser_feed(&http_pool[slot], (uint8_t)*s);
    }
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

// ---- Host header (RFC 7230 §5.4) ------------------------------------------

void test_http11_missing_host_rejected()
{
    feed_request(0, "GET / HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL(ParseState::PARSE_ERROR, http_pool[0].parse_state);
}

void test_http11_with_host_ok()
{
    feed_request(0, "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
    TEST_ASSERT_EQUAL(ParseState::PARSE_COMPLETE, http_pool[0].parse_state);
}

void test_http10_missing_host_ok()
{
    // Host is not required for HTTP/1.0.
    feed_request(0, "GET / HTTP/1.0\r\n\r\n");
    TEST_ASSERT_EQUAL(ParseState::PARSE_COMPLETE, http_pool[0].parse_state);
}

void test_duplicate_host_rejected()
{
    feed_request(0, "GET / HTTP/1.1\r\nHost: a.com\r\nHost: b.com\r\n\r\n");
    TEST_ASSERT_EQUAL(ParseState::PARSE_ERROR, http_pool[0].parse_state);
}

void test_duplicate_host_rejected_http10()
{
    // More than one Host is invalid regardless of version.
    feed_request(0, "GET / HTTP/1.0\r\nHost: a.com\r\nHost: b.com\r\n\r\n");
    TEST_ASSERT_EQUAL(ParseState::PARSE_ERROR, http_pool[0].parse_state);
}

void test_host_beyond_max_headers_still_counted()
{
    // A valid Host that appears after MAX_HEADERS other fields is still counted
    // (detection is independent of the header storage cap), so the request is
    // accepted rather than wrongly rejected as "missing Host".
    char req[2048];
    int n = snprintf(req, sizeof(req), "GET / HTTP/1.1\r\n");
    for (int i = 0; i < MAX_HEADERS; i++)
    {
        n += snprintf(req + n, sizeof(req) - (size_t)n, "X-Filler-%d: v\r\n", i);
    }
    snprintf(req + n, sizeof(req) - (size_t)n, "Host: example.com\r\n\r\n");
    feed_request(0, req);
    TEST_ASSERT_EQUAL(ParseState::PARSE_COMPLETE, http_pool[0].parse_state);
}

void test_duplicate_host_with_one_beyond_cap_rejected()
{
    // First Host is stored; a second Host pushed past MAX_HEADERS must still be
    // counted so the duplicate is detected.
    char req[2048];
    int n = snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nHost: a.com\r\n");
    for (int i = 0; i < MAX_HEADERS; i++)
    {
        n += snprintf(req + n, sizeof(req) - (size_t)n, "X-Filler-%d: v\r\n", i);
    }
    snprintf(req + n, sizeof(req) - (size_t)n, "Host: b.com\r\n\r\n");
    feed_request(0, req);
    TEST_ASSERT_EQUAL(ParseState::PARSE_ERROR, http_pool[0].parse_state);
}

// ---- Content-Length (RFC 7230 §3.3.2) -------------------------------------

void test_content_length_non_digit_rejected()
{
    feed_request(0, "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 12abc\r\n\r\n");
    TEST_ASSERT_EQUAL(ParseState::PARSE_ERROR, http_pool[0].parse_state);
}

void test_content_length_empty_rejected()
{
    feed_request(0, "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: \r\n\r\n");
    TEST_ASSERT_EQUAL(ParseState::PARSE_ERROR, http_pool[0].parse_state);
}

void test_content_length_conflicting_duplicate_rejected()
{
    feed_request(0, "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\nContent-Length: 5\r\n\r\nabc");
    TEST_ASSERT_EQUAL(ParseState::PARSE_ERROR, http_pool[0].parse_state);
}

void test_content_length_matching_duplicate_ok()
{
    // Two identical Content-Length values are not a conflict.
    feed_request(0, "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\nContent-Length: 3\r\n\r\nabc");
    TEST_ASSERT_EQUAL(ParseState::PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(3, (int)http_pool[0].content_length);
}

void test_content_length_valid_body()
{
    feed_request(0, "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nhello");
    TEST_ASSERT_EQUAL(ParseState::PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(5, (int)http_pool[0].body_len);
    TEST_ASSERT_EQUAL_MEMORY("hello", http_pool[0].body, 5);
}

// ---- Transfer-Encoding (RFC 9112 §6.1/§6.3) -------------------------------
// The server does not decode chunked request bodies; a Transfer-Encoding alone
// (the chunk octets would otherwise be reparsed as the next request) or together
// with Content-Length is a request-smuggling vector. Both must be rejected.

void test_transfer_encoding_chunked_rejected()
{
    feed_request(0, "POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n");
    TEST_ASSERT_EQUAL(ParseState::PARSE_ERROR, http_pool[0].parse_state);
}

void test_transfer_encoding_with_content_length_rejected()
{
    // CL + TE present: the classic CL.TE smuggling desync - must be rejected.
    feed_request(0, "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 6\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
    TEST_ASSERT_EQUAL(ParseState::PARSE_ERROR, http_pool[0].parse_state);
}

void test_transfer_encoding_case_insensitive_rejected()
{
    // Header-name match must be case-insensitive (RFC 7230 §3.2).
    feed_request(0, "POST / HTTP/1.1\r\nHost: x\r\ntRaNsFeR-eNcOdInG: chunked\r\n\r\n0\r\n\r\n");
    TEST_ASSERT_EQUAL(ParseState::PARSE_ERROR, http_pool[0].parse_state);
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_http11_missing_host_rejected);
    RUN_TEST(test_http11_with_host_ok);
    RUN_TEST(test_http10_missing_host_ok);
    RUN_TEST(test_duplicate_host_rejected);
    RUN_TEST(test_duplicate_host_rejected_http10);
    RUN_TEST(test_host_beyond_max_headers_still_counted);
    RUN_TEST(test_duplicate_host_with_one_beyond_cap_rejected);

    RUN_TEST(test_content_length_non_digit_rejected);
    RUN_TEST(test_content_length_empty_rejected);
    RUN_TEST(test_content_length_conflicting_duplicate_rejected);
    RUN_TEST(test_content_length_matching_duplicate_ok);
    RUN_TEST(test_content_length_valid_body);

    RUN_TEST(test_transfer_encoding_chunked_rejected);
    RUN_TEST(test_transfer_encoding_with_content_length_rejected);
    RUN_TEST(test_transfer_encoding_case_insensitive_rejected);

    return UNITY_END();
}
