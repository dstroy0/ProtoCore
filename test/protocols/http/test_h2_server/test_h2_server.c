// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the HTTP/2 -> request-pipeline bridge
// (network_drivers/presentation/http/http2/h2_server): the RFC 9113 sec 8.2 / 8.3 validation a
// request header block has to survive before it reaches the route dispatcher. A block that fails
// must leave the slot's HttpReq untouched and draw an RST_STREAM, never a dispatch.

#include "network_drivers/presentation/http/http2/h2_server.h"

#include "network_drivers/presentation/http/http2/h2_conn.h"
#include "network_drivers/presentation/http/http2/h2_frame.h"
#include "network_drivers/presentation/http/http2/hpack.h"
#include "network_drivers/presentation/http/http_parser/http_parser.h"
#include "network_drivers/transport/tcp/tcp_conn.h"

#include <stdio.h>
#include <string.h>
#include <unity.h>

// ---------------------------------------------------------------------------
// The transport under the module: h2_server reads and writes only through these, so the test owns
// both ends of the wire.
// ---------------------------------------------------------------------------

TcpConn conn_pool[CONN_POOL_SLOTS];
uint32_t pc_ap_ip;

#define WIRE_MAX 8192

static uint8_t g_in[WIRE_MAX]; // bytes pc_tls_read hands to the module
static size_t g_in_len;
static size_t g_in_off;
static uint8_t g_out[WIRE_MAX]; // bytes the module wrote back
static size_t g_out_len;

int pc_tls_read(uint8_t slot, uint8_t *buf, size_t len)
{
    (void)slot;
    size_t n = g_in_len - g_in_off;
    if (n == 0)
    {
        return 0;
    }
    if (n > len)
    {
        n = len;
    }
    memcpy(buf, g_in + g_in_off, n);
    g_in_off += n;
    return (int)n;
}

int pc_tls_write(uint8_t slot, const void *data, size_t len)
{
    (void)slot;
    if (g_out_len + len <= WIRE_MAX)
    {
        memcpy(g_out + g_out_len, data, len);
        g_out_len += len;
    }
    return (int)len;
}

static void wire_reset(void)
{
    g_in_len = 0;
    g_in_off = 0;
    g_out_len = 0;
}

static void in_add(const void *p, size_t n)
{
    TEST_ASSERT_TRUE(g_in_len + n <= WIRE_MAX);
    memcpy(g_in + g_in_len, p, n);
    g_in_len += n;
}

// Counts frames of one type in what the module wrote, and reports the last one's error code.
static int out_count(uint8_t type, uint32_t *last_err)
{
    int n = 0;
    size_t off = 0;
    while (off + H2_FRAME_HEADER_LEN <= g_out_len)
    {
        H2FrameHeader h;
        pc_h2_parse_header(g_out + off, H2_FRAME_HEADER_LEN, &h);
        if (off + H2_FRAME_HEADER_LEN + h.length > g_out_len)
        {
            break;
        }
        if (h.type == type)
        {
            n++;
            if (last_err && h.length >= 4)
            {
                const uint8_t *p = g_out + off + H2_FRAME_HEADER_LEN;
                *last_err = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
            }
        }
        off += H2_FRAME_HEADER_LEN + h.length;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Driving one request through the bridge
// ---------------------------------------------------------------------------

typedef struct
{
    const char *name;
    const char *value;
} Hdr;

// Opens a fresh slot 0, feeds the preface, an empty client SETTINGS, and one END_HEADERS
// END_STREAM HEADERS frame carrying @p n fields on stream 1.
static void feed_request(const Hdr *fields, size_t n)
{
    wire_reset();
    memset(&conn_pool[0], 0, sizeof conn_pool[0]);
    pc_h2_server_open(0);
    g_out_len = 0; // drop our initial SETTINGS; the test only cares what the request provokes

    uint8_t block[1024];
    size_t bo = 0;
    for (size_t i = 0; i < n; i++)
    {
        bo += pc_hpack_encode_header(block + bo, sizeof block - bo, fields[i].name, strlen(fields[i].name),
                                     fields[i].value, strlen(fields[i].value));
    }

    uint8_t sf[9];
    in_add(H2_PREFACE, H2_PREFACE_LEN);
    in_add(sf, pc_h2_build_settings(sf, sizeof sf, NULL, NULL, 0));

    uint8_t hf[H2_FRAME_HEADER_LEN + sizeof block];
    in_add(hf, pc_h2_build_headers(hf, sizeof hf, 1, block, bo, PROTO_TRUE));

    pc_h2_server_data(0);
}

// A well-formed minimum: the three sec 8.3.1 pseudo-headers.
#define BASE3                                                                                                          \
    {":method", "GET"}, {":scheme", "https"},                                                                          \
    {                                                                                                                  \
        ":path", "/"                                                                                                   \
    }

// Asserts the block was rejected: nothing dispatched, the HttpReq left clean, one RST_STREAM out.
static void assert_malformed(void)
{
    TEST_ASSERT_NOT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)http_pool[0].method[0]);
    uint32_t err = 0;
    TEST_ASSERT_EQUAL_INT(1, out_count(H2_RST_STREAM, &err));
    TEST_ASSERT_EQUAL_UINT32(H2_PROTOCOL_ERROR, err);
}

static void assert_accepted(void)
{
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, (int)http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_INT(0, out_count(H2_RST_STREAM, NULL));
}

void setUp(void)
{
}
void tearDown(void)
{
}

// ---------------------------------------------------------------------------

void test_h2s_probe(void)
{
    const Hdr f[] = {BASE3};
    feed_request(f, 3);
    char msg[256];
    snprintf(msg, sizeof msg, "in=%u out=%u settings=%d goaway=%d rst=%d hdrs=%d parse=%d", (unsigned)g_in_len,
             (unsigned)g_out_len, out_count(H2_SETTINGS, NULL), out_count(H2_GOAWAY, NULL),
             out_count(H2_RST_STREAM, NULL), out_count(H2_HEADERS, NULL), (int)http_pool[0].parse_state);
    TEST_FAIL_MESSAGE(msg);
}

void test_h2s_minimal_request_is_accepted(void)
{
    const Hdr f[] = {BASE3, {":authority", "example.com"}, {"accept", "*/*"}};
    feed_request(f, 5);
    assert_accepted();
    TEST_ASSERT_EQUAL_STRING("GET", http_pool[0].method);
    TEST_ASSERT_EQUAL_STRING("/", http_pool[0].path);
    TEST_ASSERT_EQUAL_UINT32(1, conn_pool[0].pc_h2_stream);
}

void test_h2s_path_query_split(void)
{
    const Hdr f[] = {{":method", "GET"}, {":scheme", "https"}, {":path", "/api/v1?a=1&b=2"}};
    feed_request(f, 3);
    assert_accepted();
    TEST_ASSERT_EQUAL_STRING("/api/v1", http_pool[0].path);
    TEST_ASSERT_EQUAL_STRING("a=1&b=2", http_pool[0].query);
    TEST_ASSERT_EQUAL_UINT32(7, (uint32_t)http_pool[0].path_idx);
    TEST_ASSERT_EQUAL_UINT32(7, (uint32_t)http_pool[0].query_idx);
}

// RFC 9113 sec 8.3.1: ":method", ":scheme" and ":path" are each mandatory.
void test_h2s_missing_method_is_malformed(void)
{
    const Hdr f[] = {{":scheme", "https"}, {":path", "/"}};
    feed_request(f, 2);
    assert_malformed();
}

void test_h2s_missing_scheme_is_malformed(void)
{
    const Hdr f[] = {{":method", "GET"}, {":path", "/"}};
    feed_request(f, 2);
    assert_malformed();
}

void test_h2s_missing_path_is_malformed(void)
{
    const Hdr f[] = {{":method", "GET"}, {":scheme", "https"}};
    feed_request(f, 2);
    assert_malformed();
}

void test_h2s_empty_path_is_malformed(void)
{
    const Hdr f[] = {{":method", "GET"}, {":scheme", "https"}, {":path", ""}};
    feed_request(f, 3);
    assert_malformed();
}

// sec 8.3: a pseudo-header appears at most once.
void test_h2s_duplicate_pseudo_is_malformed(void)
{
    const Hdr f[] = {BASE3, {":path", "/other"}};
    feed_request(f, 4);
    assert_malformed();
}

// sec 8.3: pseudo-headers all precede the regular fields.
void test_h2s_pseudo_after_regular_is_malformed(void)
{
    const Hdr f[] = {{":method", "GET"}, {":scheme", "https"}, {"accept", "*/*"}, {":path", "/"}};
    feed_request(f, 4);
    assert_malformed();
}

// sec 8.3: a pseudo-header this server does not define.
void test_h2s_unknown_pseudo_is_malformed(void)
{
    const Hdr f[] = {BASE3, {":upgrade", "h2c"}};
    feed_request(f, 4);
    assert_malformed();
}

// sec 8.2.1: field names carry no uppercase.
void test_h2s_uppercase_name_is_malformed(void)
{
    const Hdr f[] = {BASE3, {"Accept", "*/*"}};
    feed_request(f, 4);
    assert_malformed();
}

// sec 8.2.1: nor a space, nor a colon anywhere but the pseudo-header marker.
void test_h2s_bad_name_bytes_are_malformed(void)
{
    const Hdr sp[] = {BASE3, {"bad name", "x"}};
    feed_request(sp, 4);
    assert_malformed();

    const Hdr cl[] = {BASE3, {"bad:name", "x"}};
    feed_request(cl, 4);
    assert_malformed();

    const Hdr empty[] = {BASE3, {"", "x"}};
    feed_request(empty, 4);
    assert_malformed();
}

// sec 8.2.1: a field value neither leads nor trails with a space or a tab.
void test_h2s_padded_value_is_malformed(void)
{
    const Hdr lead[] = {BASE3, {"accept", " */*"}};
    feed_request(lead, 4);
    assert_malformed();

    const Hdr trail[] = {BASE3, {"accept", "*/*\t"}};
    feed_request(trail, 4);
    assert_malformed();
}

// sec 8.2.2: the connection-specific fields have no meaning in HTTP/2.
void test_h2s_connection_specific_is_malformed(void)
{
    static const char *const banned[] = {"connection", "proxy-connection", "keep-alive", "transfer-encoding",
                                         "upgrade"};
    for (size_t i = 0; i < sizeof banned / sizeof banned[0]; i++)
    {
        const Hdr f[] = {BASE3, {banned[i], "x"}};
        feed_request(f, 4);
        assert_malformed();
    }
}

// sec 8.2.2: TE survives, carrying nothing but "trailers".
void test_h2s_te_trailers_only(void)
{
    const Hdr ok[] = {BASE3, {"te", "trailers"}};
    feed_request(ok, 4);
    assert_accepted();

    const Hdr bad[] = {BASE3, {"te", "gzip"}};
    feed_request(bad, 4);
    assert_malformed();
}

// The mask is per block: a rejected request must not condemn the next one on the same slot.
void test_h2s_mask_clears_between_blocks(void)
{
    const Hdr bad[] = {{":method", "GET"}, {":path", "/"}};
    feed_request(bad, 2);
    assert_malformed();

    const Hdr good[] = {BASE3};
    feed_request(good, 3);
    assert_accepted();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_h2s_probe);
    RUN_TEST(test_h2s_minimal_request_is_accepted);
    RUN_TEST(test_h2s_path_query_split);
    RUN_TEST(test_h2s_missing_method_is_malformed);
    RUN_TEST(test_h2s_missing_scheme_is_malformed);
    RUN_TEST(test_h2s_missing_path_is_malformed);
    RUN_TEST(test_h2s_empty_path_is_malformed);
    RUN_TEST(test_h2s_duplicate_pseudo_is_malformed);
    RUN_TEST(test_h2s_pseudo_after_regular_is_malformed);
    RUN_TEST(test_h2s_unknown_pseudo_is_malformed);
    RUN_TEST(test_h2s_uppercase_name_is_malformed);
    RUN_TEST(test_h2s_bad_name_bytes_are_malformed);
    RUN_TEST(test_h2s_padded_value_is_malformed);
    RUN_TEST(test_h2s_connection_specific_is_malformed);
    RUN_TEST(test_h2s_te_trailers_only);
    RUN_TEST(test_h2s_mask_clears_between_blocks);
    return UNITY_END();
}
