// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "mmgr/ring.h"
#include "network_drivers/transport/tcp/common.h"
#include "network_drivers/transport/tcp/protocol/protocol.h"
#include <stdint.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

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

static void test_sec3_8_6_2_quiescent_connection_has_no_user_data(void)
{
    reset_slot(0);
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)protocore_conn_available(0));
}

static void test_sec3_8_6_2_received_octets_are_rcv_user(void)
{
    reset_slot(0);
    feed(0, (const uint8_t *)"abcdef", 6);

    TEST_ASSERT_EQUAL_UINT32(6u, (uint32_t)protocore_conn_available(0));
}

static void test_sec3_8_6_2_consuming_reduces_rcv_user(void)
{
    reset_slot(0);
    feed(0, (const uint8_t *)"abcdef", 6);

    protocore_conn_consume(0, 2u);
    TEST_ASSERT_EQUAL_UINT32(4u, (uint32_t)protocore_conn_available(0));

    protocore_conn_consume(0, 4u);
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)protocore_conn_available(0));
}

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

static void test_read_of_an_empty_buffer_yields_nothing(void)
{
    reset_slot(0);
    uint8_t got[4] = {0xEE, 0xEE, 0xEE, 0xEE};

    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)protocore_conn_read(0, got, sizeof(got)));
    TEST_ASSERT_EQUAL_UINT8(0xEEu, got[0]);
}

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

static void test_peek_does_not_consume(void)
{
    reset_slot(0);
    feed(0, (const uint8_t *)"HEADERBODY", 10);

    uint8_t hdr[6] = {0};
    protocore_conn_peek(0, 0, hdr, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("HEADER", hdr, 6);
    TEST_ASSERT_EQUAL_UINT32(10u, (uint32_t)protocore_conn_available(0));

    uint8_t all[16] = {0};
    TEST_ASSERT_EQUAL_UINT32(10u, (uint32_t)protocore_conn_read(0, all, sizeof(all)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY("HEADERBODY", all, 10);
}

static void test_peek_honours_its_offset(void)
{
    reset_slot(0);
    feed(0, (const uint8_t *)"HEADERBODY", 10);

    uint8_t body[4] = {0};
    protocore_conn_peek(0, 6, body, 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("BODY", body, 4);
}

static void test_stream_wraps_around_the_ring(void)
{
    reset_slot(0);

    PROTO_ATOMIC_STORE(&conn_pool[0].rx_head, (size_t)(RX_BUF_SIZE - 4u));
    PROTO_ATOMIC_STORE(&conn_pool[0].rx_tail, (size_t)(RX_BUF_SIZE - 4u));
    feed(0, (const uint8_t *)"WRAPPED!", 8);

    TEST_ASSERT_EQUAL_UINT32(8u, (uint32_t)protocore_conn_available(0));
    uint8_t got[8] = {0};
    TEST_ASSERT_EQUAL_UINT32(8u, (uint32_t)protocore_conn_read(0, got, sizeof(got)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY("WRAPPED!", got, 8);
}

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

static void test_offered_space_never_reaches_the_ambiguous_point(void)
{
    reset_slot(0);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(RX_BUF_SIZE - 1u),
                             (uint32_t)protocore_ring_free(&conn_pool[0].rx_head, &conn_pool[0].rx_tail, RX_BUF_SIZE));

    feed(0, (const uint8_t *)"abcd", 4);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(RX_BUF_SIZE - 1u - 4u),
                             (uint32_t)protocore_ring_free(&conn_pool[0].rx_head, &conn_pool[0].rx_tail, RX_BUF_SIZE));

    protocore_conn_consume(0, 4u);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(RX_BUF_SIZE - 1u),
                             (uint32_t)protocore_ring_free(&conn_pool[0].rx_head, &conn_pool[0].rx_tail, RX_BUF_SIZE));
}

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

static void test_a_free_unheld_slot_is_allocatable(void)
{
    _Atomic uint32_t free_mask = 0;
    _Atomic uint32_t held = 0;
    protocore_slot_mark(&free_mask, 0);

    TEST_ASSERT_EQUAL_UINT32(protocore_slot_bit(0), protocore_slot_ready(&free_mask, &held, MAX_CONNS));
    TEST_ASSERT_EQUAL_INT32(0, protocore_slot_next(protocore_slot_ready(&free_mask, &held, MAX_CONNS)));
}

static void test_a_held_slot_is_not_allocatable_even_when_free(void)
{
    _Atomic uint32_t free_mask = 0;
    _Atomic uint32_t held = 0;
    protocore_slot_mark(&free_mask, 0);
    TEST_ASSERT_TRUE(protocore_slot_take(&held, 0));

    TEST_ASSERT_EQUAL_UINT32(0u, protocore_slot_ready(&free_mask, &held, MAX_CONNS));
    TEST_ASSERT_EQUAL_INT32(-1, protocore_slot_next(protocore_slot_ready(&free_mask, &held, MAX_CONNS)));
}

static void test_dropping_the_hold_returns_the_slot(void)
{
    _Atomic uint32_t free_mask = 0;
    _Atomic uint32_t held = 0;
    protocore_slot_mark(&free_mask, 0);
    (void)protocore_slot_take(&held, 0);
    protocore_slot_drop(&held, 0);

    TEST_ASSERT_EQUAL_UINT32(protocore_slot_bit(0), protocore_slot_ready(&free_mask, &held, MAX_CONNS));
}

static void test_a_busy_slot_is_not_allocatable(void)
{
    _Atomic uint32_t free_mask = 0;
    _Atomic uint32_t held = 0;

    TEST_ASSERT_EQUAL_UINT32(0u, protocore_slot_ready(&free_mask, &held, MAX_CONNS));
    protocore_slot_mark(&free_mask, 0);
    protocore_slot_clear(&free_mask, 0);
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_slot_ready(&free_mask, &held, MAX_CONNS));
}

static void test_a_hold_is_taken_once(void)
{
    _Atomic uint32_t held = 0;

    TEST_ASSERT_TRUE(protocore_slot_take(&held, 0));
    TEST_ASSERT_FALSE(protocore_slot_take(&held, 0));
}

static void test_the_lowest_ready_slot_is_picked(void)
{
    _Atomic uint32_t free_mask = 0;
    _Atomic uint32_t held = 0;
    for (size_t i = 0; i < MAX_CONNS; i++)
    {
        protocore_slot_mark(&free_mask, i);
    }

    TEST_ASSERT_EQUAL_INT32(0, protocore_slot_next(protocore_slot_ready(&free_mask, &held, MAX_CONNS)));

    if (MAX_CONNS >= 3u)
    {
        (void)protocore_slot_take(&held, 0);
        (void)protocore_slot_take(&held, 1);
        TEST_ASSERT_EQUAL_INT32(2, protocore_slot_next(protocore_slot_ready(&free_mask, &held, MAX_CONNS)));
    }
}

static void test_an_exhausted_pool_reports_no_slot(void)
{
    _Atomic uint32_t free_mask = 0;
    _Atomic uint32_t held = 0;

    TEST_ASSERT_EQUAL_INT32(-1, protocore_slot_next(protocore_slot_ready(&free_mask, &held, MAX_CONNS)));
}

static void test_readiness_is_masked_to_the_pool(void)
{
    _Atomic uint32_t free_mask = 0xFFFFFFFFu;
    _Atomic uint32_t held = 0;

    uint32_t ready = protocore_slot_ready(&free_mask, &held, MAX_CONNS);
    TEST_ASSERT_EQUAL_UINT32(protocore_slot_all(MAX_CONNS), ready);
    TEST_ASSERT_EQUAL_UINT32(0u, ready & ~protocore_slot_all(MAX_CONNS));
}

static void test_slot_states_are_distinct_and_one_byte(void)
{
    TEST_ASSERT_EQUAL_UINT32(1u, (uint32_t)sizeof(ConnState));
    TEST_ASSERT_NOT_EQUAL(CONN_FREE, CONN_ACTIVE);
    TEST_ASSERT_NOT_EQUAL(CONN_ACTIVE, CONN_CLOSING);
    TEST_ASSERT_NOT_EQUAL(CONN_FREE, CONN_CLOSING);
}

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

static void test_transition_reasons_stay_one_byte(void)
{
    TEST_ASSERT_EQUAL_UINT32(1u, (uint32_t)sizeof(protocore_conn_reason));
}

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
