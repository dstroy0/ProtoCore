// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/web/edge_cache/edge_mesh: the CDN edge cache's mesh (sibling-cache) wire codec and
// async peer-query engine. Covers the request/response frame build + tri-state accumulating parse (partial
// reads, magic/version/opcode validation), the freshness-carrying entry frame round-trip (content via the
// shared edge_sd body plus the timing trailer, incl. a binary body), RFC 9111 age propagation (a peer's
// corrected age transfers so the receiver stays fresh for the remaining lifetime and its age keeps growing),
// and the requester engine reaching HIT / MISS / FAILED (open fail, send fail, timeout, peer close, malformed)
// over a scripted mock transport seam.

#include "services/web/edge_cache/edge_cache.h"
#include "services/web/edge_cache/edge_mesh.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// --- helpers -------------------------------------------------------------------------------------
static void mkcanon(char *out, size_t cap, const char *path)
{
    snprintf(out, cap, "GET\nexample.com\n%s", path);
}

// Fill a bare entry (fields only, not linked to a store) with a validator + body + freshness.
static void fill_entry(EdgeEntry *e, const char *canon, const char *etag, const uint8_t *body, uint16_t body_len)
{
    memset(e, 0, sizeof(*e));
    strncpy(e->key, canon, sizeof(e->key) - 1);
    edge_key_digest(e->key, strlen(e->key), e->digest);
    e->status = 200;
    strncpy(e->content_type, "text/plain", sizeof(e->content_type) - 1);
    strncpy(e->etag, etag, sizeof(e->etag) - 1);
    if (body && body_len)
    {
        memcpy(e->body, body, body_len);
    }
    e->body_len = body_len;
    e->date_epoch = -1;
    e->expires_epoch = -1;
    e->age_hdr = 0;
    e->lifetime_s = 60;
    e->initial_age = 0;
    e->insert_ms = 0;
}

// --- request frame -------------------------------------------------------------------------------
static void test_request_roundtrip()
{
    uint8_t digest[32];
    for (int i = 0; i < 32; i++)
    {
        digest[i] = (uint8_t)(i * 7 + 1);
    }
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/app.js?v=2");
    const char *vary = "Accept-Encoding\x1egzip\x1f";

    uint8_t req[PC_EDGE_MESH_REQ_MAX];
    size_t n = edge_mesh_build_request(digest, canon, vary, req, sizeof(req));
    TEST_ASSERT_TRUE(n > 0);

    uint8_t d2[32];
    char c2[PC_EDGE_KEY_MAX];
    char v2[PC_MESH_HDRS_MAX];
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_HIT, edge_mesh_parse_request(req, n, d2, c2, sizeof(c2), v2, sizeof(v2)));
    TEST_ASSERT_EQUAL_MEMORY(digest, d2, 32);
    TEST_ASSERT_EQUAL_STRING(canon, c2);
    TEST_ASSERT_EQUAL_STRING(vary, v2);
}

static void test_request_incomplete_then_complete()
{
    uint8_t digest[32];
    memset(digest, 0xAB, 32);
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/x");
    uint8_t req[PC_EDGE_MESH_REQ_MAX];
    size_t n = edge_mesh_build_request(digest, canon, "Accept\x1e*\x1f", req, sizeof(req));
    TEST_ASSERT_TRUE(n > 3);

    uint8_t d2[32];
    char c2[PC_EDGE_KEY_MAX];
    char v2[PC_MESH_HDRS_MAX];
    // A valid prefix (short of the full frame) accumulates rather than erroring.
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE, edge_mesh_parse_request(req, 2, d2, c2, sizeof(c2), v2, sizeof(v2)));
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE,
                      edge_mesh_parse_request(req, n - 1, d2, c2, sizeof(c2), v2, sizeof(v2)));
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_HIT, edge_mesh_parse_request(req, n, d2, c2, sizeof(c2), v2, sizeof(v2)));
}

static void test_request_malformed()
{
    uint8_t digest[32];
    memset(digest, 1, 32);
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/y");
    uint8_t req[PC_EDGE_MESH_REQ_MAX];
    size_t n = edge_mesh_build_request(digest, canon, "", req, sizeof(req));

    uint8_t d2[32];
    char c2[PC_EDGE_KEY_MAX];
    char v2[PC_MESH_HDRS_MAX];
    uint8_t bad = req[0];
    req[0] = 'X'; // wrong magic
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_MALFORMED, edge_mesh_parse_request(req, n, d2, c2, sizeof(c2), v2, sizeof(v2)));
    req[0] = bad;
    req[3] = 9; // unknown opcode
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_MALFORMED, edge_mesh_parse_request(req, n, d2, c2, sizeof(c2), v2, sizeof(v2)));
    req[3] = PC_EDGE_MESH_OP_GET;
    // a key that cannot fit the destination buffer is malformed, not silently truncated
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_MALFORMED, edge_mesh_parse_request(req, n, d2, c2, 4, v2, sizeof(v2)));
}

// --- entry frame (content + freshness) -----------------------------------------------------------
static void test_entry_frame_roundtrip()
{
    static const uint8_t body[] = {0x00, 0xFF, 0x10, 'a', 0x00, 'z', 0x7F, 0x80}; // binary-safe
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/img.png");
    EdgeEntry in;
    fill_entry(&in, canon, "\"etag-v1\"", body, sizeof(body));
    strncpy(in.content_encoding, "gzip", sizeof(in.content_encoding) - 1);
    strncpy(in.last_modified, "Wed, 21 Oct 2026 07:28:00 GMT", sizeof(in.last_modified) - 1);
    strncpy(in.vary_names, "Accept-Encoding", sizeof(in.vary_names) - 1);
    strncpy(in.vary_vals, "Accept-Encoding\x1egzip\x1f", sizeof(in.vary_vals) - 1);
    in.date_epoch = 1000000;
    in.expires_epoch = 1000060;
    in.age_hdr = 3;
    in.lifetime_s = 60;

    long cur = 12;
    uint8_t frame[PC_EDGE_MESH_ENTRY_MAX];
    size_t n = edge_mesh_serialize_entry(&in, cur, frame, sizeof(frame));
    TEST_ASSERT_TRUE(n > 0);

    EdgeEntry out;
    memset(&out, 0, sizeof(out));
    uint32_t now2 = 50000;
    TEST_ASSERT_TRUE(edge_mesh_deserialize_entry(frame, n, &out, now2));
    // content
    TEST_ASSERT_EQUAL_STRING(in.key, out.key);
    TEST_ASSERT_EQUAL_INT(in.status, out.status);
    TEST_ASSERT_EQUAL_STRING(in.content_type, out.content_type);
    TEST_ASSERT_EQUAL_STRING(in.etag, out.etag);
    TEST_ASSERT_EQUAL_STRING(in.content_encoding, out.content_encoding);
    TEST_ASSERT_EQUAL_STRING(in.last_modified, out.last_modified);
    TEST_ASSERT_EQUAL_STRING(in.vary_names, out.vary_names);
    TEST_ASSERT_EQUAL_STRING(in.vary_vals, out.vary_vals);
    TEST_ASSERT_EQUAL_UINT(in.body_len, out.body_len);
    TEST_ASSERT_EQUAL_MEMORY(in.body, out.body, in.body_len);
    TEST_ASSERT_EQUAL_MEMORY(in.digest, out.digest, 32); // re-derived from the restored key
    // freshness
    TEST_ASSERT_EQUAL_INT64(in.date_epoch, out.date_epoch);
    TEST_ASSERT_EQUAL_INT64(in.expires_epoch, out.expires_epoch);
    TEST_ASSERT_EQUAL_INT(in.lifetime_s, out.lifetime_s);
    TEST_ASSERT_EQUAL_INT(in.age_hdr, out.age_hdr);
    TEST_ASSERT_EQUAL_INT(cur, out.initial_age);
    TEST_ASSERT_EQUAL_UINT32(now2, out.insert_ms);
}

static void test_age_propagation()
{
    static const uint8_t body[] = {'h', 'i'};
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/z");
    EdgeEntry peer;
    fill_entry(&peer, canon, "\"v1\"", body, sizeof(body));
    peer.lifetime_s = 60;
    peer.initial_age = 5;
    peer.insert_ms = 10000;

    uint32_t send_now = 10000 + 12000;                                       // 12 s resident on the peer
    long cur = edge_current_age(peer.initial_age, peer.insert_ms, send_now); // 5 + 12
    TEST_ASSERT_EQUAL_INT(17, cur);

    uint8_t frame[PC_EDGE_MESH_ENTRY_MAX];
    size_t n = edge_mesh_serialize_entry(&peer, cur, frame, sizeof(frame));
    EdgeEntry recv;
    memset(&recv, 0, sizeof(recv));
    uint32_t recv_now = 999000; // the receiver's clock is unrelated to the peer's
    TEST_ASSERT_TRUE(edge_mesh_deserialize_entry(frame, n, &recv, recv_now));

    // the receiver's age at receipt equals the transferred age, and grows with local hold
    TEST_ASSERT_EQUAL_INT(17, edge_current_age(recv.initial_age, recv.insert_ms, recv_now));
    TEST_ASSERT_EQUAL_INT(17 + 30, edge_current_age(recv.initial_age, recv.insert_ms, recv_now + 30000));
    // still fresh (17 < 60), and stale once the remaining lifetime (43 s) elapses
    TEST_ASSERT_TRUE(edge_entry_fresh(&recv, recv_now));
    TEST_ASSERT_TRUE(edge_entry_fresh(&recv, recv_now + 42000));
    TEST_ASSERT_FALSE(edge_entry_fresh(&recv, recv_now + 44000));
}

// --- response frame ------------------------------------------------------------------------------
static void build_hit_frame(uint8_t *frame, size_t cap, size_t *fn_out, long current_age)
{
    static const uint8_t body[] = {'p', 'a', 'y', 'l', 'o', 'a', 'd'};
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/r");
    EdgeEntry e;
    fill_entry(&e, canon, "\"rv\"", body, sizeof(body));
    *fn_out = edge_mesh_serialize_entry(&e, current_age, frame, cap);
}

static void test_response_roundtrip()
{
    uint8_t frame[PC_EDGE_MESH_ENTRY_MAX];
    size_t fn = 0;
    build_hit_frame(frame, sizeof(frame), &fn, 0);
    TEST_ASSERT_TRUE(fn > 0);

    uint8_t resp[PC_EDGE_MESH_RESP_MAX];
    size_t rn = edge_mesh_build_response(PROTO_TRUE, frame, fn, resp, sizeof(resp));
    TEST_ASSERT_TRUE(rn > 0);

    size_t eoff = 0;
    size_t elen = 0;
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_HIT, edge_mesh_parse_response(resp, rn, &eoff, &elen));
    TEST_ASSERT_EQUAL_UINT(fn, elen);
    TEST_ASSERT_EQUAL_MEMORY(frame, resp + eoff, fn);
    // a truncated HIT accumulates
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE, edge_mesh_parse_response(resp, 5, &eoff, &elen));
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE, edge_mesh_parse_response(resp, rn - 1, &eoff, &elen));

    uint8_t miss[8];
    size_t mn = edge_mesh_build_response(PROTO_FALSE, NULL, 0, miss, sizeof(miss));
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_MISS, edge_mesh_parse_response(miss, mn, &eoff, &elen));
}

static void test_response_malformed()
{
    size_t eoff = 0;
    size_t elen = 0;
    uint8_t bad_magic[6] = {'X', 'M', PC_EDGE_MESH_VERSION, 1, 0, 0};
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_MALFORMED, edge_mesh_parse_response(bad_magic, 6, &eoff, &elen));
    uint8_t bad_status[6] = {'E', 'M', PC_EDGE_MESH_VERSION, 5, 0, 0};
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_MALFORMED, edge_mesh_parse_response(bad_status, 6, &eoff, &elen));
    uint8_t zero_len[6] = {'E', 'M', PC_EDGE_MESH_VERSION, 1, 0, 0}; // HIT with entry_len 0
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_MALFORMED, edge_mesh_parse_response(zero_len, 6, &eoff, &elen));
}

// --- scripted mock peer transport (delivers a canned response) -----------------------------------
typedef struct
{
    const uint8_t *data;
    size_t len, cursor, throttle;
    proto_bool closes;
    int open_ret;
    proto_bool send_ok;
    int reads; ///< how many times the engine asked the transport for bytes
} MockPeer;
static int p_open(void *c, const char *h, uint16_t p, uint32_t t)
{
    (void)h;
    (void)p;
    (void)t;
    return ((MockPeer *)c)->open_ret;
}
static proto_bool p_send(void *c, int cid, const void *d, size_t l)
{
    (void)cid;
    (void)d;
    (void)l;
    return ((MockPeer *)c)->send_ok;
}
static size_t p_read(void *c, int cid, uint8_t *buf, size_t cap)
{
    (void)cid;
    MockPeer *m = (MockPeer *)c;
    m->reads++;
    size_t avail = m->len - m->cursor;
    size_t n = avail < cap ? avail : cap;
    if (m->throttle && n > m->throttle)
    {
        n = m->throttle;
    }
    memcpy(buf, m->data + m->cursor, n);
    m->cursor += n;
    return n;
}
static proto_bool p_closed(void *c, int cid)
{
    (void)cid;
    MockPeer *m = (MockPeer *)c;
    return m->closes && m->cursor >= m->len;
}
static void p_close(void *c, int cid)
{
    (void)c;
    (void)cid;
}
static EdgeFetchTransport peer_transport(MockPeer *m)
{
    EdgeFetchTransport t;
    t.open = p_open;
    t.send = p_send;
    t.read = p_read;
    t.closed = p_closed;
    t.close = p_close;
    t.ctx = m;
    return t;
}
static EdgeMeshStatus run_mesh(EdgeMeshFetch *m, const EdgeFetchTransport *t, uint32_t now)
{
    for (int i = 0; i < 100000 && m->st == EDGE_MESH_STATUS_PENDING; i++)
    {
        edge_mesh_fetch_pump(m, t, now);
    }
    return m->st;
}

static uint8_t g_rbuf[PC_EDGE_MESH_RESP_MAX]; // caller-owned response accumulation buffer for the requester engine

// --- requester engine ----------------------------------------------------------------------------
static void test_requester_hit()
{
    uint8_t frame[PC_EDGE_MESH_ENTRY_MAX];
    size_t fn = 0;
    build_hit_frame(frame, sizeof(frame), &fn, 3);
    uint8_t resp[PC_EDGE_MESH_RESP_MAX];
    size_t rn = edge_mesh_build_response(PROTO_TRUE, frame, fn, resp, sizeof(resp));

    MockPeer m = {resp, rn, 0, 4, PROTO_TRUE, 7, PROTO_TRUE}; // 4 bytes/read -> exercises multi-pump accumulation
    EdgeFetchTransport t = peer_transport(&m);
    EdgeMeshFetch mf;
    edge_mesh_fetch_begin(&mf, &t, "peer", 7645, resp, 8, g_rbuf, sizeof(g_rbuf), 1000); // request ignored by the mock
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_HIT, run_mesh(&mf, &t, 1000));
    TEST_ASSERT_EQUAL_UINT(fn, mf.entry_len);

    EdgeEntry got;
    memset(&got, 0, sizeof(got));
    TEST_ASSERT_TRUE(edge_mesh_deserialize_entry(mf.buf + mf.entry_off, mf.entry_len, &got, 2000));
    TEST_ASSERT_EQUAL_MEMORY(frame, mf.buf + mf.entry_off, fn);
    TEST_ASSERT_EQUAL_UINT(7, got.body_len);
    TEST_ASSERT_EQUAL_MEMORY("payload", got.body, 7);
    edge_mesh_fetch_end(&mf, &t);
}

static void test_requester_miss()
{
    uint8_t resp[8];
    size_t rn = edge_mesh_build_response(PROTO_FALSE, NULL, 0, resp, sizeof(resp));
    MockPeer m = {resp, rn, 0, 0, PROTO_TRUE, 7, PROTO_TRUE};
    EdgeFetchTransport t = peer_transport(&m);
    EdgeMeshFetch mf;
    edge_mesh_fetch_begin(&mf, &t, "peer", 7645, resp, 8, g_rbuf, sizeof(g_rbuf), 1000);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_MISS, run_mesh(&mf, &t, 1000));
}

static void test_requester_open_fail()
{
    MockPeer m = {(const uint8_t *)"", 0, 0, 0, PROTO_FALSE, -1, PROTO_TRUE};
    EdgeFetchTransport t = peer_transport(&m);
    EdgeMeshFetch mf;
    edge_mesh_fetch_begin(&mf, &t, "peer", 7645, (const uint8_t *)"x", 1, g_rbuf, sizeof(g_rbuf), 1000);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, mf.st);
}

static void test_requester_send_fail()
{
    MockPeer m = {(const uint8_t *)"", 0, 0, 0, PROTO_FALSE, 7, PROTO_FALSE}; // open ok, send fails
    EdgeFetchTransport t = peer_transport(&m);
    EdgeMeshFetch mf;
    edge_mesh_fetch_begin(&mf, &t, "peer", 7645, (const uint8_t *)"x", 1, g_rbuf, sizeof(g_rbuf), 1000);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, mf.st);
}

static void test_requester_timeout()
{
    // A truncated frame that never completes and the peer never closes -> deadline drives FAILED.
    uint8_t partial[4] = {'E', 'M', PC_EDGE_MESH_VERSION, 1}; // HIT header, no entry
    MockPeer m = {partial, sizeof(partial), 0, 0, PROTO_FALSE, 7, PROTO_TRUE};
    EdgeFetchTransport t = peer_transport(&m);
    EdgeMeshFetch mf;
    edge_mesh_fetch_begin(&mf, &t, "peer", 7645, (const uint8_t *)"x", 1, g_rbuf, sizeof(g_rbuf), 1000);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_PENDING, edge_mesh_fetch_pump(&mf, &t, 1000));
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, edge_mesh_fetch_pump(&mf, &t, 1000 + PC_MESH_QUERY_MS + 1));
}

static void test_requester_peer_closed_early()
{
    uint8_t partial[5] = {'E', 'M', PC_EDGE_MESH_VERSION, 1, 0}; // incomplete entry length prefix
    MockPeer m = {partial, sizeof(partial), 0, 0, PROTO_TRUE, 7, PROTO_TRUE};
    EdgeFetchTransport t = peer_transport(&m);
    EdgeMeshFetch mf;
    edge_mesh_fetch_begin(&mf, &t, "peer", 7645, (const uint8_t *)"x", 1, g_rbuf, sizeof(g_rbuf), 1000);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, run_mesh(&mf, &t, 1000));
}

static void test_requester_malformed()
{
    uint8_t junk[6] = {'X', 'X', 0, 0, 0, 0};
    MockPeer m = {junk, sizeof(junk), 0, 0, PROTO_TRUE, 7, PROTO_TRUE};
    EdgeFetchTransport t = peer_transport(&m);
    EdgeMeshFetch mf;
    edge_mesh_fetch_begin(&mf, &t, "peer", 7645, (const uint8_t *)"x", 1, g_rbuf, sizeof(g_rbuf), 1000);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, run_mesh(&mf, &t, 1000));
}

// --- magic / version validation across short prefixes --------------------------------------------
static void test_parse_short_and_bad_prefixes()
{
    size_t eoff = 0;
    size_t elen = 0;
    // A prefix shorter than the magic cannot be judged yet - it accumulates.
    uint8_t ok[6] = {'E', 'M', PC_EDGE_MESH_VERSION, 0, 0, 0};
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE, edge_mesh_parse_response(ok, 0, &eoff, &elen));
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE, edge_mesh_parse_response(ok, 1, &eoff, &elen));
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE, edge_mesh_parse_response(ok, 2, &eoff, &elen));
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE, edge_mesh_parse_response(ok, 3, &eoff, &elen));
    // The second magic byte is only checkable once two bytes have arrived.
    uint8_t m1[4] = {'E', 'X', PC_EDGE_MESH_VERSION, 0};
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE, edge_mesh_parse_response(m1, 1, &eoff, &elen));
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_MALFORMED, edge_mesh_parse_response(m1, 2, &eoff, &elen));
    // Likewise the version byte at three.
    uint8_t v1[4] = {'E', 'M', (uint8_t)(PC_EDGE_MESH_VERSION + 1), 0};
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE, edge_mesh_parse_response(v1, 2, &eoff, &elen));
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_MALFORMED, edge_mesh_parse_response(v1, 3, &eoff, &elen));
}

// --- request frame guards ------------------------------------------------------------------------
static void test_build_request_guards()
{
    uint8_t digest[32];
    memset(digest, 5, sizeof(digest));
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/g");
    uint8_t out[PC_EDGE_MESH_REQ_MAX];
    TEST_ASSERT_EQUAL_UINT(0, edge_mesh_build_request(NULL, canon, "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0, edge_mesh_build_request(digest, NULL, "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0, edge_mesh_build_request(digest, canon, "", NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0, edge_mesh_build_request(digest, canon, "", out, 8)); // will not fit
    // A null header snapshot is treated as an empty one.
    size_t n = edge_mesh_build_request(digest, canon, NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(2 + 1 + 1 + 32 + 2 + strlen(canon) + 2, n);
}

static void test_parse_request_incomplete_at_every_field()
{
    uint8_t digest[32];
    memset(digest, 0x5A, sizeof(digest));
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/fields");
    uint8_t req[PC_EDGE_MESH_REQ_MAX];
    size_t n = edge_mesh_build_request(digest, canon,
                                       "A\x1e"
                                       "b\x1f",
                                       req, sizeof(req));
    TEST_ASSERT_TRUE(n > 0);

    uint8_t d2[32];
    char c2[PC_EDGE_KEY_MAX];
    char v2[PC_MESH_HDRS_MAX];
    // Every truncation of a valid frame accumulates: never MALFORMED, never a partial fill.
    for (size_t l = 4; l < n; l++)
    {
        TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE,
                          edge_mesh_parse_request(req, l, d2, c2, sizeof(c2), v2, sizeof(v2)));
    }
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_HIT, edge_mesh_parse_request(req, n, d2, c2, sizeof(c2), v2, sizeof(v2)));
}

static void test_parse_request_hdrs_too_long_for_destination()
{
    uint8_t digest[32];
    memset(digest, 1, sizeof(digest));
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/h");
    char hdrs[64];
    memset(hdrs, 'h', sizeof(hdrs) - 1);
    hdrs[sizeof(hdrs) - 1] = '\0';
    uint8_t req[PC_EDGE_MESH_REQ_MAX];
    size_t n = edge_mesh_build_request(digest, canon, hdrs, req, sizeof(req));
    TEST_ASSERT_TRUE(n > 0);

    uint8_t d2[32];
    char c2[PC_EDGE_KEY_MAX];
    char v2[16]; // too small for the 63-byte snapshot -> malformed, not truncated
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_MALFORMED, edge_mesh_parse_request(req, n, d2, c2, sizeof(c2), v2, sizeof(v2)));
}

static void test_parse_request_null_outputs()
{
    uint8_t digest[32];
    memset(digest, 9, sizeof(digest));
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/n");
    uint8_t req[PC_EDGE_MESH_REQ_MAX];
    size_t n = edge_mesh_build_request(digest, canon, "x", req, sizeof(req));
    // A peer that only needs to know the frame is whole can pass null outputs.
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_HIT,
                      edge_mesh_parse_request(req, n, NULL, NULL, PC_EDGE_KEY_MAX, NULL, PC_MESH_HDRS_MAX));
}

// --- entry frame guards --------------------------------------------------------------------------
static void test_serialize_entry_guards_and_clamps()
{
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/s");
    EdgeEntry e;
    fill_entry(&e, canon, "\"s\"", (const uint8_t *)"body", 4);
    uint8_t out[PC_EDGE_MESH_ENTRY_MAX];
    TEST_ASSERT_EQUAL_UINT(0, edge_mesh_serialize_entry(NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0, edge_mesh_serialize_entry(&e, 0, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0, edge_mesh_serialize_entry(&e, 0, out, PC_EDGE_MESH_TRAILER - 1)); // no trailer room
    TEST_ASSERT_EQUAL_UINT(0, edge_mesh_serialize_entry(&e, 0, out, PC_EDGE_MESH_TRAILER + 4)); // no content room

    EdgeEntry got;
    // A negative age is clamped to zero on the wire (an unsigned field).
    size_t n = edge_mesh_serialize_entry(&e, -5, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    memset(&got, 0, sizeof(got));
    TEST_ASSERT_TRUE(edge_mesh_deserialize_entry(out, n, &got, 1234));
    TEST_ASSERT_EQUAL_INT(0, got.initial_age);

    // So are a negative lifetime and a negative stored Age header.
    e.lifetime_s = -1;
    e.age_hdr = -2;
    n = edge_mesh_serialize_entry(&e, 0, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    memset(&got, 0, sizeof(got));
    TEST_ASSERT_TRUE(edge_mesh_deserialize_entry(out, n, &got, 1234));
    TEST_ASSERT_EQUAL_INT(0, got.lifetime_s);
    TEST_ASSERT_EQUAL_INT(0, got.age_hdr);
}

static void test_deserialize_entry_guards()
{
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/d");
    EdgeEntry e;
    fill_entry(&e, canon, "\"d\"", (const uint8_t *)"xy", 2);
    uint8_t frame[PC_EDGE_MESH_ENTRY_MAX];
    size_t n = edge_mesh_serialize_entry(&e, 1, frame, sizeof(frame));
    TEST_ASSERT_TRUE(n > 0);

    EdgeEntry got;
    memset(&got, 0, sizeof(got));
    TEST_ASSERT_FALSE(edge_mesh_deserialize_entry(NULL, n, &got, 0));
    TEST_ASSERT_FALSE(edge_mesh_deserialize_entry(frame, n, NULL, 0));
    TEST_ASSERT_FALSE(edge_mesh_deserialize_entry(frame, PC_EDGE_MESH_TRAILER - 1, &got, 0));
    // A good trailer in front of a corrupt content body fails the whole frame.
    uint8_t save = frame[PC_EDGE_MESH_TRAILER];
    frame[PC_EDGE_MESH_TRAILER] = 0x7F; // not the entry-serialization version
    TEST_ASSERT_FALSE(edge_mesh_deserialize_entry(frame, n, &got, 0));
    frame[PC_EDGE_MESH_TRAILER] = save;
    TEST_ASSERT_TRUE(edge_mesh_deserialize_entry(frame, n, &got, 0));
}

// --- response frame guards -----------------------------------------------------------------------
static void test_build_response_guards()
{
    uint8_t out[64];
    uint8_t entry[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_EQUAL_UINT(0, edge_mesh_build_response(PROTO_FALSE, NULL, 0, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0, edge_mesh_build_response(PROTO_FALSE, NULL, 0, out, 3));          // no header room
    TEST_ASSERT_EQUAL_UINT(0, edge_mesh_build_response(PROTO_TRUE, NULL, 8, out, sizeof(out))); // hit without an entry
    TEST_ASSERT_EQUAL_UINT(0,
                           edge_mesh_build_response(PROTO_TRUE, entry, 0, out, sizeof(out))); // hit with an empty one
    TEST_ASSERT_EQUAL_UINT(
        0, edge_mesh_build_response(PROTO_TRUE, entry, 0x10000, out, sizeof(out))); // past the u16 length
    TEST_ASSERT_EQUAL_UINT(0, edge_mesh_build_response(PROTO_TRUE, entry, 100, out, sizeof(out))); // will not fit cap
    TEST_ASSERT_EQUAL_UINT(4 + 2 + 8, edge_mesh_build_response(PROTO_TRUE, entry, 8, out, sizeof(out)));
}

static void test_parse_response_null_outputs()
{
    uint8_t frame[PC_EDGE_MESH_ENTRY_MAX];
    size_t fn = 0;
    build_hit_frame(frame, sizeof(frame), &fn, 0);
    TEST_ASSERT_TRUE(fn > 0);
    uint8_t resp[PC_EDGE_MESH_RESP_MAX];
    size_t rn = edge_mesh_build_response(PROTO_TRUE, frame, fn, resp, sizeof(resp));
    TEST_ASSERT_TRUE(rn > 0);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_HIT, edge_mesh_parse_response(resp, rn, NULL, NULL));
}

// --- requester engine guards ---------------------------------------------------------------------
static void test_requester_begin_argument_guards()
{
    MockPeer m = {(const uint8_t *)"", 0, 0, 0, PROTO_FALSE, 7, PROTO_TRUE};
    EdgeFetchTransport t = peer_transport(&m);
    const uint8_t req[1] = {'x'};
    EdgeMeshFetch mf;
    edge_mesh_fetch_begin(&mf, NULL, "peer", 1, req, 1, g_rbuf, sizeof(g_rbuf), 1000);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, mf.st);
    edge_mesh_fetch_begin(&mf, &t, NULL, 1, req, 1, g_rbuf, sizeof(g_rbuf), 1000);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, mf.st);
    edge_mesh_fetch_begin(&mf, &t, "peer", 1, NULL, 1, g_rbuf, sizeof(g_rbuf), 1000);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, mf.st);
    edge_mesh_fetch_begin(&mf, &t, "peer", 1, req, 0, g_rbuf, sizeof(g_rbuf), 1000);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, mf.st);
    edge_mesh_fetch_begin(&mf, &t, "peer", 1, req, 1, NULL, sizeof(g_rbuf), 1000);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, mf.st);
    // An accumulation buffer that cannot hold a worst-case response is refused up front.
    edge_mesh_fetch_begin(&mf, &t, "peer", 1, req, 1, g_rbuf, PC_EDGE_MESH_RESP_MAX - 1, 1000);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, mf.st);
    TEST_ASSERT_EQUAL_INT(-1, mf.cid); // no connection was ever opened
}

static void test_requester_pump_guards()
{
    MockPeer m = {(const uint8_t *)"", 0, 0, 0, PROTO_FALSE, 7, PROTO_TRUE};
    EdgeFetchTransport t = peer_transport(&m);
    const uint8_t req[1] = {'x'};
    EdgeMeshFetch mf;
    edge_mesh_fetch_begin(&mf, &t, "peer", 1, req, 1, g_rbuf, sizeof(g_rbuf), 1000);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_PENDING, mf.st);
    // No transport while pending: fail rather than dereference it.
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, edge_mesh_fetch_pump(&mf, NULL, 1000));
    // Already settled: pumping again just reports the status, it does not read again.
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, edge_mesh_fetch_pump(&mf, &t, 1000));
    TEST_ASSERT_EQUAL_UINT(0, m.cursor);

    // Pending with no connection handle.
    edge_mesh_fetch_begin(&mf, &t, "peer", 1, req, 1, g_rbuf, sizeof(g_rbuf), 1000);
    mf.cid = -1;
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, edge_mesh_fetch_pump(&mf, &t, 1000));
}

static uint8_t g_flood[PC_EDGE_MESH_RESP_MAX];

static void test_requester_buffer_full_without_a_frame()
{
    // A HIT header announcing a 64 KiB entry: the accumulation buffer fills long before the frame can
    // complete, which must fail the query instead of spinning on a never-satisfiable read.
    memset(g_flood, 0xAA, sizeof(g_flood));
    g_flood[0] = PC_EDGE_MESH_MAGIC0;
    g_flood[1] = PC_EDGE_MESH_MAGIC1;
    g_flood[2] = PC_EDGE_MESH_VERSION;
    g_flood[3] = 1;
    g_flood[4] = 0xFF;
    g_flood[5] = 0xFF; // entry_len = 65535
    MockPeer m = {g_flood, sizeof(g_flood), 0, 0, PROTO_FALSE, 7, PROTO_TRUE};
    EdgeFetchTransport t = peer_transport(&m);
    EdgeMeshFetch mf;
    edge_mesh_fetch_begin(&mf, &t, "peer", 7645, (const uint8_t *)"x", 1, g_rbuf, sizeof(g_rbuf), 1000);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, edge_mesh_fetch_pump(&mf, &t, 1000));
    TEST_ASSERT_EQUAL_UINT(sizeof(g_rbuf), mf.got);
}

static void test_requester_pump_skips_the_read_when_the_buffer_is_already_full()
{
    // The accumulation window is buf + got .. buf + cap. With got already at cap the pump must skip the
    // read outright rather than call the transport with a zero-length window, and settle the query as
    // FAILED (buffer exhausted with no complete frame). Pins that nothing can ever be written past cap.
    static uint8_t hdr[6] = {'E', 'M', PC_EDGE_MESH_VERSION, 1, 0xFF, 0xFF}; // HIT announcing a 64 KiB entry
    MockPeer m = {hdr, sizeof(hdr), 0, 0, PROTO_FALSE, 7, PROTO_TRUE};
    EdgeFetchTransport t = peer_transport(&m);
    EdgeMeshFetch mf;
    edge_mesh_fetch_begin(&mf, &t, "peer", 7645, (const uint8_t *)"x", 1, g_rbuf, sizeof(g_rbuf), 1000);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_PENDING, mf.st);

    memset(g_rbuf, 0, sizeof(g_rbuf));
    memcpy(g_rbuf, hdr, sizeof(hdr));
    mf.got = mf.cap; // the caller-owned buffer is already full from earlier pumps
    m.reads = 0;
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, edge_mesh_fetch_pump(&mf, &t, 1000));
    TEST_ASSERT_EQUAL_INT(0, m.reads);      // no read was issued at all
    TEST_ASSERT_EQUAL_UINT(mf.cap, mf.got); // and the accumulated count did not move past cap
    TEST_ASSERT_EQUAL_UINT(0, m.cursor);    // the peer's bytes were left where they were
}

static void test_requester_end_without_a_connection()
{
    MockPeer m = {(const uint8_t *)"", 0, 0, 0, PROTO_FALSE, -1, PROTO_TRUE}; // open fails
    EdgeFetchTransport t = peer_transport(&m);
    EdgeMeshFetch mf;
    edge_mesh_fetch_begin(&mf, &t, "peer", 7645, (const uint8_t *)"x", 1, g_rbuf, sizeof(g_rbuf), 1000);
    TEST_ASSERT_EQUAL_INT(-1, mf.cid); // nothing to release
    edge_mesh_fetch_end(&mf, &t);
    TEST_ASSERT_EQUAL_INT(-1, mf.cid);

    // A live handle with no transport is still cleared (idempotent release).
    m.open_ret = 7;
    edge_mesh_fetch_begin(&mf, &t, "peer", 7645, (const uint8_t *)"x", 1, g_rbuf, sizeof(g_rbuf), 1000);
    TEST_ASSERT_EQUAL_INT(7, mf.cid);
    edge_mesh_fetch_end(&mf, NULL);
    TEST_ASSERT_EQUAL_INT(-1, mf.cid);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_request_roundtrip);
    RUN_TEST(test_request_incomplete_then_complete);
    RUN_TEST(test_request_malformed);
    RUN_TEST(test_entry_frame_roundtrip);
    RUN_TEST(test_age_propagation);
    RUN_TEST(test_response_roundtrip);
    RUN_TEST(test_response_malformed);
    RUN_TEST(test_requester_hit);
    RUN_TEST(test_requester_miss);
    RUN_TEST(test_requester_open_fail);
    RUN_TEST(test_requester_send_fail);
    RUN_TEST(test_requester_timeout);
    RUN_TEST(test_requester_peer_closed_early);
    RUN_TEST(test_requester_malformed);
    RUN_TEST(test_parse_short_and_bad_prefixes);
    RUN_TEST(test_build_request_guards);
    RUN_TEST(test_parse_request_incomplete_at_every_field);
    RUN_TEST(test_parse_request_hdrs_too_long_for_destination);
    RUN_TEST(test_parse_request_null_outputs);
    RUN_TEST(test_serialize_entry_guards_and_clamps);
    RUN_TEST(test_deserialize_entry_guards);
    RUN_TEST(test_build_response_guards);
    RUN_TEST(test_parse_response_null_outputs);
    RUN_TEST(test_requester_begin_argument_guards);
    RUN_TEST(test_requester_pump_guards);
    RUN_TEST(test_requester_buffer_full_without_a_frame);
    RUN_TEST(test_requester_pump_skips_the_read_when_the_buffer_is_already_full);
    RUN_TEST(test_requester_end_without_a_connection);
    return UNITY_END();
}
