// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/presentation/http/http2/h2_conn.h"
#include "network_drivers/presentation/http/http2/h2_frame.h"
#include "network_drivers/presentation/http/http2/hpack.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

#define HDR_MAX 64
#define HDR_LEN 96
#define SID_MAX 16
#define OUT_MAX 8192
#define BODY_MAX 1024
#define IN_MAX (H2_FRAME_HEADER_LEN + PROTOCORE_H2_HDR_BLOCK + 64)

typedef struct
{
    char name[HDR_LEN];
    size_t name_len;
    char value[HDR_LEN];
    size_t value_len;
} Field;

typedef struct
{
    Field f[HDR_MAX];
    size_t n;
} FieldList;

static void field_add(FieldList *l, const char *n, size_t nl, const char *v, size_t vl)
{
    if (l->n >= HDR_MAX)
    {
        return;
    }
    size_t nc = nl < HDR_LEN ? nl : HDR_LEN - 1;
    size_t vc = vl < HDR_LEN ? vl : HDR_LEN - 1;
    memcpy(l->f[l->n].name, n, nc);
    l->f[l->n].name[nc] = 0;
    l->f[l->n].name_len = nl;
    memcpy(l->f[l->n].value, v, vc);
    l->f[l->n].value[vc] = 0;
    l->f[l->n].value_len = vl;
    l->n++;
}

typedef struct
{
    uint8_t out[OUT_MAX];
    size_t out_len;
    FieldList req_headers;
    uint32_t headers_end[SID_MAX];
    size_t headers_end_n;
    proto_bool last_end_stream;
    proto_bool hend_malformed;
    char body[BODY_MAX];
    size_t body_len;
    proto_bool data_end;
} Cap;

static void cap_write(void *io, const uint8_t *d, size_t n)
{
    Cap *c = (Cap *)io;
    if (c->out_len + n <= OUT_MAX)
    {
        memcpy(c->out + c->out_len, d, n);
        c->out_len += n;
    }
}
static void cap_hdr(void *app, uint32_t sid, const char *n, size_t nl, const char *v, size_t vl)
{
    (void)sid;
    field_add(&((Cap *)app)->req_headers, n, nl, v, vl);
}
static proto_bool cap_hend(void *app, uint32_t sid, proto_bool es)
{
    Cap *c = (Cap *)app;
    if (c->headers_end_n < SID_MAX)
    {
        c->headers_end[c->headers_end_n++] = sid;
    }
    c->last_end_stream = es;
    return !c->hend_malformed;
}
static void cap_data(void *app, uint32_t sid, const uint8_t *d, size_t n, proto_bool es)
{
    (void)sid;
    Cap *c = (Cap *)app;
    if (c->body_len + n < BODY_MAX)
    {
        memcpy(c->body + c->body_len, d, n);
        c->body_len += n;
        c->body[c->body_len] = 0;
    }
    c->data_end = es;
}

static H2Conn g_conn;

static H2Callbacks mk_cb(Cap *c)
{
    H2Callbacks cb;
    memset(&cb, 0, sizeof cb);
    cb.write = cap_write;
    cb.on_header = cap_hdr;
    cb.on_headers_end = cap_hend;
    cb.on_data = cap_data;
    cb.io = c;
    cb.app = c;
    return cb;
}

static int count_frames(const uint8_t *b, size_t bn, uint8_t type, int *ack_out)
{
    int n = 0, ack = 0;
    size_t i = 0;
    while (i + 9 <= bn)
    {
        H2FrameHeader h;
        protocore_h2_parse_header(&b[i], 9, &h);
        if (h.type == type)
        {
            n++;
            if (h.flags & H2_FLAG_ACK)
            {
                ack++;
            }
        }
        i += 9 + h.length;
    }
    if (ack_out)
    {
        *ack_out = ack;
    }
    return n;
}

static size_t build_request(uint8_t *block, size_t cap)
{
    size_t bo = 0;
    bo += protocore_hpack_encode_header(block + bo, cap - bo, ":method", 7, "GET", 3);
    bo += protocore_hpack_encode_header(block + bo, cap - bo, ":scheme", 7, "https", 5);
    bo += protocore_hpack_encode_header(block + bo, cap - bo, ":path", 5, "/", 1);
    bo += protocore_hpack_encode_header(block + bo, cap - bo, ":authority", 10, "example.com", 11);
    return bo;
}

static uint8_t g_in[IN_MAX];
static size_t g_in_len;
static void in_reset(void)
{
    g_in_len = 0;
}
static void in_add(const void *p, size_t n)
{
    TEST_ASSERT_TRUE(g_in_len + n <= IN_MAX);
    memcpy(g_in + g_in_len, p, n);
    g_in_len += n;
}

static void in_preface(void)
{
    uint8_t sf[9];
    in_reset();
    in_add(H2_PREFACE, H2_PREFACE_LEN);
    in_add(sf, protocore_h2_build_settings(sf, sizeof sf, NULL, NULL, 0));
}

void test_init_and_request(void)
{
    static Cap cap;
    memset(&cap, 0, sizeof cap);
    H2Callbacks cb = mk_cb(&cap);
    protocore_h2_conn_init(&g_conn, &cb);
    int acks = 0;
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_SETTINGS, &acks));
    TEST_ASSERT_EQUAL_INT(0, acks);

    in_preface();
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    uint8_t hf[160];
    in_add(hf, protocore_h2_build_headers(hf, sizeof hf, 1, block, blen, PROTO_TRUE));

    cap.out_len = 0;
    TEST_ASSERT_TRUE(protocore_h2_conn_recv(&g_conn, g_in, g_in_len));

    TEST_ASSERT_EQUAL_INT(4, (int)cap.req_headers.n);
    TEST_ASSERT_EQUAL_STRING(":method", cap.req_headers.f[0].name);
    TEST_ASSERT_EQUAL_STRING("GET", cap.req_headers.f[0].value);
    TEST_ASSERT_EQUAL_STRING(":path", cap.req_headers.f[2].name);
    TEST_ASSERT_EQUAL_STRING("/", cap.req_headers.f[2].value);
    TEST_ASSERT_EQUAL_STRING(":authority", cap.req_headers.f[3].name);
    TEST_ASSERT_EQUAL_STRING("example.com", cap.req_headers.f[3].value);
    TEST_ASSERT_EQUAL_INT(1, (int)cap.headers_end_n);
    TEST_ASSERT_EQUAL_UINT32(1, cap.headers_end[0]);
    TEST_ASSERT_TRUE(cap.last_end_stream);

    int acks2 = 0;
    count_frames(cap.out, cap.out_len, H2_SETTINGS, &acks2);
    TEST_ASSERT_EQUAL_INT(1, acks2);
}

static proto_bool rh_emit(void *ctx, const char *n, size_t nl, const char *v, size_t vl)
{
    field_add((FieldList *)ctx, n, nl, v, vl);
    return PROTO_TRUE;
}

void test_respond_roundtrip(void)
{
    static Cap cap;
    memset(&cap, 0, sizeof cap);
    H2Callbacks cb = mk_cb(&cap);
    protocore_h2_conn_init(&g_conn, &cb);
    in_preface();
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    uint8_t hf[160];
    in_add(hf, protocore_h2_build_headers(hf, sizeof hf, 1, block, blen, PROTO_TRUE));
    TEST_ASSERT_TRUE(protocore_h2_conn_recv(&g_conn, g_in, g_in_len));

    cap.out_len = 0;
    TEST_ASSERT_TRUE(protocore_h2_conn_respond(&g_conn, 1, 200, "text/plain", "hi", 2));

    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_HEADERS, NULL));
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_DATA, NULL));

    HpackDynTable dt;
    protocore_hpack_dyn_init(&dt, 4096);
    static FieldList rh;
    memset(&rh, 0, sizeof rh);
    char data[BODY_MAX];
    size_t data_len = 0;
    data[0] = 0;
    proto_bool data_end = PROTO_FALSE;
    size_t i = 0;
    while (i + 9 <= cap.out_len)
    {
        H2FrameHeader h;
        protocore_h2_parse_header(&cap.out[i], 9, &h);
        const uint8_t *pl = &cap.out[i + 9];
        if (h.type == H2_HEADERS && h.stream_id == 1)
        {
            char scratch[256];
            protocore_hpack_decode(&dt, pl, h.length, scratch, sizeof scratch, rh_emit, &rh);
        }
        else if (h.type == H2_DATA && h.stream_id == 1)
        {
            TEST_ASSERT_TRUE(data_len + h.length < BODY_MAX);
            memcpy(data + data_len, pl, h.length);
            data_len += h.length;
            data[data_len] = 0;
            data_end = (h.flags & H2_FLAG_END_STREAM) != 0;
        }
        i += 9 + h.length;
    }
    TEST_ASSERT_EQUAL_INT(3, (int)rh.n);
    TEST_ASSERT_EQUAL_STRING(":status", rh.f[0].name);
    TEST_ASSERT_EQUAL_STRING("200", rh.f[0].value);
    TEST_ASSERT_EQUAL_STRING("content-type", rh.f[1].name);
    TEST_ASSERT_EQUAL_STRING("text/plain", rh.f[1].value);
    TEST_ASSERT_EQUAL_STRING("content-length", rh.f[2].name);
    TEST_ASSERT_EQUAL_STRING("2", rh.f[2].value);
    TEST_ASSERT_EQUAL_STRING("hi", data);
    TEST_ASSERT_TRUE(data_end);
}

void test_ping_and_split_recv(void)
{
    static Cap cap;
    memset(&cap, 0, sizeof cap);
    H2Callbacks cb = mk_cb(&cap);
    protocore_h2_conn_init(&g_conn, &cb);

    in_reset();
    in_add(H2_PREFACE, H2_PREFACE_LEN);
    const uint8_t op[8] = {9, 8, 7, 6, 5, 4, 3, 2};
    uint8_t ping[9 + 8];
    protocore_h2_write_header(ping, sizeof ping, 8, H2_PING, 0, 0);
    memcpy(ping + 9, op, 8);
    in_add(ping, sizeof ping);

    cap.out_len = 0;
    for (size_t k = 0; k < g_in_len; k++)
    {
        TEST_ASSERT_TRUE(protocore_h2_conn_recv(&g_conn, &g_in[k], 1));
    }

    int acks = 0;
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_PING, &acks));
    TEST_ASSERT_EQUAL_INT(1, acks);

    size_t i = 0;
    proto_bool found = PROTO_FALSE;
    while (i + 9 <= cap.out_len)
    {
        H2FrameHeader h;
        protocore_h2_parse_header(&cap.out[i], 9, &h);
        if (h.type == H2_PING && (h.flags & H2_FLAG_ACK))
        {
            TEST_ASSERT_EQUAL_MEMORY(op, &cap.out[i + 9], 8);
            found = PROTO_TRUE;
        }
        i += 9 + h.length;
    }
    TEST_ASSERT_TRUE(found);
}

void test_bad_preface(void)
{
    static Cap cap;
    memset(&cap, 0, sizeof cap);
    H2Callbacks cb = mk_cb(&cap);
    protocore_h2_conn_init(&g_conn, &cb);
    const uint8_t junk[] = {'G', 'E', 'T', ' ', '/', ' ', 'H'};
    TEST_ASSERT_FALSE(protocore_h2_conn_recv(&g_conn, junk, sizeof junk));
}

static void establish(H2Conn *c, Cap *cap)
{
    memset(cap, 0, sizeof *cap);
    H2Callbacks cb = mk_cb(cap);
    protocore_h2_conn_init(c, &cb);
    in_preface();
    TEST_ASSERT_TRUE(protocore_h2_conn_recv(c, g_in, g_in_len));
    cap->out_len = 0;
}

static uint8_t g_frame[IN_MAX];
static proto_bool feed_frame(H2Conn *c, uint8_t type, uint8_t flags, uint32_t sid, const uint8_t *pl, size_t pn)
{
    TEST_ASSERT_TRUE(9 + pn <= IN_MAX);
    protocore_h2_write_header(g_frame, 9, (uint32_t)pn, type, flags, sid);
    if (pn)
    {
        memcpy(g_frame + 9, pl, pn);
    }
    return protocore_h2_conn_recv(c, g_frame, 9 + pn);
}

static void open_stream(H2Conn *c, uint32_t id)
{
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    uint8_t hf[160];
    TEST_ASSERT_TRUE(
        protocore_h2_conn_recv(c, hf, protocore_h2_build_headers(hf, sizeof hf, id, block, blen, PROTO_FALSE)));
}

static uint8_t g_pl[IN_MAX];

void test_h2_headers_padded_priority(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    size_t n = 0;
    g_pl[n++] = 3;
    for (int i = 0; i < 5; i++)
    {
        g_pl[n++] = 0;
    }
    memcpy(g_pl + n, block, blen);
    n += blen;
    for (int i = 0; i < 3; i++)
    {
        g_pl[n++] = 0;
    }
    uint8_t flags = H2_FLAG_PADDED | H2_FLAG_PRIORITY | H2_FLAG_END_HEADERS | H2_FLAG_END_STREAM;
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_HEADERS, flags, 1, g_pl, n));
    TEST_ASSERT_EQUAL_INT(4, (int)cap.req_headers.n);
    TEST_ASSERT_TRUE(cap.last_end_stream);
}

void test_h2_headers_pad_overflow(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    uint8_t pl[4] = {200, 1, 2, 3};
    TEST_ASSERT_FALSE(feed_frame(&g_conn, H2_HEADERS, H2_FLAG_PADDED | H2_FLAG_END_HEADERS, 1, pl, sizeof pl));
}

void test_h2_stream_id_must_increase(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    uint8_t hf[160];
    TEST_ASSERT_TRUE(
        protocore_h2_conn_recv(&g_conn, hf, protocore_h2_build_headers(hf, sizeof hf, 3, block, blen, PROTO_TRUE)));
    TEST_ASSERT_FALSE(
        protocore_h2_conn_recv(&g_conn, hf, protocore_h2_build_headers(hf, sizeof hf, 1, block, blen, PROTO_TRUE)));
}

void test_h2_headers_rfc7541_c31_block(void)
{
    static Cap cap;
    establish(&g_conn, &cap);

    const uint8_t c31[20] = {0x82, 0x86, 0x84, 0x41, 0x0f, 0x77, 0x77, 0x77, 0x2e, 0x65,
                             0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d};
    uint8_t hf[64];
    TEST_ASSERT_TRUE(
        protocore_h2_conn_recv(&g_conn, hf, protocore_h2_build_headers(hf, sizeof hf, 1, c31, sizeof c31, PROTO_TRUE)));

    TEST_ASSERT_EQUAL_INT(4, (int)cap.req_headers.n);
    TEST_ASSERT_EQUAL_STRING(":method", cap.req_headers.f[0].name);
    TEST_ASSERT_EQUAL_STRING("GET", cap.req_headers.f[0].value);
    TEST_ASSERT_EQUAL_STRING(":scheme", cap.req_headers.f[1].name);
    TEST_ASSERT_EQUAL_STRING("http", cap.req_headers.f[1].value);
    TEST_ASSERT_EQUAL_STRING(":path", cap.req_headers.f[2].name);
    TEST_ASSERT_EQUAL_STRING("/", cap.req_headers.f[2].value);
    TEST_ASSERT_EQUAL_STRING(":authority", cap.req_headers.f[3].name);
    TEST_ASSERT_EQUAL_STRING("www.example.com", cap.req_headers.f[3].value);
    TEST_ASSERT_EQUAL_UINT32(57, g_conn.hdec.used);
}

void test_h2_trailers_on_open_stream(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    open_stream(&g_conn, 1);
    size_t headers_after_request = cap.req_headers.n;
    size_t ends_after_request = cap.headers_end_n;

    uint8_t block[128];
    size_t blen = protocore_hpack_encode_header(block, sizeof block, "x-checksum", 10, "abcd", 4);
    uint8_t hf[160];
    TEST_ASSERT_TRUE(
        protocore_h2_conn_recv(&g_conn, hf, protocore_h2_build_headers(hf, sizeof hf, 1, block, blen, PROTO_TRUE)));

    TEST_ASSERT_EQUAL_INT(0, count_frames(cap.out, cap.out_len, H2_RST_STREAM, NULL));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)headers_after_request, (uint32_t)cap.req_headers.n);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)ends_after_request, (uint32_t)cap.headers_end_n);
}

void test_h2_trailers_without_end_stream_reset_the_stream(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    open_stream(&g_conn, 1);

    uint8_t block[128];
    size_t blen = protocore_hpack_encode_header(block, sizeof block, "x-checksum", 10, "abcd", 4);
    uint8_t hf[160];
    TEST_ASSERT_TRUE(
        protocore_h2_conn_recv(&g_conn, hf, protocore_h2_build_headers(hf, sizeof hf, 1, block, blen, PROTO_FALSE)));
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_RST_STREAM, NULL));
}

void test_h2_trailers_reject_pseudo_headers(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    open_stream(&g_conn, 1);

    uint8_t block[128];
    size_t blen = protocore_hpack_encode_header(block, sizeof block, ":method", 7, "POST", 4);
    uint8_t hf[160];
    TEST_ASSERT_TRUE(
        protocore_h2_conn_recv(&g_conn, hf, protocore_h2_build_headers(hf, sizeof hf, 1, block, blen, PROTO_TRUE)));
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_RST_STREAM, NULL));
}

void test_h2_headers_on_ended_stream_is_a_connection_error(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    uint8_t hf[160];
    TEST_ASSERT_TRUE(
        protocore_h2_conn_recv(&g_conn, hf, protocore_h2_build_headers(hf, sizeof hf, 1, block, blen, PROTO_TRUE)));
    TEST_ASSERT_FALSE(
        protocore_h2_conn_recv(&g_conn, hf, protocore_h2_build_headers(hf, sizeof hf, 1, block, blen, PROTO_TRUE)));
}

void test_h2_headers_bad_stream_id(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    uint8_t hf[160];
    TEST_ASSERT_FALSE(
        protocore_h2_conn_recv(&g_conn, hf, protocore_h2_build_headers(hf, sizeof hf, 2, block, blen, PROTO_TRUE)));
}

void test_h2_stream_table_full_rst(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    for (int i = 0; i < PROTOCORE_H2_MAX_STREAMS; i++)
    {
        uint8_t hf[160];
        size_t hn = protocore_h2_build_headers(hf, sizeof hf, (uint32_t)(1 + 2 * i), block, blen, PROTO_FALSE);
        TEST_ASSERT_TRUE(protocore_h2_conn_recv(&g_conn, hf, hn));
    }
    cap.out_len = 0;
    uint8_t hf[160];
    size_t hn = protocore_h2_build_headers(hf, sizeof hf, (uint32_t)(1 + 2 * PROTOCORE_H2_MAX_STREAMS), block, blen,
                                           PROTO_FALSE);
    TEST_ASSERT_TRUE(protocore_h2_conn_recv(&g_conn, hf, hn));
    TEST_ASSERT_TRUE(count_frames(cap.out, cap.out_len, H2_RST_STREAM, NULL) >= 1);
}

void test_h2_continuation(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    size_t half = blen / 2;
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_HEADERS, 0, 1, block, half));
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_CONTINUATION, H2_FLAG_END_HEADERS, 1, block + half, blen - half));
    TEST_ASSERT_EQUAL_INT(4, (int)cap.req_headers.n);
}

void test_h2_continuation_guards(void)
{
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    {
        static Cap cap;
        establish(&g_conn, &cap);
        TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_HEADERS, 0, 1, block, blen / 2));
        uint8_t x[4] = {0};
        TEST_ASSERT_FALSE(feed_frame(&g_conn, H2_CONTINUATION, H2_FLAG_END_HEADERS, 3, x, 4));
    }
    {
        static Cap cap;
        establish(&g_conn, &cap);
        TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_HEADERS, 0, 1, block, blen / 2));
        uint8_t d[1] = {0};
        TEST_ASSERT_FALSE(feed_frame(&g_conn, H2_DATA, 0, 1, d, 1));
    }
}

void test_h2_data(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    open_stream(&g_conn, 1);
    cap.out_len = 0;
    const uint8_t body[5] = {'h', 'e', 'l', 'l', 'o'};
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_DATA, H2_FLAG_END_STREAM, 1, body, 5));
    TEST_ASSERT_EQUAL_STRING("hello", cap.body);
    TEST_ASSERT_TRUE(cap.data_end);
    TEST_ASSERT_EQUAL_INT(2, count_frames(cap.out, cap.out_len, H2_WINDOW_UPDATE, NULL));

    open_stream(&g_conn, 3);
    const uint8_t padded[5] = {2, 'x', 'y', 0, 0};
    cap.body_len = 0;
    cap.body[0] = 0;
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_DATA, H2_FLAG_PADDED, 3, padded, sizeof padded));
    TEST_ASSERT_EQUAL_STRING("xy", cap.body);

    static Cap cap2;
    H2Conn c2;
    establish(&c2, &cap2);
    const uint8_t d[1] = {0};
    TEST_ASSERT_FALSE(feed_frame(&c2, H2_DATA, 0, 0, d, 1));
    uint8_t bad[2] = {5, 1};
    TEST_ASSERT_FALSE(feed_frame(&c2, H2_DATA, H2_FLAG_PADDED, 1, bad, 2));
}

void test_h2_window_update(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    open_stream(&g_conn, 1);
    const uint8_t inc[4] = {0, 0, 0, 100};
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_WINDOW_UPDATE, 0, 0, inc, 4));
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_WINDOW_UPDATE, 0, 1, inc, 4));
    const uint8_t bad[3] = {0, 0, 1};
    TEST_ASSERT_FALSE(feed_frame(&g_conn, H2_WINDOW_UPDATE, 0, 0, bad, 3));
}

void test_h2_rst_priority_push(void)
{
    {
        static Cap cap;
        establish(&g_conn, &cap);
        open_stream(&g_conn, 1);
        const uint8_t err[4] = {0, 0, 0, 8};
        TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_RST_STREAM, 0, 1, err, 4));
        const uint8_t prio[5] = {0, 0, 0, 0, 0};
        TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_PRIORITY, 0, 3, prio, 5));
    }
    {
        static Cap cap;
        establish(&g_conn, &cap);
        const uint8_t pp[4] = {0, 0, 0, 0};
        TEST_ASSERT_FALSE(feed_frame(&g_conn, H2_PUSH_PROMISE, H2_FLAG_END_HEADERS, 1, pp, 4));
    }
}

void test_h2_goaway_then_ignore(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    const uint8_t ga[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_GOAWAY, 0, 0, ga, 8));
    const uint8_t junk[9] = {0};
    TEST_ASSERT_TRUE(protocore_h2_conn_recv(&g_conn, junk, sizeof junk));
}

void test_h2_settings_ack_and_bad(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_SETTINGS, H2_FLAG_ACK, 0, NULL, 0));
    const uint8_t bad[3] = {0, 0, 0};
    TEST_ASSERT_FALSE(feed_frame(&g_conn, H2_SETTINGS, 0, 0, bad, 3));
}

void test_h2_ping_bad(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    const uint8_t p8[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_PING, H2_FLAG_ACK, 0, p8, 8));
    const uint8_t p4[4] = {0, 0, 0, 0};
    TEST_ASSERT_FALSE(feed_frame(&g_conn, H2_PING, 0, 0, p4, 4));
}

void test_h2_frame_too_big(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    uint8_t hh[9];
    protocore_h2_write_header(hh, sizeof hh, PROTOCORE_H2_MAX_FRAME + 1, H2_DATA, 0, 1);
    TEST_ASSERT_FALSE(protocore_h2_conn_recv(&g_conn, hh, 9));
}

void test_h2_respond_paths_and_goaway(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    TEST_ASSERT_FALSE(protocore_h2_conn_respond(&g_conn, 99, 200, "text/plain", "x", 1));

    open_stream(&g_conn, 1);
    g_conn.peer.max_frame_size = 4;
    cap.out_len = 0;
    TEST_ASSERT_TRUE(protocore_h2_conn_respond(&g_conn, 1, 200, NULL, "0123456789", 10));
    TEST_ASSERT_TRUE(count_frames(cap.out, cap.out_len, H2_DATA, NULL) >= 3);

    cap.out_len = 0;
    protocore_h2_conn_goaway(&g_conn, 0);
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_GOAWAY, NULL));
}

static proto_bool fresh_feed(uint8_t type, uint8_t flags, uint32_t sid, const uint8_t *pl, size_t pn)
{
    static Cap cap;
    establish(&g_conn, &cap);
    return feed_frame(&g_conn, type, flags, sid, pl, pn);
}

void test_h2_more_guards(void)
{
    TEST_ASSERT_FALSE(fresh_feed(H2_HEADERS, H2_FLAG_PADDED | H2_FLAG_END_HEADERS, 1, NULL, 0));
    uint8_t p3[3] = {0, 0, 0};
    TEST_ASSERT_FALSE(fresh_feed(H2_HEADERS, H2_FLAG_PRIORITY | H2_FLAG_END_HEADERS, 1, p3, 3));
    uint8_t bad_hpack[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_FALSE(fresh_feed(H2_HEADERS, H2_FLAG_END_HEADERS, 1, bad_hpack, 4));
    static uint8_t huge[PROTOCORE_H2_HDR_BLOCK + 16];
    memset(huge, 0, sizeof huge);
    TEST_ASSERT_FALSE(fresh_feed(H2_HEADERS, 0, 1, huge, sizeof huge));
    TEST_ASSERT_FALSE(fresh_feed(H2_DATA, H2_FLAG_PADDED, 1, NULL, 0));
    uint8_t dpad[2] = {5, 1};
    TEST_ASSERT_FALSE(fresh_feed(H2_DATA, H2_FLAG_PADDED, 1, dpad, 2));
    uint8_t x[1] = {0};
    TEST_ASSERT_TRUE(fresh_feed(0x2A, 0, 1, x, 1));
}

void test_h2_continuation_more(void)
{
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    {
        static Cap cap;
        establish(&g_conn, &cap);
        size_t t = blen / 3;
        TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_HEADERS, 0, 1, block, t));
        TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_CONTINUATION, 0, 1, block + t, t));
        TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_CONTINUATION, H2_FLAG_END_HEADERS, 1, block + 2 * t, blen - 2 * t));
        TEST_ASSERT_EQUAL_INT(4, (int)cap.req_headers.n);
    }
    {
        static Cap cap;
        establish(&g_conn, &cap);
        static uint8_t frag[PROTOCORE_H2_HDR_BLOCK - 8];
        memset(frag, 0, sizeof frag);
        TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_HEADERS, 0, 1, frag, sizeof frag));
        uint8_t more[64];
        memset(more, 0, sizeof more);
        TEST_ASSERT_FALSE(feed_frame(&g_conn, H2_CONTINUATION, 0, 1, more, sizeof more));
    }
}

void test_h2_respond_content_type_too_big(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    open_stream(&g_conn, 1);
    char big_ct[1001];
    memset(big_ct, 'a', 1000);
    big_ct[1000] = 0;
    TEST_ASSERT_FALSE(protocore_h2_conn_respond(&g_conn, 1, 200, big_ct, "x", 1));
}

void test_h2_null_callbacks(void)
{
    H2Callbacks cb;
    memset(&cb, 0, sizeof cb);
    protocore_h2_conn_init(&g_conn, &cb);

    in_preface();
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    uint8_t hf[160];
    in_add(hf, protocore_h2_build_headers(hf, sizeof hf, 1, block, blen, PROTO_FALSE));
    TEST_ASSERT_TRUE(protocore_h2_conn_recv(&g_conn, g_in, g_in_len));

    TEST_ASSERT_EQUAL_UINT32(1, g_conn.last_peer_stream);

    const uint8_t body[3] = {'a', 'b', 'c'};
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_DATA, H2_FLAG_END_STREAM, 1, body, 3));
}

void test_h2_headers_stream_zero(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    uint8_t hf[160];
    TEST_ASSERT_FALSE(
        protocore_h2_conn_recv(&g_conn, hf, protocore_h2_build_headers(hf, sizeof hf, 0, block, blen, PROTO_TRUE)));
}

void test_h2_continuation_without_headers(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    uint8_t x[4] = {0};
    TEST_ASSERT_FALSE(feed_frame(&g_conn, H2_CONTINUATION, H2_FLAG_END_HEADERS, 1, x, 4));
}

void test_h2_idle_stream_frames_are_connection_errors(void)
{
    const uint8_t err[4] = {0, 0, 0, 8};
    const uint8_t inc[4] = {0, 0, 0, 100};

    TEST_ASSERT_FALSE(fresh_feed(H2_RST_STREAM, 0, 0, err, 4));
    TEST_ASSERT_FALSE(fresh_feed(H2_RST_STREAM, 0, 7, err, 4));
    TEST_ASSERT_FALSE(fresh_feed(H2_WINDOW_UPDATE, 0, 9, inc, 4));
}

void test_h2_window_update_on_a_closed_stream_is_ignored(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    open_stream(&g_conn, 1);
    const uint8_t err[4] = {0, 0, 0, 8};
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_RST_STREAM, 0, 1, err, 4));

    int32_t before = g_conn.conn_send_window;
    const uint8_t inc[4] = {0, 0, 0, 100};
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_WINDOW_UPDATE, 0, 1, inc, 4));
    TEST_ASSERT_EQUAL_INT32(before, g_conn.conn_send_window);
}

void test_h2_frame_size_and_stream_id_rules(void)
{
    static Cap cap;
    const uint8_t pad[16] = {0};

    establish(&g_conn, &cap);
    open_stream(&g_conn, 1);
    TEST_ASSERT_FALSE(feed_frame(&g_conn, H2_RST_STREAM, 0, 1, pad, 3));

    TEST_ASSERT_FALSE(fresh_feed(H2_PRIORITY, 0, 0, pad, 5));
    establish(&g_conn, &cap);
    open_stream(&g_conn, 1);
    cap.out_len = 0;
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_PRIORITY, 0, 1, pad, 4));
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_RST_STREAM, NULL));
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_PRIORITY, 0, 1, pad, 5));

    TEST_ASSERT_FALSE(fresh_feed(H2_GOAWAY, 0, 0, pad, 7));
    TEST_ASSERT_FALSE(fresh_feed(H2_GOAWAY, 0, 1, pad, 8));
    TEST_ASSERT_TRUE(fresh_feed(H2_GOAWAY, 0, 0, pad, 8));

    TEST_ASSERT_FALSE(fresh_feed(H2_SETTINGS, 0, 1, pad, 0));
    TEST_ASSERT_FALSE(fresh_feed(H2_PING, 0, 1, pad, 8));
}

static void open_stream_with_content_length(H2Conn *c, uint32_t id, const char *cl)
{
    uint8_t block[192];
    size_t bo = build_request(block, sizeof block);
    bo += protocore_hpack_encode_header(block + bo, sizeof block - bo, "content-length", 14, cl, strlen(cl));
    uint8_t hf[224];
    TEST_ASSERT_TRUE(
        protocore_h2_conn_recv(c, hf, protocore_h2_build_headers(hf, sizeof hf, id, block, bo, PROTO_FALSE)));
}

void test_h2_content_length_must_match_the_data(void)
{
    static Cap cap;

    establish(&g_conn, &cap);
    open_stream_with_content_length(&g_conn, 1, "5");
    cap.out_len = 0;
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_DATA, H2_FLAG_END_STREAM, 1, (const uint8_t *)"hello", 5));
    TEST_ASSERT_EQUAL_INT(0, count_frames(cap.out, cap.out_len, H2_RST_STREAM, NULL));
    TEST_ASSERT_EQUAL_STRING("hello", cap.body);

    establish(&g_conn, &cap);
    open_stream_with_content_length(&g_conn, 1, "10");
    cap.out_len = 0;
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_DATA, H2_FLAG_END_STREAM, 1, (const uint8_t *)"hello", 5));
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_RST_STREAM, NULL));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)cap.body_len);

    establish(&g_conn, &cap);
    open_stream_with_content_length(&g_conn, 1, "2");
    cap.out_len = 0;
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_DATA, 0, 1, (const uint8_t *)"hello", 5));
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_RST_STREAM, NULL));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)cap.body_len);

    establish(&g_conn, &cap);
    cap.out_len = 0;
    uint8_t block[192];
    size_t bo = build_request(block, sizeof block);
    bo += protocore_hpack_encode_header(block + bo, sizeof block - bo, "content-length", 14, "4", 1);
    uint8_t hf[224];
    TEST_ASSERT_TRUE(
        protocore_h2_conn_recv(&g_conn, hf, protocore_h2_build_headers(hf, sizeof hf, 1, block, bo, PROTO_TRUE)));
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_RST_STREAM, NULL));

    establish(&g_conn, &cap);
    open_stream_with_content_length(&g_conn, 1, "5, 5");
    cap.out_len = 0;
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_DATA, H2_FLAG_END_STREAM, 1, (const uint8_t *)"hello", 5));
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_RST_STREAM, NULL));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)cap.body_len);
}

void test_h2_continuation_flood_is_bounded(void)
{
    static Cap cap;
    establish(&g_conn, &cap);

    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    uint8_t hf[160];
    size_t hn = protocore_h2_build_headers(hf, sizeof hf, 1, block, blen, PROTO_FALSE);
    hf[4] &= (uint8_t)~H2_FLAG_END_HEADERS;
    TEST_ASSERT_TRUE(protocore_h2_conn_recv(&g_conn, hf, hn));

    proto_bool refused = PROTO_FALSE;
    for (int i = 0; i < PROTOCORE_H2_MAX_CONTINUATION + 4; i++)
    {
        if (!feed_frame(&g_conn, H2_CONTINUATION, 0, 1, NULL, 0))
        {
            refused = PROTO_TRUE;
            break;
        }
    }
    TEST_ASSERT_TRUE(refused);
}

void test_h2_data_empty_and_unknown_stream(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    open_stream(&g_conn, 1);
    cap.out_len = 0;
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_DATA, 0, 1, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, count_frames(cap.out, cap.out_len, H2_WINDOW_UPDATE, NULL));
    TEST_ASSERT_EQUAL_STRING("", cap.body);

    const uint8_t d[2] = {'o', 'k'};
    TEST_ASSERT_FALSE(feed_frame(&g_conn, H2_DATA, H2_FLAG_END_STREAM, 5, d, 2));
    TEST_ASSERT_EQUAL_STRING("", cap.body);
    TEST_ASSERT_FALSE(cap.data_end);
}

void test_h2_window_update_zero_and_overflow(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    open_stream(&g_conn, 1);

    cap.out_len = 0;
    const uint8_t zero[4] = {0, 0, 0, 0};
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_WINDOW_UPDATE, 0, 1, zero, 4));
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_RST_STREAM, NULL));

    TEST_ASSERT_FALSE(feed_frame(&g_conn, H2_WINDOW_UPDATE, 0, 0, zero, 4));

    establish(&g_conn, &cap);
    open_stream(&g_conn, 1);
    cap.out_len = 0;
    const uint8_t big[4] = {0x7F, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_WINDOW_UPDATE, 0, 1, big, 4));
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_RST_STREAM, NULL));

    establish(&g_conn, &cap);
    const int32_t before = g_conn.conn_send_window;
    TEST_ASSERT_FALSE(feed_frame(&g_conn, H2_WINDOW_UPDATE, 0, 0, big, 4));
    TEST_ASSERT_EQUAL_INT32(before, g_conn.conn_send_window);
}

void test_h2_data_after_end_stream_resets_the_stream(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    open_stream(&g_conn, 1);

    const uint8_t first[2] = {'h', 'i'};
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_DATA, H2_FLAG_END_STREAM, 1, first, 2));
    TEST_ASSERT_EQUAL_STRING("hi", cap.body);

    cap.out_len = 0;
    cap.body[0] = '\0';
    const uint8_t late[3] = {'b', 'a', 'd'};
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_DATA, 0, 1, late, 3));
    TEST_ASSERT_EQUAL_STRING("", cap.body);
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_RST_STREAM, NULL));
}

void test_h2_continuation_after_stream_freed(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    size_t half = blen / 2;
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_HEADERS, 0, 1, block, half));
    TEST_ASSERT_TRUE(protocore_h2_conn_respond(&g_conn, 1, 200, NULL, "x", 1));
    TEST_ASSERT_TRUE(feed_frame(&g_conn, H2_CONTINUATION, H2_FLAG_END_HEADERS, 1, block + half, blen - half));
    TEST_ASSERT_EQUAL_INT(4, (int)cap.req_headers.n);
}

void test_h2_respond_default_chunk_size(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    open_stream(&g_conn, 1);
    g_conn.peer.max_frame_size = 0;
    cap.out_len = 0;
    static char body[1000];
    memset(body, 'z', sizeof body);
    TEST_ASSERT_TRUE(protocore_h2_conn_respond(&g_conn, 1, 200, NULL, body, sizeof body));
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_DATA, NULL));
}

void test_h2_respond_content_length_no_room(void)
{
    static Cap cap;
    establish(&g_conn, &cap);
    open_stream(&g_conn, 1);
    cap.out_len = 0;

    char ct[251];
    memset(ct, '&', 250);
    ct[250] = 0;
    TEST_ASSERT_FALSE(protocore_h2_conn_respond(&g_conn, 1, 200, ct, "hi", 2));
    TEST_ASSERT_EQUAL_INT(0, count_frames(cap.out, cap.out_len, H2_HEADERS, NULL));

    TEST_ASSERT_TRUE(protocore_h2_conn_respond(&g_conn, 1, 200, "text/plain", "hi", 2));
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_HEADERS, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_and_request);
    RUN_TEST(test_respond_roundtrip);
    RUN_TEST(test_ping_and_split_recv);
    RUN_TEST(test_bad_preface);
    RUN_TEST(test_h2_headers_padded_priority);
    RUN_TEST(test_h2_headers_pad_overflow);
    RUN_TEST(test_h2_stream_id_must_increase);
    RUN_TEST(test_h2_headers_rfc7541_c31_block);
    RUN_TEST(test_h2_trailers_on_open_stream);
    RUN_TEST(test_h2_trailers_without_end_stream_reset_the_stream);
    RUN_TEST(test_h2_trailers_reject_pseudo_headers);
    RUN_TEST(test_h2_headers_on_ended_stream_is_a_connection_error);
    RUN_TEST(test_h2_headers_bad_stream_id);
    RUN_TEST(test_h2_stream_table_full_rst);
    RUN_TEST(test_h2_continuation);
    RUN_TEST(test_h2_continuation_guards);
    RUN_TEST(test_h2_data);
    RUN_TEST(test_h2_window_update);
    RUN_TEST(test_h2_rst_priority_push);
    RUN_TEST(test_h2_goaway_then_ignore);
    RUN_TEST(test_h2_settings_ack_and_bad);
    RUN_TEST(test_h2_ping_bad);
    RUN_TEST(test_h2_frame_too_big);
    RUN_TEST(test_h2_respond_paths_and_goaway);
    RUN_TEST(test_h2_more_guards);
    RUN_TEST(test_h2_continuation_more);
    RUN_TEST(test_h2_respond_content_type_too_big);
    RUN_TEST(test_h2_null_callbacks);
    RUN_TEST(test_h2_headers_stream_zero);
    RUN_TEST(test_h2_continuation_without_headers);
    RUN_TEST(test_h2_idle_stream_frames_are_connection_errors);
    RUN_TEST(test_h2_window_update_on_a_closed_stream_is_ignored);
    RUN_TEST(test_h2_frame_size_and_stream_id_rules);
    RUN_TEST(test_h2_content_length_must_match_the_data);
    RUN_TEST(test_h2_continuation_flood_is_bounded);
    RUN_TEST(test_h2_data_empty_and_unknown_stream);
    RUN_TEST(test_h2_data_after_end_stream_resets_the_stream);
    RUN_TEST(test_h2_window_update_zero_and_overflow);
    RUN_TEST(test_h2_continuation_after_stream_freed);
    RUN_TEST(test_h2_respond_default_chunk_size);
    RUN_TEST(test_h2_respond_content_length_no_room);
    return UNITY_END();
}
