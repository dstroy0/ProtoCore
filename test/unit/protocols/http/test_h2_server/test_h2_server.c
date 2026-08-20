// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/presentation/http/http2/h2_server/h2_server.h"
#include "network_drivers/session/session.h" // the per-connection tables this suite stands in for
#include "network_drivers/transport/tcp/common.h"

#include "network_drivers/presentation/http/http2/h2_conn/h2_conn.h"
#include "network_drivers/presentation/http/http2/h2_frame/h2_frame.h"
#include "network_drivers/presentation/http/http2/hpack/hpack.h"
#include "network_drivers/presentation/http/http_parser/http_parser.h"
#include "network_drivers/transport/tcp/protocol/protocol.h"

#include <stdio.h>
#include <string.h>
#include <unity.h>

TcpConn conn_pool[CONN_POOL_SLOTS];
uint32_t protocore_ap_ip;

// The session layer owns a connection's state and this suite stands in for it, the same way it
// stands in for the transport's conn_pool above. Declared in network_drivers/session/session.h.
uint32_t http_req_start_ms[CONN_POOL_SLOTS];
protocore_resp_sink_fn http_resp_sink[CONN_POOL_SLOTS];
uint8_t http_h2[CONN_POOL_SLOTS];
uint8_t http_h2_checked[CONN_POOL_SLOTS];
uint32_t http_h2_stream[CONN_POOL_SLOTS];

#define WIRE_MAX 8192

static uint8_t g_in[WIRE_MAX];
static size_t g_in_len;
static size_t g_in_off;
static uint8_t g_out[WIRE_MAX];
static size_t g_out_len;

int protocore_tls_read(uint8_t slot, uint8_t *buf, size_t len)
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

int protocore_tls_write(uint8_t slot, const void *data, size_t len)
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

static int out_count(uint8_t type, uint32_t *last_err)
{
    int n = 0;
    size_t off = 0;
    while (off + H2_FRAME_HEADER_LEN <= g_out_len)
    {
        H2FrameHeader h;
        (H2Frame.parse_args.buf = g_out + off, H2Frame.parse_args.len = H2_FRAME_HEADER_LEN, H2Frame.parse_header(NULL),
         *(&h) = H2Frame.header, H2Frame.ok);
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

typedef struct
{
    const char *name;
    const char *value;
} Hdr;

static void feed_request(const Hdr *fields, size_t n)
{
    wire_reset();
    memset(&conn_pool[0], 0, sizeof conn_pool[0]);
    printf("DIAG span=%p\n", (void *)protocore_h2_server_span());
    H2Server.slot = 0;
    H2Server.open(protocore_h2_server_span());
    printf("DIAG after open: g_out_len=%u g_in_len=%u\n", (unsigned)g_out_len, (unsigned)g_in_len);
    g_out_len = 0;

    uint8_t block[1024];
    size_t bo = 0;
    for (size_t i = 0; i < n; i++)
    {
        bo += (Hpack.encode_args.out = block + bo, Hpack.encode_args.cap = sizeof block - bo,
               Hpack.encode_args.name = fields[i].name, Hpack.encode_args.name_len = strlen(fields[i].name),
               Hpack.encode_args.value = fields[i].value, Hpack.encode_args.value_len = strlen(fields[i].value),
               Hpack.encode_header(NULL), Hpack.n);
    }

    uint8_t sf[9];
    in_add(H2_PREFACE, H2_PREFACE_LEN);
    in_add(sf, (H2Frame.build_settings_args.buf = sf, H2Frame.build_settings_args.cap = sizeof sf,
                H2Frame.build_settings_args.ids = NULL, H2Frame.build_settings_args.vals = NULL,
                H2Frame.build_settings_args.n = 0, H2Frame.build_settings(NULL), H2Frame.n));

    uint8_t hf[H2_FRAME_HEADER_LEN + sizeof block];
    in_add(hf, (H2Frame.headers_args.buf = hf, H2Frame.headers_args.cap = sizeof hf, H2Frame.headers_args.stream_id = 1,
                H2Frame.headers_args.block = block, H2Frame.headers_args.block_len = bo,
                H2Frame.headers_args.end_stream = PROTO_TRUE, H2Frame.build_headers(NULL), H2Frame.n));

    H2Server.slot = 0;
    H2Server.data(protocore_h2_server_span());
}

#define BASE3 {":method", "GET"}, {":scheme", "https"}, {":path", "/"}

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

void test_h2s_minimal_request_is_accepted(void)
{
    const Hdr f[] = {BASE3, {":authority", "example.com"}, {"accept", "*/*"}};
    feed_request(f, 5);
    assert_accepted();
    TEST_ASSERT_EQUAL_STRING("GET", http_pool[0].method);
    TEST_ASSERT_EQUAL_STRING("/", http_pool[0].path);
    // RFC 9113 sec 5.1.1: a client MUST use odd-numbered stream identifiers and 0 cannot establish a
    // stream, so the first request on a fresh connection arrives on stream 1.
    TEST_ASSERT_EQUAL_UINT32(1, http_h2_stream[0]);
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

void test_h2s_duplicate_pseudo_is_malformed(void)
{
    const Hdr f[] = {BASE3, {":path", "/other"}};
    feed_request(f, 4);
    assert_malformed();
}

void test_h2s_pseudo_after_regular_is_malformed(void)
{
    const Hdr f[] = {{":method", "GET"}, {":scheme", "https"}, {"accept", "*/*"}, {":path", "/"}};
    feed_request(f, 4);
    assert_malformed();
}

void test_h2s_unknown_pseudo_is_malformed(void)
{
    const Hdr f[] = {BASE3, {":upgrade", "h2c"}};
    feed_request(f, 4);
    assert_malformed();
}

void test_h2s_uppercase_name_is_malformed(void)
{
    const Hdr f[] = {BASE3, {"Accept", "*/*"}};
    feed_request(f, 4);
    assert_malformed();
}

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

void test_h2s_padded_value_is_malformed(void)
{
    const Hdr lead[] = {BASE3, {"accept", " */*"}};
    feed_request(lead, 4);
    assert_malformed();

    const Hdr trail[] = {BASE3, {"accept", "*/*\t"}};
    feed_request(trail, 4);
    assert_malformed();
}

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

void test_h2s_te_trailers_only(void)
{
    const Hdr ok[] = {BASE3, {"te", "trailers"}};
    feed_request(ok, 4);
    assert_accepted();

    const Hdr bad[] = {BASE3, {"te", "gzip"}};
    feed_request(bad, 4);
    assert_malformed();
}

void test_h2s_mask_clears_between_blocks(void)
{
    const Hdr bad[] = {{":method", "GET"}, {":path", "/"}};
    feed_request(bad, 2);
    assert_malformed();

    const Hdr good[] = {BASE3};
    feed_request(good, 3);
    assert_accepted();
}

