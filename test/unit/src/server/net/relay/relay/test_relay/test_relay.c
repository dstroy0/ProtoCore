// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "server/net/relay/relay/relay.h"
#include <string.h>

#include <unity.h>

static uint8_t relay_work[16]; // the borrow an entry takes; Relay never reads it

void setUp()
{
}
void tearDown()
{
}

typedef struct
{
    uint8_t in[4096];
    size_t in_len, in_pos;
    proto_bool in_eof;
    uint8_t out[4096];
    size_t out_len;
    size_t send_cap;
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

static protocore_relay_end end_of(MockSock *s)
{
    protocore_relay_end e;
    e.recv = msock_recv;
    e.send = msock_send;
    e.shutdown = msock_shutdown;
    e.ctx = s;
    return e;
}

static protocore_relay_status run_relay(protocore_relay *r, int max_steps)
{
    for (int i = 0; i < max_steps; i++)
    {
        RelayV.step_args.r = r;
        Relay.step(relay_work);
        protocore_relay_status st = RelayV.status;
        if (st != PROTOCORE_RELAY_RUNNING)
        {
            return st;
        }
    }
    return PROTOCORE_RELAY_RUNNING;
}

void test_bidirectional()
{
    MockSock a, b;
    sock_init(&a, "hello from client", 17, PROTO_TRUE);
    sock_init(&b, "hi from origin", 14, PROTO_TRUE);
    protocore_relay_end ea = end_of(&a), eb = end_of(&b);
    protocore_relay r;
    RelayV.init_args.r = &r;
    RelayV.init_args.client = &ea;
    RelayV.init_args.origin = &eb;
    Relay.init(relay_work);

    TEST_ASSERT_EQUAL_INT(PROTOCORE_RELAY_DONE, run_relay(&r, 64));
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
    b.send_cap = 7;
    protocore_relay_end ea = end_of(&a), eb = end_of(&b);
    protocore_relay r;
    RelayV.init_args.r = &r;
    RelayV.init_args.client = &ea;
    RelayV.init_args.origin = &eb;
    Relay.init(relay_work);

    TEST_ASSERT_EQUAL_INT(PROTOCORE_RELAY_DONE, run_relay(&r, 1000));
    TEST_ASSERT_EQUAL_size_t(1000, b.out_len);
    TEST_ASSERT_EQUAL_MEMORY(data, b.out, 1000);
}

void test_half_close_shutdown()
{
    uint8_t resp[800];
    for (int i = 0; i < 800; i++)
    {
        resp[i] = (uint8_t)(i ^ 0x3C);
    }
    MockSock a, b;
    sock_init(&a, "req", 3, PROTO_TRUE);
    sock_init(&b, resp, sizeof(resp), PROTO_TRUE);
    a.send_cap = 64;
    protocore_relay_end ea = end_of(&a), eb = end_of(&b);
    protocore_relay r;
    RelayV.init_args.r = &r;
    RelayV.init_args.client = &ea;
    RelayV.init_args.origin = &eb;
    Relay.init(relay_work);

    protocore_relay_status st = PROTOCORE_RELAY_RUNNING;
    for (int i = 0; i < 10 && !r.b_shut_sent; i++)
    {
        RelayV.step_args.r = &r;
        Relay.step(relay_work);
        st = RelayV.status;
    }
    TEST_ASSERT_TRUE(r.b_shut_sent);
    TEST_ASSERT_TRUE(b.shutdown_called);
    TEST_ASSERT_FALSE(r.b2a_done);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_RELAY_RUNNING, st);

    TEST_ASSERT_EQUAL_INT(PROTOCORE_RELAY_DONE, run_relay(&r, 64));
    TEST_ASSERT_EQUAL_MEMORY("req", b.out, 3);
    TEST_ASSERT_EQUAL_size_t(800, a.out_len);
    TEST_ASSERT_EQUAL_MEMORY(resp, a.out, 800);
    TEST_ASSERT_TRUE(a.shutdown_called);
}

void test_send_error()
{
    MockSock a, b;
    sock_init(&a, "data", 4, PROTO_TRUE);
    sock_init(&b, NULL, 0, PROTO_TRUE);
    b.fail_send = PROTO_TRUE;
    protocore_relay_end ea = end_of(&a), eb = end_of(&b);
    protocore_relay r;
    RelayV.init_args.r = &r;
    RelayV.init_args.client = &ea;
    RelayV.init_args.origin = &eb;
    Relay.init(relay_work);

    protocore_relay_status st = PROTOCORE_RELAY_RUNNING;
    for (int i = 0; i < 8 && st == PROTOCORE_RELAY_RUNNING; i++)
    {
        RelayV.step_args.r = &r;
        Relay.step(relay_work);
        st = RelayV.status;
    }
    TEST_ASSERT_EQUAL_INT(PROTOCORE_RELAY_ERROR, st);
}

void test_one_way_idle_then_close()
{

    MockSock a, b;
    sock_init(&a, "GET / HTTP/1.0\r\n\r\n", 18, PROTO_TRUE);
    sock_init(&b, NULL, 0, PROTO_TRUE);
    protocore_relay_end ea = end_of(&a), eb = end_of(&b);
    protocore_relay r;
    RelayV.init_args.r = &r;
    RelayV.init_args.client = &ea;
    RelayV.init_args.origin = &eb;
    Relay.init(relay_work);

    TEST_ASSERT_EQUAL_INT(PROTOCORE_RELAY_DONE, run_relay(&r, 32));
    TEST_ASSERT_EQUAL_size_t(18, b.out_len);
    TEST_ASSERT_EQUAL_size_t(0, a.out_len);
}

void test_note_eof_out_of_band()
{
    MockSock a, b;
    sock_init(&a, "hello", 5, PROTO_FALSE);
    sock_init(&b, "world", 5, PROTO_FALSE);
    protocore_relay_end ea = end_of(&a), eb = end_of(&b);
    protocore_relay r;
    RelayV.init_args.r = &r;
    RelayV.init_args.client = &ea;
    RelayV.init_args.origin = &eb;
    Relay.init(relay_work);

    RelayV.step_args.r = &r;
    Relay.step(relay_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_RELAY_RUNNING, RelayV.status);
    TEST_ASSERT_EQUAL_MEMORY("hello", b.out, 5);
    TEST_ASSERT_EQUAL_MEMORY("world", a.out, 5);

    RelayV.note_eof_args.r = &r;
    RelayV.note_eof_args.origin = PROTO_FALSE;
    Relay.note_eof(relay_work);
    RelayV.note_eof_args.r = &r;
    RelayV.note_eof_args.origin = PROTO_TRUE;
    Relay.note_eof(relay_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_RELAY_DONE, run_relay(&r, 8));
    TEST_ASSERT_TRUE(a.shutdown_called);
    TEST_ASSERT_TRUE(b.shutdown_called);
}

void test_zero_length_read_no_progress()
{
    MockSock a, b;
    sock_init(&a, NULL, 0, PROTO_FALSE);
    sock_init(&b, NULL, 0, PROTO_FALSE);
    protocore_relay_end ea = end_of(&a), eb = end_of(&b);
    protocore_relay r;
    RelayV.init_args.r = &r;
    RelayV.init_args.client = &ea;
    RelayV.init_args.origin = &eb;
    Relay.init(relay_work);

    for (int i = 0; i < 5; i++)
    {
        RelayV.step_args.r = &r;
        Relay.step(relay_work);
        TEST_ASSERT_EQUAL_INT(PROTOCORE_RELAY_RUNNING, RelayV.status);
    }
    TEST_ASSERT_EQUAL_UINT32(0, r.bytes_a2b);
    TEST_ASSERT_EQUAL_UINT32(0, r.bytes_b2a);
    TEST_ASSERT_EQUAL_size_t(0, a.out_len);
    TEST_ASSERT_EQUAL_size_t(0, b.out_len);

    RelayV.note_eof_args.r = &r;
    RelayV.note_eof_args.origin = PROTO_FALSE;
    Relay.note_eof(relay_work);
    RelayV.note_eof_args.r = &r;
    RelayV.note_eof_args.origin = PROTO_TRUE;
    Relay.note_eof(relay_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_RELAY_DONE, run_relay(&r, 8));
}

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
    b.send_cap = 10;
    protocore_relay_end ea = end_of(&a), eb = end_of(&b);
    protocore_relay r;
    RelayV.init_args.r = &r;
    RelayV.init_args.client = &ea;
    RelayV.init_args.origin = &eb;
    Relay.init(relay_work);

    RelayV.step_args.r = &r;
    Relay.step(relay_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_RELAY_RUNNING, RelayV.status);
    TEST_ASSERT_TRUE(r.a2b_off < r.a2b_len);

    b.fail_send = PROTO_TRUE;
    RelayV.step_args.r = &r;
    Relay.step(relay_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_RELAY_ERROR, RelayV.status);
}

void test_send_error_reverse_direction()
{
    MockSock a, b;
    sock_init(&a, NULL, 0, PROTO_TRUE);
    sock_init(&b, "resp", 4, PROTO_TRUE);
    a.fail_send = PROTO_TRUE;
    protocore_relay_end ea = end_of(&a), eb = end_of(&b);
    protocore_relay r;
    RelayV.init_args.r = &r;
    RelayV.init_args.client = &ea;
    RelayV.init_args.origin = &eb;
    Relay.init(relay_work);

    RelayV.step_args.r = &r;
    Relay.step(relay_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_RELAY_ERROR, RelayV.status);
}

void test_null_argument_guards()
{
    MockSock a, b;
    sock_init(&a, NULL, 0, PROTO_TRUE);
    sock_init(&b, NULL, 0, PROTO_TRUE);
    protocore_relay_end ea = end_of(&a), eb = end_of(&b);
    protocore_relay r;

    RelayV.init_args.r = NULL;
    RelayV.init_args.client = &ea;
    RelayV.init_args.origin = &eb;
    Relay.init(relay_work);
    RelayV.init_args.r = &r;
    RelayV.init_args.client = NULL;
    RelayV.init_args.origin = &eb;
    Relay.init(relay_work);
    RelayV.init_args.r = &r;
    RelayV.init_args.client = &ea;
    RelayV.init_args.origin = NULL;
    Relay.init(relay_work);

    RelayV.step_args.r = NULL;
    Relay.step(relay_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_RELAY_ERROR, RelayV.status);

    RelayV.note_eof_args.r = NULL;
    RelayV.note_eof_args.origin = PROTO_FALSE;
    Relay.note_eof(relay_work);
}

void test_shutdown_null_seam()
{
    MockSock a, b;
    sock_init(&a, "hi", 2, PROTO_TRUE);
    sock_init(&b, NULL, 0, PROTO_TRUE);
    protocore_relay_end ea = end_of(&a);
    protocore_relay_end eb = end_of(&b);
    eb.shutdown = NULL;
    protocore_relay r;
    RelayV.init_args.r = &r;
    RelayV.init_args.client = &ea;
    RelayV.init_args.origin = &eb;
    Relay.init(relay_work);

    TEST_ASSERT_EQUAL_INT(PROTOCORE_RELAY_DONE, run_relay(&r, 16));
    TEST_ASSERT_FALSE(r.b_shut_sent);
    TEST_ASSERT_FALSE(b.shutdown_called);
    TEST_ASSERT_TRUE(r.a_shut_sent);
    TEST_ASSERT_TRUE(a.shutdown_called);
}

void test_note_eof_with_backlog_pending()
{
    uint8_t data[20];
    for (int i = 0; i < 20; i++)
    {
        data[i] = (uint8_t)(i + 100);
    }
    MockSock a, b;
    sock_init(&a, data, sizeof(data), PROTO_FALSE);
    sock_init(&b, NULL, 0, PROTO_TRUE);
    b.send_cap = 5;
    protocore_relay_end ea = end_of(&a), eb = end_of(&b);
    protocore_relay r;
    RelayV.init_args.r = &r;
    RelayV.init_args.client = &ea;
    RelayV.init_args.origin = &eb;
    Relay.init(relay_work);

    RelayV.step_args.r = &r;
    Relay.step(relay_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_RELAY_RUNNING, RelayV.status);
    TEST_ASSERT_EQUAL_size_t(5, r.a2b_off);
    TEST_ASSERT_EQUAL_size_t(20, r.a2b_len);

    RelayV.note_eof_args.r = &r;
    RelayV.note_eof_args.origin = PROTO_FALSE;
    Relay.note_eof(relay_work);
    TEST_ASSERT_FALSE(r.a2b_done);

    TEST_ASSERT_EQUAL_INT(PROTOCORE_RELAY_DONE, run_relay(&r, 16));
    TEST_ASSERT_EQUAL_size_t(20, b.out_len);
    TEST_ASSERT_EQUAL_MEMORY(data, b.out, 20);
}
