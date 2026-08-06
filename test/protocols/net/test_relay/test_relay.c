// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the TCP relay / DNAT byte pump (services/net/relay): bidirectional transfer, the
// backpressure carry (a send that accepts partial writes), independent half-close with shutdown
// propagation, a large multi-step transfer (byte-exact), and a seam error. Two mock sockets stand
// in for the inbound and origin connections.

#include "services/net/relay/relay.h"
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// A mock socket: `in` is what the relay reads from this peer; `out` is what the relay writes to it.
typedef struct
{
    uint8_t in[4096];
    size_t in_len, in_pos;
    proto_bool in_eof; // recv returns <0 (EOF) once `in` is drained
    uint8_t out[4096];
    size_t out_len;
    size_t send_cap; // max bytes a single send accepts (0 = unlimited) - drives backpressure
    proto_bool fail_send;
    proto_bool shutdown_called;
} MockSock;

static int msock_recv(void *c, uint8_t *buf, size_t cap)
{
    MockSock *s = (MockSock *)c;
    if (s->in_pos < s->in_len)
    {
        size_t n = s->in_len - s->in_pos;
        if (n > cap)
        {
            n = cap;
        }
        memcpy(buf, s->in + s->in_pos, n);
        s->in_pos += n;
        return (int)n;
    }
    return s->in_eof ? -1 : 0;
}

static int msock_send(void *c, const uint8_t *buf, size_t len)
{
    MockSock *s = (MockSock *)c;
    if (s->fail_send)
    {
        return -1;
    }
    size_t n = len;
    if (s->send_cap && n > s->send_cap)
    {
        n = s->send_cap;
    }
    if (s->out_len + n > sizeof(s->out))
    {
        n = sizeof(s->out) - s->out_len;
    }
    memcpy(s->out + s->out_len, buf, n);
    s->out_len += n;
    return (int)n;
}

static void msock_shutdown(void *c)
{
    ((MockSock *)c)->shutdown_called = PROTO_TRUE;
}

static void sock_init(MockSock *s, const void *in, size_t in_len, proto_bool eof)
{
    memset(s, 0, sizeof(*s));
    if (in_len)
    {
        memcpy(s->in, in, in_len);
    }
    s->in_len = in_len;
    s->in_eof = eof;
}

static pc_relay_end end_of(MockSock *s)
{
    pc_relay_end e;
    e.recv = msock_recv;
    e.send = msock_send;
    e.shutdown = msock_shutdown;
    e.ctx = s;
    return e;
}

static pc_relay_status run_relay(pc_relay *r, int max_steps)
{
    for (int i = 0; i < max_steps; i++)
    {
        pc_relay_status st = pc_relay_step(r);
        if (st != PC_RELAY_RUNNING)
        {
            return st;
        }
    }
    return PC_RELAY_RUNNING; // never finished (a bug if it happens)
}

void test_bidirectional()
{
    MockSock a, b;
    sock_init(&a, "hello from client", 17, PROTO_TRUE);
    sock_init(&b, "hi from origin", 14, PROTO_TRUE);
    pc_relay_end ea = end_of(&a), eb = end_of(&b);
    pc_relay r;
    pc_relay_init(&r, &ea, &eb);

    TEST_ASSERT_EQUAL_INT(PC_RELAY_DONE, run_relay(&r, 64));
    TEST_ASSERT_EQUAL_size_t(17, b.out_len);
    TEST_ASSERT_EQUAL_MEMORY("hello from client", b.out, 17);
    TEST_ASSERT_EQUAL_size_t(14, a.out_len);
    TEST_ASSERT_EQUAL_MEMORY("hi from origin", a.out, 14);
    TEST_ASSERT_EQUAL_UINT32(17, r.bytes_a2b);
    TEST_ASSERT_EQUAL_UINT32(14, r.bytes_b2a);
}

void test_backpressure()
{
    uint8_t data[1000];
    for (int i = 0; i < 1000; i++)
    {
        data[i] = (uint8_t)(i * 37 + 11);
    }
    MockSock a, b;
    sock_init(&a, data, sizeof(data), PROTO_TRUE);
    sock_init(&b, NULL, 0, PROTO_TRUE);
    b.send_cap = 7; // the origin accepts only 7 bytes per write
    pc_relay_end ea = end_of(&a), eb = end_of(&b);
    pc_relay r;
    pc_relay_init(&r, &ea, &eb);

    TEST_ASSERT_EQUAL_INT(PC_RELAY_DONE, run_relay(&r, 1000));
    TEST_ASSERT_EQUAL_size_t(1000, b.out_len);
    TEST_ASSERT_EQUAL_MEMORY(data, b.out, 1000); // every byte carried across, in order
}

void test_half_close_shutdown()
{
    uint8_t resp[800];
    for (int i = 0; i < 800; i++)
    {
        resp[i] = (uint8_t)(i ^ 0x3C);
    }
    MockSock a, b;
    sock_init(&a, "req", 3, PROTO_TRUE);           // client sends a short request then closes
    sock_init(&b, resp, sizeof(resp), PROTO_TRUE); // origin streams a long response
    a.send_cap = 64; // client drains slowly, so b->a stays multi-step regardless of PC_RELAY_BUF
    pc_relay_end ea = end_of(&a), eb = end_of(&b);
    pc_relay r;
    pc_relay_init(&r, &ea, &eb);

    // once a->b finishes (client EOF) the origin's half-close must fire, while b->a is still
    // streaming its long response (proving the two directions close independently)
    pc_relay_status st = PC_RELAY_RUNNING;
    for (int i = 0; i < 10 && !r.b_shut_sent; i++)
    {
        st = pc_relay_step(&r);
    }
    TEST_ASSERT_TRUE(r.b_shut_sent); // origin's shutdown fired on the client's FIN
    TEST_ASSERT_TRUE(b.shutdown_called);
    TEST_ASSERT_FALSE(r.b2a_done); // ...while the response direction is still open
    TEST_ASSERT_EQUAL_INT(PC_RELAY_RUNNING, st);

    TEST_ASSERT_EQUAL_INT(PC_RELAY_DONE, run_relay(&r, 64));
    TEST_ASSERT_EQUAL_MEMORY("req", b.out, 3);
    TEST_ASSERT_EQUAL_size_t(800, a.out_len);
    TEST_ASSERT_EQUAL_MEMORY(resp, a.out, 800);
    TEST_ASSERT_TRUE(a.shutdown_called); // the client's write side was closed too, once b EOF'd
}

void test_send_error()
{
    MockSock a, b;
    sock_init(&a, "data", 4, PROTO_TRUE);
    sock_init(&b, NULL, 0, PROTO_TRUE);
    b.fail_send = PROTO_TRUE; // the origin's send errors
    pc_relay_end ea = end_of(&a), eb = end_of(&b);
    pc_relay r;
    pc_relay_init(&r, &ea, &eb);

    pc_relay_status st = PC_RELAY_RUNNING;
    for (int i = 0; i < 8 && st == PC_RELAY_RUNNING; i++)
    {
        st = pc_relay_step(&r);
    }
    TEST_ASSERT_EQUAL_INT(PC_RELAY_ERROR, st);
}

void test_one_way_idle_then_close()
{
    // origin never sends; client sends then closes -> relay completes cleanly
    MockSock a, b;
    sock_init(&a, "GET / HTTP/1.0\r\n\r\n", 18, PROTO_TRUE);
    sock_init(&b, NULL, 0, PROTO_TRUE);
    pc_relay_end ea = end_of(&a), eb = end_of(&b);
    pc_relay r;
    pc_relay_init(&r, &ea, &eb);

    TEST_ASSERT_EQUAL_INT(PC_RELAY_DONE, run_relay(&r, 32));
    TEST_ASSERT_EQUAL_size_t(18, b.out_len);
    TEST_ASSERT_EQUAL_size_t(0, a.out_len);
}

// A transport that signals close out of band (like pc_conn's on_close) rather than via recv < 0:
// the mocks never EOF through recv; pc_relay_note_eof() drives the finish.
void test_note_eof_out_of_band()
{
    MockSock a, b;
    sock_init(&a, "hello", 5, PROTO_FALSE); // in_eof=false: recv returns 0 (not -1) when drained
    sock_init(&b, "world", 5, PROTO_FALSE);
    pc_relay_end ea = end_of(&a), eb = end_of(&b);
    pc_relay r;
    pc_relay_init(&r, &ea, &eb);

    // one step moves the buffered data each way; without an EOF signal the relay keeps running
    TEST_ASSERT_EQUAL_INT(PC_RELAY_RUNNING, pc_relay_step(&r));
    TEST_ASSERT_EQUAL_MEMORY("hello", b.out, 5);
    TEST_ASSERT_EQUAL_MEMORY("world", a.out, 5);

    // both peers close out of band -> the relay finishes and both shutdowns fire
    pc_relay_note_eof(&r, PROTO_FALSE); // inbound closed
    pc_relay_note_eof(&r, PROTO_TRUE);  // origin closed
    TEST_ASSERT_EQUAL_INT(PC_RELAY_DONE, run_relay(&r, 8));
    TEST_ASSERT_TRUE(a.shutdown_called);
    TEST_ASSERT_TRUE(b.shutdown_called);
}

// The origin never has data ready (0-length read, not EOF): pump_refill's "r > 0" branch must take
// its false side without treating that as an error or as EOF - the relay just stays RUNNING and the
// buffers are left untouched until real data (or an out-of-band EOF) arrives.
void test_zero_length_read_no_progress()
{
    MockSock a, b;
    sock_init(&a, NULL, 0, PROTO_FALSE); // in_eof=false: recv keeps returning 0, never -1
    sock_init(&b, NULL, 0, PROTO_FALSE);
    pc_relay_end ea = end_of(&a), eb = end_of(&b);
    pc_relay r;
    pc_relay_init(&r, &ea, &eb);

    for (int i = 0; i < 5; i++)
    {
        TEST_ASSERT_EQUAL_INT(PC_RELAY_RUNNING, pc_relay_step(&r));
    }
    TEST_ASSERT_EQUAL_UINT32(0, r.bytes_a2b);
    TEST_ASSERT_EQUAL_UINT32(0, r.bytes_b2a);
    TEST_ASSERT_EQUAL_size_t(0, a.out_len);
    TEST_ASSERT_EQUAL_size_t(0, b.out_len);

    // now let both sides close out of band so the relay can still finish cleanly
    pc_relay_note_eof(&r, PROTO_FALSE);
    pc_relay_note_eof(&r, PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(PC_RELAY_DONE, run_relay(&r, 8));
}

// A send that already carried a partial write (backpressure) fails on the *next* step's flush
// attempt (pump()'s own send call, not pump_refill's) - distinct from test_send_error, which fails
// on the very first refill.
void test_flush_send_error()
{
    uint8_t data[50];
    for (int i = 0; i < 50; i++)
    {
        data[i] = (uint8_t)(i + 1);
    }
    MockSock a, b;
    sock_init(&a, data, sizeof(data), PROTO_TRUE);
    sock_init(&b, NULL, 0, PROTO_TRUE);
    b.send_cap = 10; // first step only flushes part of the 50 bytes, leaving a backlog
    pc_relay_end ea = end_of(&a), eb = end_of(&b);
    pc_relay r;
    pc_relay_init(&r, &ea, &eb);

    TEST_ASSERT_EQUAL_INT(PC_RELAY_RUNNING, pc_relay_step(&r));
    TEST_ASSERT_TRUE(r.a2b_off < r.a2b_len); // confirms a backlog is pending for the next flush

    b.fail_send = PROTO_TRUE; // now the origin errors on the flush of that pending backlog
    TEST_ASSERT_EQUAL_INT(PC_RELAY_ERROR, pc_relay_step(&r));
}

// test_send_error fails the a->b direction (the first pump() call in pc_relay_step); this mirrors
// it for the b->a direction so the second pump() call's error path is exercised too.
void test_send_error_reverse_direction()
{
    MockSock a, b;
    sock_init(&a, NULL, 0, PROTO_TRUE);   // client has nothing to send: a->b finishes immediately, cleanly
    sock_init(&b, "resp", 4, PROTO_TRUE); // origin has data for the client
    a.fail_send = PROTO_TRUE;             // the client's send (dst for b->a) errors
    pc_relay_end ea = end_of(&a), eb = end_of(&b);
    pc_relay r;
    pc_relay_init(&r, &ea, &eb);

    TEST_ASSERT_EQUAL_INT(PC_RELAY_ERROR, pc_relay_step(&r));
}

// Null-argument guards: every entry point bails out safely instead of dereferencing.
void test_null_argument_guards()
{
    MockSock a, b;
    sock_init(&a, NULL, 0, PROTO_TRUE);
    sock_init(&b, NULL, 0, PROTO_TRUE);
    pc_relay_end ea = end_of(&a), eb = end_of(&b);
    pc_relay r;

    pc_relay_init(NULL, &ea, &eb); // no crash
    pc_relay_init(&r, NULL, &eb);  // no crash
    pc_relay_init(&r, &ea, NULL);  // no crash

    TEST_ASSERT_EQUAL_INT(PC_RELAY_ERROR, pc_relay_step(NULL));

    pc_relay_note_eof(NULL, PROTO_FALSE); // no crash
}

// An end with no shutdown seam (shutdown == NULL, allowed per relay.h) must not be called through
// when its direction finishes; the flag it would have set stays false. The peer direction still has a
// real shutdown seam, so that one still fires normally.
void test_shutdown_null_seam()
{
    MockSock a, b;
    sock_init(&a, "hi", 2, PROTO_TRUE);
    sock_init(&b, NULL, 0, PROTO_TRUE);
    pc_relay_end ea = end_of(&a);
    pc_relay_end eb = end_of(&b);
    eb.shutdown = NULL; // origin publishes no shutdown seam
    pc_relay r;
    pc_relay_init(&r, &ea, &eb);

    TEST_ASSERT_EQUAL_INT(PC_RELAY_DONE, run_relay(&r, 16));
    TEST_ASSERT_FALSE(r.b_shut_sent); // never latches: there is no seam to call
    TEST_ASSERT_FALSE(b.shutdown_called);
    TEST_ASSERT_TRUE(r.a_shut_sent); // the other direction's real seam still fired
    TEST_ASSERT_TRUE(a.shutdown_called);
}

// An out-of-band EOF (pc_relay_note_eof) can land while a send backlog is still draining (unlike an
// EOF discovered through recv(), which only ever happens once the buffer is already empty). The
// direction must NOT finish until that backlog fully flushes on a later step.
void test_note_eof_with_backlog_pending()
{
    uint8_t data[20];
    for (int i = 0; i < 20; i++)
    {
        data[i] = (uint8_t)(i + 100);
    }
    MockSock a, b;
    sock_init(&a, data, sizeof(data), PROTO_FALSE); // in_eof=false: only note_eof() signals EOF
    sock_init(&b, NULL, 0, PROTO_TRUE);             // b->a finishes immediately, out of the way
    b.send_cap = 5;                                 // a->b needs multiple flushes to drain 20 bytes
    pc_relay_end ea = end_of(&a), eb = end_of(&b);
    pc_relay r;
    pc_relay_init(&r, &ea, &eb);

    TEST_ASSERT_EQUAL_INT(PC_RELAY_RUNNING, pc_relay_step(&r));
    TEST_ASSERT_EQUAL_size_t(5, r.a2b_off);
    TEST_ASSERT_EQUAL_size_t(20, r.a2b_len); // a backlog of 15 bytes is still pending

    pc_relay_note_eof(&r, PROTO_FALSE); // client EOF arrives out of band while the backlog is pending
    TEST_ASSERT_FALSE(r.a2b_done);      // must not finish yet: bytes are still queued to flush

    TEST_ASSERT_EQUAL_INT(PC_RELAY_DONE, run_relay(&r, 16));
    TEST_ASSERT_EQUAL_size_t(20, b.out_len);
    TEST_ASSERT_EQUAL_MEMORY(data, b.out, 20); // every byte still carried across, in order
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_bidirectional);
    RUN_TEST(test_backpressure);
    RUN_TEST(test_half_close_shutdown);
    RUN_TEST(test_send_error);
    RUN_TEST(test_one_way_idle_then_close);
    RUN_TEST(test_note_eof_out_of_band);
    RUN_TEST(test_zero_length_read_no_progress);
    RUN_TEST(test_flush_send_error);
    RUN_TEST(test_send_error_reverse_direction);
    RUN_TEST(test_null_argument_guards);
    RUN_TEST(test_shutdown_null_seam);
    RUN_TEST(test_note_eof_with_backlog_pending);
    return UNITY_END();
}
