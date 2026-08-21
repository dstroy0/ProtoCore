// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "server/web/edge_cache/edge_fetch/edge_fetch.h"
#include <stdint.h>
#include <string.h>

#include <unity.h>

static uint8_t edge_fetch_work[16]; // the borrow an entry takes; EdgeFetch never reads it

void setUp()
{
}
void tearDown()
{
}

typedef struct
{
    const uint8_t *data;
    size_t len;
    size_t cursor;
    size_t throttle;
    proto_bool closes;
    int open_ret;

    proto_bool send_fail;
    int close_calls;
    int last_closed;
} MockOrigin;

static int m_open(void *c, const char *h, uint16_t p, uint32_t t)
{
    (void)h;
    (void)p;
    (void)t;
    return ((MockOrigin *)c)->open_ret;
}
static proto_bool m_send(void *c, int cid, const void *d, size_t l)
{
    (void)cid;
    (void)d;
    (void)l;
    return !((MockOrigin *)c)->send_fail;
}
static size_t m_read(void *c, int cid, uint8_t *buf, size_t cap)
{
    (void)cid;
    MockOrigin *m = (MockOrigin *)c;
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
static proto_bool m_closed(void *c, int cid)
{
    (void)cid;
    MockOrigin *m = (MockOrigin *)c;
    return m->closes && m->cursor >= m->len;
}
static void m_close(void *c, int cid)
{
    MockOrigin *m = (MockOrigin *)c;
    m->close_calls++;
    m->last_closed = cid;
}

static proto_bool m_connected(void *c, int cid)
{
    (void)cid;
    return ((MockOrigin *)c)->open_ret >= 0;
}

static EdgeFetchTransport make_transport(MockOrigin *m)
{
    EdgeFetchTransport t;
    t.open = m_open;
    t.connected = m_connected;
    t.send = m_send;
    t.read = m_read;
    t.closed = m_closed;
    t.close = m_close;
    t.ctx = m;
    return t;
}

static EdgeFetchStatus run_fetch(EdgeFetch *f, const EdgeFetchTransport *t, uint32_t now)
{
    for (int i = 0; i < 100000 && f->st == EDGE_FETCH_STATUS_PENDING; i++)
    {
        EdgeFetcherV.pump_args.f = f;
        EdgeFetcherV.pump_args.t = t;
        EdgeFetcherV.pump_args.now_ms = now;
        EdgeFetcher.pump(edge_fetch_work);
    }
    return f->st;
}

void test_fetch_content_length()
{
    static const char *R = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Type: text/plain\r\n\r\nhello";
    MockOrigin m = {(const uint8_t *)R, strlen(R), 0, 4, PROTO_TRUE, 7};
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    EdgeFetcherV.begin_args.f = &f;
    EdgeFetcherV.begin_args.t = &t;
    EdgeFetcherV.begin_args.host = "h";
    EdgeFetcherV.begin_args.port = 80;
    EdgeFetcherV.begin_args.request = "GET / HTTP/1.1\r\n\r\n";
    EdgeFetcherV.begin_args.req_len = 18;
    EdgeFetcherV.begin_args.now_ms = 1000;
    EdgeFetcher.begin(edge_fetch_work);
    TEST_ASSERT_EQUAL(EDGE_FETCH_STATUS_DONE, run_fetch(&f, &t, 1000));
    TEST_ASSERT_EQUAL_INT(200, f.status);
    TEST_ASSERT_EQUAL_UINT(5, f.body_len);
    TEST_ASSERT_EQUAL_MEMORY("hello", f.buf + f.body_off, 5);
}

void test_fetch_chunked()
{
    static const char *R =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n";
    MockOrigin m = {(const uint8_t *)R, strlen(R), 0, 3, PROTO_TRUE, 9};
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    EdgeFetcherV.begin_args.f = &f;
    EdgeFetcherV.begin_args.t = &t;
    EdgeFetcherV.begin_args.host = "h";
    EdgeFetcherV.begin_args.port = 80;
    EdgeFetcherV.begin_args.request = "GET / HTTP/1.1\r\n\r\n";
    EdgeFetcherV.begin_args.req_len = 18;
    EdgeFetcherV.begin_args.now_ms = 1000;
    EdgeFetcher.begin(edge_fetch_work);
    TEST_ASSERT_EQUAL(EDGE_FETCH_STATUS_DONE, run_fetch(&f, &t, 1000));
    TEST_ASSERT_EQUAL_INT(200, f.status);
    TEST_ASSERT_EQUAL_UINT(11, f.body_len);
    TEST_ASSERT_EQUAL_MEMORY("hello world", f.buf + f.body_off, 11);
}

void test_fetch_close_delimited()
{
    static const char *R = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nbody-till-close";
    MockOrigin m = {(const uint8_t *)R, strlen(R), 0, 0, PROTO_TRUE, 5};
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    EdgeFetcherV.begin_args.f = &f;
    EdgeFetcherV.begin_args.t = &t;
    EdgeFetcherV.begin_args.host = "h";
    EdgeFetcherV.begin_args.port = 80;
    EdgeFetcherV.begin_args.request = "GET / HTTP/1.1\r\n\r\n";
    EdgeFetcherV.begin_args.req_len = 18;
    EdgeFetcherV.begin_args.now_ms = 1000;
    EdgeFetcher.begin(edge_fetch_work);
    TEST_ASSERT_EQUAL(EDGE_FETCH_STATUS_DONE, run_fetch(&f, &t, 1000));
    TEST_ASSERT_EQUAL_INT(200, f.status);
    TEST_ASSERT_EQUAL_MEMORY("body-till-close", f.buf + f.body_off, 15);
}

void test_fetch_oversize()
{
    static uint8_t big[PROTOCORE_EDGE_FETCH_BUF + 1024];
    const char *head = "HTTP/1.1 200 OK\r\nContent-Length: 6000\r\n\r\n";
    size_t hl = strlen(head);
    memcpy(big, head, hl);
    memset(big + hl, 'x', sizeof(big) - hl);
    MockOrigin m = {big, sizeof(big), 0, 0, PROTO_FALSE, 3};
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    EdgeFetcherV.begin_args.f = &f;
    EdgeFetcherV.begin_args.t = &t;
    EdgeFetcherV.begin_args.host = "h";
    EdgeFetcherV.begin_args.port = 80;
    EdgeFetcherV.begin_args.request = "GET / HTTP/1.1\r\n\r\n";
    EdgeFetcherV.begin_args.req_len = 18;
    EdgeFetcherV.begin_args.now_ms = 1000;
    EdgeFetcher.begin(edge_fetch_work);
    TEST_ASSERT_EQUAL(EDGE_FETCH_STATUS_OVERSIZE, run_fetch(&f, &t, 1000));
}

void test_fetch_timeout()
{
    static const char *R = "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\npartial";
    MockOrigin m = {(const uint8_t *)R, strlen(R), 0, 0, PROTO_FALSE, 2};
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    EdgeFetcherV.begin_args.f = &f;
    EdgeFetcherV.begin_args.t = &t;
    EdgeFetcherV.begin_args.host = "h";
    EdgeFetcherV.begin_args.port = 80;
    EdgeFetcherV.begin_args.request = "GET / HTTP/1.1\r\n\r\n";
    EdgeFetcherV.begin_args.req_len = 18;
    EdgeFetcherV.begin_args.now_ms = 1000;
    EdgeFetcher.begin(edge_fetch_work);
    EdgeFetcherV.pump_args.f = &f;
    EdgeFetcherV.pump_args.t = &t;
    EdgeFetcherV.pump_args.now_ms = 1000;
    EdgeFetcher.pump(edge_fetch_work);
    TEST_ASSERT_EQUAL(EDGE_FETCH_STATUS_PENDING, EdgeFetcherV.status);
    EdgeFetcherV.pump_args.f = &f;
    EdgeFetcherV.pump_args.t = &t;
    EdgeFetcherV.pump_args.now_ms = 1000 + PROTOCORE_EDGE_FETCH_TIMEOUT_MS;
    EdgeFetcher.pump(edge_fetch_work);
    TEST_ASSERT_EQUAL(EDGE_FETCH_STATUS_FAILED, EdgeFetcherV.status);
}

void test_fetch_open_fail()
{
    MockOrigin m = {(const uint8_t *)"", 0, 0, 0, PROTO_FALSE, -1};
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    EdgeFetcherV.begin_args.f = &f;
    EdgeFetcherV.begin_args.t = &t;
    EdgeFetcherV.begin_args.host = "h";
    EdgeFetcherV.begin_args.port = 80;
    EdgeFetcherV.begin_args.request = "GET / HTTP/1.1\r\n\r\n";
    EdgeFetcherV.begin_args.req_len = 18;
    EdgeFetcherV.begin_args.now_ms = 1000;
    EdgeFetcher.begin(edge_fetch_work);
    TEST_ASSERT_EQUAL(EDGE_FETCH_STATUS_FAILED, f.st);
}

void test_resp_complete_unit()
{
    size_t hl = 0;
    const char *partial = "HTTP/1.1 200 OK\r\nContent-Len";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)partial;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(partial);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_FALSE(EdgeFetcherV.ok);
    TEST_ASSERT_EQUAL_UINT(0, hl);

    const char *cl = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)cl;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(cl);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_TRUE(EdgeFetcherV.ok);
    const char *cl_short = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nab";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)cl_short;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(cl_short);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_FALSE(EdgeFetcherV.ok);

    const char *ch_inc = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)ch_inc;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(ch_inc);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_FALSE(EdgeFetcherV.ok);
    const char *ch_ok = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)ch_ok;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(ch_ok);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_TRUE(EdgeFetcherV.ok);

    const char *cd = "HTTP/1.1 200 OK\r\n\r\nsome body";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)cd;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(cd);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_FALSE(EdgeFetcherV.ok);
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)cd;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(cd);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_TRUE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_TRUE(EdgeFetcherV.ok);
}

void test_fetch_send_fail()
{
    MockOrigin m = {(const uint8_t *)"", 0, 0, 0, PROTO_FALSE, 4};
    m.send_fail = PROTO_TRUE;
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    EdgeFetcherV.begin_args.f = &f;
    EdgeFetcherV.begin_args.t = &t;
    EdgeFetcherV.begin_args.host = "h";
    EdgeFetcherV.begin_args.port = 80;
    EdgeFetcherV.begin_args.request = "GET / HTTP/1.1\r\n\r\n";
    EdgeFetcherV.begin_args.req_len = 18;
    EdgeFetcherV.begin_args.now_ms = 1000;
    EdgeFetcher.begin(edge_fetch_work);
    TEST_ASSERT_EQUAL(EDGE_FETCH_STATUS_PENDING, f.st);
    EdgeFetcherV.pump_args.f = &f;
    EdgeFetcherV.pump_args.t = &t;
    EdgeFetcherV.pump_args.now_ms = 1000;
    EdgeFetcher.pump(edge_fetch_work);
    TEST_ASSERT_EQUAL(EDGE_FETCH_STATUS_FAILED, EdgeFetcherV.status);
    TEST_ASSERT_EQUAL_INT(4, f.cid);
}

void test_fetch_end_releases_once()
{
    static const char *R = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi";
    MockOrigin m = {(const uint8_t *)R, strlen(R), 0, 0, PROTO_TRUE, 11};
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    EdgeFetcherV.begin_args.f = &f;
    EdgeFetcherV.begin_args.t = &t;
    EdgeFetcherV.begin_args.host = "h";
    EdgeFetcherV.begin_args.port = 80;
    EdgeFetcherV.begin_args.request = "GET / HTTP/1.1\r\n\r\n";
    EdgeFetcherV.begin_args.req_len = 18;
    EdgeFetcherV.begin_args.now_ms = 1000;
    EdgeFetcher.begin(edge_fetch_work);
    TEST_ASSERT_EQUAL(EDGE_FETCH_STATUS_DONE, run_fetch(&f, &t, 1000));
    EdgeFetcherV.end_args.f = &f;
    EdgeFetcherV.end_args.t = &t;
    EdgeFetcher.end(edge_fetch_work);
    TEST_ASSERT_EQUAL_INT(1, m.close_calls);
    TEST_ASSERT_EQUAL_INT(11, m.last_closed);
    TEST_ASSERT_EQUAL_INT(-1, f.cid);
    EdgeFetcherV.end_args.f = &f;
    EdgeFetcherV.end_args.t = &t;
    EdgeFetcher.end(edge_fetch_work);
    TEST_ASSERT_EQUAL_INT(1, m.close_calls);

    MockOrigin m2 = {(const uint8_t *)"", 0, 0, 0, PROTO_FALSE, -1};
    EdgeFetchTransport t2 = make_transport(&m2);
    EdgeFetch f2;
    EdgeFetcherV.begin_args.f = &f2;
    EdgeFetcherV.begin_args.t = &t2;
    EdgeFetcherV.begin_args.host = "h";
    EdgeFetcherV.begin_args.port = 80;
    EdgeFetcherV.begin_args.request = "GET / HTTP/1.1\r\n\r\n";
    EdgeFetcherV.begin_args.req_len = 18;
    EdgeFetcherV.begin_args.now_ms = 1000;
    EdgeFetcher.begin(edge_fetch_work);
    EdgeFetcherV.end_args.f = &f2;
    EdgeFetcherV.end_args.t = &t2;
    EdgeFetcher.end(edge_fetch_work);
    TEST_ASSERT_EQUAL_INT(0, m2.close_calls);
}

void test_fetch_pump_after_terminal_is_inert()
{
    static const char *R = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
    MockOrigin m = {(const uint8_t *)R, strlen(R), 0, 0, PROTO_TRUE, 6};
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    EdgeFetcherV.begin_args.f = &f;
    EdgeFetcherV.begin_args.t = &t;
    EdgeFetcherV.begin_args.host = "h";
    EdgeFetcherV.begin_args.port = 80;
    EdgeFetcherV.begin_args.request = "GET / HTTP/1.1\r\n\r\n";
    EdgeFetcherV.begin_args.req_len = 18;
    EdgeFetcherV.begin_args.now_ms = 1000;
    EdgeFetcher.begin(edge_fetch_work);
    TEST_ASSERT_EQUAL(EDGE_FETCH_STATUS_DONE, run_fetch(&f, &t, 1000));
    size_t got_before = f.got;
    EdgeFetcherV.pump_args.f = &f;
    EdgeFetcherV.pump_args.t = &t;
    EdgeFetcherV.pump_args.now_ms = 9999999;
    EdgeFetcher.pump(edge_fetch_work);
    TEST_ASSERT_EQUAL(EDGE_FETCH_STATUS_DONE, EdgeFetcherV.status);
    TEST_ASSERT_EQUAL_UINT(got_before, f.got);
}

void test_fetch_malformed_status_line()
{
    static const char *R = "NOT-HTTP AT ALL\r\nX: y\r\n\r\nbody";
    MockOrigin m = {(const uint8_t *)R, strlen(R), 0, 0, PROTO_TRUE, 3};
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    EdgeFetcherV.begin_args.f = &f;
    EdgeFetcherV.begin_args.t = &t;
    EdgeFetcherV.begin_args.host = "h";
    EdgeFetcherV.begin_args.port = 80;
    EdgeFetcherV.begin_args.request = "GET / HTTP/1.1\r\n\r\n";
    EdgeFetcherV.begin_args.req_len = 18;
    EdgeFetcherV.begin_args.now_ms = 1000;
    EdgeFetcher.begin(edge_fetch_work);
    TEST_ASSERT_EQUAL(EDGE_FETCH_STATUS_FAILED, run_fetch(&f, &t, 1000));
}

void test_fetch_closed_before_complete()
{
    static const char *R = "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nonly-this";
    MockOrigin m = {(const uint8_t *)R, strlen(R), 0, 0, PROTO_TRUE, 8};
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    EdgeFetcherV.begin_args.f = &f;
    EdgeFetcherV.begin_args.t = &t;
    EdgeFetcherV.begin_args.host = "h";
    EdgeFetcherV.begin_args.port = 80;
    EdgeFetcherV.begin_args.request = "GET / HTTP/1.1\r\n\r\n";
    EdgeFetcherV.begin_args.req_len = 18;
    EdgeFetcherV.begin_args.now_ms = 1000;
    EdgeFetcher.begin(edge_fetch_work);
    EdgeFetcherV.pump_args.f = &f;
    EdgeFetcherV.pump_args.t = &t;
    EdgeFetcherV.pump_args.now_ms = 1000;
    EdgeFetcher.pump(edge_fetch_work);
    TEST_ASSERT_EQUAL(EDGE_FETCH_STATUS_FAILED, EdgeFetcherV.status);
}

void test_chunked_hex_sizes()
{
    size_t hl = 0;
    const char *lower = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\na\r\n0123456789\r\n0\r\n\r\n";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)lower;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(lower);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_TRUE(EdgeFetcherV.ok);
    const char *upper = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nB\r\n0123456789a\r\n0\r\n\r\n";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)upper;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(upper);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_TRUE(EdgeFetcherV.ok);

    const char *no_lf = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1f";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)no_lf;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(no_lf);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_FALSE(EdgeFetcherV.ok);

    const char *short_data = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhel";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)short_data;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(short_data);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_FALSE(EdgeFetcherV.ok);

    const char *not_hex = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)not_hex;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(not_hex);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_FALSE(EdgeFetcherV.ok);
}

void test_chunked_trailers()
{
    size_t hl = 0;
    const char *trailers = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                           "5\r\nhello\r\n0\r\nX-Checksum: abc\r\nX-Extra: 1\r\n\r\n";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)trailers;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(trailers);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_TRUE(EdgeFetcherV.ok);

    const char *cut = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                      "5\r\nhello\r\n0\r\nX-Checksum: abc\r\n";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)cut;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(cut);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_FALSE(EdgeFetcherV.ok);

    const char *unterminated = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                               "0\r\nX-Checksum: abc";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)unterminated;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(unterminated);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_FALSE(EdgeFetcherV.ok);

    const char *at_end = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)at_end;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(at_end);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_FALSE(EdgeFetcherV.ok);

    const char *bare_cr = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\rX\r\n\r\n";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)bare_cr;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(bare_cr);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_TRUE(EdgeFetcherV.ok);
}

void test_head_end_near_miss_separators()
{
    size_t hl = 0;
    const char *lone_cr = "HTTP/1.1 200 OK\rX: y\r\n";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)lone_cr;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(lone_cr);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_FALSE(EdgeFetcherV.ok);
    TEST_ASSERT_EQUAL_UINT(0, hl);
    const char *crlf_cr = "HTTP/1.1 200 OK\r\n\rX";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)crlf_cr;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(crlf_cr);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_FALSE(EdgeFetcherV.ok);
    const char *lflf = "HTTP/1.1 200 OK\n\nbody";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)lflf;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(lflf);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_FALSE(EdgeFetcherV.ok);
}

void test_unusable_framing_headers_fall_through()
{
    size_t hl = 0;

    const char *bad_cl = "HTTP/1.1 200 OK\r\nContent-Length: abc\r\n\r\nbody";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)bad_cl;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(bad_cl);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_FALSE(EdgeFetcherV.ok);
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)bad_cl;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(bad_cl);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_TRUE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_TRUE(EdgeFetcherV.ok);

    const char *cl_junk = "HTTP/1.1 200 OK\r\nContent-Length: 4bogus\r\n\r\nabcd";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)cl_junk;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(cl_junk);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_TRUE(EdgeFetcherV.ok);

    const char *te_gzip = "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\nrawbytes";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)te_gzip;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(te_gzip);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_FALSE(EdgeFetcherV.ok);
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)te_gzip;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(te_gzip);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_TRUE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_TRUE(EdgeFetcherV.ok);
}

void test_transfer_encoding_case_and_length_bounds()
{
    size_t hl = 0;
    const char *mixed = "HTTP/1.1 200 OK\r\nTransfer-Encoding: ChUnKeD\r\n\r\n0\r\n\r\n";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)mixed;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(mixed);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_TRUE(EdgeFetcherV.ok);

    const char *punct = "HTTP/1.1 200 OK\r\nTransfer-Encoding: x-1_2, chunked\r\n\r\n0\r\n\r\n";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)punct;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(punct);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_TRUE(EdgeFetcherV.ok);

    const char *overlong = "HTTP/1.1 200 OK\r\n"
                           "Transfer-Encoding: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa, chunked\r\n"
                           "\r\nbody";
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)overlong;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(overlong);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_FALSE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_FALSE(EdgeFetcherV.ok);
    EdgeFetcherV.edge_resp_complete_args.buf = (const uint8_t *)overlong;
    EdgeFetcherV.edge_resp_complete_args.len = strlen(overlong);
    EdgeFetcherV.edge_resp_complete_args.conn_closed = PROTO_TRUE;
    EdgeFetcherV.edge_resp_complete_args.head_len = &hl;
    EdgeFetcher.edge_resp_complete(edge_fetch_work);
    TEST_ASSERT_TRUE(EdgeFetcherV.ok);
}
