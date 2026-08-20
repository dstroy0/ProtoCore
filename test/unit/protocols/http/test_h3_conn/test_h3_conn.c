// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Same reason as test_quic_conn: this suite reads the engine's stream table, so it compiles
// h3_conn.c into itself rather than widening h3_conn.h. The env's src list drops h3_conn.c.
#include "network_drivers/presentation/http/http3/h3_conn/h3_conn.c"
#include "network_drivers/presentation/http/http3/h3_frame/h3_frame.h"
#include "network_drivers/presentation/http/http3/qpack/qpack.h"
#include "network_drivers/presentation/http/http3/quic_conn/quic_conn.c"
#include "network_drivers/presentation/http/http3/quic_conn/quic_conn.h"
#include "network_drivers/presentation/http/http3/quic_frame/quic_frame.h"
#include "network_drivers/presentation/http/http3/quic_varint/quic_varint.h"
#include <string.h>

#include <unity.h>

static uint8_t qpack_work[16]; // the borrow an entry takes; Qpack never reads it

static uint8_t h3_frame_work[16]; // the borrow an entry takes; H3Frame never reads it

static uint8_t quic_varint_work[16]; // the borrow an entry takes; QuicVarint never reads it

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

static void on_request(void *, uint8_t *, uint64_t sid, const char *method, const char *path, const char *authority,
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

static uint8_t g_qc_ctx[PROTOCORE_QUIC_CONN_CTX_BORROW];
static uint8_t g_qc_b[PROTOCORE_QUIC_CONN_BORROW];
static uint8_t g_qc2_ctx[PROTOCORE_QUIC_CONN_CTX_BORROW];
static uint8_t g_qc2_b[PROTOCORE_QUIC_CONN_BORROW];
#define g_qc (*QUIC_CTX(g_qc_ctx))
#define g_qc2 (*QUIC_CTX(g_qc2_ctx))

static void minimal_qc(QuicConnCtx *qc)
{
    // The span is the connection's, not the call's; the stream pointers are re-wired from it.
    uint8_t *b = qc->b;
    memset(qc, 0, sizeof(*qc));
    qc->b = b;
    quic_conn_slot_storage(qc);
    for (int i = 0; i < 3; i++)
    {
        qc->space[i].largest_acked = -1;
    }
    for (size_t i = 0; i < PROTOCORE_QUIC_MAX_STREAMS; i++)
    {
        qc->streams[i].id = UINT64_MAX;
    }
}

static void established_qc(QuicConnCtx *qc)
{
    minimal_qc(qc);
    qc->tls.ap_keys_ready = PROTO_TRUE;
}

static QuicStream *find_stream(QuicConnCtx *qc, uint64_t id)
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

static uint8_t g_h3_b[PROTOCORE_H3_CONN_BORROW];
#define g_h3 (*H3_CTX(g_h3_b))

static uint8_t g_h3b_b[PROTOCORE_H3_CONN_BORROW];
#define g_h3b (*H3_CTX(g_h3b_b))

static H3Stream *find_h3(H3ConnCtx *h3, uint64_t id)
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
    g_qc.b = g_qc_b;
    minimal_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);
    TEST_ASSERT_NOT_NULL(g_qc.cb.on_stream_data);

    uint8_t block[128];
    Qpack.encode_prefix_args.out = block;
    Qpack.encode_prefix_args.cap = sizeof(block);
    Qpack.encode_prefix(qpack_work);
    size_t bp = Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":method";
    Qpack.encode_header_args.name_len = 7;
    Qpack.encode_header_args.value = "GET";
    Qpack.encode_header_args.value_len = 3;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":path";
    Qpack.encode_header_args.name_len = 5;
    Qpack.encode_header_args.value = "/index.html";
    Qpack.encode_header_args.value_len = 11;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":authority";
    Qpack.encode_header_args.name_len = 10;
    Qpack.encode_header_args.value = "example.org";
    Qpack.encode_header_args.value_len = 11;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    uint8_t req[256];
    H3Frame.build_headers_args.out = req;
    H3Frame.build_headers_args.cap = sizeof(req);
    H3Frame.build_headers_args.block = block;
    H3Frame.build_headers_args.len = bp;
    H3Frame.build_headers(h3_frame_work);
    size_t rp = H3Frame.n;

    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 0, req, rp, PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(1, g_requests);
    TEST_ASSERT_EQUAL_STRING("GET", g_method);
    TEST_ASSERT_EQUAL_STRING("/index.html", g_path);
    TEST_ASSERT_EQUAL_STRING("example.org", g_auth);
    TEST_ASSERT_EQUAL_UINT(0, g_body_len);

    H3Conn.respond_args.stream_id = 0;
    H3Conn.respond_args.status = 200;
    H3Conn.respond_args.content_type = "text/plain";
    H3Conn.respond_args.body = (const uint8_t *)"hello";
    H3Conn.respond_args.body_len = 5;
    H3Conn.respond(g_h3_b);
    TEST_ASSERT_TRUE(H3Conn.ok);
    QuicStream *st = find_stream(&g_qc, 0);
    TEST_ASSERT_NOT_NULL(st);

    size_t off = 0;
    proto_bool saw_headers = PROTO_FALSE, saw_data = PROTO_FALSE;
    char scratch[128];
    e_status[0] = e_ctype[0] = '\0';
    while (off < st->tx_have)
    {
        H3FrameHeader fr;
        H3Frame.parse_header_args.buf = st->tx + off;
        H3Frame.parse_header_args.len = st->tx_have - off;
        H3Frame.parse_header_args.out = &fr;
        H3Frame.parse_header(h3_frame_work);
        TEST_ASSERT_TRUE(H3Frame.ok);
        const uint8_t *fp = st->tx + off + fr.header_len;
        if (fr.type == H3_HEADERS)
        {
            Qpack.decode_args.block = fp;
            Qpack.decode_args.len = (size_t)fr.length;
            Qpack.decode_args.scratch = scratch;
            Qpack.decode_args.scratch_cap = sizeof(scratch);
            Qpack.decode_args.emit = protocore_resp_emit;
            Qpack.decode_args.ctx = NULL;
            Qpack.decode(qpack_work);
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
    g_qc.b = g_qc_b;
    minimal_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);

    uint8_t block[128];
    Qpack.encode_prefix_args.out = block;
    Qpack.encode_prefix_args.cap = sizeof(block);
    Qpack.encode_prefix(qpack_work);
    size_t bp = Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":method";
    Qpack.encode_header_args.name_len = 7;
    Qpack.encode_header_args.value = "POST";
    Qpack.encode_header_args.value_len = 4;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":path";
    Qpack.encode_header_args.name_len = 5;
    Qpack.encode_header_args.value = "/submit";
    Qpack.encode_header_args.value_len = 7;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    uint8_t req[256];
    H3Frame.build_headers_args.out = req;
    H3Frame.build_headers_args.cap = sizeof(req);
    H3Frame.build_headers_args.block = block;
    H3Frame.build_headers_args.len = bp;
    H3Frame.build_headers(h3_frame_work);
    size_t rp = H3Frame.n;
    H3Frame.build_data_args.out = req + rp;
    H3Frame.build_data_args.cap = sizeof(req) - rp;
    H3Frame.build_data_args.data = (const uint8_t *)"name=x";
    H3Frame.build_data_args.len = 6;
    H3Frame.build_data(h3_frame_work);
    rp += H3Frame.n;

    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 4, req, rp, PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(1, g_requests);
    TEST_ASSERT_EQUAL_STRING("POST", g_method);
    TEST_ASSERT_EQUAL_STRING("/submit", g_path);
    TEST_ASSERT_EQUAL_UINT(6, g_body_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("name=x", g_body, 6);
}

void test_control_stream_settings_sent()
{
    g_qc.b = g_qc_b;
    minimal_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);

    g_qc.cb.on_handshake_done(g_qc.cb.app, g_qc_ctx);
    QuicStream *ctrl = find_stream(&g_qc, 3);
    TEST_ASSERT_NOT_NULL(ctrl);

    uint64_t type = 0;
    size_t c = 0;
    QuicVarint.decode_args.in = ctrl->tx;
    QuicVarint.decode_args.len = ctrl->tx_have;
    QuicVarint.decode_args.value = &type;
    QuicVarint.decode_args.consumed = &c;
    QuicVarint.decode(quic_varint_work);
    TEST_ASSERT_TRUE(QuicVarint.ok);
    TEST_ASSERT_EQUAL_UINT64(0x00, type);
    H3FrameHeader fr;
    H3Frame.parse_header_args.buf = ctrl->tx + c;
    H3Frame.parse_header_args.len = ctrl->tx_have - c;
    H3Frame.parse_header_args.out = &fr;
    H3Frame.parse_header(h3_frame_work);
    TEST_ASSERT_TRUE(H3Frame.ok);
    TEST_ASSERT_EQUAL_UINT64(H3_SETTINGS, fr.type);

    TEST_ASSERT_NOT_NULL(find_stream(&g_qc, 7));
    TEST_ASSERT_NOT_NULL(find_stream(&g_qc, 11));
}

void test_client_control_stream_settings()
{
    g_qc.b = g_qc_b;
    minimal_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);

    uint8_t s[64];
    QuicVarint.encode_args.out = s;
    QuicVarint.encode_args.cap = sizeof(s);
    QuicVarint.encode_args.value = 0x00;
    QuicVarint.encode(quic_varint_work);
    size_t sp = QuicVarint.n;
    const uint64_t ids[] = {H3_SETTINGS_MAX_FIELD_SECTION_SIZE};
    const uint64_t vals[] = {12345};
    H3Frame.build_settings_args.out = s + sp;
    H3Frame.build_settings_args.cap = sizeof(s) - sp;
    H3Frame.build_settings_args.ids = ids;
    H3Frame.build_settings_args.vals = vals;
    H3Frame.build_settings_args.n = 1;
    H3Frame.build_settings(h3_frame_work);
    sp += H3Frame.n;
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 2, s, sp, PROTO_FALSE);

    H3Stream *st = find_h3(&g_h3, 2);
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_CONTROL, st->role);
    TEST_ASSERT_EQUAL_UINT64(12345, g_h3.peer_settings.max_field_section_size);
}

void test_client_uni_stream_types()
{
    g_qc.b = g_qc_b;
    minimal_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);

    uint8_t t;
    QuicVarint.encode_args.out = &t;
    QuicVarint.encode_args.cap = 1;
    QuicVarint.encode_args.value = 0x02;
    QuicVarint.encode(quic_varint_work);
    size_t n = QuicVarint.n;
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 6, &t, n, PROTO_FALSE);
    QuicVarint.encode_args.out = &t;
    QuicVarint.encode_args.cap = 1;
    QuicVarint.encode_args.value = 0x03;
    QuicVarint.encode(quic_varint_work);
    n = QuicVarint.n;
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 10, &t, n, PROTO_FALSE);
    QuicVarint.encode_args.out = &t;
    QuicVarint.encode_args.cap = 1;
    QuicVarint.encode_args.value = 0x1f;
    QuicVarint.encode(quic_varint_work);
    n = QuicVarint.n;
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 14, &t, n, PROTO_FALSE);

    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_QPACK_ENC, find_h3(&g_h3, 6)->role);
    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_QPACK_DEC, find_h3(&g_h3, 10)->role);
    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_OTHER_UNI, find_h3(&g_h3, 14)->role);
}

void test_handshake_done_idempotent()
{
    g_qc.b = g_qc_b;
    minimal_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);

    g_qc.cb.on_handshake_done(g_qc.cb.app, g_qc_ctx);
    QuicStream *ctrl = find_stream(&g_qc, 3);
    TEST_ASSERT_NOT_NULL(ctrl);
    size_t first = ctrl->tx_have;
    g_qc.cb.on_handshake_done(g_qc.cb.app, g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(first, ctrl->tx_have);
}

void test_malformed_request_frame()
{
    g_requests = 0;
    g_qc.b = g_qc_b;
    minimal_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);

    uint8_t hdr[8];
    H3Frame.write_header_args.out = hdr;
    H3Frame.write_header_args.cap = sizeof(hdr);
    H3Frame.write_header_args.type = H3_HEADERS;
    H3Frame.write_header_args.length = 9999;
    H3Frame.write_header(h3_frame_work);
    size_t hp = H3Frame.n;
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 0, hdr, hp, PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(0, g_requests);

    uint8_t junk[1] = {0xC0};
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 4, junk, sizeof(junk), PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(0, g_requests);
}

void test_respond_body_too_large()
{
    g_qc.b = g_qc_b;
    minimal_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);
    static uint8_t big[PROTOCORE_H3_STREAM_BUF + 200];
    memset(big, 'x', sizeof(big));
    H3Conn.respond_args.stream_id = 0;
    H3Conn.respond_args.status = 200;
    H3Conn.respond_args.content_type = "text/plain";
    H3Conn.respond_args.body = big;
    H3Conn.respond_args.body_len = sizeof(big);
    H3Conn.respond(g_h3_b);
    TEST_ASSERT_FALSE(H3Conn.ok);
}

void test_stream_pool_full()
{
    g_requests = 0;
    g_qc.b = g_qc_b;
    minimal_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);

    uint8_t b = 0x00;
    for (uint64_t i = 0; i < PROTOCORE_H3_MAX_STREAMS; i++)
    {
        g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, i * 4, &b, 1, PROTO_FALSE);
    }

    uint8_t block[64];
    Qpack.encode_prefix_args.out = block;
    Qpack.encode_prefix_args.cap = sizeof(block);
    Qpack.encode_prefix(qpack_work);
    size_t bp = Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":method";
    Qpack.encode_header_args.name_len = 7;
    Qpack.encode_header_args.value = "GET";
    Qpack.encode_header_args.value_len = 3;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":path";
    Qpack.encode_header_args.name_len = 5;
    Qpack.encode_header_args.value = "/x";
    Qpack.encode_header_args.value_len = 2;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    uint8_t req[128];
    H3Frame.build_headers_args.out = req;
    H3Frame.build_headers_args.cap = sizeof(req);
    H3Frame.build_headers_args.block = block;
    H3Frame.build_headers_args.len = bp;
    H3Frame.build_headers(h3_frame_work);
    size_t rp = H3Frame.n;
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, (uint64_t)PROTOCORE_H3_MAX_STREAMS * 4, req, rp, PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(0, g_requests);
}

void test_uni_stream_partial_type()
{
    g_qc.b = g_qc_b;
    minimal_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);

    uint8_t b0 = 0x40;
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 2, &b0, 1, PROTO_FALSE);
    TEST_ASSERT_FALSE(find_h3(&g_h3, 2)->type_read);
    uint8_t b1 = 0x00;
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 2, &b1, 1, PROTO_FALSE);
    TEST_ASSERT_TRUE(find_h3(&g_h3, 2)->type_read);
    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_CONTROL, find_h3(&g_h3, 2)->role);
}

void test_overlong_field_truncated()
{
    g_requests = 0;
    g_qc.b = g_qc_b;
    minimal_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);

    uint8_t block[128];
    Qpack.encode_prefix_args.out = block;
    Qpack.encode_prefix_args.cap = sizeof(block);
    Qpack.encode_prefix(qpack_work);
    size_t bp = Qpack.n;
    const char *m = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":method";
    Qpack.encode_header_args.name_len = 7;
    Qpack.encode_header_args.value = m;
    Qpack.encode_header_args.value_len = 26;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":path";
    Qpack.encode_header_args.name_len = 5;
    Qpack.encode_header_args.value = "/";
    Qpack.encode_header_args.value_len = 1;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    uint8_t req[256];
    H3Frame.build_headers_args.out = req;
    H3Frame.build_headers_args.cap = sizeof(req);
    H3Frame.build_headers_args.block = block;
    H3Frame.build_headers_args.len = bp;
    H3Frame.build_headers(h3_frame_work);
    size_t rp = H3Frame.n;
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 0, req, rp, PROTO_TRUE);

    TEST_ASSERT_EQUAL_INT(1, g_requests);
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_H3_METHOD_LEN - 1, strlen(g_method));
}

void test_h3_pseudo_header_name_variants()
{
    g_requests = 0;
    g_qc.b = g_qc_b;
    minimal_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);

    uint8_t block[256];
    Qpack.encode_prefix_args.out = block;
    Qpack.encode_prefix_args.cap = sizeof(block);
    Qpack.encode_prefix(qpack_work);
    size_t bp = Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":scheme";
    Qpack.encode_header_args.name_len = 7;
    Qpack.encode_header_args.value = "https";
    Qpack.encode_header_args.value_len = 5;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = "hello";
    Qpack.encode_header_args.name_len = 5;
    Qpack.encode_header_args.value = "world";
    Qpack.encode_header_args.value_len = 5;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = "user-agent";
    Qpack.encode_header_args.name_len = 10;
    Qpack.encode_header_args.value = "curl";
    Qpack.encode_header_args.value_len = 4;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = "abc";
    Qpack.encode_header_args.name_len = 3;
    Qpack.encode_header_args.value = "z";
    Qpack.encode_header_args.value_len = 1;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":method";
    Qpack.encode_header_args.name_len = 7;
    Qpack.encode_header_args.value = "GET";
    Qpack.encode_header_args.value_len = 3;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":path";
    Qpack.encode_header_args.name_len = 5;
    Qpack.encode_header_args.value = "/ok";
    Qpack.encode_header_args.value_len = 3;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    uint8_t req[512];
    H3Frame.build_headers_args.out = req;
    H3Frame.build_headers_args.cap = sizeof(req);
    H3Frame.build_headers_args.block = block;
    H3Frame.build_headers_args.len = bp;
    H3Frame.build_headers(h3_frame_work);
    size_t rp = H3Frame.n;

    strcpy(g_auth, "unset");
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 0, req, rp, PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(1, g_requests);
    TEST_ASSERT_EQUAL_STRING("GET", g_method);
    TEST_ASSERT_EQUAL_STRING("/ok", g_path);
    TEST_ASSERT_EQUAL_STRING("", g_auth);
}

void test_h3_request_unknown_frame_and_empty_data()
{
    g_requests = 0;
    g_qc.b = g_qc_b;
    minimal_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);

    uint8_t block[128];
    Qpack.encode_prefix_args.out = block;
    Qpack.encode_prefix_args.cap = sizeof(block);
    Qpack.encode_prefix(qpack_work);
    size_t bp = Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":method";
    Qpack.encode_header_args.name_len = 7;
    Qpack.encode_header_args.value = "POST";
    Qpack.encode_header_args.value_len = 4;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":path";
    Qpack.encode_header_args.name_len = 5;
    Qpack.encode_header_args.value = "/u";
    Qpack.encode_header_args.value_len = 2;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;

    uint8_t req[512];
    H3Frame.build_headers_args.out = req;
    H3Frame.build_headers_args.cap = sizeof(req);
    H3Frame.build_headers_args.block = block;
    H3Frame.build_headers_args.len = bp;
    H3Frame.build_headers(h3_frame_work);
    size_t rp = H3Frame.n;
    H3Frame.build_data_args.out = req + rp;
    H3Frame.build_data_args.cap = sizeof(req) - rp;
    H3Frame.build_data_args.data = NULL;
    H3Frame.build_data_args.len = 0;
    H3Frame.build_data(h3_frame_work);
    rp += H3Frame.n;
    H3Frame.build_data_args.out = req + rp;
    H3Frame.build_data_args.cap = sizeof(req) - rp;
    H3Frame.build_data_args.data = (const uint8_t *)"body";
    H3Frame.build_data_args.len = 4;
    H3Frame.build_data(h3_frame_work);
    rp += H3Frame.n;

    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 0, req, rp, PROTO_TRUE);
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
        g_qc.b = g_qc_b;
        established_qc(&g_qc);
        H3Conn.bind.qc = g_qc_ctx;
        H3Conn.app_args.on_request = on_request;
        H3Conn.app_args.app = NULL;
        H3Conn.init(g_h3_b);

        uint8_t block[128];
        Qpack.encode_prefix_args.out = block;
        Qpack.encode_prefix_args.cap = sizeof(block);
        Qpack.encode_prefix(qpack_work);
        size_t bp = Qpack.n;
        Qpack.encode_header_args.out = block + bp;
        Qpack.encode_header_args.cap = sizeof(block) - bp;
        Qpack.encode_header_args.name = ":method";
        Qpack.encode_header_args.name_len = 7;
        Qpack.encode_header_args.value = "GET";
        Qpack.encode_header_args.value_len = 3;
        Qpack.encode_header(qpack_work);
        bp += Qpack.n;
        Qpack.encode_header_args.out = block + bp;
        Qpack.encode_header_args.cap = sizeof(block) - bp;
        Qpack.encode_header_args.name = ":path";
        Qpack.encode_header_args.name_len = 5;
        Qpack.encode_header_args.value = "/";
        Qpack.encode_header_args.value_len = 1;
        Qpack.encode_header(qpack_work);
        bp += Qpack.n;

        uint8_t req[512];
        H3Frame.build_headers_args.out = req;
        H3Frame.build_headers_args.cap = sizeof(req);
        H3Frame.build_headers_args.block = block;
        H3Frame.build_headers_args.len = bp;
        H3Frame.build_headers(h3_frame_work);
        size_t rp = H3Frame.n;
        H3Frame.write_header_args.out = req + rp;
        H3Frame.write_header_args.cap = sizeof(req) - rp;
        H3Frame.write_header_args.type = only_control[i];
        H3Frame.write_header_args.length = 0;
        H3Frame.write_header(h3_frame_work);
        rp += H3Frame.n;

        g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 0, req, rp, PROTO_TRUE);
        TEST_ASSERT_EQUAL_INT(0, g_requests);
        TEST_ASSERT_TRUE(g_qc.close_queued);
        TEST_ASSERT_TRUE(g_qc.close_is_app);
        TEST_ASSERT_EQUAL_UINT64(H3_FRAME_UNEXPECTED, g_qc.close_error);
    }
}

void test_h3_error_before_app_keys_falls_back_to_transport()
{
    g_qc.b = g_qc_b;
    minimal_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);

    uint8_t req[128];
    H3Frame.build_data_args.out = req;
    H3Frame.build_data_args.cap = sizeof(req);
    H3Frame.build_data_args.data = (const uint8_t *)"body";
    H3Frame.build_data_args.len = 4;
    H3Frame.build_data(h3_frame_work);
    size_t rp = H3Frame.n;
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 0, req, rp, PROTO_TRUE);

    TEST_ASSERT_TRUE(g_qc.close_queued);
    TEST_ASSERT_FALSE(g_qc.close_is_app);
    TEST_ASSERT_EQUAL_UINT64(QUIC_ERR_APPLICATION, g_qc.close_error);
}

void test_h3_data_before_headers()
{
    g_requests = 0;
    g_qc.b = g_qc_b;
    established_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);

    uint8_t req[128];
    H3Frame.build_data_args.out = req;
    H3Frame.build_data_args.cap = sizeof(req);
    H3Frame.build_data_args.data = (const uint8_t *)"body";
    H3Frame.build_data_args.len = 4;
    H3Frame.build_data(h3_frame_work);
    size_t rp = H3Frame.n;
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 0, req, rp, PROTO_TRUE);

    TEST_ASSERT_EQUAL_INT(0, g_requests);
    TEST_ASSERT_TRUE(g_qc.close_queued);
    TEST_ASSERT_TRUE(g_qc.close_is_app);
    TEST_ASSERT_EQUAL_UINT64(H3_FRAME_UNEXPECTED, g_qc.close_error);
}

void test_h3_second_control_stream()
{
    g_qc.b = g_qc_b;
    established_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);

    uint8_t s1[64];
    QuicVarint.encode_args.out = s1;
    QuicVarint.encode_args.cap = sizeof(s1);
    QuicVarint.encode_args.value = 0x00;
    QuicVarint.encode(quic_varint_work);
    size_t p1 = QuicVarint.n;
    H3Frame.build_settings_args.out = s1 + p1;
    H3Frame.build_settings_args.cap = sizeof(s1) - p1;
    H3Frame.build_settings_args.ids = NULL;
    H3Frame.build_settings_args.vals = NULL;
    H3Frame.build_settings_args.n = 0;
    H3Frame.build_settings(h3_frame_work);
    p1 += H3Frame.n;
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 2, s1, p1, PROTO_FALSE);
    TEST_ASSERT_FALSE(g_qc.close_queued);

    uint8_t s2[64];
    QuicVarint.encode_args.out = s2;
    QuicVarint.encode_args.cap = sizeof(s2);
    QuicVarint.encode_args.value = 0x00;
    QuicVarint.encode(quic_varint_work);
    size_t p2 = QuicVarint.n;
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 6, s2, p2, PROTO_FALSE);
    TEST_ASSERT_TRUE(g_qc.close_queued);
    TEST_ASSERT_TRUE(g_qc.close_is_app);
    TEST_ASSERT_EQUAL_UINT64(H3_STREAM_CREATION_ERROR, g_qc.close_error);
}

void test_h3_second_settings_frame()
{
    g_qc.b = g_qc_b;
    established_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);

    uint8_t s[128];
    QuicVarint.encode_args.out = s;
    QuicVarint.encode_args.cap = sizeof(s);
    QuicVarint.encode_args.value = 0x00;
    QuicVarint.encode(quic_varint_work);
    size_t p = QuicVarint.n;
    H3Frame.build_settings_args.out = s + p;
    H3Frame.build_settings_args.cap = sizeof(s) - p;
    H3Frame.build_settings_args.ids = NULL;
    H3Frame.build_settings_args.vals = NULL;
    H3Frame.build_settings_args.n = 0;
    H3Frame.build_settings(h3_frame_work);
    p += H3Frame.n;
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 2, s, p, PROTO_FALSE);
    TEST_ASSERT_FALSE(g_qc.close_queued);

    uint8_t s2[128];
    H3Frame.build_settings_args.out = s2;
    H3Frame.build_settings_args.cap = sizeof(s2);
    H3Frame.build_settings_args.ids = NULL;
    H3Frame.build_settings_args.vals = NULL;
    H3Frame.build_settings_args.n = 0;
    H3Frame.build_settings(h3_frame_work);
    size_t p2 = H3Frame.n;
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 2, s2, p2, PROTO_FALSE);
    TEST_ASSERT_TRUE(g_qc.close_queued);
    TEST_ASSERT_TRUE(g_qc.close_is_app);
    TEST_ASSERT_EQUAL_UINT64(H3_FRAME_UNEXPECTED, g_qc.close_error);
}

void test_h3_no_request_callback()
{
    g_requests = 0;
    g_qc.b = g_qc_b;
    minimal_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = NULL;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);

    uint8_t block[128];
    Qpack.encode_prefix_args.out = block;
    Qpack.encode_prefix_args.cap = sizeof(block);
    Qpack.encode_prefix(qpack_work);
    size_t bp = Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":method";
    Qpack.encode_header_args.name_len = 7;
    Qpack.encode_header_args.value = "GET";
    Qpack.encode_header_args.value_len = 3;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":path";
    Qpack.encode_header_args.name_len = 5;
    Qpack.encode_header_args.value = "/x";
    Qpack.encode_header_args.value_len = 2;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    uint8_t req[256];
    H3Frame.build_headers_args.out = req;
    H3Frame.build_headers_args.cap = sizeof(req);
    H3Frame.build_headers_args.block = block;
    H3Frame.build_headers_args.len = bp;
    H3Frame.build_headers(h3_frame_work);
    size_t rp = H3Frame.n;

    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 0, req, rp, PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(0, g_requests);
    H3Stream *st = find_h3(&g_h3, 0);
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_TRUE(st->have_headers);
    TEST_ASSERT_EQUAL_STRING("GET", st->method);
}

void test_h3_stream_buffer_overflow_clamped()
{
    g_qc.b = g_qc_b;
    minimal_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);

    static uint8_t big[PROTOCORE_H3_STREAM_BUF + 64];
    memset(big, 0x00, sizeof(big));
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 0, big, sizeof(big), PROTO_FALSE);

    H3Stream *st = find_h3(&g_h3, 0);
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_H3_STREAM_BUF, st->buf_len);
}

void test_h3_control_stream_frame_guards()
{
    g_qc.b = g_qc_b;
    minimal_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);
    H3Settings defaults;
    H3Frame.settings_defaults_args.s = &defaults;
    H3Frame.settings_defaults(h3_frame_work);

    uint8_t s[64];
    QuicVarint.encode_args.out = s;
    QuicVarint.encode_args.cap = sizeof(s);
    QuicVarint.encode_args.value = 0x00;
    QuicVarint.encode(quic_varint_work);
    size_t sp = QuicVarint.n;
    s[sp++] = 0xC0;
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 2, s, sp, PROTO_FALSE);
    H3Stream *st = find_h3(&g_h3, 2);
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_CONTROL, st->role);
    TEST_ASSERT_EQUAL_UINT64(defaults.max_field_section_size, g_h3.peer_settings.max_field_section_size);

    g_qc2.b = g_qc2_b;
    minimal_qc(&g_qc2);
    H3Conn.bind.qc = g_qc2_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3b_b);
    uint8_t s2[64];
    QuicVarint.encode_args.out = s2;
    QuicVarint.encode_args.cap = sizeof(s2);
    QuicVarint.encode_args.value = 0x00;
    QuicVarint.encode(quic_varint_work);
    size_t sp2 = QuicVarint.n;
    H3Frame.write_header_args.out = s2 + sp2;
    H3Frame.write_header_args.cap = sizeof(s2) - sp2;
    H3Frame.write_header_args.type = H3_SETTINGS;
    H3Frame.write_header_args.length = 40;
    H3Frame.write_header(h3_frame_work);
    sp2 += H3Frame.n;
    g_qc2.cb.on_stream_data(g_qc2.cb.app, g_qc2_ctx, 2, s2, sp2, PROTO_FALSE);
    TEST_ASSERT_EQUAL_UINT64(defaults.max_field_section_size, g_h3b.peer_settings.max_field_section_size);

    g_qc2.b = g_qc2_b;
    established_qc(&g_qc2);
    H3Conn.bind.qc = g_qc2_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3b_b);
    uint8_t s3[64];
    QuicVarint.encode_args.out = s3;
    QuicVarint.encode_args.cap = sizeof(s3);
    QuicVarint.encode_args.value = 0x00;
    QuicVarint.encode(quic_varint_work);
    size_t sp3 = QuicVarint.n;
    H3Frame.write_header_args.out = s3 + sp3;
    H3Frame.write_header_args.cap = sizeof(s3) - sp3;
    H3Frame.write_header_args.type = 0x07;
    H3Frame.write_header_args.length = 1;
    H3Frame.write_header(h3_frame_work);
    sp3 += H3Frame.n;
    s3[sp3++] = 0x00;
    g_qc2.cb.on_stream_data(g_qc2.cb.app, g_qc2_ctx, 2, s3, sp3, PROTO_FALSE);
    TEST_ASSERT_TRUE(g_qc2.close_queued);
    TEST_ASSERT_TRUE(g_qc2.close_is_app);
    TEST_ASSERT_EQUAL_UINT64(H3_MISSING_SETTINGS, g_qc2.close_error);

    g_qc2.b = g_qc2_b;
    minimal_qc(&g_qc2);
    H3Conn.bind.qc = g_qc2_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3b_b);
    uint8_t s4[64];
    QuicVarint.encode_args.out = s4;
    QuicVarint.encode_args.cap = sizeof(s4);
    QuicVarint.encode_args.value = 0x00;
    QuicVarint.encode(quic_varint_work);
    size_t sp4 = QuicVarint.n;
    H3Frame.build_settings_args.out = s4 + sp4;
    H3Frame.build_settings_args.cap = sizeof(s4) - sp4;
    H3Frame.build_settings_args.ids = NULL;
    H3Frame.build_settings_args.vals = NULL;
    H3Frame.build_settings_args.n = 0;
    H3Frame.build_settings(h3_frame_work);
    sp4 += H3Frame.n;
    H3Frame.write_header_args.out = s4 + sp4;
    H3Frame.write_header_args.cap = sizeof(s4) - sp4;
    H3Frame.write_header_args.type = 0x07;
    H3Frame.write_header_args.length = 1;
    H3Frame.write_header(h3_frame_work);
    sp4 += H3Frame.n;
    s4[sp4++] = 0x00;
    g_qc2.cb.on_stream_data(g_qc2.cb.app, g_qc2_ctx, 2, s4, sp4, PROTO_FALSE);
    TEST_ASSERT_FALSE(g_qc2.close_queued);
    H3Stream *sc = find_h3(&g_h3b, 2);
    TEST_ASSERT_NOT_NULL(sc);
    TEST_ASSERT_EQUAL_UINT(0, sc->buf_len);
}

void test_h3_uni_stream_empty_and_repeat_delivery()
{
    g_qc.b = g_qc_b;
    minimal_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);

    uint8_t none = 0;
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 2, &none, 0, PROTO_FALSE);
    H3Stream *st = find_h3(&g_h3, 2);
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_FALSE(st->type_read);
    TEST_ASSERT_EQUAL_UINT(0, st->buf_len);

    uint8_t t[16];
    QuicVarint.encode_args.out = t;
    QuicVarint.encode_args.cap = sizeof(t);
    QuicVarint.encode_args.value = 0x00;
    QuicVarint.encode(quic_varint_work);
    size_t tn = QuicVarint.n;
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 2, t, tn, PROTO_FALSE);
    TEST_ASSERT_TRUE(st->type_read);
    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_CONTROL, st->role);

    uint8_t s[64];
    const uint64_t ids[] = {H3_SETTINGS_MAX_FIELD_SECTION_SIZE};
    const uint64_t vals[] = {4321};
    H3Frame.build_settings_args.out = s;
    H3Frame.build_settings_args.cap = sizeof(s);
    H3Frame.build_settings_args.ids = ids;
    H3Frame.build_settings_args.vals = vals;
    H3Frame.build_settings_args.n = 1;
    H3Frame.build_settings(h3_frame_work);
    size_t sp = H3Frame.n;
    g_qc.cb.on_stream_data(g_qc.cb.app, g_qc_ctx, 2, s, sp, PROTO_FALSE);
    TEST_ASSERT_EQUAL_UINT64(4321, g_h3.peer_settings.max_field_section_size);
}

void test_h3_respond_no_content_type_empty_body()
{
    g_qc.b = g_qc_b;
    minimal_qc(&g_qc);
    H3Conn.bind.qc = g_qc_ctx;
    H3Conn.app_args.on_request = on_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(g_h3_b);

    H3Conn.respond_args.stream_id = 0;
    H3Conn.respond_args.status = 204;
    H3Conn.respond_args.content_type = NULL;
    H3Conn.respond_args.body = NULL;
    H3Conn.respond_args.body_len = 0;
    H3Conn.respond(g_h3_b);
    TEST_ASSERT_TRUE(H3Conn.ok);
    QuicStream *st = find_stream(&g_qc, 0);
    TEST_ASSERT_NOT_NULL(st);

    size_t off = 0;
    int frames = 0;
    char scratch[128];
    e_status[0] = e_ctype[0] = '\0';
    while (off < st->tx_have)
    {
        H3FrameHeader fr;
        H3Frame.parse_header_args.buf = st->tx + off;
        H3Frame.parse_header_args.len = st->tx_have - off;
        H3Frame.parse_header_args.out = &fr;
        H3Frame.parse_header(h3_frame_work);
        TEST_ASSERT_TRUE(H3Frame.ok);
        TEST_ASSERT_EQUAL_UINT64(H3_HEADERS, fr.type);
        Qpack.decode_args.block = st->tx + off + fr.header_len;
        Qpack.decode_args.len = (size_t)fr.length;
        Qpack.decode_args.scratch = scratch;
        Qpack.decode_args.scratch_cap = sizeof(scratch);
        Qpack.decode_args.emit = protocore_resp_emit;
        Qpack.decode_args.ctx = NULL;
        Qpack.decode(qpack_work);
        off += fr.header_len + (size_t)fr.length;
        frames++;
    }
    TEST_ASSERT_EQUAL_INT(1, frames);
    TEST_ASSERT_EQUAL_STRING("204", e_status);
    TEST_ASSERT_EQUAL_STRING("", e_ctype);
}
