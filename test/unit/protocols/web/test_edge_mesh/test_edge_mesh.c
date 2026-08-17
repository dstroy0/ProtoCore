// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/presentation/http/httpcache/httpcache.h"
#include "server/web/edge_cache/edge_cache.h"
#include "server/web/edge_cache/edge_cache_sd.h"
#include "server/web/edge_cache/edge_fetch.h"
#include "server/web/edge_cache/edge_mesh.h"
#include "shared/http_date/http_date.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unity.h>

static uint8_t edge_cache_work[16]; // the borrow an entry takes; EdgeCache never reads it

static uint8_t edge_mesh_work[16]; // the borrow an entry takes; EdgeMesh never reads it

static uint8_t tw[4096];

void setUp()
{
}
void tearDown()
{
}

static void mkcanon(char *out, size_t cap, const char *path)
{
    snprintf(out, cap, "GET\nexample.com\n%s", path);
}

static void fill_entry(EdgeEntry *e, const char *canon, const char *etag, const uint8_t *body, uint16_t body_len)
{
    memset(e, 0, sizeof(*e));
    strncpy(e->key, canon, sizeof(e->key) - 1);
    EdgeCache.key_digest_args.digest_work = tw;
    EdgeCache.key_digest_args.canon = e->key;
    EdgeCache.key_digest_args.len = strlen(e->key);
    EdgeCache.key_digest_args.digest = e->digest;
    EdgeCache.key_digest(edge_cache_work);
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

static void test_request_roundtrip()
{
    uint8_t digest[32];
    for (int i = 0; i < 32; i++)
    {
        digest[i] = (uint8_t)(i * 7 + 1);
    }
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/app.js?v=2");
    const char *vary = "Accept-Encoding\x1egzip\x1f";

    uint8_t req[PROTOCORE_EDGE_MESH_REQ_MAX];
    EdgeMesh.build_request_args.digest = digest;
    EdgeMesh.build_request_args.canon = canon;
    EdgeMesh.build_request_args.req_hdrs = vary;
    EdgeMesh.build_request_args.out = req;
    EdgeMesh.build_request_args.cap = sizeof(req);
    EdgeMesh.build_request(edge_mesh_work);
    size_t n = EdgeMesh.n;
    TEST_ASSERT_TRUE(n > 0);

    uint8_t d2[32];
    char c2[PROTOCORE_EDGE_KEY_MAX];
    char v2[PROTOCORE_MESH_HDRS_MAX];
    EdgeMesh.parse_request_args.buf = req;
    EdgeMesh.parse_request_args.len = n;
    EdgeMesh.parse_request_args.digest_out = d2;
    EdgeMesh.parse_request_args.canon_out = c2;
    EdgeMesh.parse_request_args.canon_cap = sizeof(c2);
    EdgeMesh.parse_request_args.hdrs_out = v2;
    EdgeMesh.parse_request_args.hdrs_cap = sizeof(v2);
    EdgeMesh.parse_request(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_HIT, EdgeMesh.parse);
    TEST_ASSERT_EQUAL_MEMORY(digest, d2, 32);
    TEST_ASSERT_EQUAL_STRING(canon, c2);
    TEST_ASSERT_EQUAL_STRING(vary, v2);
}

static void test_request_incomplete_then_complete()
{
    uint8_t digest[32];
    memset(digest, 0xAB, 32);
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/x");
    uint8_t req[PROTOCORE_EDGE_MESH_REQ_MAX];
    EdgeMesh.build_request_args.digest = digest;
    EdgeMesh.build_request_args.canon = canon;
    EdgeMesh.build_request_args.req_hdrs = "Accept\x1e*\x1f";
    EdgeMesh.build_request_args.out = req;
    EdgeMesh.build_request_args.cap = sizeof(req);
    EdgeMesh.build_request(edge_mesh_work);
    size_t n = EdgeMesh.n;
    TEST_ASSERT_TRUE(n > 3);

    uint8_t d2[32];
    char c2[PROTOCORE_EDGE_KEY_MAX];
    char v2[PROTOCORE_MESH_HDRS_MAX];

    EdgeMesh.parse_request_args.buf = req;
    EdgeMesh.parse_request_args.len = 2;
    EdgeMesh.parse_request_args.digest_out = d2;
    EdgeMesh.parse_request_args.canon_out = c2;
    EdgeMesh.parse_request_args.canon_cap = sizeof(c2);
    EdgeMesh.parse_request_args.hdrs_out = v2;
    EdgeMesh.parse_request_args.hdrs_cap = sizeof(v2);
    EdgeMesh.parse_request(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE, EdgeMesh.parse);
    EdgeMesh.parse_request_args.buf = req;
    EdgeMesh.parse_request_args.len = n - 1;
    EdgeMesh.parse_request_args.digest_out = d2;
    EdgeMesh.parse_request_args.canon_out = c2;
    EdgeMesh.parse_request_args.canon_cap = sizeof(c2);
    EdgeMesh.parse_request_args.hdrs_out = v2;
    EdgeMesh.parse_request_args.hdrs_cap = sizeof(v2);
    EdgeMesh.parse_request(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE, EdgeMesh.parse);
    EdgeMesh.parse_request_args.buf = req;
    EdgeMesh.parse_request_args.len = n;
    EdgeMesh.parse_request_args.digest_out = d2;
    EdgeMesh.parse_request_args.canon_out = c2;
    EdgeMesh.parse_request_args.canon_cap = sizeof(c2);
    EdgeMesh.parse_request_args.hdrs_out = v2;
    EdgeMesh.parse_request_args.hdrs_cap = sizeof(v2);
    EdgeMesh.parse_request(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_HIT, EdgeMesh.parse);
}

static void test_request_malformed()
{
    uint8_t digest[32];
    memset(digest, 1, 32);
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/y");
    uint8_t req[PROTOCORE_EDGE_MESH_REQ_MAX];
    EdgeMesh.build_request_args.digest = digest;
    EdgeMesh.build_request_args.canon = canon;
    EdgeMesh.build_request_args.req_hdrs = "";
    EdgeMesh.build_request_args.out = req;
    EdgeMesh.build_request_args.cap = sizeof(req);
    EdgeMesh.build_request(edge_mesh_work);
    size_t n = EdgeMesh.n;

    uint8_t d2[32];
    char c2[PROTOCORE_EDGE_KEY_MAX];
    char v2[PROTOCORE_MESH_HDRS_MAX];
    uint8_t bad = req[0];
    req[0] = 'X';
    EdgeMesh.parse_request_args.buf = req;
    EdgeMesh.parse_request_args.len = n;
    EdgeMesh.parse_request_args.digest_out = d2;
    EdgeMesh.parse_request_args.canon_out = c2;
    EdgeMesh.parse_request_args.canon_cap = sizeof(c2);
    EdgeMesh.parse_request_args.hdrs_out = v2;
    EdgeMesh.parse_request_args.hdrs_cap = sizeof(v2);
    EdgeMesh.parse_request(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_MALFORMED, EdgeMesh.parse);
    req[0] = bad;
    req[3] = 9;
    EdgeMesh.parse_request_args.buf = req;
    EdgeMesh.parse_request_args.len = n;
    EdgeMesh.parse_request_args.digest_out = d2;
    EdgeMesh.parse_request_args.canon_out = c2;
    EdgeMesh.parse_request_args.canon_cap = sizeof(c2);
    EdgeMesh.parse_request_args.hdrs_out = v2;
    EdgeMesh.parse_request_args.hdrs_cap = sizeof(v2);
    EdgeMesh.parse_request(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_MALFORMED, EdgeMesh.parse);
    req[3] = PROTOCORE_EDGE_MESH_OP_GET;

    EdgeMesh.parse_request_args.buf = req;
    EdgeMesh.parse_request_args.len = n;
    EdgeMesh.parse_request_args.digest_out = d2;
    EdgeMesh.parse_request_args.canon_out = c2;
    EdgeMesh.parse_request_args.canon_cap = 4;
    EdgeMesh.parse_request_args.hdrs_out = v2;
    EdgeMesh.parse_request_args.hdrs_cap = sizeof(v2);
    EdgeMesh.parse_request(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_MALFORMED, EdgeMesh.parse);
}

static void test_entry_frame_roundtrip()
{
    static const uint8_t body[] = {0x00, 0xFF, 0x10, 'a', 0x00, 'z', 0x7F, 0x80};
    char canon[PROTOCORE_EDGE_KEY_MAX];
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
    uint8_t frame[PROTOCORE_EDGE_MESH_ENTRY_MAX];
    EdgeMesh.serialize_entry_args.e = &in;
    EdgeMesh.serialize_entry_args.current_age = cur;
    EdgeMesh.serialize_entry_args.out = frame;
    EdgeMesh.serialize_entry_args.cap = sizeof(frame);
    EdgeMesh.serialize_entry(edge_mesh_work);
    size_t n = EdgeMesh.n;
    TEST_ASSERT_TRUE(n > 0);

    EdgeEntry out;
    memset(&out, 0, sizeof(out));
    uint32_t now2 = 50000;
    EdgeMesh.deserialize_entry_args.entry_buf = tw;
    EdgeMesh.deserialize_entry_args.buf = frame;
    EdgeMesh.deserialize_entry_args.len = n;
    EdgeMesh.deserialize_entry_args.e = &out;
    EdgeMesh.deserialize_entry_args.now_ms = now2;
    EdgeMesh.deserialize_entry(edge_mesh_work);
    TEST_ASSERT_TRUE(EdgeMesh.ok);

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
    TEST_ASSERT_EQUAL_MEMORY(in.digest, out.digest, 32);

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
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/z");
    EdgeEntry peer;
    fill_entry(&peer, canon, "\"v1\"", body, sizeof(body));
    peer.lifetime_s = 60;
    peer.initial_age = 5;
    peer.insert_ms = 10000;

    uint32_t send_now = 10000 + 12000;
    EdgeCache.current_age_args.initial_age = peer.initial_age;
    EdgeCache.current_age_args.insert_ms = peer.insert_ms;
    EdgeCache.current_age_args.now_ms = send_now;
    EdgeCache.current_age(edge_cache_work);
    long cur = EdgeCache.secs;
    TEST_ASSERT_EQUAL_INT(17, cur);

    uint8_t frame[PROTOCORE_EDGE_MESH_ENTRY_MAX];
    EdgeMesh.serialize_entry_args.e = &peer;
    EdgeMesh.serialize_entry_args.current_age = cur;
    EdgeMesh.serialize_entry_args.out = frame;
    EdgeMesh.serialize_entry_args.cap = sizeof(frame);
    EdgeMesh.serialize_entry(edge_mesh_work);
    size_t n = EdgeMesh.n;
    EdgeEntry recv;
    memset(&recv, 0, sizeof(recv));
    uint32_t recv_now = 999000;
    EdgeMesh.deserialize_entry_args.entry_buf = tw;
    EdgeMesh.deserialize_entry_args.buf = frame;
    EdgeMesh.deserialize_entry_args.len = n;
    EdgeMesh.deserialize_entry_args.e = &recv;
    EdgeMesh.deserialize_entry_args.now_ms = recv_now;
    EdgeMesh.deserialize_entry(edge_mesh_work);
    TEST_ASSERT_TRUE(EdgeMesh.ok);

    EdgeCache.current_age_args.initial_age = recv.initial_age;
    EdgeCache.current_age_args.insert_ms = recv.insert_ms;
    EdgeCache.current_age_args.now_ms = recv_now;
    EdgeCache.current_age(edge_cache_work);
    TEST_ASSERT_EQUAL_INT(17, EdgeCache.secs);
    EdgeCache.current_age_args.initial_age = recv.initial_age;
    EdgeCache.current_age_args.insert_ms = recv.insert_ms;
    EdgeCache.current_age_args.now_ms = recv_now + 30000;
    EdgeCache.current_age(edge_cache_work);
    TEST_ASSERT_EQUAL_INT(17 + 30, EdgeCache.secs);

    EdgeCache.entry_fresh_args.e = &recv;
    EdgeCache.entry_fresh_args.now_ms = recv_now;
    EdgeCache.entry_fresh(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    EdgeCache.entry_fresh_args.e = &recv;
    EdgeCache.entry_fresh_args.now_ms = recv_now + 42000;
    EdgeCache.entry_fresh(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    EdgeCache.entry_fresh_args.e = &recv;
    EdgeCache.entry_fresh_args.now_ms = recv_now + 44000;
    EdgeCache.entry_fresh(edge_cache_work);
    TEST_ASSERT_FALSE(EdgeCache.ok);
}

static void build_hit_frame(uint8_t *frame, size_t cap, size_t *fn_out, long current_age)
{
    static const uint8_t body[] = {'p', 'a', 'y', 'l', 'o', 'a', 'd'};
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/r");
    EdgeEntry e;
    fill_entry(&e, canon, "\"rv\"", body, sizeof(body));
    EdgeMesh.serialize_entry_args.e = &e;
    EdgeMesh.serialize_entry_args.current_age = current_age;
    EdgeMesh.serialize_entry_args.out = frame;
    EdgeMesh.serialize_entry_args.cap = cap;
    EdgeMesh.serialize_entry(edge_mesh_work);
    *fn_out = EdgeMesh.n;
}

static void test_response_roundtrip()
{
    uint8_t frame[PROTOCORE_EDGE_MESH_ENTRY_MAX];
    size_t fn = 0;
    build_hit_frame(frame, sizeof(frame), &fn, 0);
    TEST_ASSERT_TRUE(fn > 0);

    uint8_t resp[PROTOCORE_EDGE_MESH_RESP_MAX];
    EdgeMesh.build_response_args.hit = PROTO_TRUE;
    EdgeMesh.build_response_args.entry = frame;
    EdgeMesh.build_response_args.entry_len = fn;
    EdgeMesh.build_response_args.out = resp;
    EdgeMesh.build_response_args.cap = sizeof(resp);
    EdgeMesh.build_response(edge_mesh_work);
    size_t rn = EdgeMesh.n;
    TEST_ASSERT_TRUE(rn > 0);

    size_t eoff = 0;
    size_t elen = 0;
    EdgeMesh.parse_response_args.buf = resp;
    EdgeMesh.parse_response_args.len = rn;
    EdgeMesh.parse_response_args.entry_off = &eoff;
    EdgeMesh.parse_response_args.entry_len = &elen;
    EdgeMesh.parse_response(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_HIT, EdgeMesh.parse);
    TEST_ASSERT_EQUAL_UINT(fn, elen);
    TEST_ASSERT_EQUAL_MEMORY(frame, resp + eoff, fn);

    EdgeMesh.parse_response_args.buf = resp;
    EdgeMesh.parse_response_args.len = 5;
    EdgeMesh.parse_response_args.entry_off = &eoff;
    EdgeMesh.parse_response_args.entry_len = &elen;
    EdgeMesh.parse_response(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE, EdgeMesh.parse);
    EdgeMesh.parse_response_args.buf = resp;
    EdgeMesh.parse_response_args.len = rn - 1;
    EdgeMesh.parse_response_args.entry_off = &eoff;
    EdgeMesh.parse_response_args.entry_len = &elen;
    EdgeMesh.parse_response(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE, EdgeMesh.parse);

    uint8_t miss[8];
    EdgeMesh.build_response_args.hit = PROTO_FALSE;
    EdgeMesh.build_response_args.entry = NULL;
    EdgeMesh.build_response_args.entry_len = 0;
    EdgeMesh.build_response_args.out = miss;
    EdgeMesh.build_response_args.cap = sizeof(miss);
    EdgeMesh.build_response(edge_mesh_work);
    size_t mn = EdgeMesh.n;
    EdgeMesh.parse_response_args.buf = miss;
    EdgeMesh.parse_response_args.len = mn;
    EdgeMesh.parse_response_args.entry_off = &eoff;
    EdgeMesh.parse_response_args.entry_len = &elen;
    EdgeMesh.parse_response(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_MISS, EdgeMesh.parse);
}

static void test_response_malformed()
{
    size_t eoff = 0;
    size_t elen = 0;
    uint8_t bad_magic[6] = {'X', 'M', PROTOCORE_EDGE_MESH_VERSION, 1, 0, 0};
    EdgeMesh.parse_response_args.buf = bad_magic;
    EdgeMesh.parse_response_args.len = 6;
    EdgeMesh.parse_response_args.entry_off = &eoff;
    EdgeMesh.parse_response_args.entry_len = &elen;
    EdgeMesh.parse_response(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_MALFORMED, EdgeMesh.parse);
    uint8_t bad_status[6] = {'E', 'M', PROTOCORE_EDGE_MESH_VERSION, 5, 0, 0};
    EdgeMesh.parse_response_args.buf = bad_status;
    EdgeMesh.parse_response_args.len = 6;
    EdgeMesh.parse_response_args.entry_off = &eoff;
    EdgeMesh.parse_response_args.entry_len = &elen;
    EdgeMesh.parse_response(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_MALFORMED, EdgeMesh.parse);
    uint8_t zero_len[6] = {'E', 'M', PROTOCORE_EDGE_MESH_VERSION, 1, 0, 0};
    EdgeMesh.parse_response_args.buf = zero_len;
    EdgeMesh.parse_response_args.len = 6;
    EdgeMesh.parse_response_args.entry_off = &eoff;
    EdgeMesh.parse_response_args.entry_len = &elen;
    EdgeMesh.parse_response(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_MALFORMED, EdgeMesh.parse);
}

typedef struct
{
    const uint8_t *data;
    size_t len, cursor, throttle;
    proto_bool closes;
    int open_ret;
    proto_bool send_ok;
    int reads;
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

static proto_bool p_connected(void *c, int cid)
{
    (void)cid;
    return ((MockPeer *)c)->open_ret >= 0;
}
static EdgeFetchTransport peer_transport(MockPeer *m)
{
    EdgeFetchTransport t;
    t.open = p_open;
    t.connected = p_connected;
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
        EdgeMesh.fetch_pump_args.m = m;
        EdgeMesh.fetch_pump_args.t = t;
        EdgeMesh.fetch_pump_args.now_ms = now;
        EdgeMesh.fetch_pump(edge_mesh_work);
    }
    return m->st;
}

static uint8_t g_rbuf[PROTOCORE_EDGE_MESH_RESP_MAX];

static void test_requester_hit()
{
    uint8_t frame[PROTOCORE_EDGE_MESH_ENTRY_MAX];
    size_t fn = 0;
    build_hit_frame(frame, sizeof(frame), &fn, 3);
    uint8_t resp[PROTOCORE_EDGE_MESH_RESP_MAX];
    EdgeMesh.build_response_args.hit = PROTO_TRUE;
    EdgeMesh.build_response_args.entry = frame;
    EdgeMesh.build_response_args.entry_len = fn;
    EdgeMesh.build_response_args.out = resp;
    EdgeMesh.build_response_args.cap = sizeof(resp);
    EdgeMesh.build_response(edge_mesh_work);
    size_t rn = EdgeMesh.n;

    MockPeer m = {resp, rn, 0, 4, PROTO_TRUE, 7, PROTO_TRUE};
    EdgeFetchTransport t = peer_transport(&m);
    EdgeMeshFetch mf;
    EdgeMesh.fetch_begin_args.m = &mf;
    EdgeMesh.fetch_begin_args.t = &t;
    EdgeMesh.fetch_begin_args.host = "peer";
    EdgeMesh.fetch_begin_args.port = 7645;
    EdgeMesh.fetch_begin_args.request = resp;
    EdgeMesh.fetch_begin_args.req_len = 8;
    EdgeMesh.fetch_begin_args.buf = g_rbuf;
    EdgeMesh.fetch_begin_args.cap = sizeof(g_rbuf);
    EdgeMesh.fetch_begin_args.now_ms = 1000;
    EdgeMesh.fetch_begin(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_HIT, run_mesh(&mf, &t, 1000));
    TEST_ASSERT_EQUAL_UINT(fn, mf.entry_len);

    EdgeEntry got;
    memset(&got, 0, sizeof(got));
    EdgeMesh.deserialize_entry_args.entry_buf = tw;
    EdgeMesh.deserialize_entry_args.buf = mf.buf + mf.entry_off;
    EdgeMesh.deserialize_entry_args.len = mf.entry_len;
    EdgeMesh.deserialize_entry_args.e = &got;
    EdgeMesh.deserialize_entry_args.now_ms = 2000;
    EdgeMesh.deserialize_entry(edge_mesh_work);
    TEST_ASSERT_TRUE(EdgeMesh.ok);
    TEST_ASSERT_EQUAL_MEMORY(frame, mf.buf + mf.entry_off, fn);
    TEST_ASSERT_EQUAL_UINT(7, got.body_len);
    TEST_ASSERT_EQUAL_MEMORY("payload", got.body, 7);
    EdgeMesh.fetch_end_args.m = &mf;
    EdgeMesh.fetch_end_args.t = &t;
    EdgeMesh.fetch_end(edge_mesh_work);
}

static void test_requester_miss()
{
    uint8_t resp[8];
    EdgeMesh.build_response_args.hit = PROTO_FALSE;
    EdgeMesh.build_response_args.entry = NULL;
    EdgeMesh.build_response_args.entry_len = 0;
    EdgeMesh.build_response_args.out = resp;
    EdgeMesh.build_response_args.cap = sizeof(resp);
    EdgeMesh.build_response(edge_mesh_work);
    size_t rn = EdgeMesh.n;
    MockPeer m = {resp, rn, 0, 0, PROTO_TRUE, 7, PROTO_TRUE};
    EdgeFetchTransport t = peer_transport(&m);
    EdgeMeshFetch mf;
    EdgeMesh.fetch_begin_args.m = &mf;
    EdgeMesh.fetch_begin_args.t = &t;
    EdgeMesh.fetch_begin_args.host = "peer";
    EdgeMesh.fetch_begin_args.port = 7645;
    EdgeMesh.fetch_begin_args.request = resp;
    EdgeMesh.fetch_begin_args.req_len = 8;
    EdgeMesh.fetch_begin_args.buf = g_rbuf;
    EdgeMesh.fetch_begin_args.cap = sizeof(g_rbuf);
    EdgeMesh.fetch_begin_args.now_ms = 1000;
    EdgeMesh.fetch_begin(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_MISS, run_mesh(&mf, &t, 1000));
}

static void test_requester_open_fail()
{
    MockPeer m = {(const uint8_t *)"", 0, 0, 0, PROTO_FALSE, -1, PROTO_TRUE};
    EdgeFetchTransport t = peer_transport(&m);
    EdgeMeshFetch mf;
    EdgeMesh.fetch_begin_args.m = &mf;
    EdgeMesh.fetch_begin_args.t = &t;
    EdgeMesh.fetch_begin_args.host = "peer";
    EdgeMesh.fetch_begin_args.port = 7645;
    EdgeMesh.fetch_begin_args.request = (const uint8_t *)"x";
    EdgeMesh.fetch_begin_args.req_len = 1;
    EdgeMesh.fetch_begin_args.buf = g_rbuf;
    EdgeMesh.fetch_begin_args.cap = sizeof(g_rbuf);
    EdgeMesh.fetch_begin_args.now_ms = 1000;
    EdgeMesh.fetch_begin(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, mf.st);
}

static void test_requester_send_fail()
{
    MockPeer m = {(const uint8_t *)"", 0, 0, 0, PROTO_FALSE, 7, PROTO_FALSE};
    EdgeFetchTransport t = peer_transport(&m);
    EdgeMeshFetch mf;
    EdgeMesh.fetch_begin_args.m = &mf;
    EdgeMesh.fetch_begin_args.t = &t;
    EdgeMesh.fetch_begin_args.host = "peer";
    EdgeMesh.fetch_begin_args.port = 7645;
    EdgeMesh.fetch_begin_args.request = (const uint8_t *)"x";
    EdgeMesh.fetch_begin_args.req_len = 1;
    EdgeMesh.fetch_begin_args.buf = g_rbuf;
    EdgeMesh.fetch_begin_args.cap = sizeof(g_rbuf);
    EdgeMesh.fetch_begin_args.now_ms = 1000;
    EdgeMesh.fetch_begin(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, mf.st);
}

static void test_requester_timeout()
{

    uint8_t partial[4] = {'E', 'M', PROTOCORE_EDGE_MESH_VERSION, 1};
    MockPeer m = {partial, sizeof(partial), 0, 0, PROTO_FALSE, 7, PROTO_TRUE};
    EdgeFetchTransport t = peer_transport(&m);
    EdgeMeshFetch mf;
    EdgeMesh.fetch_begin_args.m = &mf;
    EdgeMesh.fetch_begin_args.t = &t;
    EdgeMesh.fetch_begin_args.host = "peer";
    EdgeMesh.fetch_begin_args.port = 7645;
    EdgeMesh.fetch_begin_args.request = (const uint8_t *)"x";
    EdgeMesh.fetch_begin_args.req_len = 1;
    EdgeMesh.fetch_begin_args.buf = g_rbuf;
    EdgeMesh.fetch_begin_args.cap = sizeof(g_rbuf);
    EdgeMesh.fetch_begin_args.now_ms = 1000;
    EdgeMesh.fetch_begin(edge_mesh_work);
    EdgeMesh.fetch_pump_args.m = &mf;
    EdgeMesh.fetch_pump_args.t = &t;
    EdgeMesh.fetch_pump_args.now_ms = 1000;
    EdgeMesh.fetch_pump(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_PENDING, EdgeMesh.status);
    EdgeMesh.fetch_pump_args.m = &mf;
    EdgeMesh.fetch_pump_args.t = &t;
    EdgeMesh.fetch_pump_args.now_ms = 1000 + PROTOCORE_MESH_QUERY_MS + 1;
    EdgeMesh.fetch_pump(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, EdgeMesh.status);
}

static void test_requester_peer_closed_early()
{
    uint8_t partial[5] = {'E', 'M', PROTOCORE_EDGE_MESH_VERSION, 1, 0};
    MockPeer m = {partial, sizeof(partial), 0, 0, PROTO_TRUE, 7, PROTO_TRUE};
    EdgeFetchTransport t = peer_transport(&m);
    EdgeMeshFetch mf;
    EdgeMesh.fetch_begin_args.m = &mf;
    EdgeMesh.fetch_begin_args.t = &t;
    EdgeMesh.fetch_begin_args.host = "peer";
    EdgeMesh.fetch_begin_args.port = 7645;
    EdgeMesh.fetch_begin_args.request = (const uint8_t *)"x";
    EdgeMesh.fetch_begin_args.req_len = 1;
    EdgeMesh.fetch_begin_args.buf = g_rbuf;
    EdgeMesh.fetch_begin_args.cap = sizeof(g_rbuf);
    EdgeMesh.fetch_begin_args.now_ms = 1000;
    EdgeMesh.fetch_begin(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, run_mesh(&mf, &t, 1000));
}

static void test_requester_malformed()
{
    uint8_t junk[6] = {'X', 'X', 0, 0, 0, 0};
    MockPeer m = {junk, sizeof(junk), 0, 0, PROTO_TRUE, 7, PROTO_TRUE};
    EdgeFetchTransport t = peer_transport(&m);
    EdgeMeshFetch mf;
    EdgeMesh.fetch_begin_args.m = &mf;
    EdgeMesh.fetch_begin_args.t = &t;
    EdgeMesh.fetch_begin_args.host = "peer";
    EdgeMesh.fetch_begin_args.port = 7645;
    EdgeMesh.fetch_begin_args.request = (const uint8_t *)"x";
    EdgeMesh.fetch_begin_args.req_len = 1;
    EdgeMesh.fetch_begin_args.buf = g_rbuf;
    EdgeMesh.fetch_begin_args.cap = sizeof(g_rbuf);
    EdgeMesh.fetch_begin_args.now_ms = 1000;
    EdgeMesh.fetch_begin(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, run_mesh(&mf, &t, 1000));
}

static void test_parse_short_and_bad_prefixes()
{
    size_t eoff = 0;
    size_t elen = 0;

    uint8_t ok[6] = {'E', 'M', PROTOCORE_EDGE_MESH_VERSION, 0, 0, 0};
    EdgeMesh.parse_response_args.buf = ok;
    EdgeMesh.parse_response_args.len = 0;
    EdgeMesh.parse_response_args.entry_off = &eoff;
    EdgeMesh.parse_response_args.entry_len = &elen;
    EdgeMesh.parse_response(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE, EdgeMesh.parse);
    EdgeMesh.parse_response_args.buf = ok;
    EdgeMesh.parse_response_args.len = 1;
    EdgeMesh.parse_response_args.entry_off = &eoff;
    EdgeMesh.parse_response_args.entry_len = &elen;
    EdgeMesh.parse_response(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE, EdgeMesh.parse);
    EdgeMesh.parse_response_args.buf = ok;
    EdgeMesh.parse_response_args.len = 2;
    EdgeMesh.parse_response_args.entry_off = &eoff;
    EdgeMesh.parse_response_args.entry_len = &elen;
    EdgeMesh.parse_response(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE, EdgeMesh.parse);
    EdgeMesh.parse_response_args.buf = ok;
    EdgeMesh.parse_response_args.len = 3;
    EdgeMesh.parse_response_args.entry_off = &eoff;
    EdgeMesh.parse_response_args.entry_len = &elen;
    EdgeMesh.parse_response(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE, EdgeMesh.parse);

    uint8_t m1[4] = {'E', 'X', PROTOCORE_EDGE_MESH_VERSION, 0};
    EdgeMesh.parse_response_args.buf = m1;
    EdgeMesh.parse_response_args.len = 1;
    EdgeMesh.parse_response_args.entry_off = &eoff;
    EdgeMesh.parse_response_args.entry_len = &elen;
    EdgeMesh.parse_response(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE, EdgeMesh.parse);
    EdgeMesh.parse_response_args.buf = m1;
    EdgeMesh.parse_response_args.len = 2;
    EdgeMesh.parse_response_args.entry_off = &eoff;
    EdgeMesh.parse_response_args.entry_len = &elen;
    EdgeMesh.parse_response(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_MALFORMED, EdgeMesh.parse);

    uint8_t v1[4] = {'E', 'M', (uint8_t)(PROTOCORE_EDGE_MESH_VERSION + 1), 0};
    EdgeMesh.parse_response_args.buf = v1;
    EdgeMesh.parse_response_args.len = 2;
    EdgeMesh.parse_response_args.entry_off = &eoff;
    EdgeMesh.parse_response_args.entry_len = &elen;
    EdgeMesh.parse_response(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE, EdgeMesh.parse);
    EdgeMesh.parse_response_args.buf = v1;
    EdgeMesh.parse_response_args.len = 3;
    EdgeMesh.parse_response_args.entry_off = &eoff;
    EdgeMesh.parse_response_args.entry_len = &elen;
    EdgeMesh.parse_response(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_MALFORMED, EdgeMesh.parse);
}

static void test_build_request_guards()
{
    uint8_t digest[32];
    memset(digest, 5, sizeof(digest));
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/g");
    uint8_t out[PROTOCORE_EDGE_MESH_REQ_MAX];
    EdgeMesh.build_request_args.digest = NULL;
    EdgeMesh.build_request_args.canon = canon;
    EdgeMesh.build_request_args.req_hdrs = "";
    EdgeMesh.build_request_args.out = out;
    EdgeMesh.build_request_args.cap = sizeof(out);
    EdgeMesh.build_request(edge_mesh_work);
    TEST_ASSERT_EQUAL_UINT(0, EdgeMesh.n);
    EdgeMesh.build_request_args.digest = digest;
    EdgeMesh.build_request_args.canon = NULL;
    EdgeMesh.build_request_args.req_hdrs = "";
    EdgeMesh.build_request_args.out = out;
    EdgeMesh.build_request_args.cap = sizeof(out);
    EdgeMesh.build_request(edge_mesh_work);
    TEST_ASSERT_EQUAL_UINT(0, EdgeMesh.n);
    EdgeMesh.build_request_args.digest = digest;
    EdgeMesh.build_request_args.canon = canon;
    EdgeMesh.build_request_args.req_hdrs = "";
    EdgeMesh.build_request_args.out = NULL;
    EdgeMesh.build_request_args.cap = sizeof(out);
    EdgeMesh.build_request(edge_mesh_work);
    TEST_ASSERT_EQUAL_UINT(0, EdgeMesh.n);
    EdgeMesh.build_request_args.digest = digest;
    EdgeMesh.build_request_args.canon = canon;
    EdgeMesh.build_request_args.req_hdrs = "";
    EdgeMesh.build_request_args.out = out;
    EdgeMesh.build_request_args.cap = 8;
    EdgeMesh.build_request(edge_mesh_work);
    TEST_ASSERT_EQUAL_UINT(0, EdgeMesh.n);

    EdgeMesh.build_request_args.digest = digest;
    EdgeMesh.build_request_args.canon = canon;
    EdgeMesh.build_request_args.req_hdrs = NULL;
    EdgeMesh.build_request_args.out = out;
    EdgeMesh.build_request_args.cap = sizeof(out);
    EdgeMesh.build_request(edge_mesh_work);
    size_t n = EdgeMesh.n;
    TEST_ASSERT_EQUAL_UINT(2 + 1 + 1 + 32 + 2 + strlen(canon) + 2, n);
}

static void test_parse_request_incomplete_at_every_field()
{
    uint8_t digest[32];
    memset(digest, 0x5A, sizeof(digest));
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/fields");
    uint8_t req[PROTOCORE_EDGE_MESH_REQ_MAX];
    EdgeMesh.build_request_args.digest = digest;
    EdgeMesh.build_request_args.canon = canon;
    EdgeMesh.build_request_args.req_hdrs = "A\x1e"
                                           "b\x1f";
    EdgeMesh.build_request_args.out = req;
    EdgeMesh.build_request_args.cap = sizeof(req);
    EdgeMesh.build_request(edge_mesh_work);
    size_t n = EdgeMesh.n;
    TEST_ASSERT_TRUE(n > 0);

    uint8_t d2[32];
    char c2[PROTOCORE_EDGE_KEY_MAX];
    char v2[PROTOCORE_MESH_HDRS_MAX];

    for (size_t l = 4; l < n; l++)
    {
        EdgeMesh.parse_request_args.buf = req;
        EdgeMesh.parse_request_args.len = l;
        EdgeMesh.parse_request_args.digest_out = d2;
        EdgeMesh.parse_request_args.canon_out = c2;
        EdgeMesh.parse_request_args.canon_cap = sizeof(c2);
        EdgeMesh.parse_request_args.hdrs_out = v2;
        EdgeMesh.parse_request_args.hdrs_cap = sizeof(v2);
        EdgeMesh.parse_request(edge_mesh_work);
        TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_INCOMPLETE, EdgeMesh.parse);
    }
    EdgeMesh.parse_request_args.buf = req;
    EdgeMesh.parse_request_args.len = n;
    EdgeMesh.parse_request_args.digest_out = d2;
    EdgeMesh.parse_request_args.canon_out = c2;
    EdgeMesh.parse_request_args.canon_cap = sizeof(c2);
    EdgeMesh.parse_request_args.hdrs_out = v2;
    EdgeMesh.parse_request_args.hdrs_cap = sizeof(v2);
    EdgeMesh.parse_request(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_HIT, EdgeMesh.parse);
}

static void test_parse_request_hdrs_too_long_for_destination()
{
    uint8_t digest[32];
    memset(digest, 1, sizeof(digest));
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/h");
    char hdrs[64];
    memset(hdrs, 'h', sizeof(hdrs) - 1);
    hdrs[sizeof(hdrs) - 1] = '\0';
    uint8_t req[PROTOCORE_EDGE_MESH_REQ_MAX];
    EdgeMesh.build_request_args.digest = digest;
    EdgeMesh.build_request_args.canon = canon;
    EdgeMesh.build_request_args.req_hdrs = hdrs;
    EdgeMesh.build_request_args.out = req;
    EdgeMesh.build_request_args.cap = sizeof(req);
    EdgeMesh.build_request(edge_mesh_work);
    size_t n = EdgeMesh.n;
    TEST_ASSERT_TRUE(n > 0);

    uint8_t d2[32];
    char c2[PROTOCORE_EDGE_KEY_MAX];
    char v2[16];
    EdgeMesh.parse_request_args.buf = req;
    EdgeMesh.parse_request_args.len = n;
    EdgeMesh.parse_request_args.digest_out = d2;
    EdgeMesh.parse_request_args.canon_out = c2;
    EdgeMesh.parse_request_args.canon_cap = sizeof(c2);
    EdgeMesh.parse_request_args.hdrs_out = v2;
    EdgeMesh.parse_request_args.hdrs_cap = sizeof(v2);
    EdgeMesh.parse_request(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_MALFORMED, EdgeMesh.parse);
}

static void test_parse_request_null_outputs()
{
    uint8_t digest[32];
    memset(digest, 9, sizeof(digest));
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/n");
    uint8_t req[PROTOCORE_EDGE_MESH_REQ_MAX];
    EdgeMesh.build_request_args.digest = digest;
    EdgeMesh.build_request_args.canon = canon;
    EdgeMesh.build_request_args.req_hdrs = "x";
    EdgeMesh.build_request_args.out = req;
    EdgeMesh.build_request_args.cap = sizeof(req);
    EdgeMesh.build_request(edge_mesh_work);
    size_t n = EdgeMesh.n;

    EdgeMesh.parse_request_args.buf = req;
    EdgeMesh.parse_request_args.len = n;
    EdgeMesh.parse_request_args.digest_out = NULL;
    EdgeMesh.parse_request_args.canon_out = NULL;
    EdgeMesh.parse_request_args.canon_cap = PROTOCORE_EDGE_KEY_MAX;
    EdgeMesh.parse_request_args.hdrs_out = NULL;
    EdgeMesh.parse_request_args.hdrs_cap = PROTOCORE_MESH_HDRS_MAX;
    EdgeMesh.parse_request(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_HIT, EdgeMesh.parse);
}

static void test_serialize_entry_guards_and_clamps()
{
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/s");
    EdgeEntry e;
    fill_entry(&e, canon, "\"s\"", (const uint8_t *)"body", 4);
    uint8_t out[PROTOCORE_EDGE_MESH_ENTRY_MAX];
    EdgeMesh.serialize_entry_args.e = NULL;
    EdgeMesh.serialize_entry_args.current_age = 0;
    EdgeMesh.serialize_entry_args.out = out;
    EdgeMesh.serialize_entry_args.cap = sizeof(out);
    EdgeMesh.serialize_entry(edge_mesh_work);
    TEST_ASSERT_EQUAL_UINT(0, EdgeMesh.n);
    EdgeMesh.serialize_entry_args.e = &e;
    EdgeMesh.serialize_entry_args.current_age = 0;
    EdgeMesh.serialize_entry_args.out = NULL;
    EdgeMesh.serialize_entry_args.cap = sizeof(out);
    EdgeMesh.serialize_entry(edge_mesh_work);
    TEST_ASSERT_EQUAL_UINT(0, EdgeMesh.n);
    EdgeMesh.serialize_entry_args.e = &e;
    EdgeMesh.serialize_entry_args.current_age = 0;
    EdgeMesh.serialize_entry_args.out = out;
    EdgeMesh.serialize_entry_args.cap = PROTOCORE_EDGE_MESH_TRAILER - 1;
    EdgeMesh.serialize_entry(edge_mesh_work);
    TEST_ASSERT_EQUAL_UINT(0, EdgeMesh.n);
    EdgeMesh.serialize_entry_args.e = &e;
    EdgeMesh.serialize_entry_args.current_age = 0;
    EdgeMesh.serialize_entry_args.out = out;
    EdgeMesh.serialize_entry_args.cap = PROTOCORE_EDGE_MESH_TRAILER + 4;
    EdgeMesh.serialize_entry(edge_mesh_work);
    TEST_ASSERT_EQUAL_UINT(0, EdgeMesh.n);

    EdgeEntry got;

    EdgeMesh.serialize_entry_args.e = &e;
    EdgeMesh.serialize_entry_args.current_age = -5;
    EdgeMesh.serialize_entry_args.out = out;
    EdgeMesh.serialize_entry_args.cap = sizeof(out);
    EdgeMesh.serialize_entry(edge_mesh_work);
    size_t n = EdgeMesh.n;
    TEST_ASSERT_TRUE(n > 0);
    memset(&got, 0, sizeof(got));
    EdgeMesh.deserialize_entry_args.entry_buf = tw;
    EdgeMesh.deserialize_entry_args.buf = out;
    EdgeMesh.deserialize_entry_args.len = n;
    EdgeMesh.deserialize_entry_args.e = &got;
    EdgeMesh.deserialize_entry_args.now_ms = 1234;
    EdgeMesh.deserialize_entry(edge_mesh_work);
    TEST_ASSERT_TRUE(EdgeMesh.ok);
    TEST_ASSERT_EQUAL_INT(0, got.initial_age);

    e.lifetime_s = -1;
    e.age_hdr = -2;
    EdgeMesh.serialize_entry_args.e = &e;
    EdgeMesh.serialize_entry_args.current_age = 0;
    EdgeMesh.serialize_entry_args.out = out;
    EdgeMesh.serialize_entry_args.cap = sizeof(out);
    EdgeMesh.serialize_entry(edge_mesh_work);
    n = EdgeMesh.n;
    TEST_ASSERT_TRUE(n > 0);
    memset(&got, 0, sizeof(got));
    EdgeMesh.deserialize_entry_args.entry_buf = tw;
    EdgeMesh.deserialize_entry_args.buf = out;
    EdgeMesh.deserialize_entry_args.len = n;
    EdgeMesh.deserialize_entry_args.e = &got;
    EdgeMesh.deserialize_entry_args.now_ms = 1234;
    EdgeMesh.deserialize_entry(edge_mesh_work);
    TEST_ASSERT_TRUE(EdgeMesh.ok);
    TEST_ASSERT_EQUAL_INT(0, got.lifetime_s);
    TEST_ASSERT_EQUAL_INT(0, got.age_hdr);
}

static void test_deserialize_entry_guards()
{
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/d");
    EdgeEntry e;
    fill_entry(&e, canon, "\"d\"", (const uint8_t *)"xy", 2);
    uint8_t frame[PROTOCORE_EDGE_MESH_ENTRY_MAX];
    EdgeMesh.serialize_entry_args.e = &e;
    EdgeMesh.serialize_entry_args.current_age = 1;
    EdgeMesh.serialize_entry_args.out = frame;
    EdgeMesh.serialize_entry_args.cap = sizeof(frame);
    EdgeMesh.serialize_entry(edge_mesh_work);
    size_t n = EdgeMesh.n;
    TEST_ASSERT_TRUE(n > 0);

    EdgeEntry got;
    memset(&got, 0, sizeof(got));
    EdgeMesh.deserialize_entry_args.entry_buf = tw;
    EdgeMesh.deserialize_entry_args.buf = NULL;
    EdgeMesh.deserialize_entry_args.len = n;
    EdgeMesh.deserialize_entry_args.e = &got;
    EdgeMesh.deserialize_entry_args.now_ms = 0;
    EdgeMesh.deserialize_entry(edge_mesh_work);
    TEST_ASSERT_FALSE(EdgeMesh.ok);
    EdgeMesh.deserialize_entry_args.entry_buf = tw;
    EdgeMesh.deserialize_entry_args.buf = frame;
    EdgeMesh.deserialize_entry_args.len = n;
    EdgeMesh.deserialize_entry_args.e = NULL;
    EdgeMesh.deserialize_entry_args.now_ms = 0;
    EdgeMesh.deserialize_entry(edge_mesh_work);
    TEST_ASSERT_FALSE(EdgeMesh.ok);
    EdgeMesh.deserialize_entry_args.entry_buf = tw;
    EdgeMesh.deserialize_entry_args.buf = frame;
    EdgeMesh.deserialize_entry_args.len = PROTOCORE_EDGE_MESH_TRAILER - 1;
    EdgeMesh.deserialize_entry_args.e = &got;
    EdgeMesh.deserialize_entry_args.now_ms = 0;
    EdgeMesh.deserialize_entry(edge_mesh_work);
    TEST_ASSERT_FALSE(EdgeMesh.ok);

    uint8_t save = frame[PROTOCORE_EDGE_MESH_TRAILER];
    frame[PROTOCORE_EDGE_MESH_TRAILER] = 0x7F;
    EdgeMesh.deserialize_entry_args.entry_buf = tw;
    EdgeMesh.deserialize_entry_args.buf = frame;
    EdgeMesh.deserialize_entry_args.len = n;
    EdgeMesh.deserialize_entry_args.e = &got;
    EdgeMesh.deserialize_entry_args.now_ms = 0;
    EdgeMesh.deserialize_entry(edge_mesh_work);
    TEST_ASSERT_FALSE(EdgeMesh.ok);
    frame[PROTOCORE_EDGE_MESH_TRAILER] = save;
    EdgeMesh.deserialize_entry_args.entry_buf = tw;
    EdgeMesh.deserialize_entry_args.buf = frame;
    EdgeMesh.deserialize_entry_args.len = n;
    EdgeMesh.deserialize_entry_args.e = &got;
    EdgeMesh.deserialize_entry_args.now_ms = 0;
    EdgeMesh.deserialize_entry(edge_mesh_work);
    TEST_ASSERT_TRUE(EdgeMesh.ok);
}

static void test_build_response_guards()
{
    uint8_t out[64];
    uint8_t entry[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    EdgeMesh.build_response_args.hit = PROTO_FALSE;
    EdgeMesh.build_response_args.entry = NULL;
    EdgeMesh.build_response_args.entry_len = 0;
    EdgeMesh.build_response_args.out = NULL;
    EdgeMesh.build_response_args.cap = sizeof(out);
    EdgeMesh.build_response(edge_mesh_work);
    TEST_ASSERT_EQUAL_UINT(0, EdgeMesh.n);
    EdgeMesh.build_response_args.hit = PROTO_FALSE;
    EdgeMesh.build_response_args.entry = NULL;
    EdgeMesh.build_response_args.entry_len = 0;
    EdgeMesh.build_response_args.out = out;
    EdgeMesh.build_response_args.cap = 3;
    EdgeMesh.build_response(edge_mesh_work);
    TEST_ASSERT_EQUAL_UINT(0, EdgeMesh.n);
    EdgeMesh.build_response_args.hit = PROTO_TRUE;
    EdgeMesh.build_response_args.entry = NULL;
    EdgeMesh.build_response_args.entry_len = 8;
    EdgeMesh.build_response_args.out = out;
    EdgeMesh.build_response_args.cap = sizeof(out);
    EdgeMesh.build_response(edge_mesh_work);
    TEST_ASSERT_EQUAL_UINT(0, EdgeMesh.n);
    EdgeMesh.build_response_args.hit = PROTO_TRUE;
    EdgeMesh.build_response_args.entry = entry;
    EdgeMesh.build_response_args.entry_len = 0;
    EdgeMesh.build_response_args.out = out;
    EdgeMesh.build_response_args.cap = sizeof(out);
    EdgeMesh.build_response(edge_mesh_work);
    TEST_ASSERT_EQUAL_UINT(0, EdgeMesh.n);
    EdgeMesh.build_response_args.hit = PROTO_TRUE;
    EdgeMesh.build_response_args.entry = entry;
    EdgeMesh.build_response_args.entry_len = 0x10000;
    EdgeMesh.build_response_args.out = out;
    EdgeMesh.build_response_args.cap = sizeof(out);
    EdgeMesh.build_response(edge_mesh_work);
    TEST_ASSERT_EQUAL_UINT(0, EdgeMesh.n);
    EdgeMesh.build_response_args.hit = PROTO_TRUE;
    EdgeMesh.build_response_args.entry = entry;
    EdgeMesh.build_response_args.entry_len = 100;
    EdgeMesh.build_response_args.out = out;
    EdgeMesh.build_response_args.cap = sizeof(out);
    EdgeMesh.build_response(edge_mesh_work);
    TEST_ASSERT_EQUAL_UINT(0, EdgeMesh.n);
    EdgeMesh.build_response_args.hit = PROTO_TRUE;
    EdgeMesh.build_response_args.entry = entry;
    EdgeMesh.build_response_args.entry_len = 8;
    EdgeMesh.build_response_args.out = out;
    EdgeMesh.build_response_args.cap = sizeof(out);
    EdgeMesh.build_response(edge_mesh_work);
    TEST_ASSERT_EQUAL_UINT(4 + 2 + 8, EdgeMesh.n);
}

static void test_parse_response_null_outputs()
{
    uint8_t frame[PROTOCORE_EDGE_MESH_ENTRY_MAX];
    size_t fn = 0;
    build_hit_frame(frame, sizeof(frame), &fn, 0);
    TEST_ASSERT_TRUE(fn > 0);
    uint8_t resp[PROTOCORE_EDGE_MESH_RESP_MAX];
    EdgeMesh.build_response_args.hit = PROTO_TRUE;
    EdgeMesh.build_response_args.entry = frame;
    EdgeMesh.build_response_args.entry_len = fn;
    EdgeMesh.build_response_args.out = resp;
    EdgeMesh.build_response_args.cap = sizeof(resp);
    EdgeMesh.build_response(edge_mesh_work);
    size_t rn = EdgeMesh.n;
    TEST_ASSERT_TRUE(rn > 0);
    EdgeMesh.parse_response_args.buf = resp;
    EdgeMesh.parse_response_args.len = rn;
    EdgeMesh.parse_response_args.entry_off = NULL;
    EdgeMesh.parse_response_args.entry_len = NULL;
    EdgeMesh.parse_response(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_PARSE_HIT, EdgeMesh.parse);
}

static void test_requester_begin_argument_guards()
{
    MockPeer m = {(const uint8_t *)"", 0, 0, 0, PROTO_FALSE, 7, PROTO_TRUE};
    EdgeFetchTransport t = peer_transport(&m);
    const uint8_t req[1] = {'x'};
    EdgeMeshFetch mf;
    EdgeMesh.fetch_begin_args.m = &mf;
    EdgeMesh.fetch_begin_args.t = NULL;
    EdgeMesh.fetch_begin_args.host = "peer";
    EdgeMesh.fetch_begin_args.port = 1;
    EdgeMesh.fetch_begin_args.request = req;
    EdgeMesh.fetch_begin_args.req_len = 1;
    EdgeMesh.fetch_begin_args.buf = g_rbuf;
    EdgeMesh.fetch_begin_args.cap = sizeof(g_rbuf);
    EdgeMesh.fetch_begin_args.now_ms = 1000;
    EdgeMesh.fetch_begin(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, mf.st);
    EdgeMesh.fetch_begin_args.m = &mf;
    EdgeMesh.fetch_begin_args.t = &t;
    EdgeMesh.fetch_begin_args.host = NULL;
    EdgeMesh.fetch_begin_args.port = 1;
    EdgeMesh.fetch_begin_args.request = req;
    EdgeMesh.fetch_begin_args.req_len = 1;
    EdgeMesh.fetch_begin_args.buf = g_rbuf;
    EdgeMesh.fetch_begin_args.cap = sizeof(g_rbuf);
    EdgeMesh.fetch_begin_args.now_ms = 1000;
    EdgeMesh.fetch_begin(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, mf.st);
    EdgeMesh.fetch_begin_args.m = &mf;
    EdgeMesh.fetch_begin_args.t = &t;
    EdgeMesh.fetch_begin_args.host = "peer";
    EdgeMesh.fetch_begin_args.port = 1;
    EdgeMesh.fetch_begin_args.request = NULL;
    EdgeMesh.fetch_begin_args.req_len = 1;
    EdgeMesh.fetch_begin_args.buf = g_rbuf;
    EdgeMesh.fetch_begin_args.cap = sizeof(g_rbuf);
    EdgeMesh.fetch_begin_args.now_ms = 1000;
    EdgeMesh.fetch_begin(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, mf.st);
    EdgeMesh.fetch_begin_args.m = &mf;
    EdgeMesh.fetch_begin_args.t = &t;
    EdgeMesh.fetch_begin_args.host = "peer";
    EdgeMesh.fetch_begin_args.port = 1;
    EdgeMesh.fetch_begin_args.request = req;
    EdgeMesh.fetch_begin_args.req_len = 0;
    EdgeMesh.fetch_begin_args.buf = g_rbuf;
    EdgeMesh.fetch_begin_args.cap = sizeof(g_rbuf);
    EdgeMesh.fetch_begin_args.now_ms = 1000;
    EdgeMesh.fetch_begin(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, mf.st);
    EdgeMesh.fetch_begin_args.m = &mf;
    EdgeMesh.fetch_begin_args.t = &t;
    EdgeMesh.fetch_begin_args.host = "peer";
    EdgeMesh.fetch_begin_args.port = 1;
    EdgeMesh.fetch_begin_args.request = req;
    EdgeMesh.fetch_begin_args.req_len = 1;
    EdgeMesh.fetch_begin_args.buf = NULL;
    EdgeMesh.fetch_begin_args.cap = sizeof(g_rbuf);
    EdgeMesh.fetch_begin_args.now_ms = 1000;
    EdgeMesh.fetch_begin(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, mf.st);

    EdgeMesh.fetch_begin_args.m = &mf;
    EdgeMesh.fetch_begin_args.t = &t;
    EdgeMesh.fetch_begin_args.host = "peer";
    EdgeMesh.fetch_begin_args.port = 1;
    EdgeMesh.fetch_begin_args.request = req;
    EdgeMesh.fetch_begin_args.req_len = 1;
    EdgeMesh.fetch_begin_args.buf = g_rbuf;
    EdgeMesh.fetch_begin_args.cap = PROTOCORE_EDGE_MESH_RESP_MAX - 1;
    EdgeMesh.fetch_begin_args.now_ms = 1000;
    EdgeMesh.fetch_begin(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, mf.st);
    TEST_ASSERT_EQUAL_INT(-1, mf.cid);
}

static void test_requester_pump_guards()
{
    MockPeer m = {(const uint8_t *)"", 0, 0, 0, PROTO_FALSE, 7, PROTO_TRUE};
    EdgeFetchTransport t = peer_transport(&m);
    const uint8_t req[1] = {'x'};
    EdgeMeshFetch mf;
    EdgeMesh.fetch_begin_args.m = &mf;
    EdgeMesh.fetch_begin_args.t = &t;
    EdgeMesh.fetch_begin_args.host = "peer";
    EdgeMesh.fetch_begin_args.port = 1;
    EdgeMesh.fetch_begin_args.request = req;
    EdgeMesh.fetch_begin_args.req_len = 1;
    EdgeMesh.fetch_begin_args.buf = g_rbuf;
    EdgeMesh.fetch_begin_args.cap = sizeof(g_rbuf);
    EdgeMesh.fetch_begin_args.now_ms = 1000;
    EdgeMesh.fetch_begin(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_PENDING, mf.st);

    EdgeMesh.fetch_pump_args.m = &mf;
    EdgeMesh.fetch_pump_args.t = NULL;
    EdgeMesh.fetch_pump_args.now_ms = 1000;
    EdgeMesh.fetch_pump(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, EdgeMesh.status);

    EdgeMesh.fetch_pump_args.m = &mf;
    EdgeMesh.fetch_pump_args.t = &t;
    EdgeMesh.fetch_pump_args.now_ms = 1000;
    EdgeMesh.fetch_pump(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, EdgeMesh.status);
    TEST_ASSERT_EQUAL_UINT(0, m.cursor);

    EdgeMesh.fetch_begin_args.m = &mf;
    EdgeMesh.fetch_begin_args.t = &t;
    EdgeMesh.fetch_begin_args.host = "peer";
    EdgeMesh.fetch_begin_args.port = 1;
    EdgeMesh.fetch_begin_args.request = req;
    EdgeMesh.fetch_begin_args.req_len = 1;
    EdgeMesh.fetch_begin_args.buf = g_rbuf;
    EdgeMesh.fetch_begin_args.cap = sizeof(g_rbuf);
    EdgeMesh.fetch_begin_args.now_ms = 1000;
    EdgeMesh.fetch_begin(edge_mesh_work);
    mf.cid = -1;
    EdgeMesh.fetch_pump_args.m = &mf;
    EdgeMesh.fetch_pump_args.t = &t;
    EdgeMesh.fetch_pump_args.now_ms = 1000;
    EdgeMesh.fetch_pump(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, EdgeMesh.status);
}

static uint8_t g_flood[PROTOCORE_EDGE_MESH_RESP_MAX];

static void test_requester_buffer_full_without_a_frame()
{

    memset(g_flood, 0xAA, sizeof(g_flood));
    g_flood[0] = PROTOCORE_EDGE_MESH_MAGIC0;
    g_flood[1] = PROTOCORE_EDGE_MESH_MAGIC1;
    g_flood[2] = PROTOCORE_EDGE_MESH_VERSION;
    g_flood[3] = 1;
    g_flood[4] = 0xFF;
    g_flood[5] = 0xFF;
    MockPeer m = {g_flood, sizeof(g_flood), 0, 0, PROTO_FALSE, 7, PROTO_TRUE};
    EdgeFetchTransport t = peer_transport(&m);
    EdgeMeshFetch mf;
    EdgeMesh.fetch_begin_args.m = &mf;
    EdgeMesh.fetch_begin_args.t = &t;
    EdgeMesh.fetch_begin_args.host = "peer";
    EdgeMesh.fetch_begin_args.port = 7645;
    EdgeMesh.fetch_begin_args.request = (const uint8_t *)"x";
    EdgeMesh.fetch_begin_args.req_len = 1;
    EdgeMesh.fetch_begin_args.buf = g_rbuf;
    EdgeMesh.fetch_begin_args.cap = sizeof(g_rbuf);
    EdgeMesh.fetch_begin_args.now_ms = 1000;
    EdgeMesh.fetch_begin(edge_mesh_work);
    EdgeMesh.fetch_pump_args.m = &mf;
    EdgeMesh.fetch_pump_args.t = &t;
    EdgeMesh.fetch_pump_args.now_ms = 1000;
    EdgeMesh.fetch_pump(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, EdgeMesh.status);
    TEST_ASSERT_EQUAL_UINT(sizeof(g_rbuf), mf.got);
}

static void test_requester_pump_skips_the_read_when_the_buffer_is_already_full()
{

    static uint8_t hdr[6] = {'E', 'M', PROTOCORE_EDGE_MESH_VERSION, 1, 0xFF, 0xFF};
    MockPeer m = {hdr, sizeof(hdr), 0, 0, PROTO_FALSE, 7, PROTO_TRUE};
    EdgeFetchTransport t = peer_transport(&m);
    EdgeMeshFetch mf;
    EdgeMesh.fetch_begin_args.m = &mf;
    EdgeMesh.fetch_begin_args.t = &t;
    EdgeMesh.fetch_begin_args.host = "peer";
    EdgeMesh.fetch_begin_args.port = 7645;
    EdgeMesh.fetch_begin_args.request = (const uint8_t *)"x";
    EdgeMesh.fetch_begin_args.req_len = 1;
    EdgeMesh.fetch_begin_args.buf = g_rbuf;
    EdgeMesh.fetch_begin_args.cap = sizeof(g_rbuf);
    EdgeMesh.fetch_begin_args.now_ms = 1000;
    EdgeMesh.fetch_begin(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_PENDING, mf.st);

    memset(g_rbuf, 0, sizeof(g_rbuf));
    memcpy(g_rbuf, hdr, sizeof(hdr));
    mf.got = mf.cap;
    m.reads = 0;
    EdgeMesh.fetch_pump_args.m = &mf;
    EdgeMesh.fetch_pump_args.t = &t;
    EdgeMesh.fetch_pump_args.now_ms = 1000;
    EdgeMesh.fetch_pump(edge_mesh_work);
    TEST_ASSERT_EQUAL(EDGE_MESH_STATUS_FAILED, EdgeMesh.status);
    TEST_ASSERT_EQUAL_INT(0, m.reads);
    TEST_ASSERT_EQUAL_UINT(mf.cap, mf.got);
    TEST_ASSERT_EQUAL_UINT(0, m.cursor);
}

static void test_requester_end_without_a_connection()
{
    MockPeer m = {(const uint8_t *)"", 0, 0, 0, PROTO_FALSE, -1, PROTO_TRUE};
    EdgeFetchTransport t = peer_transport(&m);
    EdgeMeshFetch mf;
    EdgeMesh.fetch_begin_args.m = &mf;
    EdgeMesh.fetch_begin_args.t = &t;
    EdgeMesh.fetch_begin_args.host = "peer";
    EdgeMesh.fetch_begin_args.port = 7645;
    EdgeMesh.fetch_begin_args.request = (const uint8_t *)"x";
    EdgeMesh.fetch_begin_args.req_len = 1;
    EdgeMesh.fetch_begin_args.buf = g_rbuf;
    EdgeMesh.fetch_begin_args.cap = sizeof(g_rbuf);
    EdgeMesh.fetch_begin_args.now_ms = 1000;
    EdgeMesh.fetch_begin(edge_mesh_work);
    TEST_ASSERT_EQUAL_INT(-1, mf.cid);
    EdgeMesh.fetch_end_args.m = &mf;
    EdgeMesh.fetch_end_args.t = &t;
    EdgeMesh.fetch_end(edge_mesh_work);
    TEST_ASSERT_EQUAL_INT(-1, mf.cid);

    m.open_ret = 7;
    EdgeMesh.fetch_begin_args.m = &mf;
    EdgeMesh.fetch_begin_args.t = &t;
    EdgeMesh.fetch_begin_args.host = "peer";
    EdgeMesh.fetch_begin_args.port = 7645;
    EdgeMesh.fetch_begin_args.request = (const uint8_t *)"x";
    EdgeMesh.fetch_begin_args.req_len = 1;
    EdgeMesh.fetch_begin_args.buf = g_rbuf;
    EdgeMesh.fetch_begin_args.cap = sizeof(g_rbuf);
    EdgeMesh.fetch_begin_args.now_ms = 1000;
    EdgeMesh.fetch_begin(edge_mesh_work);
    TEST_ASSERT_EQUAL_INT(7, mf.cid);
    EdgeMesh.fetch_end_args.m = &mf;
    EdgeMesh.fetch_end_args.t = NULL;
    EdgeMesh.fetch_end(edge_mesh_work);
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
