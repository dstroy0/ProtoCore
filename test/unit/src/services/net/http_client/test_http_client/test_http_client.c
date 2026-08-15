// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the HTTP user agent (services/net/http_client/http_client.h).
//
// Three published sections govern the pure half of this module. RFC 9112 sec 3.2.1 fixes the
// request target: "a client MUST send only the absolute path and query components of the target URI
// as the request-target", and "If the target URI's path component is empty, the client MUST send
// '/'". RFC 9110 sec 4.2.1 / 4.2.2 fix the scheme defaults (http on TCP port 80, https on 443) and
// sec 7.2 the Host field. RFC 9112 sec 6.3 fixes message-body framing in precedence order: item 3
// "the Transfer-Encoding overrides the Content-Length", item 4 chunked when it is the final coding,
// item 6 a valid Content-Length, item 8 "the number of octets received prior to the server closing
// the connection".
//
// test_body_framing_follows_the_rfc9112_precedence is the load-bearing case: it drives all four of
// those items over the same field section. Getting the precedence wrong is the request-smuggling /
// response-splitting failure sec 6.3 item 3 exists to name, so it is the one rule here whose
// violation is a security bug rather than a formatting one.

#include "services/net/http_client/http_client.h"
#include <string.h>

#include <unity.h>

static char g_host[80];
static char g_path[160];
static char g_req[768];
static uint8_t g_msg[512];

void setUp(void)
{
    memset(g_host, 0, sizeof(g_host));
    memset(g_path, 0, sizeof(g_path));
    memset(g_req, 0, sizeof(g_req));
    memset(g_msg, 0, sizeof(g_msg));
    HttpClient.target.host = g_host;
    HttpClient.target.host_cap = sizeof(g_host);
    HttpClient.target.path = g_path;
    HttpClient.target.path_cap = sizeof(g_path);
}

void tearDown(void)
{
}

static proto_bool split(const char *url)
{
    HttpClient.target.url = url;
    HttpClient.parse_target_uri(HttpClient.internal);
    return HttpClient.ok;
}

// Frame a response into the message buffer and parse it. The buffer is mutable because chunked
// decoding rewrites it in place.
static void parse(const char *text)
{
    size_t n = strlen(text);
    TEST_ASSERT_TRUE(n <= sizeof(g_msg));
    memcpy(g_msg, text, n);
    HttpClient.message.buf = g_msg;
    HttpClient.message.len = n;
    HttpClient.parse_response(HttpClient.internal);
}

static void assert_body(const char *want)
{
    TEST_ASSERT_EQUAL_size_t(strlen(want), HttpClient.body_len);
    TEST_ASSERT_EQUAL_MEMORY(want, g_msg + HttpClient.body_off, HttpClient.body_len);
}

// RFC 9112 sec 3.2.1 origin-form plus the RFC 9110 sec 4.2.1 / 4.2.2 scheme defaults.
void test_target_uri_split(void)
{
    TEST_ASSERT_TRUE(split("http://example.com/path"));
    TEST_ASSERT_EQUAL_STRING("example.com", g_host);
    TEST_ASSERT_EQUAL_STRING("/path", g_path);
    TEST_ASSERT_EQUAL_UINT16(80u, HttpClient.target.port);
    TEST_ASSERT_FALSE(HttpClient.target.https);

    TEST_ASSERT_TRUE(split("https://example.com/path"));
    TEST_ASSERT_EQUAL_UINT16(443u, HttpClient.target.port);
    TEST_ASSERT_TRUE(HttpClient.target.https);

    // an explicit port overrides the scheme default
    TEST_ASSERT_TRUE(split("http://example.com:8080/x"));
    TEST_ASSERT_EQUAL_STRING("example.com", g_host);
    TEST_ASSERT_EQUAL_STRING("/x", g_path);
    TEST_ASSERT_EQUAL_UINT16(8080u, HttpClient.target.port);

    // the query travels with the path, and only those two components do
    TEST_ASSERT_TRUE(split("http://example.com/search?q=a%20b&n=2"));
    TEST_ASSERT_EQUAL_STRING("/search?q=a%20b&n=2", g_path);

    // an empty path component sends "/"
    TEST_ASSERT_TRUE(split("https://example.com"));
    TEST_ASSERT_EQUAL_STRING("example.com", g_host);
    TEST_ASSERT_EQUAL_STRING("/", g_path);

    // the boundary values of the port field
    TEST_ASSERT_TRUE(split("http://h:1/"));
    TEST_ASSERT_EQUAL_UINT16(1u, HttpClient.target.port);
    TEST_ASSERT_TRUE(split("http://h:65535/"));
    TEST_ASSERT_EQUAL_UINT16(65535u, HttpClient.target.port);
}

// A URI the user agent cannot dial is refused rather than half-parsed.
void test_target_uri_refusals(void)
{
    static const char *const BAD[] = {
        "",                  // nothing
        "example.com/x",     // no scheme
        "ftp://example.com", // a scheme this agent does not speak
        "http://",           // no uri-host
        "http://:80/x",      // an empty uri-host
        "http://h:/x",       // a colon with no port
        "http://h:abc/x",    // a non-numeric port
        "http://h:65536/x",  // one past the 16-bit port field
        "http://h:99999/x",
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        TEST_ASSERT_FALSE_MESSAGE(split(BAD[i]), BAD[i]);
    }

    // a host or path longer than the caller's buffer is refused, not truncated
    HttpClient.target.host_cap = 5;
    TEST_ASSERT_FALSE(split("http://example.com/x"));
    HttpClient.target.host_cap = sizeof(g_host);
    HttpClient.target.path_cap = 3;
    TEST_ASSERT_FALSE(split("http://example.com/longer"));
    HttpClient.target.path_cap = 1; // no room even for "/"
    TEST_ASSERT_FALSE(split("http://example.com"));
    HttpClient.target.path_cap = sizeof(g_path);

    // and a call with nowhere to write the parts
    HttpClient.target.host = NULL;
    TEST_ASSERT_FALSE(split("http://example.com/x"));
    HttpClient.target.host = g_host;
    HttpClient.target.path = NULL;
    TEST_ASSERT_FALSE(split("http://example.com/x"));
    HttpClient.target.path = g_path;
    TEST_ASSERT_FALSE(split(NULL));
}

// RFC 9112 sec 3: "method SP request-target SP HTTP-version CRLF", then the field lines, then the
// empty line that ends the field section (sec 2.1).
void test_get_request_message(void)
{
    TEST_ASSERT_TRUE(split("http://example.com/path?q=1"));
    HttpClient.request.method = "GET";
    HttpClient.request.body = NULL;
    HttpClient.request.body_len = 0;
    HttpClient.request.out = g_req;
    HttpClient.request.cap = sizeof(g_req);
    HttpClient.build_request(HttpClient.internal);

    static const char WANT[] = "GET /path?q=1 HTTP/1.1\r\n"
                               "Host: example.com\r\n"
                               "User-Agent: PC\r\n"
                               "Connection: close\r\n"
                               "\r\n";
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT) - 1, HttpClient.n);
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_req, HttpClient.n);
    // a request with no content declares no Content-Length
    TEST_ASSERT_NULL(strstr(g_req, "Content-Length"));
}

// RFC 9110 sec 7.2 with sec 4.2.1 / 4.2.2: the authority's port is in Host only when it is not the
// scheme's default.
void test_host_field_carries_only_a_non_default_port(void)
{
    HttpClient.request.method = "GET";
    HttpClient.request.body = NULL;
    HttpClient.request.body_len = 0;
    HttpClient.request.out = g_req;
    HttpClient.request.cap = sizeof(g_req);

    TEST_ASSERT_TRUE(split("http://example.com:80/x"));
    HttpClient.build_request(HttpClient.internal);
    TEST_ASSERT_NOT_NULL(strstr(g_req, "Host: example.com\r\n"));

    TEST_ASSERT_TRUE(split("https://example.com:443/x"));
    HttpClient.build_request(HttpClient.internal);
    TEST_ASSERT_NOT_NULL(strstr(g_req, "Host: example.com\r\n"));

    TEST_ASSERT_TRUE(split("http://example.com:8080/x"));
    HttpClient.build_request(HttpClient.internal);
    TEST_ASSERT_NOT_NULL(strstr(g_req, "Host: example.com:8080\r\n"));

    // 443 is not the default for http, so it is carried
    TEST_ASSERT_TRUE(split("http://example.com:443/x"));
    HttpClient.build_request(HttpClient.internal);
    TEST_ASSERT_NOT_NULL(strstr(g_req, "Host: example.com:443\r\n"));
}

// RFC 9110 sec 8.3 Content-Type and sec 8.6 Content-Length accompany the enclosed content, and the
// content follows the empty line.
void test_post_request_message(void)
{
    static const uint8_t BODY[] = "hello";
    TEST_ASSERT_TRUE(split("http://example.com/submit"));
    HttpClient.request.method = "POST";
    HttpClient.request.content_type = "text/plain";
    HttpClient.request.body = BODY;
    HttpClient.request.body_len = 5;
    HttpClient.request.out = g_req;
    HttpClient.request.cap = sizeof(g_req);
    HttpClient.build_request(HttpClient.internal);

    static const char WANT[] = "POST /submit HTTP/1.1\r\n"
                               "Host: example.com\r\n"
                               "User-Agent: PC\r\n"
                               "Content-Type: text/plain\r\n"
                               "Content-Length: 5\r\n"
                               "Connection: close\r\n"
                               "\r\n"
                               "hello";
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT) - 1, HttpClient.n);
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_req, HttpClient.n);
}

// With content but no stated media type, RFC 9110 sec 8.3's fallback: application/octet-stream.
void test_post_defaults_the_content_type(void)
{
    static const uint8_t BODY[] = {0x01, 0x02, 0x03};
    TEST_ASSERT_TRUE(split("http://example.com/raw"));
    HttpClient.request.method = "POST";
    HttpClient.request.content_type = NULL;
    HttpClient.request.body = BODY;
    HttpClient.request.body_len = sizeof(BODY);
    HttpClient.request.out = g_req;
    HttpClient.request.cap = sizeof(g_req);
    HttpClient.build_request(HttpClient.internal);

    TEST_ASSERT_TRUE(HttpClient.n > 0);
    TEST_ASSERT_NOT_NULL(strstr(g_req, "Content-Type: application/octet-stream\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(g_req, "Content-Length: 3\r\n"));
    TEST_ASSERT_EQUAL_MEMORY(BODY, g_req + HttpClient.n - sizeof(BODY), sizeof(BODY));
}

// A buffer that cannot hold the whole message reports 0: half a request-line is a different request.
void test_build_refuses_a_short_buffer(void)
{
    static const uint8_t BODY[] = "hello";
    TEST_ASSERT_TRUE(split("http://example.com/path"));
    HttpClient.request.method = "GET";
    HttpClient.request.body = NULL;
    HttpClient.request.body_len = 0;
    HttpClient.request.out = g_req;
    HttpClient.request.cap = sizeof(g_req);
    HttpClient.build_request(HttpClient.internal);
    size_t full = HttpClient.n;
    TEST_ASSERT_TRUE(full > 0);

    HttpClient.request.cap = full; // one octet short of the message plus its NUL
    HttpClient.build_request(HttpClient.internal);
    TEST_ASSERT_EQUAL_size_t(0u, HttpClient.n);

    // the field section fits but the content does not: one octet short of header plus body
    HttpClient.request.method = "POST";
    HttpClient.request.content_type = "text/plain";
    HttpClient.request.body = BODY;
    HttpClient.request.body_len = 5;
    HttpClient.request.cap = sizeof(g_req);
    HttpClient.build_request(HttpClient.internal);
    size_t post_full = HttpClient.n;
    TEST_ASSERT_TRUE(post_full > 5);

    HttpClient.request.cap = post_full - 1;
    HttpClient.build_request(HttpClient.internal);
    TEST_ASSERT_EQUAL_size_t(0u, HttpClient.n);

    HttpClient.request.out = NULL;
    HttpClient.request.cap = sizeof(g_req);
    HttpClient.build_request(HttpClient.internal);
    TEST_ASSERT_EQUAL_size_t(0u, HttpClient.n);
    HttpClient.request.out = g_req;
    HttpClient.request.method = NULL;
    HttpClient.build_request(HttpClient.internal);
    TEST_ASSERT_EQUAL_size_t(0u, HttpClient.n);
}

// RFC 9112 sec 4: "HTTP-version SP status-code SP [ reason-phrase ]", status-code = 3DIGIT.
void test_status_line(void)
{
    parse("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    TEST_ASSERT_EQUAL_INT32(200, HttpClient.status);

    parse("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
    TEST_ASSERT_EQUAL_INT32(404, HttpClient.status);

    // RFC 9110 sec 15: the range is 100..599, at both ends
    parse("HTTP/1.1 100 Continue\r\n\r\n");
    TEST_ASSERT_EQUAL_INT32(100, HttpClient.status);
    parse("HTTP/1.1 599 Whatever\r\n\r\n");
    TEST_ASSERT_EQUAL_INT32(599, HttpClient.status);

    // HTTP/1.0 is still an HTTP-version this parser reads
    parse("HTTP/1.0 204 No Content\r\n\r\n");
    TEST_ASSERT_EQUAL_INT32(204, HttpClient.status);

    // an empty reason-phrase is allowed by the grammar
    parse("HTTP/1.1 301 \r\nLocation: /x\r\n\r\n");
    TEST_ASSERT_EQUAL_INT32(301, HttpClient.status);
}

// A message that is not a status-line, or whose field section never ends, is a parse error, not a
// status of its own.
void test_malformed_responses_are_refused(void)
{
    static const char *const BAD[] = {
        "",                            // nothing
        "HTTP/1.1",                    // shorter than a status-line can be
        "NOTHTTP/1.1 200 OK\r\n\r\n",  // not an HTTP-version
        "HTTP/1.1 20 OK\r\n\r\n",      // status-code is 3DIGIT
        "HTTP/1.1 2xx OK\r\n\r\n",     // not digits
        "HTTP/1.1 099 X\r\n\r\n",      // below 100
        "HTTP/1.1 600 X\r\n\r\n",      // above 599
        "HTTP/1.1 200 OK\r\nX: 1\r\n", // the field section never ends
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        parse(BAD[i]);
        TEST_ASSERT_EQUAL_INT32_MESSAGE((int32_t)HTTP_CLIENT_ERR_RESPONSE, HttpClient.status, BAD[i]);
        TEST_ASSERT_EQUAL_size_t(0u, HttpClient.body_len);
    }

    HttpClient.message.buf = NULL;
    HttpClient.message.len = 64;
    HttpClient.parse_response(HttpClient.internal);
    TEST_ASSERT_EQUAL_INT32((int32_t)HTTP_CLIENT_ERR_RESPONSE, HttpClient.status);
}

// RFC 9112 sec 6.3, all four items that can decide a response body's length, over one parser.
void test_body_framing_follows_the_rfc9112_precedence(void)
{
    // item 6: a valid Content-Length without Transfer-Encoding
    parse("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello");
    TEST_ASSERT_EQUAL_INT32(200, HttpClient.status);
    assert_body("hello");

    // item 4: chunked as the final coding. "4" and "5" are hex chunk-sizes (sec 7.1), so the
    // decoded content is 4 + 5 = 9 octets and the last-chunk ends it.
    parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n");
    TEST_ASSERT_EQUAL_INT32(200, HttpClient.status);
    assert_body("Wikipedia");

    // item 3: with both fields present the Transfer-Encoding wins, so the bogus Content-Length is
    // never used to frame the body
    parse("HTTP/1.1 200 OK\r\nContent-Length: 100\r\nTransfer-Encoding: chunked\r\n\r\n"
          "4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n");
    assert_body("Wikipedia");

    // item 8: neither field, so the body is what arrived before the close
    parse("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nclose delimited");
    assert_body("close delimited");
}

// sec 6.3 item 4 again: chunked that is NOT the final coding does not frame the body, so the
// message falls through to the close-delimited rule rather than being decoded as chunks.
void test_chunked_must_be_the_final_coding(void)
{
    parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked, gzip\r\n\r\nrawbytes");
    TEST_ASSERT_EQUAL_INT32(200, HttpClient.status);
    assert_body("rawbytes");

    // and a coding list that ends in chunked is the final coding
    parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n");
    assert_body("abc");

    // trailing whitespace after the coding name does not hide it
    parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked  \r\n\r\n3\r\nabc\r\n0\r\n\r\n");
    assert_body("abc");

    // a coding merely ending in those letters is not "chunked"
    parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: unchunked\r\n\r\nrawbytes");
    assert_body("rawbytes");
}

// A response cut short of its declared Content-Length reports only the octets that arrived, never
// the declared count: reading past them would hand the caller memory nobody received.
void test_a_short_body_is_clamped_to_what_arrived(void)
{
    parse("HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nhello");
    TEST_ASSERT_EQUAL_INT32(200, HttpClient.status);
    assert_body("hello");

    // a Content-Length of zero frames an empty body even with octets in the buffer
    parse("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    TEST_ASSERT_EQUAL_size_t(0u, HttpClient.body_len);
}

// Field names are case-insensitive (RFC 9112 sec 5), and the value starts past the colon and its
// leading whitespace.
void test_field_names_are_case_insensitive(void)
{
    parse("HTTP/1.1 200 OK\r\ncontent-length: 5\r\n\r\nhello");
    assert_body("hello");
    parse("HTTP/1.1 200 OK\r\nCONTENT-LENGTH:\t5\r\n\r\nhello");
    assert_body("hello");
    parse("HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n");
    assert_body("abc");
}

// The body offset points at the first octet after the empty line, whatever the field section held.
void test_body_offset_is_past_the_field_section(void)
{
    static const char MSG[] = "HTTP/1.1 200 OK\r\nServer: x\r\nContent-Length: 3\r\n\r\nabc";
    parse(MSG);
    TEST_ASSERT_EQUAL_size_t(strlen(MSG) - 3, HttpClient.body_off);
    assert_body("abc");
}
