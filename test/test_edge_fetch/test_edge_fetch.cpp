// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

// Host tests for the CDN edge-cache async origin-fetch engine (services/web/edge_cache/edge_fetch): the
// completion detector and the state machine, driven over a scripted mock transport.

#include "services/web/edge_cache/edge_fetch.h"
#include <stdint.h>
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// --- scripted mock origin transport --------------------------------------------------------------

struct MockOrigin
{
    const uint8_t *data;
    size_t len;
    size_t cursor;
    size_t throttle; // max bytes per read (0 = as much as fits)
    bool closes;     // origin closes its side once everything is delivered
    int open_ret;    // cid to hand back (or < 0 to fail the open)
    // Trailing fields: aggregate initialisers above supply only the six leading members and these
    // are value-initialised, so "send works, nothing closed yet" stays the default.
    bool send_fail;  // make send() report failure (the request never leaves)
    int close_calls; // how many times the engine closed the connection
    int last_closed; // cid passed to the last close()
};

static int m_open(void *c, const char *h, uint16_t p, uint32_t t)
{
    (void)h;
    (void)p;
    (void)t;
    return ((MockOrigin *)c)->open_ret;
}
static bool m_send(void *c, int cid, const void *d, size_t l)
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
static bool m_closed(void *c, int cid)
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

static EdgeFetchTransport make_transport(MockOrigin *m)
{
    EdgeFetchTransport t;
    t.open = m_open;
    t.send = m_send;
    t.read = m_read;
    t.closed = m_closed;
    t.close = m_close;
    t.ctx = m;
    return t;
}

// Pump to a terminal state (bounded iterations); returns the final status.
static EdgeFetchStatus run_fetch(EdgeFetch *f, const EdgeFetchTransport *t, uint32_t now)
{
    for (int i = 0; i < 100000 && f->st == EdgeFetchStatus::PENDING; i++)
    {
        edge_fetch_pump(f, t, now);
    }
    return f->st;
}

// --- tests ---------------------------------------------------------------------------------------

static void test_fetch_content_length()
{
    static const char *R = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Type: text/plain\r\n\r\nhello";
    MockOrigin m = {(const uint8_t *)R, strlen(R), 0, 4, true, 7}; // 4 bytes/read (multi-pump)
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    edge_fetch_begin(&f, &t, "h", 80, "GET / HTTP/1.1\r\n\r\n", 18, 1000);
    TEST_ASSERT_EQUAL(EdgeFetchStatus::DONE, run_fetch(&f, &t, 1000));
    TEST_ASSERT_EQUAL_INT(200, f.status);
    TEST_ASSERT_EQUAL_UINT(5, f.body_len);
    TEST_ASSERT_EQUAL_MEMORY("hello", f.buf + f.body_off, 5);
}

static void test_fetch_chunked()
{
    static const char *R =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n";
    MockOrigin m = {(const uint8_t *)R, strlen(R), 0, 3, true, 9};
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    edge_fetch_begin(&f, &t, "h", 80, "GET / HTTP/1.1\r\n\r\n", 18, 1000);
    TEST_ASSERT_EQUAL(EdgeFetchStatus::DONE, run_fetch(&f, &t, 1000));
    TEST_ASSERT_EQUAL_INT(200, f.status);
    TEST_ASSERT_EQUAL_UINT(11, f.body_len);
    TEST_ASSERT_EQUAL_MEMORY("hello world", f.buf + f.body_off, 11);
}

static void test_fetch_close_delimited()
{
    static const char *R = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nbody-till-close";
    MockOrigin m = {(const uint8_t *)R, strlen(R), 0, 0, true, 5}; // no CL / chunked -> ends on close
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    edge_fetch_begin(&f, &t, "h", 80, "GET / HTTP/1.1\r\n\r\n", 18, 1000);
    TEST_ASSERT_EQUAL(EdgeFetchStatus::DONE, run_fetch(&f, &t, 1000));
    TEST_ASSERT_EQUAL_INT(200, f.status);
    TEST_ASSERT_EQUAL_MEMORY("body-till-close", f.buf + f.body_off, 15);
}

static void test_fetch_oversize()
{
    static uint8_t big[PC_EDGE_FETCH_BUF + 1024];
    const char *head = "HTTP/1.1 200 OK\r\nContent-Length: 6000\r\n\r\n";
    size_t hl = strlen(head);
    memcpy(big, head, hl);
    memset(big + hl, 'x', sizeof(big) - hl); // body larger than the fetch buffer
    MockOrigin m = {big, sizeof(big), 0, 0, false, 3};
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    edge_fetch_begin(&f, &t, "h", 80, "GET / HTTP/1.1\r\n\r\n", 18, 1000);
    TEST_ASSERT_EQUAL(EdgeFetchStatus::OVERSIZE, run_fetch(&f, &t, 1000));
}

static void test_fetch_timeout()
{
    static const char *R = "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\npartial"; // never completes
    MockOrigin m = {(const uint8_t *)R, strlen(R), 0, 0, false, 2};
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    edge_fetch_begin(&f, &t, "h", 80, "GET / HTTP/1.1\r\n\r\n", 18, 1000);
    TEST_ASSERT_EQUAL(EdgeFetchStatus::PENDING, edge_fetch_pump(&f, &t, 1000)); // drained, still waiting
    TEST_ASSERT_EQUAL(EdgeFetchStatus::FAILED, edge_fetch_pump(&f, &t, 1000 + PC_EDGE_FETCH_TIMEOUT_MS));
}

static void test_fetch_open_fail()
{
    MockOrigin m = {(const uint8_t *)"", 0, 0, 0, false, -1}; // open returns < 0
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    edge_fetch_begin(&f, &t, "h", 80, "GET / HTTP/1.1\r\n\r\n", 18, 1000);
    TEST_ASSERT_EQUAL(EdgeFetchStatus::FAILED, f.st);
}

static void test_resp_complete_unit()
{
    size_t hl = 0;
    const char *partial = "HTTP/1.1 200 OK\r\nContent-Len"; // header block not terminated
    TEST_ASSERT_FALSE(edge_resp_complete((const uint8_t *)partial, strlen(partial), false, &hl));
    TEST_ASSERT_EQUAL_UINT(0, hl);

    const char *cl = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc";
    TEST_ASSERT_TRUE(edge_resp_complete((const uint8_t *)cl, strlen(cl), false, &hl));
    const char *cl_short = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nab";
    TEST_ASSERT_FALSE(edge_resp_complete((const uint8_t *)cl_short, strlen(cl_short), false, &hl));

    const char *ch_inc = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n"; // no 0-chunk
    TEST_ASSERT_FALSE(edge_resp_complete((const uint8_t *)ch_inc, strlen(ch_inc), false, &hl));
    const char *ch_ok = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n";
    TEST_ASSERT_TRUE(edge_resp_complete((const uint8_t *)ch_ok, strlen(ch_ok), false, &hl));

    // close-delimited: incomplete until the peer closes
    const char *cd = "HTTP/1.1 200 OK\r\n\r\nsome body";
    TEST_ASSERT_FALSE(edge_resp_complete((const uint8_t *)cd, strlen(cd), false, &hl));
    TEST_ASSERT_TRUE(edge_resp_complete((const uint8_t *)cd, strlen(cd), true, &hl));
}

// The request could not be handed to the transport: the fetch fails immediately, without waiting
// out the timeout on a connection the origin will never answer.
static void test_fetch_send_fail()
{
    MockOrigin m = {(const uint8_t *)"", 0, 0, 0, false, 4};
    m.send_fail = true;
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    edge_fetch_begin(&f, &t, "h", 80, "GET / HTTP/1.1\r\n\r\n", 18, 1000);
    TEST_ASSERT_EQUAL(EdgeFetchStatus::FAILED, f.st);
    TEST_ASSERT_EQUAL_INT(4, f.cid); // the connection was opened, so it still has to be released
}

// edge_fetch_end releases the connection exactly once and is safe to call again (including after a
// failed open, where there is no connection to release).
static void test_fetch_end_releases_once()
{
    static const char *R = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi";
    MockOrigin m = {(const uint8_t *)R, strlen(R), 0, 0, true, 11};
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    edge_fetch_begin(&f, &t, "h", 80, "GET / HTTP/1.1\r\n\r\n", 18, 1000);
    TEST_ASSERT_EQUAL(EdgeFetchStatus::DONE, run_fetch(&f, &t, 1000));
    edge_fetch_end(&f, &t);
    TEST_ASSERT_EQUAL_INT(1, m.close_calls);
    TEST_ASSERT_EQUAL_INT(11, m.last_closed);
    TEST_ASSERT_EQUAL_INT(-1, f.cid); // cleared, so a second end() is a no-op
    edge_fetch_end(&f, &t);
    TEST_ASSERT_EQUAL_INT(1, m.close_calls);

    // Never connected: nothing to close.
    MockOrigin m2 = {(const uint8_t *)"", 0, 0, 0, false, -1};
    EdgeFetchTransport t2 = make_transport(&m2);
    EdgeFetch f2;
    edge_fetch_begin(&f2, &t2, "h", 80, "GET / HTTP/1.1\r\n\r\n", 18, 1000);
    edge_fetch_end(&f2, &t2);
    TEST_ASSERT_EQUAL_INT(0, m2.close_calls);
}

// Pumping a fetch that has already settled returns the settled status without touching the
// transport again (the engine is polled from a loop that may not stop on the first terminal state).
static void test_fetch_pump_after_terminal_is_inert()
{
    static const char *R = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
    MockOrigin m = {(const uint8_t *)R, strlen(R), 0, 0, true, 6};
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    edge_fetch_begin(&f, &t, "h", 80, "GET / HTTP/1.1\r\n\r\n", 18, 1000);
    TEST_ASSERT_EQUAL(EdgeFetchStatus::DONE, run_fetch(&f, &t, 1000));
    size_t got_before = f.got;
    TEST_ASSERT_EQUAL(EdgeFetchStatus::DONE, edge_fetch_pump(&f, &t, 9999999));
    TEST_ASSERT_EQUAL_UINT(got_before, f.got); // no further reads
}

// A complete header block that is not a valid status line: the parser rejects it and the fetch
// fails rather than caching garbage.
static void test_fetch_malformed_status_line()
{
    static const char *R = "NOT-HTTP AT ALL\r\nX: y\r\n\r\nbody";
    MockOrigin m = {(const uint8_t *)R, strlen(R), 0, 0, true, 3}; // closes -> completion detected
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    edge_fetch_begin(&f, &t, "h", 80, "GET / HTTP/1.1\r\n\r\n", 18, 1000);
    TEST_ASSERT_EQUAL(EdgeFetchStatus::FAILED, run_fetch(&f, &t, 1000));
}

// The origin hangs up mid-body: a Content-Length that never arrives fails on the close, not on the
// timeout (which is what the close check is there to short-circuit).
static void test_fetch_closed_before_complete()
{
    static const char *R = "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nonly-this";
    MockOrigin m = {(const uint8_t *)R, strlen(R), 0, 0, true, 8};
    EdgeFetchTransport t = make_transport(&m);
    EdgeFetch f;
    edge_fetch_begin(&f, &t, "h", 80, "GET / HTTP/1.1\r\n\r\n", 18, 1000);
    TEST_ASSERT_EQUAL(EdgeFetchStatus::FAILED, edge_fetch_pump(&f, &t, 1000)); // well inside the timeout
}

// Chunk sizes are hexadecimal: the a-f digits (either case) and a size wide enough to need them.
static void test_chunked_hex_sizes()
{
    size_t hl = 0;
    const char *lower = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\na\r\n0123456789\r\n0\r\n\r\n";
    TEST_ASSERT_TRUE(edge_resp_complete((const uint8_t *)lower, strlen(lower), false, &hl));
    const char *upper = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nB\r\n0123456789a\r\n0\r\n\r\n";
    TEST_ASSERT_TRUE(edge_resp_complete((const uint8_t *)upper, strlen(upper), false, &hl));
    // A size line whose digits run to the end of the buffer: no LF yet, so not complete.
    const char *no_lf = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1f";
    TEST_ASSERT_FALSE(edge_resp_complete((const uint8_t *)no_lf, strlen(no_lf), false, &hl));
    // Declared size larger than the bytes actually present.
    const char *short_data = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhel";
    TEST_ASSERT_FALSE(edge_resp_complete((const uint8_t *)short_data, strlen(short_data), false, &hl));
    // A chunk size that is not hex at all.
    const char *not_hex = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n";
    TEST_ASSERT_FALSE(edge_resp_complete((const uint8_t *)not_hex, strlen(not_hex), false, &hl));
}

// The zero chunk may be followed by trailer header lines; the body ends at the empty line AFTER
// them, so a trailer section still pending means the response is not complete yet.
static void test_chunked_trailers()
{
    size_t hl = 0;
    const char *trailers = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                           "5\r\nhello\r\n0\r\nX-Checksum: abc\r\nX-Extra: 1\r\n\r\n";
    TEST_ASSERT_TRUE(edge_resp_complete((const uint8_t *)trailers, strlen(trailers), false, &hl));
    // Same message cut off mid-trailer: the terminating empty line has not arrived.
    const char *cut = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                      "5\r\nhello\r\n0\r\nX-Checksum: abc\r\n";
    TEST_ASSERT_FALSE(edge_resp_complete((const uint8_t *)cut, strlen(cut), false, &hl));
    // A trailer line with no line ending at all.
    const char *unterminated = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                               "0\r\nX-Checksum: abc";
    TEST_ASSERT_FALSE(edge_resp_complete((const uint8_t *)unterminated, strlen(unterminated), false, &hl));
    // The buffer ends immediately after the zero-chunk size line: the trailer scan starts already at
    // the end, with no room for the terminating CRLF.
    const char *at_end = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n";
    TEST_ASSERT_FALSE(edge_resp_complete((const uint8_t *)at_end, strlen(at_end), false, &hl));
    // A trailer line starting with a bare CR that is NOT the CRLF ending the section: the scan must
    // treat it as ordinary line content and run on to the real terminator.
    const char *bare_cr = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\rX\r\n\r\n";
    TEST_ASSERT_TRUE(edge_resp_complete((const uint8_t *)bare_cr, strlen(bare_cr), false, &hl));
}

// head_end scans for CRLFCRLF; a lone CR, a CR-LF-CR that is not followed by LF, and a bare LFLF
// separator must all fail to terminate the header block.
static void test_head_end_near_miss_separators()
{
    size_t hl = 0;
    const char *lone_cr = "HTTP/1.1 200 OK\rX: y\r\n";
    TEST_ASSERT_FALSE(edge_resp_complete((const uint8_t *)lone_cr, strlen(lone_cr), false, &hl));
    TEST_ASSERT_EQUAL_UINT(0, hl);
    const char *crlf_cr = "HTTP/1.1 200 OK\r\n\rX"; // CRLF then CR but no final LF
    TEST_ASSERT_FALSE(edge_resp_complete((const uint8_t *)crlf_cr, strlen(crlf_cr), false, &hl));
    const char *lflf = "HTTP/1.1 200 OK\n\nbody"; // bare LFs are not the HTTP terminator
    TEST_ASSERT_FALSE(edge_resp_complete((const uint8_t *)lflf, strlen(lflf), false, &hl));
}

// Content-Length that is present but unusable, and Transfer-Encoding that is present but not
// chunked: in both cases the framing falls through to close-delimited rather than being trusted.
static void test_unusable_framing_headers_fall_through()
{
    size_t hl = 0;
    // Non-numeric Content-Length: no digits consumed, so the length is not believed.
    const char *bad_cl = "HTTP/1.1 200 OK\r\nContent-Length: abc\r\n\r\nbody";
    TEST_ASSERT_FALSE(edge_resp_complete((const uint8_t *)bad_cl, strlen(bad_cl), false, &hl));
    TEST_ASSERT_TRUE(edge_resp_complete((const uint8_t *)bad_cl, strlen(bad_cl), true, &hl));
    // Digits then trailing junk: the digits still count (the loop stops at the non-digit).
    const char *cl_junk = "HTTP/1.1 200 OK\r\nContent-Length: 4bogus\r\n\r\nabcd";
    TEST_ASSERT_TRUE(edge_resp_complete((const uint8_t *)cl_junk, strlen(cl_junk), false, &hl));
    // Transfer-Encoding present but not chunked -> close-delimited, not chunk-scanned.
    const char *te_gzip = "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\nrawbytes";
    TEST_ASSERT_FALSE(edge_resp_complete((const uint8_t *)te_gzip, strlen(te_gzip), false, &hl));
    TEST_ASSERT_TRUE(edge_resp_complete((const uint8_t *)te_gzip, strlen(te_gzip), true, &hl));
}

// has_chunked lower-cases into a fixed 40-byte scratch: mixed case must match, and a value longer
// than the scratch must be truncated safely rather than overrun it.
static void test_transfer_encoding_case_and_length_bounds()
{
    size_t hl = 0;
    const char *mixed = "HTTP/1.1 200 OK\r\nTransfer-Encoding: ChUnKeD\r\n\r\n0\r\n\r\n";
    TEST_ASSERT_TRUE(edge_resp_complete((const uint8_t *)mixed, strlen(mixed), false, &hl));
    // Non-alphabetic bytes take the other arm of the upper-case test.
    const char *punct = "HTTP/1.1 200 OK\r\nTransfer-Encoding: x-1_2, chunked\r\n\r\n0\r\n\r\n";
    TEST_ASSERT_TRUE(edge_resp_complete((const uint8_t *)punct, strlen(punct), false, &hl));
    // Longer than has_chunked's 40-byte scratch, with "chunked" pushed past the cut: the copy stops
    // at the bound, so it is not seen and the body is treated as close-delimited.
    const char *overlong = "HTTP/1.1 200 OK\r\n"
                           "Transfer-Encoding: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa, chunked\r\n"
                           "\r\nbody";
    TEST_ASSERT_FALSE(edge_resp_complete((const uint8_t *)overlong, strlen(overlong), false, &hl));
    TEST_ASSERT_TRUE(edge_resp_complete((const uint8_t *)overlong, strlen(overlong), true, &hl));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_fetch_content_length);
    RUN_TEST(test_fetch_chunked);
    RUN_TEST(test_fetch_close_delimited);
    RUN_TEST(test_fetch_oversize);
    RUN_TEST(test_fetch_timeout);
    RUN_TEST(test_fetch_open_fail);
    RUN_TEST(test_resp_complete_unit);
    RUN_TEST(test_fetch_send_fail);
    RUN_TEST(test_fetch_end_releases_once);
    RUN_TEST(test_fetch_pump_after_terminal_is_inert);
    RUN_TEST(test_fetch_malformed_status_line);
    RUN_TEST(test_fetch_closed_before_complete);
    RUN_TEST(test_chunked_hex_sizes);
    RUN_TEST(test_chunked_trailers);
    RUN_TEST(test_head_end_near_miss_separators);
    RUN_TEST(test_unusable_framing_headers_fall_through);
    RUN_TEST(test_transfer_encoding_case_and_length_bounds);
    return UNITY_END();
}
