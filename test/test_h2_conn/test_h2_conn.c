// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the HTTP/2 connection engine (network_drivers/presentation/http/http2/pc_h2_conn,
// RFC 9113): initial SETTINGS on init, the preface + client SETTINGS -> SETTINGS ACK, decoding a
// real HPACK-encoded request into the header callbacks, PING -> PING ACK, split-across-reads
// frame reassembly, and pc_h2_conn_respond serializing a HEADERS + DATA response we can decode back.

#include "network_drivers/presentation/http/http2/h2_conn.h"
#include "network_drivers/presentation/http/http2/h2_frame.h"
#include "network_drivers/presentation/http/http2/hpack.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Bounds for what a test collects. A field that does not fit its slot is copied truncated and its
// real length is kept, so an over-long field shows up as a string mismatch instead of passing.
#define HDR_MAX 64
#define HDR_LEN 96
#define SID_MAX 16
#define OUT_MAX 8192
#define BODY_MAX 1024
#define IN_MAX (H2_FRAME_HEADER_LEN + PC_H2_HDR_BLOCK + 64)

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
static void cap_hend(void *app, uint32_t sid, proto_bool es)
{
    Cap *c = (Cap *)app;
    if (c->headers_end_n < SID_MAX)
    {
        c->headers_end[c->headers_end_n++] = sid;
    }
    c->last_end_stream = es;
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

// Count frames of a given type in a captured byte stream (walking the 9-byte headers).
static int count_frames(const uint8_t *b, size_t bn, uint8_t type, int *ack_out)
{
    int n = 0, ack = 0;
    size_t i = 0;
    while (i + 9 <= bn)
    {
        H2FrameHeader h;
        pc_h2_parse_header(&b[i], 9, &h);
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

// Build a client GET request header block via the HPACK encoder.
static size_t build_request(uint8_t *block, size_t cap)
{
    size_t bo = 0;
    bo += pc_hpack_encode_header(block + bo, cap - bo, ":method", 7, "GET", 3);
    bo += pc_hpack_encode_header(block + bo, cap - bo, ":scheme", 7, "https", 5);
    bo += pc_hpack_encode_header(block + bo, cap - bo, ":path", 5, "/", 1);
    bo += pc_hpack_encode_header(block + bo, cap - bo, ":authority", 10, "example.com", 11);
    return bo;
}

// One staging buffer for the byte stream a test feeds into recv, appended to in pieces.
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
// The preface plus an empty client SETTINGS: what every test sends before its own frames.
static void in_preface(void)
{
    uint8_t sf[9];
    in_reset();
    in_add(H2_PREFACE, H2_PREFACE_LEN);
    in_add(sf, pc_h2_build_settings(sf, sizeof sf, NULL, NULL, 0));
}

void test_init_and_request(void)
{
    static Cap cap;
    memset(&cap, 0, sizeof cap);
    H2Callbacks cb = mk_cb(&cap);
    H2Conn c;
    pc_h2_conn_init(&c, &cb); // must emit our SETTINGS
    int acks = 0;
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_SETTINGS, &acks));
    TEST_ASSERT_EQUAL_INT(0, acks); // our SETTINGS is not an ACK

    // Assemble: preface + empty client SETTINGS + HEADERS(stream 1, END_HEADERS|END_STREAM).
    in_preface();
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    uint8_t hf[160];
    in_add(hf, pc_h2_build_headers(hf, sizeof hf, 1, block, blen, PROTO_TRUE));

    cap.out_len = 0;
    TEST_ASSERT_TRUE(pc_h2_conn_recv(&c, g_in, g_in_len));

    // The request headers were decoded and delivered.
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
    // We ACKed the client's SETTINGS.
    int acks2 = 0;
    count_frames(cap.out, cap.out_len, H2_SETTINGS, &acks2);
    TEST_ASSERT_EQUAL_INT(1, acks2);
}

// Collector for a response header block decoded back out of the captured HEADERS frame.
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
    H2Conn c;
    pc_h2_conn_init(&c, &cb);
    in_preface();
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    uint8_t hf[160];
    in_add(hf, pc_h2_build_headers(hf, sizeof hf, 1, block, blen, PROTO_TRUE));
    TEST_ASSERT_TRUE(pc_h2_conn_recv(&c, g_in, g_in_len));

    cap.out_len = 0;
    TEST_ASSERT_TRUE(pc_h2_conn_respond(&c, 1, 200, "text/plain", "hi", 2));
    // Output holds a HEADERS frame + a DATA frame on stream 1.
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_HEADERS, NULL));
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_DATA, NULL));

    // Walk the frames; decode the response HEADERS block and check the DATA.
    HpackDynTable dt;
    pc_hpack_dyn_init(&dt, 4096);
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
        pc_h2_parse_header(&cap.out[i], 9, &h);
        const uint8_t *pl = &cap.out[i + 9];
        if (h.type == H2_HEADERS && h.stream_id == 1)
        {
            char scratch[256];
            pc_hpack_decode(&dt, pl, h.length, scratch, sizeof scratch, rh_emit, &rh);
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
    H2Conn c;
    pc_h2_conn_init(&c, &cb);
    // Preface, then a PING frame, fed one byte at a time (exercises reassembly).
    in_reset();
    in_add(H2_PREFACE, H2_PREFACE_LEN);
    const uint8_t op[8] = {9, 8, 7, 6, 5, 4, 3, 2};
    uint8_t ping[9 + 8];
    pc_h2_write_header(ping, sizeof ping, 8, H2_PING, 0, 0);
    memcpy(ping + 9, op, 8);
    in_add(ping, sizeof ping);

    cap.out_len = 0;
    for (size_t k = 0; k < g_in_len; k++)
    {
        TEST_ASSERT_TRUE(pc_h2_conn_recv(&c, &g_in[k], 1));
    }
    // A PING ACK echoing the opaque data was sent.
    int acks = 0;
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_PING, &acks));
    TEST_ASSERT_EQUAL_INT(1, acks);
    // Locate the PING ACK payload and confirm it echoes the opaque bytes.
    size_t i = 0;
    proto_bool found = PROTO_FALSE;
    while (i + 9 <= cap.out_len)
    {
        H2FrameHeader h;
        pc_h2_parse_header(&cap.out[i], 9, &h);
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
    H2Conn c;
    pc_h2_conn_init(&c, &cb);
    const uint8_t junk[] = {'G', 'E', 'T', ' ', '/', ' ', 'H'};
    TEST_ASSERT_FALSE(pc_h2_conn_recv(&c, junk, sizeof junk));
}

// ---- frame-handler helpers -------------------------------------------------

// Init + feed preface + empty client SETTINGS, then clear the capture so a test
// observes only its own output. c->cb holds a copy, so the local cb is fine.
static void establish(H2Conn *c, Cap *cap)
{
    memset(cap, 0, sizeof *cap);
    H2Callbacks cb = mk_cb(cap);
    pc_h2_conn_init(c, &cb);
    in_preface();
    TEST_ASSERT_TRUE(pc_h2_conn_recv(c, g_in, g_in_len));
    cap->out_len = 0;
}

// Feed one raw frame (9-byte header + payload) through recv.
static uint8_t g_frame[IN_MAX];
static proto_bool feed_frame(H2Conn *c, uint8_t type, uint8_t flags, uint32_t sid, const uint8_t *pl, size_t pn)
{
    TEST_ASSERT_TRUE(9 + pn <= IN_MAX);
    pc_h2_write_header(g_frame, 9, (uint32_t)pn, type, flags, sid);
    if (pn)
    {
        memcpy(g_frame + 9, pl, pn);
    }
    return pc_h2_conn_recv(c, g_frame, 9 + pn);
}

// Open a request stream (HEADERS, END_HEADERS, no END_STREAM) so it stays OPEN.
static void open_stream(H2Conn *c, uint32_t id)
{
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    uint8_t hf[160];
    TEST_ASSERT_TRUE(pc_h2_conn_recv(c, hf, pc_h2_build_headers(hf, sizeof hf, id, block, blen, PROTO_FALSE)));
}

// A payload staging buffer for the tests that hand-assemble a frame body.
static uint8_t g_pl[IN_MAX];

// HEADERS carrying PADDED + PRIORITY still decodes: the pad-length byte and the
// 5-byte priority prefix are stripped, the block in between is delivered.
void test_h2_headers_padded_priority(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    size_t n = 0;
    g_pl[n++] = 3; // pad length
    for (int i = 0; i < 5; i++)
    {
        g_pl[n++] = 0; // priority (accepted, ignored)
    }
    memcpy(g_pl + n, block, blen); // the header block
    n += blen;
    for (int i = 0; i < 3; i++)
    {
        g_pl[n++] = 0; // trailing padding
    }
    uint8_t flags = H2_FLAG_PADDED | H2_FLAG_PRIORITY | H2_FLAG_END_HEADERS | H2_FLAG_END_STREAM;
    TEST_ASSERT_TRUE(feed_frame(&c, H2_HEADERS, flags, 1, g_pl, n));
    TEST_ASSERT_EQUAL_INT(4, (int)cap.req_headers.n);
    TEST_ASSERT_TRUE(cap.last_end_stream);
}

// A pad length larger than the remaining payload is a protocol error.
void test_h2_headers_pad_overflow(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    uint8_t pl[4] = {200, 1, 2, 3}; // pad=200, only 3 bytes left
    TEST_ASSERT_FALSE(feed_frame(&c, H2_HEADERS, H2_FLAG_PADDED | H2_FLAG_END_HEADERS, 1, pl, sizeof pl));
}

// Stream ids must strictly increase; a HEADERS on a lower id is rejected.
void test_h2_stream_id_must_increase(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    uint8_t hf[160];
    TEST_ASSERT_TRUE(pc_h2_conn_recv(&c, hf, pc_h2_build_headers(hf, sizeof hf, 3, block, blen, PROTO_TRUE)));
    TEST_ASSERT_FALSE(pc_h2_conn_recv(&c, hf, pc_h2_build_headers(hf, sizeof hf, 1, block, blen, PROTO_TRUE)));
}

// A stream 0 / even id on HEADERS is rejected (requests are odd, client-initiated).
void test_h2_headers_bad_stream_id(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    uint8_t hf[160];
    TEST_ASSERT_FALSE(pc_h2_conn_recv(&c, hf, pc_h2_build_headers(hf, sizeof hf, 2, block, blen, PROTO_TRUE)));
}

// Once MAX_STREAMS are open, a new stream is refused with RST_STREAM but the
// connection is kept.
void test_h2_stream_table_full_rst(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    for (int i = 0; i < PC_H2_MAX_STREAMS; i++)
    {
        uint8_t hf[160];
        size_t hn = pc_h2_build_headers(hf, sizeof hf, (uint32_t)(1 + 2 * i), block, blen, PROTO_FALSE);
        TEST_ASSERT_TRUE(pc_h2_conn_recv(&c, hf, hn));
    }
    cap.out_len = 0;
    uint8_t hf[160];
    size_t hn = pc_h2_build_headers(hf, sizeof hf, (uint32_t)(1 + 2 * PC_H2_MAX_STREAMS), block, blen, PROTO_FALSE);
    TEST_ASSERT_TRUE(pc_h2_conn_recv(&c, hf, hn)); // kept alive
    TEST_ASSERT_TRUE(count_frames(cap.out, cap.out_len, H2_RST_STREAM, NULL) >= 1);
}

// A header block split across HEADERS (no END_HEADERS) + CONTINUATION reassembles.
void test_h2_continuation(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    size_t half = blen / 2;
    TEST_ASSERT_TRUE(feed_frame(&c, H2_HEADERS, 0, 1, block, half)); // buffered, no END_HEADERS
    TEST_ASSERT_TRUE(feed_frame(&c, H2_CONTINUATION, H2_FLAG_END_HEADERS, 1, block + half, blen - half));
    TEST_ASSERT_EQUAL_INT(4, (int)cap.req_headers.n);
}

// CONTINUATION on the wrong stream, and a non-CONTINUATION frame mid-block, are
// both protocol errors (RFC 9113 sec 6.10).
void test_h2_continuation_guards(void)
{
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    {
        static Cap cap;
        H2Conn c;
        establish(&c, &cap);
        TEST_ASSERT_TRUE(feed_frame(&c, H2_HEADERS, 0, 1, block, blen / 2));
        uint8_t x[4] = {0};
        TEST_ASSERT_FALSE(feed_frame(&c, H2_CONTINUATION, H2_FLAG_END_HEADERS, 3, x, 4)); // wrong stream
    }
    {
        static Cap cap;
        H2Conn c;
        establish(&c, &cap);
        TEST_ASSERT_TRUE(feed_frame(&c, H2_HEADERS, 0, 1, block, blen / 2));
        uint8_t d[1] = {0};
        TEST_ASSERT_FALSE(feed_frame(&c, H2_DATA, 0, 1, d, 1)); // non-CONTINUATION mid-block
    }
}

// DATA is delivered to the app and both flow-control windows are replenished;
// stream 0, padding, and pad-overflow are handled.
void test_h2_data(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    open_stream(&c, 1);
    cap.out_len = 0;
    const uint8_t body[5] = {'h', 'e', 'l', 'l', 'o'};
    TEST_ASSERT_TRUE(feed_frame(&c, H2_DATA, H2_FLAG_END_STREAM, 1, body, 5));
    TEST_ASSERT_EQUAL_STRING("hello", cap.body);
    TEST_ASSERT_TRUE(cap.data_end);
    TEST_ASSERT_EQUAL_INT(2, count_frames(cap.out, cap.out_len, H2_WINDOW_UPDATE, NULL)); // conn + stream

    // Padded DATA: [pad=2][body][2 pad].
    open_stream(&c, 3);
    const uint8_t padded[5] = {2, 'x', 'y', 0, 0};
    cap.body_len = 0;
    cap.body[0] = 0;
    TEST_ASSERT_TRUE(feed_frame(&c, H2_DATA, H2_FLAG_PADDED, 3, padded, sizeof padded));
    TEST_ASSERT_EQUAL_STRING("xy", cap.body);

    // DATA on stream 0 and pad-overflow are rejected.
    static Cap cap2;
    H2Conn c2;
    establish(&c2, &cap2);
    const uint8_t d[1] = {0};
    TEST_ASSERT_FALSE(feed_frame(&c2, H2_DATA, 0, 0, d, 1)); // stream 0
    uint8_t bad[2] = {5, 1};                                 // pad=5 > 1 byte left
    TEST_ASSERT_FALSE(feed_frame(&c2, H2_DATA, H2_FLAG_PADDED, 1, bad, 2));
}

// WINDOW_UPDATE adjusts the connection (stream 0) or per-stream send window; a
// non-4-byte payload is a frame-size error.
void test_h2_window_update(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    open_stream(&c, 1);
    const uint8_t inc[4] = {0, 0, 0, 100};
    TEST_ASSERT_TRUE(feed_frame(&c, H2_WINDOW_UPDATE, 0, 0, inc, 4)); // connection window
    TEST_ASSERT_TRUE(feed_frame(&c, H2_WINDOW_UPDATE, 0, 1, inc, 4)); // stream window
    const uint8_t bad[3] = {0, 0, 1};
    TEST_ASSERT_FALSE(feed_frame(&c, H2_WINDOW_UPDATE, 0, 0, bad, 3));
}

// RST_STREAM frees the slot; PRIORITY is accepted-and-ignored; PUSH_PROMISE to a
// server is a protocol error.
void test_h2_rst_priority_push(void)
{
    {
        static Cap cap;
        H2Conn c;
        establish(&c, &cap);
        open_stream(&c, 1);
        const uint8_t err[4] = {0, 0, 0, 8};
        TEST_ASSERT_TRUE(feed_frame(&c, H2_RST_STREAM, 0, 1, err, 4));
        const uint8_t prio[5] = {0, 0, 0, 0, 0};
        TEST_ASSERT_TRUE(feed_frame(&c, H2_PRIORITY, 0, 3, prio, 5));
    }
    {
        static Cap cap;
        H2Conn c;
        establish(&c, &cap);
        const uint8_t pp[4] = {0, 0, 0, 0};
        TEST_ASSERT_FALSE(feed_frame(&c, H2_PUSH_PROMISE, H2_FLAG_END_HEADERS, 1, pp, 4));
    }
}

// GOAWAY enters the closing phase, after which further input is ignored.
void test_h2_goaway_then_ignore(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    const uint8_t ga[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_TRUE(feed_frame(&c, H2_GOAWAY, 0, 0, ga, 8)); // phase -> closing
    const uint8_t junk[9] = {0};
    TEST_ASSERT_TRUE(pc_h2_conn_recv(&c, junk, sizeof junk)); // ignored while closing
}

// SETTINGS ACK is accepted; a length that is not a multiple of 6 is malformed.
void test_h2_settings_ack_and_bad(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    TEST_ASSERT_TRUE(feed_frame(&c, H2_SETTINGS, H2_FLAG_ACK, 0, NULL, 0));
    const uint8_t bad[3] = {0, 0, 0};
    TEST_ASSERT_FALSE(feed_frame(&c, H2_SETTINGS, 0, 0, bad, 3));
}

// PING ACK is a no-op; a PING whose length is not 8 is a frame-size error.
void test_h2_ping_bad(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    const uint8_t p8[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_TRUE(feed_frame(&c, H2_PING, H2_FLAG_ACK, 0, p8, 8));
    const uint8_t p4[4] = {0, 0, 0, 0};
    TEST_ASSERT_FALSE(feed_frame(&c, H2_PING, 0, 0, p4, 4));
}

// A frame whose declared length exceeds MAX_FRAME is a frame-size error, caught
// from the header alone (before any payload is read).
void test_h2_frame_too_big(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    uint8_t hh[9];
    pc_h2_write_header(hh, sizeof hh, PC_H2_MAX_FRAME + 1, H2_DATA, 0, 1);
    TEST_ASSERT_FALSE(pc_h2_conn_recv(&c, hh, 9));
}

// respond() to an unknown stream fails; pc_h2_conn_goaway emits a GOAWAY; a body
// larger than the peer's max frame size is split across DATA frames.
void test_h2_respond_paths_and_goaway(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    TEST_ASSERT_FALSE(pc_h2_conn_respond(&c, 99, 200, "text/plain", "x", 1)); // no such stream

    open_stream(&c, 1);
    c.peer.max_frame_size = 4; // force multi-chunk DATA
    cap.out_len = 0;
    TEST_ASSERT_TRUE(pc_h2_conn_respond(&c, 1, 200, NULL, "0123456789", 10));
    TEST_ASSERT_TRUE(count_frames(cap.out, cap.out_len, H2_DATA, NULL) >= 3); // 10 bytes / 4 -> >=3 frames

    cap.out_len = 0;
    pc_h2_conn_goaway(&c, 0);
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_GOAWAY, NULL));
}

// A fresh established conn fed one raw frame. A conn is dead after any false
// return (recv leaves fhave stale on error), so each error-frame check needs its
// own conn rather than reusing one.
static proto_bool fresh_feed(uint8_t type, uint8_t flags, uint32_t sid, const uint8_t *pl, size_t pn)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    return feed_frame(&c, type, flags, sid, pl, pn);
}

// The remaining per-frame guards: empty PADDED frames, a short PRIORITY prefix,
// an undecodable HPACK block, an oversized header fragment, DATA pad-overflow,
// and an unknown frame type (ignored per RFC 9113 sec 4.1).
void test_h2_more_guards(void)
{
    TEST_ASSERT_FALSE(fresh_feed(H2_HEADERS, H2_FLAG_PADDED | H2_FLAG_END_HEADERS, 1, NULL, 0)); // no pad byte
    uint8_t p3[3] = {0, 0, 0};
    TEST_ASSERT_FALSE(fresh_feed(H2_HEADERS, H2_FLAG_PRIORITY | H2_FLAG_END_HEADERS, 1, p3, 3)); // priority < 5
    uint8_t bad_hpack[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_FALSE(fresh_feed(H2_HEADERS, H2_FLAG_END_HEADERS, 1, bad_hpack, 4)); // COMPRESSION_ERROR
    static uint8_t huge[PC_H2_HDR_BLOCK + 16];
    memset(huge, 0, sizeof huge);
    TEST_ASSERT_FALSE(fresh_feed(H2_HEADERS, 0, 1, huge, sizeof huge)); // fragment > hblock
    TEST_ASSERT_FALSE(fresh_feed(H2_DATA, H2_FLAG_PADDED, 1, NULL, 0)); // no pad byte
    uint8_t dpad[2] = {5, 1};
    TEST_ASSERT_FALSE(fresh_feed(H2_DATA, H2_FLAG_PADDED, 1, dpad, 2)); // pad > payload
    uint8_t x[1] = {0};
    TEST_ASSERT_TRUE(fresh_feed(0x2A, 0, 1, x, 1)); // unknown frame type ignored
}

// A CONTINUATION without END_HEADERS keeps buffering (returns true); one that
// overflows the reassembly buffer is a protocol error.
void test_h2_continuation_more(void)
{
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    {
        static Cap cap;
        H2Conn c;
        establish(&c, &cap);
        size_t t = blen / 3;
        TEST_ASSERT_TRUE(feed_frame(&c, H2_HEADERS, 0, 1, block, t));          // fragment 1
        TEST_ASSERT_TRUE(feed_frame(&c, H2_CONTINUATION, 0, 1, block + t, t)); // more to come
        TEST_ASSERT_TRUE(feed_frame(&c, H2_CONTINUATION, H2_FLAG_END_HEADERS, 1, block + 2 * t, blen - 2 * t));
        TEST_ASSERT_EQUAL_INT(4, (int)cap.req_headers.n);
    }
    {
        static Cap cap;
        H2Conn c;
        establish(&c, &cap);
        static uint8_t frag[PC_H2_HDR_BLOCK - 8];
        memset(frag, 0, sizeof frag);
        TEST_ASSERT_TRUE(feed_frame(&c, H2_HEADERS, 0, 1, frag, sizeof frag)); // buffered (< hblock)
        uint8_t more[64];
        memset(more, 0, sizeof more);
        TEST_ASSERT_FALSE(feed_frame(&c, H2_CONTINUATION, 0, 1, more, sizeof more)); // overflow
    }
}

// respond() rejects a content-type too large to fit the HPACK header block.
void test_h2_respond_content_type_too_big(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    open_stream(&c, 1);
    char big_ct[1001];
    memset(big_ct, 'a', 1000);
    big_ct[1000] = 0;
    TEST_ASSERT_FALSE(pc_h2_conn_respond(&c, 1, 200, big_ct, "x", 1));
}

// Every callback is optional: with an all-null H2Callbacks the engine still runs the
// whole preface -> SETTINGS -> HEADERS -> DATA path, it just emits and reports nothing.
void test_h2_null_callbacks(void)
{
    H2Callbacks cb;
    memset(&cb, 0, sizeof cb); // no write, no on_header, no on_headers_end, no on_data
    H2Conn c;
    pc_h2_conn_init(&c, &cb); // send_our_settings has nowhere to write; must not crash

    in_preface();
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    uint8_t hf[160];
    in_add(hf, pc_h2_build_headers(hf, sizeof hf, 1, block, blen, PROTO_FALSE));
    TEST_ASSERT_TRUE(pc_h2_conn_recv(&c, g_in, g_in_len));
    // The stream was still opened even though no header callback observed it.
    TEST_ASSERT_EQUAL_UINT32(1, c.last_peer_stream);

    const uint8_t body[3] = {'a', 'b', 'c'};
    TEST_ASSERT_TRUE(feed_frame(&c, H2_DATA, H2_FLAG_END_STREAM, 1, body, 3));
}

// HEADERS on stream 0 is a connection error (requests use odd, client-initiated ids).
void test_h2_headers_stream_zero(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    uint8_t hf[160];
    TEST_ASSERT_FALSE(pc_h2_conn_recv(&c, hf, pc_h2_build_headers(hf, sizeof hf, 0, block, blen, PROTO_TRUE)));
}

// A CONTINUATION arriving with no header block in progress is a protocol error.
void test_h2_continuation_without_headers(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    uint8_t x[4] = {0};
    TEST_ASSERT_FALSE(feed_frame(&c, H2_CONTINUATION, H2_FLAG_END_HEADERS, 1, x, 4));
}

// Frames naming a stream that is not in the table are tolerated: RST_STREAM and
// WINDOW_UPDATE for an unknown id are no-ops, and stream 0 never matches a slot.
void test_h2_unknown_stream_frames(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    const uint8_t err[4] = {0, 0, 0, 8};
    // RST_STREAM on stream 0: find_stream must not match an empty (id == 0) slot.
    TEST_ASSERT_TRUE(feed_frame(&c, H2_RST_STREAM, 0, 0, err, 4));
    // RST_STREAM for a stream that was never opened.
    TEST_ASSERT_TRUE(feed_frame(&c, H2_RST_STREAM, 0, 7, err, 4));
    // WINDOW_UPDATE for a stream that was never opened: ignored, connection window untouched.
    int32_t before = c.conn_send_window;
    const uint8_t inc[4] = {0, 0, 0, 100};
    TEST_ASSERT_TRUE(feed_frame(&c, H2_WINDOW_UPDATE, 0, 9, inc, 4));
    TEST_ASSERT_EQUAL_INT32(before, c.conn_send_window);
}

// DATA with an empty payload is delivered but replenishes no flow-control window, and
// DATA naming a stream that is not open is still passed to the app.
void test_h2_data_empty_and_unknown_stream(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    open_stream(&c, 1);
    cap.out_len = 0;
    TEST_ASSERT_TRUE(feed_frame(&c, H2_DATA, 0, 1, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, count_frames(cap.out, cap.out_len, H2_WINDOW_UPDATE, NULL)); // nothing consumed
    TEST_ASSERT_EQUAL_STRING("", cap.body);

    // DATA with END_STREAM on an id with no stream slot: delivered, no state to update.
    const uint8_t d[2] = {'o', 'k'};
    TEST_ASSERT_TRUE(feed_frame(&c, H2_DATA, H2_FLAG_END_STREAM, 5, d, 2));
    TEST_ASSERT_EQUAL_STRING("ok", cap.body);
    TEST_ASSERT_TRUE(cap.data_end);
}

// respond() frees the stream slot, so a header block still being reassembled when the
// app responds decodes into a stream that no longer exists - accepted, no state update.
void test_h2_continuation_after_stream_freed(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    uint8_t block[128];
    size_t blen = build_request(block, sizeof block);
    size_t half = blen / 2;
    TEST_ASSERT_TRUE(feed_frame(&c, H2_HEADERS, 0, 1, block, half)); // no END_HEADERS
    TEST_ASSERT_TRUE(pc_h2_conn_respond(&c, 1, 200, NULL, "x", 1));  // frees the slot
    TEST_ASSERT_TRUE(feed_frame(&c, H2_CONTINUATION, H2_FLAG_END_HEADERS, 1, block + half, blen - half));
    TEST_ASSERT_EQUAL_INT(4, (int)cap.req_headers.n); // headers still decoded and delivered
}

// A peer that never announced SETTINGS_MAX_FRAME_SIZE falls back to the RFC 9113 default
// 16384, so a small body goes out as a single DATA frame.
void test_h2_respond_default_chunk_size(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    open_stream(&c, 1);
    c.peer.max_frame_size = 0; // unset -> default 16384
    cap.out_len = 0;
    static char body[1000];
    memset(body, 'z', sizeof body);
    TEST_ASSERT_TRUE(pc_h2_conn_respond(&c, 1, 200, NULL, body, sizeof body));
    TEST_ASSERT_EQUAL_INT(1, count_frames(cap.out, cap.out_len, H2_DATA, NULL));
}

// A content-type that *just* fits the 256-byte HPACK block leaves no room for the
// content-length header that follows it, so respond() fails closed rather than
// emitting a HEADERS frame missing content-length.
void test_h2_respond_content_length_no_room(void)
{
    static Cap cap;
    H2Conn c;
    establish(&c, &cap);
    open_stream(&c, 1);
    cap.out_len = 0;
    // '&' has an 8-bit Huffman code, so the value is stored literally and its encoded size is
    // predictable: 2 (indexed name) + 2 (length prefix) + 250 = 254 of the 255 bytes left after
    // ":status: 200", leaving 1 byte - too few for content-length's 2-byte indexed name alone.
    char ct[251];
    memset(ct, '&', 250);
    ct[250] = 0;
    TEST_ASSERT_FALSE(pc_h2_conn_respond(&c, 1, 200, ct, "hi", 2));
    TEST_ASSERT_EQUAL_INT(0, count_frames(cap.out, cap.out_len, H2_HEADERS, NULL)); // nothing emitted
    // A short content-type on the same connection still works, so the stream itself is fine.
    TEST_ASSERT_TRUE(pc_h2_conn_respond(&c, 1, 200, "text/plain", "hi", 2));
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
    RUN_TEST(test_h2_unknown_stream_frames);
    RUN_TEST(test_h2_data_empty_and_unknown_stream);
    RUN_TEST(test_h2_continuation_after_stream_freed);
    RUN_TEST(test_h2_respond_default_chunk_size);
    RUN_TEST(test_h2_respond_content_length_no_room);
    return UNITY_END();
}
