// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/presentation/http/http_parser/http_parser.h"
#include "shared/ip/ip.h"
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
        HttpParserV.feed_args.req = r;
        HttpParserV.feed_args.byte = (uint8_t)s[i];
        HttpParser.feed(protocore_http_parser_span());
    }
}

void setUp()
{
    g_total = 0;
    g_chunks = 0;
    HttpParserV.set_stream_hooks_args.begin = NULL;
    HttpParserV.set_stream_hooks_args.data = NULL;
    HttpParserV.set_stream_hooks_args.abort = NULL;
    HttpParser.set_stream_hooks(protocore_http_parser_span());
}
void tearDown()
{
}

void test_large_body_streams_to_completion()
{
    HttpParserV.set_stream_hooks_args.begin = begin_cb;
    HttpParserV.set_stream_hooks_args.data = data_cb;
    HttpParserV.set_stream_hooks_args.abort = NULL;
    HttpParser.set_stream_hooks(protocore_http_parser_span());
    HttpReq r;
    r.slot_id = 0;
    HttpParserV.reset_args.req = &r;
    HttpParser.reset(protocore_http_parser_span());

    const size_t N = 4096;
    char hdr[128];
    snprintf(hdr, sizeof(hdr), "POST /update HTTP/1.1\r\nHost: x\r\nContent-Length: %u\r\n\r\n", (unsigned)N);
    feed(&r, hdr);
    for (size_t i = 0; i < N; i++)
    {
        HttpParserV.feed_args.req = &r;
        HttpParserV.feed_args.byte = (uint8_t)('A' + (i % 26));
        HttpParser.feed(protocore_http_parser_span());
    }

    TEST_ASSERT_EQUAL(PARSE_COMPLETE, r.parse_state);
    TEST_ASSERT_TRUE(r.body_streaming);
    TEST_ASSERT_EQUAL_UINT(N, (unsigned)g_total);
    TEST_ASSERT_GREATER_THAN(1, g_chunks);
    for (size_t i = 0; i < N; i++)
    {
        TEST_ASSERT_EQUAL_UINT8('A' + (i % 26), g_capture[i]);
    }
}

void test_partial_tail_chunk_is_flushed()
{
    HttpParserV.set_stream_hooks_args.begin = begin_cb;
    HttpParserV.set_stream_hooks_args.data = data_cb;
    HttpParserV.set_stream_hooks_args.abort = NULL;
    HttpParser.set_stream_hooks(protocore_http_parser_span());
    HttpReq r;
    r.slot_id = 0;
    HttpParserV.reset_args.req = &r;
    HttpParser.reset(protocore_http_parser_span());

    const size_t N = 300;
    char hdr[128];
    snprintf(hdr, sizeof(hdr), "POST /update HTTP/1.1\r\nHost: x\r\nContent-Length: %u\r\n\r\n", (unsigned)N);
    feed(&r, hdr);
    for (size_t i = 0; i < N; i++)
    {
        HttpParserV.feed_args.req = &r;
        HttpParserV.feed_args.byte = (uint8_t)('A' + (i % 26));
        HttpParser.feed(protocore_http_parser_span());
    }

    TEST_ASSERT_EQUAL(PARSE_COMPLETE, r.parse_state);
    TEST_ASSERT_EQUAL_UINT(N, (unsigned)g_total);
    TEST_ASSERT_EQUAL_INT(2, g_chunks);
}

void test_stream_begin_without_data_sink_tolerates_null()
{
    HttpParserV.set_stream_hooks_args.begin = begin_cb;
    HttpParserV.set_stream_hooks_args.data = NULL;
    HttpParserV.set_stream_hooks_args.abort = NULL;
    HttpParser.set_stream_hooks(protocore_http_parser_span());
    HttpReq r;
    r.slot_id = 0;
    HttpParserV.reset_args.req = &r;
    HttpParser.reset(protocore_http_parser_span());

    const size_t N = 300;
    char hdr[128];
    snprintf(hdr, sizeof(hdr), "POST /update HTTP/1.1\r\nHost: x\r\nContent-Length: %u\r\n\r\n", (unsigned)N);
    feed(&r, hdr);
    for (size_t i = 0; i < N; i++)
    {
        HttpParserV.feed_args.req = &r;
        HttpParserV.feed_args.byte = (uint8_t)('A' + (i % 26));
        HttpParser.feed(protocore_http_parser_span());
    }

    TEST_ASSERT_EQUAL(PARSE_COMPLETE, r.parse_state);
    TEST_ASSERT_TRUE(r.body_streaming);
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)g_total);
}

void test_no_hooks_large_body_is_413()
{
    HttpReq r;
    r.slot_id = 0;
    HttpParserV.reset_args.req = &r;
    HttpParser.reset(protocore_http_parser_span());
    feed(&r, "POST /update HTTP/1.1\r\nHost: x\r\nContent-Length: 4096\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_ENTITY_TOO_LARGE, r.parse_state);
}

void test_nonmatching_path_not_streamed()
{
    HttpParserV.set_stream_hooks_args.begin = begin_cb;
    HttpParserV.set_stream_hooks_args.data = data_cb;
    HttpParserV.set_stream_hooks_args.abort = NULL;
    HttpParser.set_stream_hooks(protocore_http_parser_span());
    HttpReq r;
    r.slot_id = 0;
    HttpParserV.reset_args.req = &r;
    HttpParser.reset(protocore_http_parser_span());
    feed(&r, "POST /other HTTP/1.1\r\nHost: x\r\nContent-Length: 4096\r\n\r\n");
    TEST_ASSERT_EQUAL(PARSE_ENTITY_TOO_LARGE, r.parse_state);
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)g_total);
}

void test_xff_bracketed_ipv6_overflow()
{
    char req[128];
    int off = snprintf(req, sizeof(req), "GET / HTTP/1.1\r\nX-Forwarded-For: [");
    for (int i = 0; i < PROTOCORE_IP_STR_MAX; i++)
    {
        req[off++] = 'f';
    }
    off += snprintf(req + off, sizeof(req) - (size_t)off, "\r\n\r\n");
    req[off] = '\0';

    HttpReq r;
    r.slot_id = 0;
    HttpParserV.reset_args.req = &r;
    HttpParser.reset(protocore_http_parser_span());
    feed(&r, req);

    char ip[PROTOCORE_IP_STR_MAX];
    HttpParserV.forwarded_client_args.req = &r;
    HttpParserV.forwarded_client_args.ip_out = ip;
    HttpParserV.forwarded_client_args.ip_cap = sizeof(ip);
    HttpParserV.forwarded_client_args.is_https = NULL;
    HttpParser.forwarded_client(protocore_http_parser_span());
    TEST_ASSERT_FALSE(HttpParserV.ok);

    HttpReq r2;
    r2.slot_id = 0;
    HttpParserV.reset_args.req = &r2;
    HttpParser.reset(protocore_http_parser_span());
    feed(&r2, "GET / HTTP/1.1\r\nX-Forwarded-For: [2001:db8::1]\r\n\r\n");
    HttpParserV.forwarded_client_args.req = &r2;
    HttpParserV.forwarded_client_args.ip_out = ip;
    HttpParserV.forwarded_client_args.ip_cap = sizeof(ip);
    HttpParserV.forwarded_client_args.is_https = NULL;
    HttpParser.forwarded_client(protocore_http_parser_span());
    TEST_ASSERT_TRUE(HttpParserV.ok);
    TEST_ASSERT_EQUAL_STRING("2001:db8::1", ip);
}
