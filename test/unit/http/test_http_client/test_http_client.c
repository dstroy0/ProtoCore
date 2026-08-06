// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the outbound HTTP client's pure core: URL parsing, request
// building, and response parsing (incl. chunked decode). No sockets.

#include "services/net/http_client/http_client.h"
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// ---- URL parsing ----------------------------------------------------------

void test_url_http_default()
{
    proto_bool https;
    char host[64], path[64];
    uint16_t port;
    TEST_ASSERT_TRUE(
        http_client_parse_url("http://example.com/api/v1", &https, host, sizeof(host), &port, path, sizeof(path)));
    TEST_ASSERT_FALSE(https);
    TEST_ASSERT_EQUAL_STRING("example.com", host);
    TEST_ASSERT_EQUAL_UINT(80, port);
    TEST_ASSERT_EQUAL_STRING("/api/v1", path);
}

void test_url_https_port_nopath()
{
    proto_bool https;
    char host[64], path[64];
    uint16_t port;
    TEST_ASSERT_TRUE(
        http_client_parse_url("https://10.0.0.5:8443", &https, host, sizeof(host), &port, path, sizeof(path)));
    TEST_ASSERT_TRUE(https);
    TEST_ASSERT_EQUAL_STRING("10.0.0.5", host);
    TEST_ASSERT_EQUAL_UINT(8443, port);
    TEST_ASSERT_EQUAL_STRING("/", path); // default path
}

void test_url_bad_scheme()
{
    proto_bool https;
    char host[64], path[64];
    uint16_t port;
    TEST_ASSERT_FALSE(http_client_parse_url("ftp://x/y", &https, host, sizeof(host), &port, path, sizeof(path)));
    TEST_ASSERT_FALSE(http_client_parse_url("example.com", &https, host, sizeof(host), &port, path, sizeof(path)));
}

// ---- request building -----------------------------------------------------

void test_build_get()
{
    char buf[256];
    size_t n = http_client_build_request("GET", "example.com", 80, "/path", NULL, NULL, 0, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN_UINT(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "GET /path HTTP/1.1\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Host: example.com\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Connection: close\r\n\r\n"));
    TEST_ASSERT_NULL(strstr(buf, "Content-Length"));
}

void test_build_post_with_body_and_port()
{
    char buf[256];
    const char *body = "{\"k\":1}";
    size_t n = http_client_build_request("POST", "host", 8080, "/ingest", "application/json", (const uint8_t *)body,
                                         strlen(body), buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN_UINT(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "POST /ingest HTTP/1.1\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Host: host:8080\r\n")); // non-default port in Host
    TEST_ASSERT_NOT_NULL(strstr(buf, "Content-Type: application/json\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Content-Length: 7\r\n"));
    TEST_ASSERT_EQUAL_MEMORY(body, buf + n - 7, 7); // body appended verbatim
}

// ---- response parsing -----------------------------------------------------

void test_parse_content_length()
{
    char resp[] = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n\r\nhello";
    size_t off, blen;
    int st = http_client_parse_response((uint8_t *)resp, strlen(resp), &off, &blen);
    TEST_ASSERT_EQUAL_INT(200, st);
    TEST_ASSERT_EQUAL_UINT(5, blen);
    TEST_ASSERT_EQUAL_MEMORY("hello", resp + off, 5);
}

void test_parse_status_404()
{
    char resp[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    size_t off, blen;
    int st = http_client_parse_response((uint8_t *)resp, strlen(resp), &off, &blen);
    TEST_ASSERT_EQUAL_INT(404, st);
    TEST_ASSERT_EQUAL_UINT(0, blen);
}

void test_parse_chunked()
{
    // two chunks "Wiki" (4) + "pedia" (5) -> "Wikipedia"
    char resp[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n";
    size_t off, blen;
    int st = http_client_parse_response((uint8_t *)resp, strlen(resp), &off, &blen);
    TEST_ASSERT_EQUAL_INT(200, st);
    TEST_ASSERT_EQUAL_UINT(9, blen);
    TEST_ASSERT_EQUAL_MEMORY("Wikipedia", resp + off, 9);
}

// A chunk size that overflows size_t (16 hex digits = 0xFFFFFFFFFFFFFFFF) must be clamped to
// the bytes actually present, not trusted. The pre-fix bound `in + csz > len` wrapped and
// skipped the clamp, leaving a gigantic memmove (this crashes the old code even on a 64-bit
// host). The wrap-safe `csz > len - in` clamps it to the buffered bytes.
void test_parse_chunked_oversize_size_clamped()
{
    char resp[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nFFFFFFFFFFFFFFFF\r\nAB";
    size_t off, blen;
    int st = http_client_parse_response((uint8_t *)resp, strlen(resp), &off, &blen);
    TEST_ASSERT_EQUAL_INT(200, st);
    TEST_ASSERT_EQUAL_UINT(2, blen); // clamped to the 2 bytes actually buffered ("AB")
    TEST_ASSERT_EQUAL_MEMORY("AB", resp + off, 2);
}

void test_parse_connection_close_body()
{
    // No Content-Length / chunked: body is everything after the headers.
    char resp[] = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nstreamed body";
    size_t off, blen;
    int st = http_client_parse_response((uint8_t *)resp, strlen(resp), &off, &blen);
    TEST_ASSERT_EQUAL_INT(200, st);
    TEST_ASSERT_EQUAL_UINT(13, blen);
    TEST_ASSERT_EQUAL_MEMORY("streamed body", resp + off, 13);
}

void test_parse_malformed()
{
    char resp[] = "not http at all";
    size_t off, blen;
    int st = http_client_parse_response((uint8_t *)resp, strlen(resp), &off, &blen);
    TEST_ASSERT_LESS_THAN_INT(0, st);
}

void test_url_edge_rejections()
{
    proto_bool https = PROTO_FALSE;
    char host[64], path[128];
    uint16_t port = 0;
    TEST_ASSERT_FALSE(http_client_parse_url(NULL, &https, host, 64, &port, path, 128));        // null url
    TEST_ASSERT_FALSE(http_client_parse_url("http://h/", NULL, host, 64, &port, path, 128));   // null out
    TEST_ASSERT_FALSE(http_client_parse_url("http:///p", &https, host, 64, &port, path, 128)); // empty host
    TEST_ASSERT_FALSE(
        http_client_parse_url("http://longhost.example/", &https, host, 6, &port, path, 128));   // host too long
    TEST_ASSERT_FALSE(http_client_parse_url("http://h:/x", &https, host, 64, &port, path, 128)); // ':' then no digit
    TEST_ASSERT_FALSE(http_client_parse_url("http://h:99999/", &https, host, 64, &port, path, 128)); // port > 65535
    TEST_ASSERT_FALSE(http_client_parse_url("http://h/longpath", &https, host, 64, &port, path, 4)); // path too long
    TEST_ASSERT_FALSE(http_client_parse_url("http://h", &https, host, 64, &port, path, 1));          // no room for "/"

    TEST_ASSERT_TRUE(http_client_parse_url("http://h:8080/api", &https, host, 64, &port, path, 128));
    TEST_ASSERT_EQUAL_UINT16(8080, port);
    TEST_ASSERT_EQUAL_STRING("/api", path);
}

void test_build_edge_rejections()
{
    char out[256];
    const uint8_t body[] = "data";
    TEST_ASSERT_EQUAL_size_t(0, http_client_build_request(NULL, "h", 80, "/", NULL, NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, http_client_build_request("GET", "h", 80, "/", NULL, NULL, 0, out, 0));
    TEST_ASSERT_EQUAL_size_t(
        0, http_client_build_request("POST", "h", 80, "/", "text/plain", body, 4, out, 20)); // body overflow
    TEST_ASSERT_EQUAL_size_t(0, http_client_build_request("GET", "h", 80, "/", NULL, NULL, 0, out, 10)); // hdr overflow

    size_t n = http_client_build_request("GET", "h", 8080, "/", NULL, NULL, 0, out, sizeof(out));
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_NOT_NULL(strstr(out, "Host: h:8080\r\n")); // non-default port in the Host header
}

void test_response_edge_rejections()
{
    size_t boff = 0, blen = 0;
    char r2[] = "HTTP/1.1XXXXXXXX"; // no space after the version
    TEST_ASSERT_EQUAL_INT(HTTP_CLIENT_ERR_RESPONSE,
                          http_client_parse_response((uint8_t *)r2, sizeof(r2) - 1, &boff, &blen));
    char r3[] = "HTTP/1.1 999 Z\r\n\r\n"; // status outside 100..599
    TEST_ASSERT_EQUAL_INT(HTTP_CLIENT_ERR_RESPONSE,
                          http_client_parse_response((uint8_t *)r3, sizeof(r3) - 1, &boff, &blen));
    char r4[] = "HTTP/1.1 200 OK\r\nHost: x\r\n"; // no blank-line terminator
    TEST_ASSERT_EQUAL_INT(HTTP_CLIENT_ERR_RESPONSE,
                          http_client_parse_response((uint8_t *)r4, sizeof(r4) - 1, &boff, &blen));

    char r5[] =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\na\r\n0123456789\r\n0\r\n\r\n"; // lower-case hex size
    TEST_ASSERT_EQUAL_INT(200, http_client_parse_response((uint8_t *)r5, sizeof(r5) - 1, &boff, &blen));
    TEST_ASSERT_EQUAL_size_t(10, blen);
    TEST_ASSERT_EQUAL_MEMORY("0123456789", r5 + boff, 10);

    char r6[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n;ext\r\nx"; // junk size line -> empty body
    TEST_ASSERT_EQUAL_INT(200, http_client_parse_response((uint8_t *)r6, sizeof(r6) - 1, &boff, &blen));
    TEST_ASSERT_EQUAL_size_t(0, blen);

    char r7[] = "HTTP/1.1 200 OK\r\nContent-Length: -5\r\n\r\nbody"; // negative length clamps to 0
    TEST_ASSERT_EQUAL_INT(200, http_client_parse_response((uint8_t *)r7, sizeof(r7) - 1, &boff, &blen));
    TEST_ASSERT_EQUAL_size_t(0, blen);
}

// On a host build the transport entry points are stubs that report a connect error.
void test_host_transport_stubs()
{
    HttpClientResult res;
    TEST_ASSERT_EQUAL_INT(HTTP_CLIENT_ERR_CONNECT, http_get("http://x/", &res));
    const uint8_t b[] = "x";
    TEST_ASSERT_EQUAL_INT(HTTP_CLIENT_ERR_CONNECT, http_post("http://x/", "text/plain", b, 1, &res));
    http_client_set_ca(NULL, 0);
    http_client_set_pin(NULL);
    http_client_clear_verify();
    TEST_PASS();
}

// ---- coverage: parse_url null-arg / port-digit branch edges --------------

void test_url_parse_arg_and_port_edges()
{
    proto_bool https;
    char host[64], path[64];
    uint16_t port;

    // Each out-param (host, port, path) must reject on its own, not just when url/is_https are
    // missing (test_url_edge_rejections already covers those two).
    TEST_ASSERT_FALSE(http_client_parse_url("http://h/", &https, NULL, sizeof(host), &port, path, sizeof(path)));
    TEST_ASSERT_FALSE(http_client_parse_url("http://h/", &https, host, sizeof(host), NULL, path, sizeof(path)));
    TEST_ASSERT_FALSE(http_client_parse_url("http://h/", &https, host, sizeof(host), &port, NULL, sizeof(path)));

    // ':' followed by a char that's not a digit and *greater* than '9' (edge_rejections only
    // covers a char below '0').
    TEST_ASSERT_FALSE(http_client_parse_url("http://h:x/y", &https, host, sizeof(host), &port, path, sizeof(path)));

    // A second ':' right after a valid port stops the digit loop (without erroring); whatever
    // follows becomes the path verbatim.
    TEST_ASSERT_TRUE(http_client_parse_url("http://h:80:1/z", &https, host, sizeof(host), &port, path, sizeof(path)));
    TEST_ASSERT_EQUAL_UINT16(80, port);
    TEST_ASSERT_EQUAL_STRING(":1/z", path);
}

// ---- coverage: build_request null-arg / port / body-len / mime edges -----

void test_build_request_arg_and_port_edges()
{
    char out[256];
    const uint8_t body[] = "x";

    // Each out-param (host, path, out) must reject on its own (edge_rejections already covers
    // null method and cap == 0).
    TEST_ASSERT_EQUAL_size_t(0, http_client_build_request("GET", NULL, 80, "/", NULL, NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, http_client_build_request("GET", "h", 80, NULL, NULL, NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, http_client_build_request("GET", "h", 80, "/", NULL, NULL, 0, NULL, sizeof(out)));

    // Port 443 is the other "default" port: the Host header omits it, same as port 80.
    size_t n = http_client_build_request("GET", "h", 443, "/", NULL, NULL, 0, out, sizeof(out));
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_NOT_NULL(strstr(out, "Host: h\r\n"));
    TEST_ASSERT_NULL(strstr(out, "Host: h:443"));

    // A non-null body pointer with body_len == 0 must take the no-body path.
    n = http_client_build_request("GET", "h", 80, "/", "text/plain", body, 0, out, sizeof(out));
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_NULL(strstr(out, "Content-Length"));

    // A real body with content_type == NULL falls back to the default octet-stream mime type.
    n = http_client_build_request("POST", "h", 80, "/", NULL, body, 1, out, sizeof(out));
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_NOT_NULL(strstr(out, "Content-Type: application/octet-stream\r\n"));
}

// ---- coverage: parse_response framing edges (buf/len/status guards) ------

void test_response_framing_edges()
{
    size_t off = 0, blen = 0;

    // Null buffer and a too-short buffer (< 12 bytes) are rejected before any parsing begins.
    TEST_ASSERT_EQUAL_INT(HTTP_CLIENT_ERR_RESPONSE, http_client_parse_response(NULL, 20, &off, &blen));
    char short_resp[] = "HTTP/1.1 2"; // 10 bytes, under the 12-byte floor
    TEST_ASSERT_EQUAL_INT(HTTP_CLIENT_ERR_RESPONSE,
                          http_client_parse_response((uint8_t *)short_resp, sizeof(short_resp) - 1, &off, &blen));

    // A space is found, but fewer than 3 status digits remain in the buffer after it.
    char no_room[] = "HTTP/1.1xxx 1"; // space at index 11, only 1 byte left
    TEST_ASSERT_EQUAL_INT(HTTP_CLIENT_ERR_RESPONSE,
                          http_client_parse_response((uint8_t *)no_room, sizeof(no_room) - 1, &off, &blen));

    // Status digits that parse below 100 (edge_rejections only covers above 599).
    char low_status[] = "HTTP/1.1 099 X\r\n\r\n";
    TEST_ASSERT_EQUAL_INT(HTTP_CLIENT_ERR_RESPONSE,
                          http_client_parse_response((uint8_t *)low_status, sizeof(low_status) - 1, &off, &blen));

    // A lone '\r' (no '\n') and a "\r\n\r" not followed by '\n' are near-misses for the header
    // terminator; the scanner must keep looking instead of matching early or stopping short.
    char near_miss[] = "HTTP/1.1 200 OK\rX\r\n\rZ\r\n\r\nbody";
    int st = http_client_parse_response((uint8_t *)near_miss, sizeof(near_miss) - 1, &off, &blen);
    TEST_ASSERT_EQUAL_INT(200, st);
    TEST_ASSERT_EQUAL_size_t(4, blen);
    TEST_ASSERT_EQUAL_MEMORY("body", near_miss + off, 4);
}

// ---- coverage: find_header name-boundary / whitespace-skip edges ---------

void test_response_header_scan_edges()
{
    size_t off = 0, blen = 0;

    // A header whose name is a prefix of the target name, but not followed by ':', must not be
    // treated as a match - the scanner keeps looking for the real header.
    char prefix[] = "HTTP/1.1 200 OK\r\nContent-Length-Foo: 99\r\nContent-Length: 4\r\n\r\nbody";
    int st = http_client_parse_response((uint8_t *)prefix, sizeof(prefix) - 1, &off, &blen);
    TEST_ASSERT_EQUAL_INT(200, st);
    TEST_ASSERT_EQUAL_size_t(4, blen);
    TEST_ASSERT_EQUAL_MEMORY("body", prefix + off, 4);

    // A header value led by a tab instead of a space.
    char tabbed[] = "HTTP/1.1 200 OK\r\nContent-Length:\t4\r\n\r\nbody";
    st = http_client_parse_response((uint8_t *)tabbed, sizeof(tabbed) - 1, &off, &blen);
    TEST_ASSERT_EQUAL_INT(200, st);
    TEST_ASSERT_EQUAL_size_t(4, blen);
    TEST_ASSERT_EQUAL_MEMORY("body", tabbed + off, 4);

    // A header value that is nothing but spaces, running right up to the header terminator: the
    // whitespace-skip loop must exit via "v < end" going false, not via a non-space char.
    char spaces_only[] = "HTTP/1.1 200 OK\r\nContent-Length:   \r\n\r\nbody";
    st = http_client_parse_response((uint8_t *)spaces_only, sizeof(spaces_only) - 1, &off, &blen);
    TEST_ASSERT_EQUAL_INT(200, st);
}

// ---- coverage: chunked-decode junk / truncation edges ---------------------

void test_response_chunked_junk_edges()
{
    size_t off = 0, blen = 0;

    // Transfer-Encoding present but not "chunked": falls through to the Content-Length path.
    char not_chunked[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: identity\r\nContent-Length: 4\r\n\r\nbody";
    int st = http_client_parse_response((uint8_t *)not_chunked, sizeof(not_chunked) - 1, &off, &blen);
    TEST_ASSERT_EQUAL_INT(200, st);
    TEST_ASSERT_EQUAL_size_t(4, blen);
    TEST_ASSERT_EQUAL_MEMORY("body", not_chunked + off, 4);

    // A chunk-size line of valid hex digits that runs off the end of the buffer with no trailing
    // CRLF at all: the hex-scan and skip-to-newline loops must both exit via "in < len" going
    // false (buffer exhaustion), not via a break or a found '\r'/'\n'.
    char truncated_hex[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nFF";
    st = http_client_parse_response((uint8_t *)truncated_hex, sizeof(truncated_hex) - 1, &off, &blen);
    TEST_ASSERT_EQUAL_INT(200, st);
    TEST_ASSERT_EQUAL_size_t(0, blen);

    // Chunk-size line starting with a character below '0' (edge_rejections' ';' case is above '9').
    char below_digit[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n!ext\r\nx";
    st = http_client_parse_response((uint8_t *)below_digit, sizeof(below_digit) - 1, &off, &blen);
    TEST_ASSERT_EQUAL_INT(200, st);
    TEST_ASSERT_EQUAL_size_t(0, blen);

    // Chunk-size line starting with a lowercase letter past 'f' (out of both hex letter ranges).
    char past_hex[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\ng\r\n0\r\n\r\n";
    st = http_client_parse_response((uint8_t *)past_hex, sizeof(past_hex) - 1, &off, &blen);
    TEST_ASSERT_EQUAL_INT(200, st);
    TEST_ASSERT_EQUAL_size_t(0, blen);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_url_edge_rejections);
    RUN_TEST(test_build_edge_rejections);
    RUN_TEST(test_response_edge_rejections);
    RUN_TEST(test_host_transport_stubs);
    RUN_TEST(test_url_http_default);
    RUN_TEST(test_url_https_port_nopath);
    RUN_TEST(test_url_bad_scheme);
    RUN_TEST(test_build_get);
    RUN_TEST(test_build_post_with_body_and_port);
    RUN_TEST(test_parse_content_length);
    RUN_TEST(test_parse_status_404);
    RUN_TEST(test_parse_chunked);
    RUN_TEST(test_parse_chunked_oversize_size_clamped);
    RUN_TEST(test_parse_connection_close_body);
    RUN_TEST(test_parse_malformed);
    RUN_TEST(test_url_parse_arg_and_port_edges);
    RUN_TEST(test_build_request_arg_and_port_edges);
    RUN_TEST(test_response_framing_edges);
    RUN_TEST(test_response_header_scan_edges);
    RUN_TEST(test_response_chunked_junk_edges);
    return UNITY_END();
}
