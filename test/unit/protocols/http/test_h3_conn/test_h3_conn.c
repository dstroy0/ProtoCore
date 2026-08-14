// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/presentation/http/http3/h3_conn.h"
#include "network_drivers/presentation/http/http3/h3_frame.h"
#include "network_drivers/presentation/http/http3/qpack.h"
#include "network_drivers/presentation/http/http3/quic_conn.h"
#include "network_drivers/presentation/http/http3/quic_frame.h"
#include "network_drivers/presentation/http/http3/quic_varint.h"
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static char g_method[16], g_path[64], g_auth[64];
static uint8_t g_body[64];
static size_t g_body_len;
static uint64_t g_sid;
static int g_requests;

static void on_request(void *, H3Conn *, uint64_t sid, const char *method, const char *path, const char *authority,
                       const uint8_t *body, size_t body_len)
{
    g_sid = sid;
    strncpy(g_method, method, sizeof(g_method) - 1);
    strncpy(g_path, path, sizeof(g_path) - 1);
    strncpy(g_auth, authority, sizeof(g_auth) - 1);
    g_body_len = body_len < sizeof(g_body) ? body_len : sizeof(g_body);
    memcpy(g_body, body, g_body_len);
    g_requests++;
}

static QuicConn g_qc;
static QuicConn g_qc2;

static void minimal_qc(struct QuicConn *qc)
{
    uint8_t *tx = qc->streams[0].tx;
    memset(qc, 0, sizeof(*qc));
    qc->streams[0].tx = tx;
    for (int i = 0; i < 3; i++)
    {
        qc->space[i].largest_acked = -1;
    }
    for (size_t i = 0; i < PROTOCORE_QUIC_MAX_STREAMS; i++)
    {
        qc->streams[i].id = UINT64_MAX;
    }
}

static void established_qc(struct QuicConn *qc)
{
    minimal_qc(qc);
    qc->tls.ap_keys_ready = PROTO_TRUE;
}

static QuicStream *find_stream(struct QuicConn *qc, uint64_t id)
{
    for (size_t i = 0; i < PROTOCORE_QUIC_MAX_STREAMS; i++)
    {
        if (qc->streams[i].id == id)
        {
            return &qc->streams[i];
        }
    }
    return NULL;
}

static H3Conn g_h3;

static H3Conn g_h3b;

static H3Stream *find_h3(H3Conn *h3, uint64_t id)
{
    for (size_t i = 0; i < PROTOCORE_H3_MAX_STREAMS; i++)
    {
        if (h3->streams[i].role != H3_ROLE_FREE && h3->streams[i].id == id)
        {
            return &h3->streams[i];
        }
    }
    return NULL;
}

static char e_status[8], e_ctype[32];
static proto_bool protocore_resp_emit(void *, const char *name, size_t nlen, const char *value, size_t vlen)
{
    if (nlen == 7 && memcmp(name, ":status", 7) == 0)
    {
        memcpy(e_status, value, vlen);
        e_status[vlen] = '\0';
    }
    else if (nlen == 12 && memcmp(name, "content-type", 12) == 0)
    {
        memcpy(e_ctype, value, vlen);
        e_ctype[vlen] = '\0';
    }
    return PROTO_TRUE;
}

void test_request_dispatch_and_response()
{
    g_requests = 0;
    minimal_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);
    TEST_ASSERT_NOT_NULL(g_qc.cb.on_stream_data);

    uint8_t block[128];
    size_t bp = protocore_qpack_encode_prefix(block, sizeof(block));
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":method", 7, "GET", 3);
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":path", 5, "/index.html", 11);
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":authority", 10, "example.org", 11);
    uint8_t req[256];
    size_t rp = protocore_h3_build_headers(req, sizeof(req), block, bp);

    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 0, req, rp, PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(1, g_requests);
    TEST_ASSERT_EQUAL_STRING("GET", g_method);
    TEST_ASSERT_EQUAL_STRING("/index.html", g_path);
    TEST_ASSERT_EQUAL_STRING("example.org", g_auth);
    TEST_ASSERT_EQUAL_UINT(0, g_body_len);

    TEST_ASSERT_TRUE(protocore_h3_conn_respond(&g_h3, 0, 200, "text/plain", (const uint8_t *)"hello", 5));
    QuicStream *st = find_stream(&g_qc, 0);
    TEST_ASSERT_NOT_NULL(st);

    size_t off = 0;
    proto_bool saw_headers = PROTO_FALSE, saw_data = PROTO_FALSE;
    char scratch[128];
    e_status[0] = e_ctype[0] = '\0';
    while (off < st->tx_have)
    {
        H3Frame fr;
        TEST_ASSERT_TRUE(protocore_h3_frame_parse(st->tx + off, st->tx_have - off, &fr));
        const uint8_t *fp = st->tx + off + fr.header_len;
        if (fr.type == H3_HEADERS)
        {
            protocore_qpack_decode(fp, (size_t)fr.length, scratch, sizeof(scratch), protocore_resp_emit, NULL);
            saw_headers = PROTO_TRUE;
        }
        else if (fr.type == H3_DATA)
        {
            TEST_ASSERT_EQUAL_UINT(5, (size_t)fr.length);
            TEST_ASSERT_EQUAL_UINT8_ARRAY("hello", fp, 5);
            saw_data = PROTO_TRUE;
        }
        off += fr.header_len + (size_t)fr.length;
    }
    TEST_ASSERT_TRUE(saw_headers);
    TEST_ASSERT_TRUE(saw_data);
    TEST_ASSERT_EQUAL_STRING("200", e_status);
    TEST_ASSERT_EQUAL_STRING("text/plain", e_ctype);
}

void test_post_with_body()
{
    g_requests = 0;
    minimal_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);

    uint8_t block[128];
    size_t bp = protocore_qpack_encode_prefix(block, sizeof(block));
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":method", 7, "POST", 4);
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":path", 5, "/submit", 7);
    uint8_t req[256];
    size_t rp = protocore_h3_build_headers(req, sizeof(req), block, bp);
    rp += protocore_h3_build_data(req + rp, sizeof(req) - rp, (const uint8_t *)"name=x", 6);

    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 4, req, rp, PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(1, g_requests);
    TEST_ASSERT_EQUAL_STRING("POST", g_method);
    TEST_ASSERT_EQUAL_STRING("/submit", g_path);
    TEST_ASSERT_EQUAL_UINT(6, g_body_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("name=x", g_body, 6);
}

void test_control_stream_settings_sent()
{
    minimal_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);

    g_qc.cb.on_handshake_done(g_qc.cb.app, &g_qc);
    QuicStream *ctrl = find_stream(&g_qc, 3);
    TEST_ASSERT_NOT_NULL(ctrl);

    uint64_t type = 0;
    size_t c = 0;
    TEST_ASSERT_TRUE(protocore_quic_varint_decode(ctrl->tx, ctrl->tx_have, &type, &c));
    TEST_ASSERT_EQUAL_UINT64(0x00, type);
    H3Frame fr;
    TEST_ASSERT_TRUE(protocore_h3_frame_parse(ctrl->tx + c, ctrl->tx_have - c, &fr));
    TEST_ASSERT_EQUAL_UINT64(H3_SETTINGS, fr.type);

    TEST_ASSERT_NOT_NULL(find_stream(&g_qc, 7));
    TEST_ASSERT_NOT_NULL(find_stream(&g_qc, 11));
}

void test_client_control_stream_settings()
{
    minimal_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);

    uint8_t s[64];
    size_t sp = protocore_quic_varint_encode(s, sizeof(s), 0x00);
    const uint64_t ids[] = {H3_SETTINGS_MAX_FIELD_SECTION_SIZE};
    const uint64_t vals[] = {12345};
    sp += protocore_h3_build_settings(s + sp, sizeof(s) - sp, ids, vals, 1);
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 2, s, sp, PROTO_FALSE);

    H3Stream *st = find_h3(&g_h3, 2);
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_CONTROL, st->role);
    TEST_ASSERT_EQUAL_UINT64(12345, g_h3.peer_settings.max_field_section_size);
}

void test_client_uni_stream_types()
{
    minimal_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);

    uint8_t t;
    size_t n = protocore_quic_varint_encode(&t, 1, 0x02);
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 6, &t, n, PROTO_FALSE);
    n = protocore_quic_varint_encode(&t, 1, 0x03);
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 10, &t, n, PROTO_FALSE);
    n = protocore_quic_varint_encode(&t, 1, 0x1f);
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 14, &t, n, PROTO_FALSE);

    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_QPACK_ENC, find_h3(&g_h3, 6)->role);
    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_QPACK_DEC, find_h3(&g_h3, 10)->role);
    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_OTHER_UNI, find_h3(&g_h3, 14)->role);
}

void test_handshake_done_idempotent()
{
    minimal_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);

    g_qc.cb.on_handshake_done(g_qc.cb.app, &g_qc);
    QuicStream *ctrl = find_stream(&g_qc, 3);
    TEST_ASSERT_NOT_NULL(ctrl);
    size_t first = ctrl->tx_have;
    g_qc.cb.on_handshake_done(g_qc.cb.app, &g_qc);
    TEST_ASSERT_EQUAL_UINT(first, ctrl->tx_have);
}

void test_malformed_request_frame()
{
    g_requests = 0;
    minimal_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);

    uint8_t hdr[8];
    size_t hp = protocore_h3_frame_write_header(hdr, sizeof(hdr), H3_HEADERS, 9999);
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 0, hdr, hp, PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(0, g_requests);

    uint8_t junk[1] = {0xC0};
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 4, junk, sizeof(junk), PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(0, g_requests);
}

void test_respond_body_too_large()
{
    minimal_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);
    static uint8_t big[PROTOCORE_H3_STREAM_BUF + 200];
    memset(big, 'x', sizeof(big));
    TEST_ASSERT_FALSE(protocore_h3_conn_respond(&g_h3, 0, 200, "text/plain", big, sizeof(big)));
}

void test_stream_pool_full()
{
    g_requests = 0;
    minimal_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);

    uint8_t b = 0x00;
    for (uint64_t i = 0; i < PROTOCORE_H3_MAX_STREAMS; i++)
    {
        g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, i * 4, &b, 1, PROTO_FALSE);
    }

    uint8_t block[64];
    size_t bp = protocore_qpack_encode_prefix(block, sizeof(block));
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":method", 7, "GET", 3);
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":path", 5, "/x", 2);
    uint8_t req[128];
    size_t rp = protocore_h3_build_headers(req, sizeof(req), block, bp);
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, (uint64_t)PROTOCORE_H3_MAX_STREAMS * 4, req, rp, PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(0, g_requests);
}

void test_uni_stream_partial_type()
{
    minimal_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);

    uint8_t b0 = 0x40;
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 2, &b0, 1, PROTO_FALSE);
    TEST_ASSERT_FALSE(find_h3(&g_h3, 2)->type_read);
    uint8_t b1 = 0x00;
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 2, &b1, 1, PROTO_FALSE);
    TEST_ASSERT_TRUE(find_h3(&g_h3, 2)->type_read);
    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_CONTROL, find_h3(&g_h3, 2)->role);
}

void test_overlong_field_truncated()
{
    g_requests = 0;
    minimal_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);

    uint8_t block[128];
    size_t bp = protocore_qpack_encode_prefix(block, sizeof(block));
    const char *m = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":method", 7, m, 26);
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":path", 5, "/", 1);
    uint8_t req[256];
    size_t rp = protocore_h3_build_headers(req, sizeof(req), block, bp);
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 0, req, rp, PROTO_TRUE);

    TEST_ASSERT_EQUAL_INT(1, g_requests);
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_H3_METHOD_LEN - 1, strlen(g_method));
}

void test_h3_pseudo_header_name_variants()
{
    g_requests = 0;
    minimal_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);

    uint8_t block[256];
    size_t bp = protocore_qpack_encode_prefix(block, sizeof(block));
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":scheme", 7, "https", 5);
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, "hello", 5, "world", 5);
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, "user-agent", 10, "curl", 4);
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, "abc", 3, "z", 1);
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":method", 7, "GET", 3);
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":path", 5, "/ok", 3);
    uint8_t req[512];
    size_t rp = protocore_h3_build_headers(req, sizeof(req), block, bp);

    strcpy(g_auth, "unset");
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 0, req, rp, PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(1, g_requests);
    TEST_ASSERT_EQUAL_STRING("GET", g_method);
    TEST_ASSERT_EQUAL_STRING("/ok", g_path);
    TEST_ASSERT_EQUAL_STRING("", g_auth);
}

void test_h3_request_unknown_frame_and_empty_data()
{
    g_requests = 0;
    minimal_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);

    uint8_t block[128];
    size_t bp = protocore_qpack_encode_prefix(block, sizeof(block));
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":method", 7, "POST", 4);
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":path", 5, "/u", 2);

    uint8_t req[512];
    size_t rp = protocore_h3_build_headers(req, sizeof(req), block, bp);
    rp += protocore_h3_build_data(req + rp, sizeof(req) - rp, NULL, 0);
    rp += protocore_h3_build_data(req + rp, sizeof(req) - rp, (const uint8_t *)"body", 4);

    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 0, req, rp, PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(1, g_requests);
    TEST_ASSERT_EQUAL_STRING("POST", g_method);
    TEST_ASSERT_EQUAL_UINT(4, g_body_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("body", g_body, 4);
}

void test_h3_control_only_frames_on_a_request_stream()
{
    static const uint64_t only_control[] = {H3_SETTINGS, H3_GOAWAY, H3_MAX_PUSH_ID, H3_CANCEL_PUSH};
    for (size_t i = 0; i < sizeof(only_control) / sizeof(only_control[0]); i++)
    {
        g_requests = 0;
        established_qc(&g_qc);
        protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);

        uint8_t block[128];
        size_t bp = protocore_qpack_encode_prefix(block, sizeof(block));
        bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":method", 7, "GET", 3);
        bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":path", 5, "/", 1);

        uint8_t req[512];
        size_t rp = protocore_h3_build_headers(req, sizeof(req), block, bp);
        rp += protocore_h3_frame_write_header(req + rp, sizeof(req) - rp, only_control[i], 0);

        g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 0, req, rp, PROTO_TRUE);
        TEST_ASSERT_EQUAL_INT(0, g_requests);
        TEST_ASSERT_TRUE(g_qc.close_queued);
        TEST_ASSERT_TRUE(g_qc.close_is_app);
        TEST_ASSERT_EQUAL_UINT64(H3_FRAME_UNEXPECTED, g_qc.close_error);
    }
}

void test_h3_error_before_app_keys_falls_back_to_transport()
{
    minimal_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);

    uint8_t req[128];
    size_t rp = protocore_h3_build_data(req, sizeof(req), (const uint8_t *)"body", 4);
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 0, req, rp, PROTO_TRUE);

    TEST_ASSERT_TRUE(g_qc.close_queued);
    TEST_ASSERT_FALSE(g_qc.close_is_app);
    TEST_ASSERT_EQUAL_UINT64(QUIC_ERR_APPLICATION, g_qc.close_error);
}

void test_h3_data_before_headers()
{
    g_requests = 0;
    established_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);

    uint8_t req[128];
    size_t rp = protocore_h3_build_data(req, sizeof(req), (const uint8_t *)"body", 4);
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 0, req, rp, PROTO_TRUE);

    TEST_ASSERT_EQUAL_INT(0, g_requests);
    TEST_ASSERT_TRUE(g_qc.close_queued);
    TEST_ASSERT_TRUE(g_qc.close_is_app);
    TEST_ASSERT_EQUAL_UINT64(H3_FRAME_UNEXPECTED, g_qc.close_error);
}

void test_h3_second_control_stream()
{
    established_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);

    uint8_t s1[64];
    size_t p1 = protocore_quic_varint_encode(s1, sizeof(s1), 0x00);
    p1 += protocore_h3_build_settings(s1 + p1, sizeof(s1) - p1, NULL, NULL, 0);
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 2, s1, p1, PROTO_FALSE);
    TEST_ASSERT_FALSE(g_qc.close_queued);

    uint8_t s2[64];
    size_t p2 = protocore_quic_varint_encode(s2, sizeof(s2), 0x00);
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 6, s2, p2, PROTO_FALSE);
    TEST_ASSERT_TRUE(g_qc.close_queued);
    TEST_ASSERT_TRUE(g_qc.close_is_app);
    TEST_ASSERT_EQUAL_UINT64(H3_STREAM_CREATION_ERROR, g_qc.close_error);
}

void test_h3_second_settings_frame()
{
    established_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);

    uint8_t s[128];
    size_t p = protocore_quic_varint_encode(s, sizeof(s), 0x00);
    p += protocore_h3_build_settings(s + p, sizeof(s) - p, NULL, NULL, 0);
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 2, s, p, PROTO_FALSE);
    TEST_ASSERT_FALSE(g_qc.close_queued);

    uint8_t s2[128];
    size_t p2 = protocore_h3_build_settings(s2, sizeof(s2), NULL, NULL, 0);
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 2, s2, p2, PROTO_FALSE);
    TEST_ASSERT_TRUE(g_qc.close_queued);
    TEST_ASSERT_TRUE(g_qc.close_is_app);
    TEST_ASSERT_EQUAL_UINT64(H3_FRAME_UNEXPECTED, g_qc.close_error);
}

void test_h3_no_request_callback()
{
    g_requests = 0;
    minimal_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, NULL, NULL);

    uint8_t block[128];
    size_t bp = protocore_qpack_encode_prefix(block, sizeof(block));
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":method", 7, "GET", 3);
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":path", 5, "/x", 2);
    uint8_t req[256];
    size_t rp = protocore_h3_build_headers(req, sizeof(req), block, bp);

    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 0, req, rp, PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(0, g_requests);
    H3Stream *st = find_h3(&g_h3, 0);
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_TRUE(st->have_headers);
    TEST_ASSERT_EQUAL_STRING("GET", st->method);
}

void test_h3_stream_buffer_overflow_clamped()
{
    minimal_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);

    static uint8_t big[PROTOCORE_H3_STREAM_BUF + 64];
    memset(big, 0x00, sizeof(big));
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 0, big, sizeof(big), PROTO_FALSE);

    H3Stream *st = find_h3(&g_h3, 0);
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_H3_STREAM_BUF, st->buf_len);
}

void test_h3_control_stream_frame_guards()
{
    minimal_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);
    H3Settings defaults;
    protocore_h3_settings_defaults(&defaults);

    uint8_t s[64];
    size_t sp = protocore_quic_varint_encode(s, sizeof(s), 0x00);
    s[sp++] = 0xC0;
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 2, s, sp, PROTO_FALSE);
    H3Stream *st = find_h3(&g_h3, 2);
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_CONTROL, st->role);
    TEST_ASSERT_EQUAL_UINT64(defaults.max_field_section_size, g_h3.peer_settings.max_field_section_size);

    minimal_qc(&g_qc2);
    protocore_h3_conn_init(&g_h3b, &g_qc2, on_request, NULL);
    uint8_t s2[64];
    size_t sp2 = protocore_quic_varint_encode(s2, sizeof(s2), 0x00);
    sp2 += protocore_h3_frame_write_header(s2 + sp2, sizeof(s2) - sp2, H3_SETTINGS, 40);
    g_qc2.cb.on_stream_data(g_qc2.cb.app, &g_qc2, 2, s2, sp2, PROTO_FALSE);
    TEST_ASSERT_EQUAL_UINT64(defaults.max_field_section_size, g_h3b.peer_settings.max_field_section_size);

    established_qc(&g_qc2);
    protocore_h3_conn_init(&g_h3b, &g_qc2, on_request, NULL);
    uint8_t s3[64];
    size_t sp3 = protocore_quic_varint_encode(s3, sizeof(s3), 0x00);
    sp3 += protocore_h3_frame_write_header(s3 + sp3, sizeof(s3) - sp3, 0x07 , 1);
    s3[sp3++] = 0x00;
    g_qc2.cb.on_stream_data(g_qc2.cb.app, &g_qc2, 2, s3, sp3, PROTO_FALSE);
    TEST_ASSERT_TRUE(g_qc2.close_queued);
    TEST_ASSERT_TRUE(g_qc2.close_is_app);
    TEST_ASSERT_EQUAL_UINT64(H3_MISSING_SETTINGS, g_qc2.close_error);

    minimal_qc(&g_qc2);
    protocore_h3_conn_init(&g_h3b, &g_qc2, on_request, NULL);
    uint8_t s4[64];
    size_t sp4 = protocore_quic_varint_encode(s4, sizeof(s4), 0x00);
    sp4 += protocore_h3_build_settings(s4 + sp4, sizeof(s4) - sp4, NULL, NULL, 0);
    sp4 += protocore_h3_frame_write_header(s4 + sp4, sizeof(s4) - sp4, 0x07 , 1);
    s4[sp4++] = 0x00;
    g_qc2.cb.on_stream_data(g_qc2.cb.app, &g_qc2, 2, s4, sp4, PROTO_FALSE);
    TEST_ASSERT_FALSE(g_qc2.close_queued);
    H3Stream *sc = find_h3(&g_h3b, 2);
    TEST_ASSERT_NOT_NULL(sc);
    TEST_ASSERT_EQUAL_UINT(0, sc->buf_len);
}

void test_h3_uni_stream_empty_and_repeat_delivery()
{
    minimal_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);

    uint8_t none = 0;
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 2, &none, 0, PROTO_FALSE);
    H3Stream *st = find_h3(&g_h3, 2);
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_FALSE(st->type_read);
    TEST_ASSERT_EQUAL_UINT(0, st->buf_len);

    uint8_t t[16];
    size_t tn = protocore_quic_varint_encode(t, sizeof(t), 0x00);
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 2, t, tn, PROTO_FALSE);
    TEST_ASSERT_TRUE(st->type_read);
    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_CONTROL, st->role);

    uint8_t s[64];
    const uint64_t ids[] = {H3_SETTINGS_MAX_FIELD_SECTION_SIZE};
    const uint64_t vals[] = {4321};
    size_t sp = protocore_h3_build_settings(s, sizeof(s), ids, vals, 1);
    g_qc.cb.on_stream_data(g_qc.cb.app, &g_qc, 2, s, sp, PROTO_FALSE);
    TEST_ASSERT_EQUAL_UINT64(4321, g_h3.peer_settings.max_field_section_size);
}

void test_h3_respond_no_content_type_empty_body()
{
    minimal_qc(&g_qc);
    protocore_h3_conn_init(&g_h3, &g_qc, on_request, NULL);

    TEST_ASSERT_TRUE(protocore_h3_conn_respond(&g_h3, 0, 204, NULL, NULL, 0));
    QuicStream *st = find_stream(&g_qc, 0);
    TEST_ASSERT_NOT_NULL(st);

    size_t off = 0;
    int frames = 0;
    char scratch[128];
    e_status[0] = e_ctype[0] = '\0';
    while (off < st->tx_have)
    {
        H3Frame fr;
        TEST_ASSERT_TRUE(protocore_h3_frame_parse(st->tx + off, st->tx_have - off, &fr));
        TEST_ASSERT_EQUAL_UINT64(H3_HEADERS, fr.type);
        protocore_qpack_decode(st->tx + off + fr.header_len, (size_t)fr.length, scratch, sizeof(scratch), protocore_resp_emit, NULL);
        off += fr.header_len + (size_t)fr.length;
        frames++;
    }
    TEST_ASSERT_EQUAL_INT(1, frames);
    TEST_ASSERT_EQUAL_STRING("204", e_status);
    TEST_ASSERT_EQUAL_STRING("", e_ctype);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_request_dispatch_and_response);
    RUN_TEST(test_h3_pseudo_header_name_variants);
    RUN_TEST(test_h3_request_unknown_frame_and_empty_data);
    RUN_TEST(test_h3_control_only_frames_on_a_request_stream);
    RUN_TEST(test_h3_error_before_app_keys_falls_back_to_transport);
    RUN_TEST(test_h3_data_before_headers);
    RUN_TEST(test_h3_second_control_stream);
    RUN_TEST(test_h3_second_settings_frame);
    RUN_TEST(test_h3_no_request_callback);
    RUN_TEST(test_h3_stream_buffer_overflow_clamped);
    RUN_TEST(test_h3_control_stream_frame_guards);
    RUN_TEST(test_h3_uni_stream_empty_and_repeat_delivery);
    RUN_TEST(test_h3_respond_no_content_type_empty_body);
    RUN_TEST(test_post_with_body);
    RUN_TEST(test_control_stream_settings_sent);
    RUN_TEST(test_client_control_stream_settings);
    RUN_TEST(test_client_uni_stream_types);
    RUN_TEST(test_handshake_done_idempotent);
    RUN_TEST(test_malformed_request_frame);
    RUN_TEST(test_respond_body_too_large);
    RUN_TEST(test_stream_pool_full);
    RUN_TEST(test_uni_stream_partial_type);
    RUN_TEST(test_overlong_field_truncated);
    return UNITY_END();
}
