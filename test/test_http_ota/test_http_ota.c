// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Tests the parser's streaming-body hook (PC_ENABLE_OTA): a body larger than
// BODY_BUF_SIZE is streamed to a sink in chunks and reaches PARSE_COMPLETE
// (bypassing the 413 cap), while the default 413 behavior is preserved when no
// hook matches. Uses a mock sink (no ESP32 Update dependency).
//
// Also covers an unrelated fwd_extract_client() edge case (see
// test_xff_bracketed_ipv6_overflow below): this is the only native_* environment that
// links http_parser.cpp without also linking test_http_parser.cpp, so it is where that
// edge case's own coverage must be demonstrated for this build.

#include "network_drivers/network/ip.h" // PC_IP_STR_MAX for the bracketed-IPv6 overflow case
#include "network_drivers/presentation/http/http_parser/http_parser.h"
#include <stdio.h>
#include <string.h>

#include <unity.h>

static size_t g_total;
static int g_chunks;
static uint8_t g_capture[8192];

static proto_bool begin_cb(HttpReq *req)
{
    return strcmp(req->method, "POST") == 0 && strcmp(req->path, "/update") == 0;
}
static void data_cb(HttpReq *req, const uint8_t *d, size_t n)
{
    (void)req;
    if (g_total + n <= sizeof(g_capture))
    {
        memcpy(g_capture + g_total, d, n);
    }
    g_total += n;
    g_chunks++;
}

static void feed(HttpReq *r, const char *s)
{
    for (size_t i = 0; s[i]; i++)
    {
        http_parser_feed(r, (uint8_t)s[i]);
    }
}

void setUp()
{
    g_total = 0;
    g_chunks = 0;
    http_parser_set_stream_hooks(NULL, NULL, NULL);
}
void tearDown()
{
}

// A >BODY_BUF_SIZE body on the matched route streams to completion in chunks.
void test_large_body_streams_to_completion()
{
    http_parser_set_stream_hooks(begin_cb, data_cb, NULL);
    HttpReq r;
    r.slot_id = 0;
    http_parser_reset(&r);

    const size_t N = 4096; // >> BODY_BUF_SIZE (256)
    char hdr[128];
    snprintf(hdr, sizeof(hdr), "POST /update HTTP/1.1\r\nHost: x\r\nContent-Length: %u\r\n\r\n", (unsigned)N);
    feed(&r, hdr);
    for (size_t i = 0; i < N; i++)
    {
        http_parser_feed(&r, (uint8_t)('A' + (i % 26)));
    }

    TEST_ASSERT_EQUAL(PARSE_COMPLETE, r.parse_state);
    TEST_ASSERT_TRUE(r.body_streaming);
    TEST_ASSERT_EQUAL_UINT(N, (unsigned)g_total); // every byte delivered
    TEST_ASSERT_GREATER_THAN(1, g_chunks);        // multiple chunks → cap bypassed
    for (size_t i = 0; i < N; i++)                // content intact
    {
        TEST_ASSERT_EQUAL_UINT8('A' + (i % 26), g_capture[i]);
    }
}

// A body whose length is not a whole multiple of BODY_BUF_SIZE leaves a partial tail in the flush
// buffer at completion: the tail must be handed to the sink (line 482 body_len!=0 arm + the tail
// flush at 483). N = 256 + 44 fills one whole chunk, then a 44-byte remainder.
void test_partial_tail_chunk_is_flushed()
{
    http_parser_set_stream_hooks(begin_cb, data_cb, NULL);
    HttpReq r;
    r.slot_id = 0;
    http_parser_reset(&r);

    const size_t N = 300; // 256 (one full chunk) + 44 (tail) - not a multiple of BODY_BUF_SIZE
    char hdr[128];
    snprintf(hdr, sizeof(hdr), "POST /update HTTP/1.1\r\nHost: x\r\nContent-Length: %u\r\n\r\n", (unsigned)N);
    feed(&r, hdr);
    for (size_t i = 0; i < N; i++)
    {
        http_parser_feed(&r, (uint8_t)('A' + (i % 26)));
    }

    TEST_ASSERT_EQUAL(PARSE_COMPLETE, r.parse_state);
    TEST_ASSERT_EQUAL_UINT(N, (unsigned)g_total); // whole chunk + tail both delivered
    TEST_ASSERT_EQUAL_INT(2, g_chunks);           // one full flush + one tail flush
}

// A caller may install only the begin hook (null data sink): the streaming path must tolerate a
// null s_hp.stream_data at both the mid-body flush (line 475) and the tail flush (line 482's
// stream_data arm) without dispatching or crashing. N = 300 exercises both flush sites.
void test_stream_begin_without_data_sink_tolerates_null()
{
    http_parser_set_stream_hooks(begin_cb, NULL, NULL); // begin matches, but no data sink
    HttpReq r;
    r.slot_id = 0;
    http_parser_reset(&r);

    const size_t N = 300;
    char hdr[128];
    snprintf(hdr, sizeof(hdr), "POST /update HTTP/1.1\r\nHost: x\r\nContent-Length: %u\r\n\r\n", (unsigned)N);
    feed(&r, hdr);
    for (size_t i = 0; i < N; i++)
    {
        http_parser_feed(&r, (uint8_t)('A' + (i % 26)));
    }

    TEST_ASSERT_EQUAL(PARSE_COMPLETE, r.parse_state); // completes anyway
    TEST_ASSERT_TRUE(r.body_streaming);
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)g_total); // sink was null - nothing dispatched
}

// Without hooks, a >BODY_BUF_SIZE body still yields 413 (default unchanged).
void test_no_hooks_large_body_is_413()
{
    HttpReq r;
    r.slot_id = 0;
    http_parser_reset(&r);
    feed(&r, "POST /update HTTP/1.1\r\nHost: x\r\nContent-Length: 4096\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_ENTITY_TOO_LARGE, r.parse_state);
}

// A non-matching path is not streamed, so the large body still 413s.
void test_nonmatching_path_not_streamed()
{
    http_parser_set_stream_hooks(begin_cb, data_cb, NULL);
    HttpReq r;
    r.slot_id = 0;
    http_parser_reset(&r);
    feed(&r, "POST /other HTTP/1.1\r\nHost: x\r\nContent-Length: 4096\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_ENTITY_TOO_LARGE, r.parse_state);
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)g_total);
}

// fwd_extract_client()'s bracketed-IPv6 scratch buffer (tok[PC_IP_STR_MAX]) overflow guard
// is unreachable via "Forwarded: for=[...]" - the "for=" prefix eats 4 of the header value's
// MAX_VAL_LEN-1 (47) budget bytes, capping bracket content at 43 bytes, short of the 46 needed
// to trip the guard. But fwd_extract_client() is also called directly on X-Forwarded-For with
// no "for=" prefix stealing budget, so the full 47 bytes are available: '[' + 46 non-']' bytes
// drives tlen to PC_IP_STR_MAX-1 (45) and trips the guard on the 46th content byte.
void test_xff_bracketed_ipv6_overflow()
{
    char req[128];
    int off = snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nX-Forwarded-For: [");
    for (int i = 0; i < PC_IP_STR_MAX; i++) // 46 non-']' bytes: exactly enough to trip the guard
    {
        req[off++] = 'f';
    }
    off += snprintf(req + off, sizeof(req) - (size_t)off, "\r\n\r\n"); // no ']' - overflow fires first
    req[off] = '\0';

    HttpReq r;
    r.slot_id = 0;
    http_parser_reset(&r);
    feed(&r, req);

    char ip[PC_IP_STR_MAX];
    TEST_ASSERT_FALSE(http_forwarded_client(&r, ip, sizeof(ip), NULL));

    // Same call path, ordinary short bracketed IPv6: the guard's other arm (no overflow).
    HttpReq r2;
    r2.slot_id = 0;
    http_parser_reset(&r2);
    feed(&r2, "GET / HTTP/1.1\r\nX-Forwarded-For: [2001:db8::1]\r\n\r\n");
    TEST_ASSERT_TRUE(http_forwarded_client(&r2, ip, sizeof(ip), NULL));
    TEST_ASSERT_EQUAL_STRING("2001:db8::1", ip);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_large_body_streams_to_completion);
    RUN_TEST(test_partial_tail_chunk_is_flushed);
    RUN_TEST(test_stream_begin_without_data_sink_tolerates_null);
    RUN_TEST(test_no_hooks_large_body_is_413);
    RUN_TEST(test_nonmatching_path_not_streamed);
    RUN_TEST(test_xff_bracketed_ipv6_overflow);
    return UNITY_END();
}
