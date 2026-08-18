// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the server's signalling bucket (server/signaling/signaling.h).
//
// The bucket holds no logic - every entry point is a store, a copy, or a forward - so the one thing
// that can be wrong is which tally a deposit lands in. RFC 9110 sec 15 settles that: "All valid
// status codes are within the range of 100 to 599, inclusive. The first digit of the status code
// defines the class of response", with 1xx Informational, 2xx Successful, 3xx Redirection, 4xx
// Client Error and 5xx Server Error. test_rfc9110_first_digit_selects_the_class walks each class
// boundary from that sentence, so an off-by-one range cannot pass.
//
// ConnPool is defined here rather than linked from the transport: kill is a forward, and a local
// definition is what makes the forward observable without pulling the stack into a host test.

#include "network_drivers/transport/tcp/protocol/protocol.h"
#include "server/signaling/signaling.h"

#include <unity.h>

static int g_close_calls;
static uint8_t g_close_slot;

// The double this env links in place of protocol.c: it builds the signaling half only, so the pool
// is the one symbol it has to supply. Every entry takes the borrow, and this one never reads it.
static void stub_close(uint8_t *restrict work)
{
    (void)work;
    g_close_calls++;
    g_close_slot = ConnPool.slot;
}

ConnPoolNs ConnPool = {.close = stub_close};

// The pool owns state now, so it publishes the bytes that state lives in and signaling.c passes
// them to every call. The double holds none, and the entry above never reads what it is handed.
static uint8_t g_conn_pool_work[16];
uint8_t *protocore_conn_pool_span(void)
{
    return g_conn_pool_work;
}

static void put_response(int code)
{
    Signal.put.code = code;
    Signal.put_response(protocore_signaling_span());
}

static void put_tick(uint32_t uptime_ms, uint32_t conns, uint32_t listeners)
{
    Signal.put.uptime_ms = uptime_ms;
    Signal.put.conns_active = conns;
    Signal.put.listeners_up = listeners;
    Signal.put_tick(protocore_signaling_span());
}

static protocore_signal_snapshot know(void)
{
    protocore_signal_snapshot s;
    Signal.out = &s;
    Signal.know(protocore_signaling_span());
    return s;
}

static void kill_slot(uint8_t slot)
{
    Signal.slot = slot;
    Signal.kill(protocore_signaling_span());
}

void setUp(void)
{
    g_close_calls = 0;
    g_close_slot = 0xFF;
    Signal.reset(protocore_signaling_span());
}
void tearDown(void)
{
}

// RFC 9110 sec 15: the first digit is the class. Every code is counted once in the total, and once
// more in the tally its leading digit names - 1xx and 3xx have no tally, so they reach the total
// alone.
void test_rfc9110_first_digit_selects_the_class(void)
{
    put_response(100);
    put_response(200);
    put_response(201);
    put_response(301);
    put_response(404);
    put_response(451);
    put_response(500);
    put_response(599);

    protocore_signal_snapshot s = know();
    TEST_ASSERT_EQUAL_UINT32(8u, s.requests_total);
    TEST_ASSERT_EQUAL_UINT32(2u, s.responses_2xx);
    TEST_ASSERT_EQUAL_UINT32(2u, s.responses_4xx);
    TEST_ASSERT_EQUAL_UINT32(2u, s.responses_5xx);
}

// Each class is a hundred wide, so the code one below its first and the code one above its last
// belong to the neighbours. sec 15 also fixes the outer edges: below 100 and above 599 is not a
// status code at all, so neither carries a class.
void test_class_ranges_are_a_hundred_wide(void)
{
    put_response(199);
    put_response(200);
    put_response(299);
    put_response(300);
    protocore_signal_snapshot s = know();
    TEST_ASSERT_EQUAL_UINT32(2u, s.responses_2xx);

    Signal.reset(protocore_signaling_span());
    put_response(399);
    put_response(400);
    put_response(499);
    put_response(500);
    s = know();
    TEST_ASSERT_EQUAL_UINT32(2u, s.responses_4xx);
    TEST_ASSERT_EQUAL_UINT32(1u, s.responses_5xx);

    Signal.reset(protocore_signaling_span());
    put_response(99);
    put_response(600);
    s = know();
    TEST_ASSERT_EQUAL_UINT32(2u, s.requests_total);
    TEST_ASSERT_EQUAL_UINT32(0u, s.responses_2xx);
    TEST_ASSERT_EQUAL_UINT32(0u, s.responses_4xx);
    TEST_ASSERT_EQUAL_UINT32(0u, s.responses_5xx);
}

// The tick fields are absolute stores rather than tallies, so a second deposit replaces the first
// instead of adding to it.
void test_put_tick_replaces_rather_than_accumulates(void)
{
    put_tick(1234u, 0x0Bu, 0x05u);
    protocore_signal_snapshot s = know();
    TEST_ASSERT_EQUAL_UINT32(1234u, s.uptime_ms);
    TEST_ASSERT_EQUAL_UINT32(0x0Bu, s.conns_active);
    TEST_ASSERT_EQUAL_UINT32(0x05u, s.listeners_up);

    put_tick(9999u, 0u, 0u);
    s = know();
    TEST_ASSERT_EQUAL_UINT32(9999u, s.uptime_ms);
    TEST_ASSERT_EQUAL_UINT32(0u, s.conns_active);
    TEST_ASSERT_EQUAL_UINT32(0u, s.listeners_up);
}

// The fields are masks, not counts: the tally is recoverable from a mask by popcount, and which
// slot is recoverable only from the mask. 0x93 is bits 0, 1, 4 and 7; 0x05 is bits 0 and 2.
void test_masks_carry_identity_as_well_as_count(void)
{
    put_tick(0u, 0x93u, 0x05u);
    protocore_signal_snapshot s = know();

    TEST_ASSERT_EQUAL_INT(4, __builtin_popcount(s.conns_active));
    TEST_ASSERT_EQUAL_INT(2, __builtin_popcount(s.listeners_up));

    TEST_ASSERT_TRUE((s.conns_active & (1u << 0)) != 0u);
    TEST_ASSERT_TRUE((s.conns_active & (1u << 1)) != 0u);
    TEST_ASSERT_FALSE((s.conns_active & (1u << 2)) != 0u);
    TEST_ASSERT_TRUE((s.conns_active & (1u << 4)) != 0u);
    TEST_ASSERT_TRUE((s.conns_active & (1u << 7)) != 0u);

    TEST_ASSERT_TRUE((s.listeners_up & (1u << 0)) != 0u);
    TEST_ASSERT_FALSE((s.listeners_up & (1u << 1)) != 0u);
    TEST_ASSERT_TRUE((s.listeners_up & (1u << 2)) != 0u);

    // Every slot and every listener at once, which is what the header's static_asserts bound.
    put_tick(0u, 0xFFFFFFFFu, 0xFFFFFFFFu);
    s = know();
    TEST_ASSERT_EQUAL_INT(32, __builtin_popcount(s.conns_active));
    TEST_ASSERT_EQUAL_INT(32, __builtin_popcount(s.listeners_up));
}

// know copies. A snapshot taken before a deposit still reads what was there when it was taken, so
// one report cannot mix two server states.
void test_know_hands_back_a_copy_not_a_window(void)
{
    put_tick(7u, 1u, 1u);
    protocore_signal_snapshot taken = know();

    put_tick(8888u, 0xFFu, 0x7u);
    put_response(200);

    TEST_ASSERT_EQUAL_UINT32(7u, taken.uptime_ms);
    TEST_ASSERT_EQUAL_UINT32(1u, taken.conns_active);
    TEST_ASSERT_EQUAL_UINT32(1u, taken.listeners_up);
    TEST_ASSERT_EQUAL_UINT32(0u, taken.requests_total);
}

// reset returns the bucket to the state it starts a run in: every field zero.
void test_reset_empties_every_field(void)
{
    put_tick(4242u, 0xFFu, 0x3u);
    put_response(200);
    put_response(404);
    put_response(500);

    Signal.reset(protocore_signaling_span());
    protocore_signal_snapshot s = know();
    TEST_ASSERT_EQUAL_UINT32(0u, s.uptime_ms);
    TEST_ASSERT_EQUAL_UINT32(0u, s.requests_total);
    TEST_ASSERT_EQUAL_UINT32(0u, s.responses_2xx);
    TEST_ASSERT_EQUAL_UINT32(0u, s.responses_4xx);
    TEST_ASSERT_EQUAL_UINT32(0u, s.responses_5xx);
    TEST_ASSERT_EQUAL_UINT32(0u, s.conns_active);
    TEST_ASSERT_EQUAL_UINT32(0u, s.listeners_up);
}

// A read with nowhere to copy to writes nothing and does not follow the null.
void test_a_read_with_no_destination_is_refused(void)
{
    put_tick(5u, 1u, 1u);
    Signal.out = NULL;
    Signal.know(protocore_signaling_span());

    protocore_signal_snapshot s = know();
    TEST_ASSERT_EQUAL_UINT32(5u, s.uptime_ms);
}

// kill is a plain forward: transport gets the slot it was handed, once, unchanged. It carries no
// liveness test, so a slot number transport will reject still reaches transport to be rejected.
void test_kill_forwards_the_slot_unfiltered(void)
{
    kill_slot(3);
    TEST_ASSERT_EQUAL_INT(1, g_close_calls);
    TEST_ASSERT_EQUAL_UINT8(3, g_close_slot);

    kill_slot(0);
    TEST_ASSERT_EQUAL_INT(2, g_close_calls);
    TEST_ASSERT_EQUAL_UINT8(0, g_close_slot);

    kill_slot(200);
    TEST_ASSERT_EQUAL_INT(3, g_close_calls);
    TEST_ASSERT_EQUAL_UINT8(200, g_close_slot);
}

// A kill deposits nothing: the bucket is what the loop wrote, and the transport call is not a fact
// about the server's state.
void test_kill_deposits_nothing(void)
{
    put_tick(11u, 0x1u, 0x1u);
    protocore_signal_snapshot before = know();
    kill_slot(1);
    protocore_signal_snapshot after = know();

    TEST_ASSERT_EQUAL_UINT32(before.uptime_ms, after.uptime_ms);
    TEST_ASSERT_EQUAL_UINT32(before.requests_total, after.requests_total);
    TEST_ASSERT_EQUAL_UINT32(before.conns_active, after.conns_active);
}
