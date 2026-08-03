// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the HTTP/3 application engine (network_drivers/presentation/http/http3/pc_h3_conn; RFC
// 9114). Drives pc_h3_conn through the pc_quic_conn callback seam (qc->cb.on_stream_data /
// on_handshake_done) without a live QUIC handshake: it feeds a QPACK-encoded request on a request
// stream and checks the dispatched method/path, checks the SETTINGS the control stream sends on
// handshake completion, and round-trips a response (HEADERS + DATA) back through the QUIC stream
// send buffer.

#include "network_drivers/presentation/http/http3/h3_conn.h"
#include "network_drivers/presentation/http/http3/h3_frame.h"
#include "network_drivers/presentation/http/http3/qpack.h"
#include "network_drivers/presentation/http/http3/quic_conn.h"
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

static void minimal_qc(QuicConn *qc)
{
    memset(qc, 0, sizeof(*qc));
    for (int i = 0; i < 3; i++)
    {
        qc->space[i].largest_acked = -1;
    }
    for (size_t i = 0; i < PC_QUIC_MAX_STREAMS; i++)
    {
        qc->streams[i].id = UINT64_MAX;
    }
}

static QuicStream *find_stream(QuicConn *qc, uint64_t id)
{
    for (size_t i = 0; i < PC_QUIC_MAX_STREAMS; i++)
    {
        if (qc->streams[i].id == id)
        {
            return &qc->streams[i];
        }
    }
    return NULL;
}

// Find an HTTP/3 stream (with its classified role) by id.
static H3Stream *find_h3(H3Conn *h3, uint64_t id)
{
    for (size_t i = 0; i < PC_H3_MAX_STREAMS; i++)
    {
        if (h3->streams[i].role != H3_ROLE_FREE && h3->streams[i].id == id)
        {
            return &h3->streams[i];
        }
    }
    return NULL;
}

// Emit target for decoding a response field section.
static char e_status[8], e_ctype[32];
static bool pc_resp_emit(void *, const char *name, size_t nlen, const char *value, size_t vlen)
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
    return true;
}

void test_request_dispatch_and_response()
{
    g_requests = 0;
    QuicConn qc;
    minimal_qc(&qc);
    H3Conn h3;
    pc_h3_conn_init(&h3, &qc, on_request, NULL);
    TEST_ASSERT_NOT_NULL(qc.cb.on_stream_data);

    // Build a GET request: HEADERS(:method GET, :path /index.html, :authority example.org).
    uint8_t block[128];
    size_t bp = pc_qpack_encode_prefix(block, sizeof(block));
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":method", 7, "GET", 3);
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":path", 5, "/index.html", 11);
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":authority", 10, "example.org", 11);
    uint8_t req[256];
    size_t rp = pc_h3_build_headers(req, sizeof(req), block, bp);

    // Deliver it on request stream 0 with FIN through the QUIC callback seam.
    qc.cb.on_stream_data(qc.cb.app, &qc, 0, req, rp, true);
    TEST_ASSERT_EQUAL_INT(1, g_requests);
    TEST_ASSERT_EQUAL_STRING("GET", g_method);
    TEST_ASSERT_EQUAL_STRING("/index.html", g_path);
    TEST_ASSERT_EQUAL_STRING("example.org", g_auth);
    TEST_ASSERT_EQUAL_UINT(0, g_body_len);

    // Respond and decode what was queued onto the QUIC stream.
    TEST_ASSERT_TRUE(pc_h3_conn_respond(&h3, 0, 200, "text/plain", (const uint8_t *)"hello", 5));
    QuicStream *st = find_stream(&qc, 0);
    TEST_ASSERT_NOT_NULL(st);
    // Walk the response frames: HEADERS then DATA.
    size_t off = 0;
    bool saw_headers = false, saw_data = false;
    char scratch[128];
    e_status[0] = e_ctype[0] = '\0';
    while (off < st->tx_have)
    {
        H3Frame fr;
        TEST_ASSERT_TRUE(pc_h3_frame_parse(st->tx + off, st->tx_have - off, &fr));
        const uint8_t *fp = st->tx + off + fr.header_len;
        if (fr.type == H3_HEADERS)
        {
            pc_qpack_decode(fp, (size_t)fr.length, scratch, sizeof(scratch), pc_resp_emit, NULL);
            saw_headers = true;
        }
        else if (fr.type == H3_DATA)
        {
            TEST_ASSERT_EQUAL_UINT(5, (size_t)fr.length);
            TEST_ASSERT_EQUAL_UINT8_ARRAY("hello", fp, 5);
            saw_data = true;
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
    QuicConn qc;
    minimal_qc(&qc);
    H3Conn h3;
    pc_h3_conn_init(&h3, &qc, on_request, NULL);

    uint8_t block[128];
    size_t bp = pc_qpack_encode_prefix(block, sizeof(block));
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":method", 7, "POST", 4);
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":path", 5, "/submit", 7);
    uint8_t req[256];
    size_t rp = pc_h3_build_headers(req, sizeof(req), block, bp);
    rp += pc_h3_build_data(req + rp, sizeof(req) - rp, (const uint8_t *)"name=x", 6);

    qc.cb.on_stream_data(qc.cb.app, &qc, 4, req, rp, true);
    TEST_ASSERT_EQUAL_INT(1, g_requests);
    TEST_ASSERT_EQUAL_STRING("POST", g_method);
    TEST_ASSERT_EQUAL_STRING("/submit", g_path);
    TEST_ASSERT_EQUAL_UINT(6, g_body_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("name=x", g_body, 6);
}

void test_control_stream_settings_sent()
{
    QuicConn qc;
    minimal_qc(&qc);
    H3Conn h3;
    pc_h3_conn_init(&h3, &qc, on_request, NULL);

    // Simulate handshake completion: the control + QPACK streams should be opened.
    qc.cb.on_handshake_done(qc.cb.app, &qc);
    QuicStream *ctrl = find_stream(&qc, 3);
    TEST_ASSERT_NOT_NULL(ctrl);
    // Stream type 0x00, then a SETTINGS frame.
    uint64_t type = 0;
    size_t c = 0;
    TEST_ASSERT_TRUE(pc_quic_varint_decode(ctrl->tx, ctrl->tx_have, &type, &c));
    TEST_ASSERT_EQUAL_UINT64(0x00, type);
    H3Frame fr;
    TEST_ASSERT_TRUE(pc_h3_frame_parse(ctrl->tx + c, ctrl->tx_have - c, &fr));
    TEST_ASSERT_EQUAL_UINT64(H3_SETTINGS, fr.type);
    // QPACK encoder (7) and decoder (11) streams exist with their type bytes.
    TEST_ASSERT_NOT_NULL(find_stream(&qc, 7));
    TEST_ASSERT_NOT_NULL(find_stream(&qc, 11));
}

// The client's control stream (a uni stream, type 0x00) carries SETTINGS; the server classifies the
// stream by its type varint and parses the SETTINGS into peer_settings.
void test_client_control_stream_settings()
{
    QuicConn qc;
    minimal_qc(&qc);
    H3Conn h3;
    pc_h3_conn_init(&h3, &qc, on_request, NULL);

    uint8_t s[64];
    size_t sp = pc_quic_varint_encode(s, sizeof(s), 0x00); // control stream type
    const uint64_t ids[] = {H3_SETTINGS_MAX_FIELD_SECTION_SIZE};
    const uint64_t vals[] = {12345};
    sp += pc_h3_build_settings(s + sp, sizeof(s) - sp, ids, vals, 1);
    qc.cb.on_stream_data(qc.cb.app, &qc, 2, s, sp, false); // client-initiated uni stream (id 2)

    H3Stream *st = find_h3(&h3, 2);
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_CONTROL, st->role);
    TEST_ASSERT_EQUAL_UINT64(12345, h3.peer_settings.max_field_section_size);
}

// The QPACK encoder / decoder streams (type 0x02 / 0x03) and an unknown uni stream are classified
// and then drained (static-table only, nothing to process).
void test_client_uni_stream_types()
{
    QuicConn qc;
    minimal_qc(&qc);
    H3Conn h3;
    pc_h3_conn_init(&h3, &qc, on_request, NULL);

    uint8_t t;
    size_t n = pc_quic_varint_encode(&t, 1, 0x02);
    qc.cb.on_stream_data(qc.cb.app, &qc, 6, &t, n, false);
    n = pc_quic_varint_encode(&t, 1, 0x03);
    qc.cb.on_stream_data(qc.cb.app, &qc, 10, &t, n, false);
    n = pc_quic_varint_encode(&t, 1, 0x1f); // an unknown stream type
    qc.cb.on_stream_data(qc.cb.app, &qc, 14, &t, n, false);

    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_QPACK_ENC, find_h3(&h3, 6)->role);
    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_QPACK_DEC, find_h3(&h3, 10)->role);
    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_OTHER_UNI, find_h3(&h3, 14)->role);
}

// on_handshake_done opens the control + QPACK streams exactly once; a repeat call is a no-op.
void test_handshake_done_idempotent()
{
    QuicConn qc;
    minimal_qc(&qc);
    H3Conn h3;
    pc_h3_conn_init(&h3, &qc, on_request, NULL);

    qc.cb.on_handshake_done(qc.cb.app, &qc);
    QuicStream *ctrl = find_stream(&qc, 3);
    TEST_ASSERT_NOT_NULL(ctrl);
    size_t first = ctrl->tx_have;
    qc.cb.on_handshake_done(qc.cb.app, &qc); // control_opened -> no-op
    TEST_ASSERT_EQUAL_UINT(first, ctrl->tx_have);
}

// Malformed request frames (an incomplete varint header, or a length past the buffer) do not dispatch.
void test_malformed_request_frame()
{
    g_requests = 0;
    QuicConn qc;
    minimal_qc(&qc);
    H3Conn h3;
    pc_h3_conn_init(&h3, &qc, on_request, NULL);

    // A HEADERS frame that declares length 9999 but has no payload -> incomplete -> not dispatched.
    uint8_t hdr[8];
    size_t hp = pc_h3_frame_write_header(hdr, sizeof(hdr), H3_HEADERS, 9999);
    qc.cb.on_stream_data(qc.cb.app, &qc, 0, hdr, hp, true);
    TEST_ASSERT_EQUAL_INT(0, g_requests);

    // A truncated frame-header varint -> parse fails -> not dispatched.
    uint8_t junk[1] = {0xC0}; // first byte of an 8-byte varint, nothing after it
    qc.cb.on_stream_data(qc.cb.app, &qc, 4, junk, sizeof(junk), true);
    TEST_ASSERT_EQUAL_INT(0, g_requests);
}

// A response body too large to frame into the output buffer fails cleanly.
void test_respond_body_too_large()
{
    QuicConn qc;
    minimal_qc(&qc);
    H3Conn h3;
    pc_h3_conn_init(&h3, &qc, on_request, NULL);
    static uint8_t big[PC_H3_STREAM_BUF + 200];
    memset(big, 'x', sizeof(big));
    TEST_ASSERT_FALSE(pc_h3_conn_respond(&h3, 0, 200, "text/plain", big, sizeof(big)));
}

// When every stream slot is occupied, a further new stream is dropped rather than overrunning.
void test_stream_pool_full()
{
    g_requests = 0;
    QuicConn qc;
    minimal_qc(&qc);
    H3Conn h3;
    pc_h3_conn_init(&h3, &qc, on_request, NULL);

    uint8_t b = 0x00;
    for (uint64_t i = 0; i < PC_H3_MAX_STREAMS; i++)
    {
        qc.cb.on_stream_data(qc.cb.app, &qc, i * 4, &b, 1, false); // partial request streams, no FIN
    }

    // One more distinct request stream cannot allocate a slot -> silently ignored.
    uint8_t block[64];
    size_t bp = pc_qpack_encode_prefix(block, sizeof(block));
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":method", 7, "GET", 3);
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":path", 5, "/x", 2);
    uint8_t req[128];
    size_t rp = pc_h3_build_headers(req, sizeof(req), block, bp);
    qc.cb.on_stream_data(qc.cb.app, &qc, (uint64_t)PC_H3_MAX_STREAMS * 4, req, rp, true);
    TEST_ASSERT_EQUAL_INT(0, g_requests);
}

// A uni-stream type varint split across two deliveries: the first (incomplete) is buffered until the
// rest arrives, then the stream is classified.
void test_uni_stream_partial_type()
{
    QuicConn qc;
    minimal_qc(&qc);
    H3Conn h3;
    pc_h3_conn_init(&h3, &qc, on_request, NULL);

    uint8_t b0 = 0x40; // first byte of a 2-byte varint - incomplete on its own
    qc.cb.on_stream_data(qc.cb.app, &qc, 2, &b0, 1, false);
    TEST_ASSERT_FALSE(find_h3(&h3, 2)->type_read); // needs more bytes; not yet classified
    uint8_t b1 = 0x00;                             // completes the varint 0x4000 -> value 0 -> control stream
    qc.cb.on_stream_data(qc.cb.app, &qc, 2, &b1, 1, false);
    TEST_ASSERT_TRUE(find_h3(&h3, 2)->type_read);
    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_CONTROL, find_h3(&h3, 2)->role);
}

// A pseudo-header longer than its capture buffer is truncated, not overrun.
void test_overlong_field_truncated()
{
    g_requests = 0;
    QuicConn qc;
    minimal_qc(&qc);
    H3Conn h3;
    pc_h3_conn_init(&h3, &qc, on_request, NULL);

    uint8_t block[128];
    size_t bp = pc_qpack_encode_prefix(block, sizeof(block));
    const char *m = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"; // 26 chars > PC_H3_METHOD_LEN (16)
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":method", 7, m, 26);
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":path", 5, "/", 1);
    uint8_t req[256];
    size_t rp = pc_h3_build_headers(req, sizeof(req), block, bp);
    qc.cb.on_stream_data(qc.cb.app, &qc, 0, req, rp, true);

    TEST_ASSERT_EQUAL_INT(1, g_requests);
    TEST_ASSERT_EQUAL_UINT(PC_H3_METHOD_LEN - 1, strlen(g_method)); // truncated to fit
}

// Headers whose names are the same LENGTH as a pseudo-header but a different name must not be
// captured into that field: ":scheme" (7, like ":method"), "hello" (5, like ":path"), "user-agent"
// (10, like ":authority") and a short name are all ignored, while the real ones still land.
void test_h3_pseudo_header_name_variants()
{
    g_requests = 0;
    QuicConn qc;
    minimal_qc(&qc);
    H3Conn h3;
    pc_h3_conn_init(&h3, &qc, on_request, NULL);

    uint8_t block[256];
    size_t bp = pc_qpack_encode_prefix(block, sizeof(block));
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":scheme", 7, "https", 5);
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, "hello", 5, "world", 5);
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, "user-agent", 10, "curl", 4);
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, "abc", 3, "z", 1);
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":method", 7, "GET", 3);
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":path", 5, "/ok", 3);
    uint8_t req[512];
    size_t rp = pc_h3_build_headers(req, sizeof(req), block, bp);

    strcpy(g_auth, "unset");
    qc.cb.on_stream_data(qc.cb.app, &qc, 0, req, rp, true);
    TEST_ASSERT_EQUAL_INT(1, g_requests);
    TEST_ASSERT_EQUAL_STRING("GET", g_method); // ":scheme" did not overwrite :method
    TEST_ASSERT_EQUAL_STRING("/ok", g_path);   // "hello" did not overwrite :path
    TEST_ASSERT_EQUAL_STRING("", g_auth);      // "user-agent" did not become the authority
}

// A request stream carrying a frame type the engine does not act on, and a zero-length DATA frame,
// are both walked past: only the real DATA payload reaches the application.
void test_h3_request_unknown_frame_and_empty_data()
{
    g_requests = 0;
    QuicConn qc;
    minimal_qc(&qc);
    H3Conn h3;
    pc_h3_conn_init(&h3, &qc, on_request, NULL);

    uint8_t block[128];
    size_t bp = pc_qpack_encode_prefix(block, sizeof(block));
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":method", 7, "POST", 4);
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":path", 5, "/u", 2);

    uint8_t req[512];
    size_t rp = pc_h3_build_headers(req, sizeof(req), block, bp);
    // A SETTINGS frame is legal on the wire but meaningless here: neither HEADERS nor DATA.
    const uint64_t ids[] = {H3_SETTINGS_QPACK_BLOCKED_STREAMS};
    const uint64_t vals[] = {0};
    rp += pc_h3_build_settings(req + rp, sizeof(req) - rp, ids, vals, 1);
    rp += pc_h3_build_data(req + rp, sizeof(req) - rp, NULL, 0); // empty DATA
    rp += pc_h3_build_data(req + rp, sizeof(req) - rp, (const uint8_t *)"body", 4);

    qc.cb.on_stream_data(qc.cb.app, &qc, 0, req, rp, true);
    TEST_ASSERT_EQUAL_INT(1, g_requests);
    TEST_ASSERT_EQUAL_STRING("POST", g_method);
    TEST_ASSERT_EQUAL_UINT(4, g_body_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("body", g_body, 4);
}

// With no application callback registered the request is still parsed and marked complete; it is
// simply not dispatched (nothing is invoked through a null pointer).
void test_h3_no_request_callback()
{
    g_requests = 0;
    QuicConn qc;
    minimal_qc(&qc);
    H3Conn h3;
    pc_h3_conn_init(&h3, &qc, NULL, NULL);

    uint8_t block[128];
    size_t bp = pc_qpack_encode_prefix(block, sizeof(block));
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":method", 7, "GET", 3);
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":path", 5, "/x", 2);
    uint8_t req[256];
    size_t rp = pc_h3_build_headers(req, sizeof(req), block, bp);

    qc.cb.on_stream_data(qc.cb.app, &qc, 0, req, rp, true);
    TEST_ASSERT_EQUAL_INT(0, g_requests); // no callback -> nothing dispatched
    H3Stream *st = find_h3(&h3, 0);
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_TRUE(st->have_headers); // ...but the HEADERS were decoded
    TEST_ASSERT_EQUAL_STRING("GET", st->method);
}

// Stream data beyond the per-stream reassembly buffer is clamped, not overrun: the buffer fills to
// exactly its capacity and the excess is dropped.
void test_h3_stream_buffer_overflow_clamped()
{
    QuicConn qc;
    minimal_qc(&qc);
    H3Conn h3;
    pc_h3_conn_init(&h3, &qc, on_request, NULL);

    static uint8_t big[PC_H3_STREAM_BUF + 64];
    memset(big, 0x00, sizeof(big)); // PADDING-like filler; no complete frame is formed
    qc.cb.on_stream_data(qc.cb.app, &qc, 0, big, sizeof(big), false);

    H3Stream *st = find_h3(&h3, 0);
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_EQUAL_UINT(PC_H3_STREAM_BUF, st->buf_len); // clamped to capacity
}

// Control-stream frame guards: an unparseable frame header, a frame whose payload has not all
// arrived, and a frame that is not SETTINGS - none of which disturb peer_settings.
void test_h3_control_stream_frame_guards()
{
    QuicConn qc;
    minimal_qc(&qc);
    H3Conn h3;
    pc_h3_conn_init(&h3, &qc, on_request, NULL);
    H3Settings defaults;
    pc_h3_settings_defaults(&defaults);

    // Stream type 0x00 (control) then the first byte of an 8-octet varint: unparseable.
    uint8_t s[64];
    size_t sp = pc_quic_varint_encode(s, sizeof(s), 0x00);
    s[sp++] = 0xC0;
    qc.cb.on_stream_data(qc.cb.app, &qc, 2, s, sp, false);
    H3Stream *st = find_h3(&h3, 2);
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_CONTROL, st->role);
    TEST_ASSERT_EQUAL_UINT64(defaults.max_field_section_size, h3.peer_settings.max_field_section_size);

    // A SETTINGS frame header announcing more payload than has arrived: held, not parsed.
    QuicConn qc2;
    minimal_qc(&qc2);
    H3Conn h3b;
    pc_h3_conn_init(&h3b, &qc2, on_request, NULL);
    uint8_t s2[64];
    size_t sp2 = pc_quic_varint_encode(s2, sizeof(s2), 0x00);
    sp2 += pc_h3_frame_write_header(s2 + sp2, sizeof(s2) - sp2, H3_SETTINGS, 40);
    qc2.cb.on_stream_data(qc2.cb.app, &qc2, 2, s2, sp2, false);
    TEST_ASSERT_EQUAL_UINT64(defaults.max_field_section_size, h3b.peer_settings.max_field_section_size);

    // A complete control frame that is not SETTINGS is consumed and ignored.
    QuicConn qc3;
    minimal_qc(&qc3);
    H3Conn h3c;
    pc_h3_conn_init(&h3c, &qc3, on_request, NULL);
    uint8_t s3[64];
    size_t sp3 = pc_quic_varint_encode(s3, sizeof(s3), 0x00);
    sp3 += pc_h3_frame_write_header(s3 + sp3, sizeof(s3) - sp3, 0x07 /*GOAWAY*/, 1);
    s3[sp3++] = 0x00;
    qc3.cb.on_stream_data(qc3.cb.app, &qc3, 2, s3, sp3, false);
    H3Stream *sc = find_h3(&h3c, 2);
    TEST_ASSERT_NOT_NULL(sc);
    TEST_ASSERT_EQUAL_UINT(0, sc->buf_len); // fully consumed
    TEST_ASSERT_EQUAL_UINT64(defaults.max_field_section_size, h3c.peer_settings.max_field_section_size);
}

// The uni-stream classification guard: a zero-length delivery leaves the stream unclassified (there
// is no type varint to read yet), and once classified a later delivery does not re-classify.
void test_h3_uni_stream_empty_and_repeat_delivery()
{
    QuicConn qc;
    minimal_qc(&qc);
    H3Conn h3;
    pc_h3_conn_init(&h3, &qc, on_request, NULL);

    // Zero-length delivery on a fresh uni stream: nothing buffered, nothing classified.
    uint8_t none = 0;
    qc.cb.on_stream_data(qc.cb.app, &qc, 2, &none, 0, false);
    H3Stream *st = find_h3(&h3, 2);
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_FALSE(st->type_read);
    TEST_ASSERT_EQUAL_UINT(0, st->buf_len);

    // The type varint arrives and classifies it as the control stream.
    uint8_t t[16];
    size_t tn = pc_quic_varint_encode(t, sizeof(t), 0x00);
    qc.cb.on_stream_data(qc.cb.app, &qc, 2, t, tn, false);
    TEST_ASSERT_TRUE(st->type_read);
    TEST_ASSERT_EQUAL_UINT8(H3_ROLE_CONTROL, st->role);

    // A second delivery on the already-classified stream is treated as control-stream payload, not
    // as another stream type: the SETTINGS it carries are parsed.
    uint8_t s[64];
    const uint64_t ids[] = {H3_SETTINGS_MAX_FIELD_SECTION_SIZE};
    const uint64_t vals[] = {4321};
    size_t sp = pc_h3_build_settings(s, sizeof(s), ids, vals, 1);
    qc.cb.on_stream_data(qc.cb.app, &qc, 2, s, sp, false);
    TEST_ASSERT_EQUAL_UINT64(4321, h3.peer_settings.max_field_section_size);
}

// A response with no content-type and an empty body still serializes: the HEADERS carry :status and
// content-length 0, and no DATA frame is emitted.
void test_h3_respond_no_content_type_empty_body()
{
    QuicConn qc;
    minimal_qc(&qc);
    H3Conn h3;
    pc_h3_conn_init(&h3, &qc, on_request, NULL);

    TEST_ASSERT_TRUE(pc_h3_conn_respond(&h3, 0, 204, NULL, NULL, 0));
    QuicStream *st = find_stream(&qc, 0);
    TEST_ASSERT_NOT_NULL(st);

    size_t off = 0;
    int frames = 0;
    char scratch[128];
    e_status[0] = e_ctype[0] = '\0';
    while (off < st->tx_have)
    {
        H3Frame fr;
        TEST_ASSERT_TRUE(pc_h3_frame_parse(st->tx + off, st->tx_have - off, &fr));
        TEST_ASSERT_EQUAL_UINT64(H3_HEADERS, fr.type); // no DATA frame follows
        pc_qpack_decode(st->tx + off + fr.header_len, (size_t)fr.length, scratch, sizeof(scratch), pc_resp_emit,
                        NULL);
        off += fr.header_len + (size_t)fr.length;
        frames++;
    }
    TEST_ASSERT_EQUAL_INT(1, frames);
    TEST_ASSERT_EQUAL_STRING("204", e_status);
    TEST_ASSERT_EQUAL_STRING("", e_ctype); // content-type was not emitted
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_request_dispatch_and_response);
    RUN_TEST(test_h3_pseudo_header_name_variants);
    RUN_TEST(test_h3_request_unknown_frame_and_empty_data);
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
