// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// transport/tcp/tcp_conn.c (RFC 9293): the per-slot receive buffer that holds
// RCV.USER, and the slot bitmaps that keep an index out of circulation.

#include "network_drivers/transport/tcp/protocol/protocol.h"
#include "mmgr/ring.h"
#include <stdint.h>

#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// Put @p n bytes into slot @p s the way the stack's receive callback does: straight into the ring
// storage, advancing the producer index the consumer side reads.
static void feed(uint8_t s, const uint8_t *src, size_t n)
{
    size_t head = PROTO_ATOMIC_LOAD(&conn_pool[s].rx_head);
    for (size_t k = 0; k < n; k++)
    {
        conn_pool[s].rx_buffer[(head + k) % RX_BUF_SIZE] = src[k];
    }
    PROTO_ATOMIC_STORE(&conn_pool[s].rx_head, head + n);
}

static void reset_slot(uint8_t s)
{
    PROTO_ATOMIC_STORE(&conn_pool[s].rx_head, (size_t)0);
    PROTO_ATOMIC_STORE(&conn_pool[s].rx_tail, (size_t)0);
    conn_pool[s].rx_acked = 0;
    conn_pool[s].id = s;
}

// ---------------------------------------------------------------------------
// sec 3.8.6.2.2: the receive buffer and RCV.USER
// ---------------------------------------------------------------------------
// "Suppose the total receive buffer space is RCV.BUFF. At any given moment, RCV.USER octets of this
// total may be tied up with data that has been received and acknowledged but that the user process
// has not yet consumed."

// A quiescent connection has nothing tied up: "When the connection is quiescent, RCV.WND = RCV.BUFF
// and RCV.USER = 0."
static void test_sec3_8_6_2_quiescent_connection_has_no_user_data(void)
{
    reset_slot(0);
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)protocore_conn_available(0));
}

// Received-but-unconsumed octets are exactly what available() reports.
static void test_sec3_8_6_2_received_octets_are_rcv_user(void)
{
    reset_slot(0);
    feed(0, (const uint8_t *)"abcdef", 6);

    TEST_ASSERT_EQUAL_UINT32(6u, (uint32_t)protocore_conn_available(0));
}

// Consuming reduces RCV.USER by exactly what the user took, and no more.
static void test_sec3_8_6_2_consuming_reduces_rcv_user(void)
{
    reset_slot(0);
    feed(0, (const uint8_t *)"abcdef", 6);

    protocore_conn_consume(0, 2u);
    TEST_ASSERT_EQUAL_UINT32(4u, (uint32_t)protocore_conn_available(0));

    protocore_conn_consume(0, 4u);
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)protocore_conn_available(0));
}

// The octets come back in the order they arrived: a byte stream, not a datagram queue.
static void test_stream_is_delivered_in_order(void)
{
    reset_slot(0);
    feed(0, (const uint8_t *)"stream", 6);

    uint8_t got[8] = {0};
    size_t n = protocore_conn_read(0, got, sizeof(got));

    TEST_ASSERT_EQUAL_UINT32(6u, (uint32_t)n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("stream", got, 6);
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)protocore_conn_available(0));
}

// Reading with less room than there is data takes only what fits and leaves the rest queued, so a
// short read never drops the tail of the stream.
static void test_short_read_leaves_the_remainder_queued(void)
{
    reset_slot(0);
    feed(0, (const uint8_t *)"abcdefgh", 8);

    uint8_t got[3] = {0};
    size_t n = protocore_conn_read(0, got, sizeof(got));
    TEST_ASSERT_EQUAL_UINT32(3u, (uint32_t)n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("abc", got, 3);
    TEST_ASSERT_EQUAL_UINT32(5u, (uint32_t)protocore_conn_available(0));

    uint8_t rest[8] = {0};
    n = protocore_conn_read(0, rest, sizeof(rest));
    TEST_ASSERT_EQUAL_UINT32(5u, (uint32_t)n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("defgh", rest, 5);
}

// Reading an empty buffer yields nothing rather than stale bytes.
static void test_read_of_an_empty_buffer_yields_nothing(void)
{
    reset_slot(0);
    uint8_t got[4] = {0xEE, 0xEE, 0xEE, 0xEE};

    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)protocore_conn_read(0, got, sizeof(got)));
    TEST_ASSERT_EQUAL_UINT8(0xEEu, got[0]);
}

// One byte at a time is the same stream.
static void test_read_byte_walks_the_stream(void)
{
    reset_slot(0);
    feed(0, (const uint8_t *)"xy", 2);

    uint8_t b = 0;
    TEST_ASSERT_TRUE(protocore_conn_read_byte(0, &b));
    TEST_ASSERT_EQUAL_UINT8('x', b);
    TEST_ASSERT_TRUE(protocore_conn_read_byte(0, &b));
    TEST_ASSERT_EQUAL_UINT8('y', b);
    TEST_ASSERT_FALSE(protocore_conn_read_byte(0, &b));
}

// Peeking looks ahead without consuming: RCV.USER is unchanged afterwards, so a parser can decide
// on a header before committing to it.
static void test_peek_does_not_consume(void)
{
    reset_slot(0);
    feed(0, (const uint8_t *)"HEADERBODY", 10);

    uint8_t hdr[6] = {0};
    protocore_conn_peek(0, 0, hdr, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("HEADER", hdr, 6);
    TEST_ASSERT_EQUAL_UINT32(10u, (uint32_t)protocore_conn_available(0));

    // The same bytes are still there to be read.
    uint8_t all[16] = {0};
    TEST_ASSERT_EQUAL_UINT32(10u, (uint32_t)protocore_conn_read(0, all, sizeof(all)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY("HEADERBODY", all, 10);
}

// Peeking at an offset reads from there, not from the front.
static void test_peek_honours_its_offset(void)
{
    reset_slot(0);
    feed(0, (const uint8_t *)"HEADERBODY", 10);

    uint8_t body[4] = {0};
    protocore_conn_peek(0, 6, body, 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("BODY", body, 4);
}

// The buffer is a ring: a stream that runs past the end of the storage wraps and stays in order.
static void test_stream_wraps_around_the_ring(void)
{
    reset_slot(0);

    // Park the indices near the end of the storage, then push a run across the seam.
    PROTO_ATOMIC_STORE(&conn_pool[0].rx_head, (size_t)(RX_BUF_SIZE - 4u));
    PROTO_ATOMIC_STORE(&conn_pool[0].rx_tail, (size_t)(RX_BUF_SIZE - 4u));
    feed(0, (const uint8_t *)"WRAPPED!", 8);

    TEST_ASSERT_EQUAL_UINT32(8u, (uint32_t)protocore_conn_available(0));
    uint8_t got[8] = {0};
    TEST_ASSERT_EQUAL_UINT32(8u, (uint32_t)protocore_conn_read(0, got, sizeof(got)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY("WRAPPED!", got, 8);
}

// Peeking across the seam is also in order.
static void test_peek_across_the_ring_seam(void)
{
    reset_slot(0);
    PROTO_ATOMIC_STORE(&conn_pool[0].rx_head, (size_t)(RX_BUF_SIZE - 3u));
    PROTO_ATOMIC_STORE(&conn_pool[0].rx_tail, (size_t)(RX_BUF_SIZE - 3u));
    feed(0, (const uint8_t *)"SEAMTEST", 8);

    uint8_t got[8] = {0};
    protocore_conn_peek(0, 0, got, 8);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("SEAMTEST", got, 8);
}

// RCV.BUFF is the storage less one octet: the spare slot is what tells a full ring from an empty
// one, since both would otherwise put the read and write indices in the same place.
static void test_receive_buffer_holds_its_capacity_less_one(void)
{
    reset_slot(0);
    uint8_t big[RX_BUF_SIZE];
    for (size_t k = 0; k < RX_BUF_SIZE; k++)
    {
        big[k] = (uint8_t)(k & 0xFFu);
    }
    feed(0, big, RX_BUF_SIZE - 1u);

    TEST_ASSERT_EQUAL_UINT32((uint32_t)(RX_BUF_SIZE - 1u), (uint32_t)protocore_conn_available(0));

    uint8_t out[RX_BUF_SIZE];
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(RX_BUF_SIZE - 1u), (uint32_t)protocore_conn_read(0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(big, out, RX_BUF_SIZE - 1u);
}

// The producer is what keeps the ring from reaching that ambiguous point: the space it is offered
// never exceeds capacity less one, so a segment that would fill it exactly is refused instead.
// RFC 9293 sec 3.8.6.2.2 wants the advertised window to reflect real room, not an aliased zero.
static void test_offered_space_never_reaches_the_ambiguous_point(void)
{
    reset_slot(0);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(RX_BUF_SIZE - 1u),
                             (uint32_t)protocore_ring_free(&conn_pool[0].rx_head, &conn_pool[0].rx_tail, RX_BUF_SIZE));

    feed(0, (const uint8_t *)"abcd", 4);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(RX_BUF_SIZE - 1u - 4u),
                             (uint32_t)protocore_ring_free(&conn_pool[0].rx_head, &conn_pool[0].rx_tail, RX_BUF_SIZE));

    // Draining gives the space back, so the window reopens by exactly what was consumed.
    protocore_conn_consume(0, 4u);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(RX_BUF_SIZE - 1u),
                             (uint32_t)protocore_ring_free(&conn_pool[0].rx_head, &conn_pool[0].rx_tail, RX_BUF_SIZE));
}

// Consume advances past bytes the caller already peeked, so peek-then-consume of the same count
// leaves exactly the untouched remainder.
static void test_peek_then_consume_drops_exactly_what_was_peeked(void)
{
    reset_slot(0);
    feed(0, (const uint8_t *)"HEADERBODY", 10);

    uint8_t hdr[6] = {0};
    protocore_conn_peek(0, 0, hdr, 6);
    protocore_conn_consume(0, 6u);

    TEST_ASSERT_EQUAL_UINT32(4u, (uint32_t)protocore_conn_available(0));
    uint8_t body[4] = {0};
    TEST_ASSERT_EQUAL_UINT32(4u, (uint32_t)protocore_conn_read(0, body, sizeof(body)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY("BODY", body, 4);
}

// ---------------------------------------------------------------------------
// sec 3.6.1: keeping an identifier out of circulation
// ---------------------------------------------------------------------------
// A pool index is not a socket, so there is no 2xMSL quiet period to wait out; the hold bit does
// the same job, keeping the index out of the allocator while bytes are still in flight.

static void test_a_free_unheld_slot_is_allocatable(void)
{
    _Atomic uint32_t free_mask = 0;
    _Atomic uint32_t held = 0;
    protocore_slot_mark(&free_mask, 0);

    TEST_ASSERT_EQUAL_UINT32(protocore_slot_bit(0), protocore_slot_ready(&free_mask, &held, MAX_CONNS));
    TEST_ASSERT_EQUAL_INT32(0, protocore_slot_next(protocore_slot_ready(&free_mask, &held, MAX_CONNS)));
}

// Free but held is not allocatable: the index stays out of circulation until the transfer walking
// its bytes is finished with it.
static void test_a_held_slot_is_not_allocatable_even_when_free(void)
{
    _Atomic uint32_t free_mask = 0;
    _Atomic uint32_t held = 0;
    protocore_slot_mark(&free_mask, 0);
    TEST_ASSERT_TRUE(protocore_slot_take(&held, 0));

    TEST_ASSERT_EQUAL_UINT32(0u, protocore_slot_ready(&free_mask, &held, MAX_CONNS));
    TEST_ASSERT_EQUAL_INT32(-1, protocore_slot_next(protocore_slot_ready(&free_mask, &held, MAX_CONNS)));
}

// Dropping the hold returns the index to circulation.
static void test_dropping_the_hold_returns_the_slot(void)
{
    _Atomic uint32_t free_mask = 0;
    _Atomic uint32_t held = 0;
    protocore_slot_mark(&free_mask, 0);
    (void)protocore_slot_take(&held, 0);
    protocore_slot_drop(&held, 0);

    TEST_ASSERT_EQUAL_UINT32(protocore_slot_bit(0), protocore_slot_ready(&free_mask, &held, MAX_CONNS));
}

// A slot in use is not allocatable regardless of the hold bit.
static void test_a_busy_slot_is_not_allocatable(void)
{
    _Atomic uint32_t free_mask = 0;
    _Atomic uint32_t held = 0;

    TEST_ASSERT_EQUAL_UINT32(0u, protocore_slot_ready(&free_mask, &held, MAX_CONNS));
    protocore_slot_mark(&free_mask, 0);
    protocore_slot_clear(&free_mask, 0);
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_slot_ready(&free_mask, &held, MAX_CONNS));
}

// Taking a hold twice reports the second attempt as already held, so two owners cannot both believe
// they have the slot's bytes.
static void test_a_hold_is_taken_once(void)
{
    _Atomic uint32_t held = 0;

    TEST_ASSERT_TRUE(protocore_slot_take(&held, 0));
    TEST_ASSERT_FALSE(protocore_slot_take(&held, 0));
}

// The allocator picks the lowest ready index, so slot use is deterministic rather than dependent on
// which bit a scan happened to reach first.
static void test_the_lowest_ready_slot_is_picked(void)
{
    _Atomic uint32_t free_mask = 0;
    _Atomic uint32_t held = 0;
    for (size_t i = 0; i < MAX_CONNS; i++)
    {
        protocore_slot_mark(&free_mask, i);
    }

    TEST_ASSERT_EQUAL_INT32(0, protocore_slot_next(protocore_slot_ready(&free_mask, &held, MAX_CONNS)));

    // Hold the low ones and the next one up is chosen.
    if (MAX_CONNS >= 3u)
    {
        (void)protocore_slot_take(&held, 0);
        (void)protocore_slot_take(&held, 1);
        TEST_ASSERT_EQUAL_INT32(2, protocore_slot_next(protocore_slot_ready(&free_mask, &held, MAX_CONNS)));
    }
}

// An exhausted pool reports no slot rather than wrapping to index zero.
static void test_an_exhausted_pool_reports_no_slot(void)
{
    _Atomic uint32_t free_mask = 0;
    _Atomic uint32_t held = 0;

    TEST_ASSERT_EQUAL_INT32(-1, protocore_slot_next(protocore_slot_ready(&free_mask, &held, MAX_CONNS)));
}

// Readiness is masked to the pool: bits above MAX_CONNS never name a slot that does not exist.
static void test_readiness_is_masked_to_the_pool(void)
{
    _Atomic uint32_t free_mask = 0xFFFFFFFFu;
    _Atomic uint32_t held = 0;

    uint32_t ready = protocore_slot_ready(&free_mask, &held, MAX_CONNS);
    TEST_ASSERT_EQUAL_UINT32(protocore_slot_all(MAX_CONNS), ready);
    TEST_ASSERT_EQUAL_UINT32(0u, ready & ~protocore_slot_all(MAX_CONNS));
}

// ---------------------------------------------------------------------------
// The slot lifecycle this layer actually implements
// ---------------------------------------------------------------------------
// These three name whether the pool slot is available; they are not the RFC 9293 sec 3.3.2
// connection states, which belong to the stack underneath.

static void test_slot_states_are_distinct_and_one_byte(void)
{
    TEST_ASSERT_EQUAL_UINT32(1u, (uint32_t)sizeof(ConnState));
    TEST_ASSERT_NOT_EQUAL(CONN_FREE, CONN_ACTIVE);
    TEST_ASSERT_NOT_EQUAL(CONN_ACTIVE, CONN_CLOSING);
    TEST_ASSERT_NOT_EQUAL(CONN_FREE, CONN_CLOSING);
}

// An active slot is the only one the send guard admits, and only while a control block is attached.
static void test_only_an_active_slot_with_a_pcb_is_reported_active(void)
{
    protocore_pcb *saved = conn_pool[0].pcb;
    protocore_pcb stub;
    conn_pool[0].pcb = &stub;

    PROTO_ATOMIC_STORE(&conn_pool[0].state, CONN_FREE);
    TEST_ASSERT_FALSE(protocore_conn_active(0));

    PROTO_ATOMIC_STORE(&conn_pool[0].state, CONN_CLOSING);
    TEST_ASSERT_FALSE(protocore_conn_active(0));

    PROTO_ATOMIC_STORE(&conn_pool[0].state, CONN_ACTIVE);
    TEST_ASSERT_TRUE(protocore_conn_active(0));

    PROTO_ATOMIC_STORE(&conn_pool[0].state, CONN_FREE);
    conn_pool[0].pcb = saved;
}

// Every terminal edge detaches the control block before the slot is released, so a slot whose pcb
// is gone is never sendable however its state reads. Sending on it would be a write to a connection
// the stack already owns (RFC 9293 sec 3.6 leaves the FIN and its retransmission to the stack).
static void test_a_detached_slot_is_never_sendable(void)
{
    protocore_pcb *saved = conn_pool[0].pcb;
    conn_pool[0].pcb = NULL;

    PROTO_ATOMIC_STORE(&conn_pool[0].state, CONN_ACTIVE);
    TEST_ASSERT_FALSE(protocore_conn_active(0));

    PROTO_ATOMIC_STORE(&conn_pool[0].state, CONN_FREE);
    conn_pool[0].pcb = saved;
}

#if PROTOCORE_ENABLE_OBSERVABILITY
// The reasons an observer is handed stay one byte, so the counters they index cannot silently grow.
static void test_transition_reasons_stay_one_byte(void)
{
    TEST_ASSERT_EQUAL_UINT32(1u, (uint32_t)sizeof(protocore_conn_reason));
}

// A peer close and a stack error are distinct reasons, so an observer can tell a graceful FIN from
// a connection that failed underneath it (RFC 9293 sec 3.6 treats them differently).
static void test_graceful_close_and_error_are_distinct_reasons(void)
{
    TEST_ASSERT_NOT_EQUAL(PROTOCORE_CONN_R_CLOSE_REMOTE, PROTOCORE_CONN_R_ERROR);
    TEST_ASSERT_NOT_EQUAL(PROTOCORE_CONN_R_CLOSE_LOCAL, PROTOCORE_CONN_R_ERROR);
    TEST_ASSERT_NOT_EQUAL(PROTOCORE_CONN_R_CLOSE_REMOTE, PROTOCORE_CONN_R_CLOSE_LOCAL);
    TEST_ASSERT_NOT_EQUAL(PROTOCORE_CONN_R_ABORT, PROTOCORE_CONN_R_DRAINED);
}
#endif

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sec3_8_6_2_quiescent_connection_has_no_user_data);
    RUN_TEST(test_sec3_8_6_2_received_octets_are_rcv_user);
    RUN_TEST(test_sec3_8_6_2_consuming_reduces_rcv_user);
    RUN_TEST(test_stream_is_delivered_in_order);
    RUN_TEST(test_short_read_leaves_the_remainder_queued);
    RUN_TEST(test_read_of_an_empty_buffer_yields_nothing);
    RUN_TEST(test_read_byte_walks_the_stream);
    RUN_TEST(test_peek_does_not_consume);
    RUN_TEST(test_peek_honours_its_offset);
    RUN_TEST(test_stream_wraps_around_the_ring);
    RUN_TEST(test_peek_across_the_ring_seam);
    RUN_TEST(test_receive_buffer_holds_its_capacity_less_one);
    RUN_TEST(test_offered_space_never_reaches_the_ambiguous_point);
    RUN_TEST(test_peek_then_consume_drops_exactly_what_was_peeked);
    RUN_TEST(test_a_free_unheld_slot_is_allocatable);
    RUN_TEST(test_a_held_slot_is_not_allocatable_even_when_free);
    RUN_TEST(test_dropping_the_hold_returns_the_slot);
    RUN_TEST(test_a_busy_slot_is_not_allocatable);
    RUN_TEST(test_a_hold_is_taken_once);
    RUN_TEST(test_the_lowest_ready_slot_is_picked);
    RUN_TEST(test_an_exhausted_pool_reports_no_slot);
    RUN_TEST(test_readiness_is_masked_to_the_pool);
    RUN_TEST(test_slot_states_are_distinct_and_one_byte);
    RUN_TEST(test_only_an_active_slot_with_a_pcb_is_reported_active);
    RUN_TEST(test_a_detached_slot_is_never_sendable);
#if PROTOCORE_ENABLE_OBSERVABILITY
    RUN_TEST(test_transition_reasons_stay_one_byte);
    RUN_TEST(test_graceful_close_and_error_are_distinct_reasons);
#endif
    return UNITY_END();
}
