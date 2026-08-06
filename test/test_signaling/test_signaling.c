// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for server/signaling: the application-layer state bucket.
//
// The module deliberately holds no logic - every entry point is a store, a copy, or a forward - so
// what is worth testing is exactly that: what is deposited comes back unchanged, a read is a copy
// rather than a window onto live storage, and kill reaches the transport with the slot it was given.
//
// Tcp.conn->close() is stubbed here rather than linked from tcp.cpp. Signaling's kill is a forward, and
// a stub is what makes the forward observable; linking the real transport would test lwIP instead.

#include "server/signaling/signaling.h"
#include <unity.h>

static int g_close_calls = 0;
static uint8_t g_close_slot = 0xFF;

void Tcp.conn->close(uint8_t slot)
{
    g_close_calls++;
    g_close_slot = slot;
}

void setUp(void)
{
    g_close_calls = 0;
    g_close_slot = 0xFF;
}
void tearDown(void)
{
}

// The response counters accumulate for the life of the process (there is no reset - the bucket is
// deposited into, never cleared), so every assertion here is a delta across the call under test.
void test_put_response_counts_by_class(void)
{
    pc_signal_snapshot before;
    pc_signal_know(&before);

    pc_signal_put_response(200);
    pc_signal_put_response(201);
    pc_signal_put_response(404);
    pc_signal_put_response(500);

    pc_signal_snapshot after;
    pc_signal_know(&after);

    TEST_ASSERT_EQUAL_UINT32(4, after.requests_total - before.requests_total);
    TEST_ASSERT_EQUAL_UINT32(2, after.responses_2xx - before.responses_2xx);
    TEST_ASSERT_EQUAL_UINT32(1, after.responses_4xx - before.responses_4xx);
    TEST_ASSERT_EQUAL_UINT32(1, after.responses_5xx - before.responses_5xx);
}

// 1xx and 3xx are counted in the total and in no class bucket. This is the documented behavior, so
// it gets a test rather than a comment.
void test_put_response_1xx_3xx_total_only(void)
{
    pc_signal_snapshot before;
    pc_signal_know(&before);

    pc_signal_put_response(100);
    pc_signal_put_response(301);
    pc_signal_put_response(304);

    pc_signal_snapshot after;
    pc_signal_know(&after);

    TEST_ASSERT_EQUAL_UINT32(3, after.requests_total - before.requests_total);
    TEST_ASSERT_EQUAL_UINT32(0, after.responses_2xx - before.responses_2xx);
    TEST_ASSERT_EQUAL_UINT32(0, after.responses_4xx - before.responses_4xx);
    TEST_ASSERT_EQUAL_UINT32(0, after.responses_5xx - before.responses_5xx);
}

// The tick fields are absolute stores, not tallies, so the last deposit is what a reader sees.
void test_put_tick_stores_verbatim(void)
{
    pc_signal_put_tick(1234u, 0x0Bu, 0x05u);

    pc_signal_snapshot s;
    pc_signal_know(&s);
    TEST_ASSERT_EQUAL_UINT32(1234u, s.uptime_ms);
    TEST_ASSERT_EQUAL_UINT32(0x0Bu, s.conns_active);
    TEST_ASSERT_EQUAL_UINT32(0x05u, s.listeners_up);

    pc_signal_put_tick(9999u, 0u, 0u);
    pc_signal_know(&s);
    TEST_ASSERT_EQUAL_UINT32(9999u, s.uptime_ms);
    TEST_ASSERT_EQUAL_UINT32(0u, s.conns_active);
    TEST_ASSERT_EQUAL_UINT32(0u, s.listeners_up);
}

// The masks are the point of the fields: the count is recoverable from the mask, and which slot is
// recoverable only from the mask.
void test_masks_carry_identity_not_just_count(void)
{
    pc_signal_put_tick(0u, 0b10010011u, 0b101u);

    pc_signal_snapshot s;
    pc_signal_know(&s);

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
}

// know() hands back a copy. A reader formatting several fields must not have the loop change them
// underneath it, which is the whole reason it is not a pointer into the bucket.
void test_know_is_a_copy_not_a_window(void)
{
    pc_signal_put_tick(7u, 1u, 1u);

    pc_signal_snapshot taken;
    pc_signal_know(&taken);

    pc_signal_put_tick(8888u, 0xFFu, 0x7u);
    pc_signal_put_response(200);

    TEST_ASSERT_EQUAL_UINT32(7u, taken.uptime_ms);
    TEST_ASSERT_EQUAL_UINT32(1u, taken.conns_active);
    TEST_ASSERT_EQUAL_UINT32(1u, taken.listeners_up);
}

void test_know_null_is_safe(void)
{
    pc_signal_know(NULL); // must not fault
    TEST_ASSERT_TRUE(PROTO_TRUE);
}

// Kill is a forward: the transport gets the slot, once, unchanged.
void test_kill_forwards_the_slot(void)
{
    pc_signal_kill(3);
    TEST_ASSERT_EQUAL_INT(1, g_close_calls);
    TEST_ASSERT_EQUAL_UINT8(3, g_close_slot);

    pc_signal_kill(0);
    TEST_ASSERT_EQUAL_INT(2, g_close_calls);
    TEST_ASSERT_EQUAL_UINT8(0, g_close_slot);
}

// A kill is unconditional. Transport owns slot lifetime and reaps a stale one on its own sweep, so
// signaling does not test liveness and must not start filtering.
void test_kill_does_not_filter(void)
{
    pc_signal_kill(200); // not a real slot; still forwarded, transport decides
    TEST_ASSERT_EQUAL_INT(1, g_close_calls);
    TEST_ASSERT_EQUAL_UINT8(200, g_close_slot);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_put_response_counts_by_class);
    RUN_TEST(test_put_response_1xx_3xx_total_only);
    RUN_TEST(test_put_tick_stores_verbatim);
    RUN_TEST(test_masks_carry_identity_not_just_count);
    RUN_TEST(test_know_is_a_copy_not_a_window);
    RUN_TEST(test_know_null_is_safe);
    RUN_TEST(test_kill_forwards_the_slot);
    RUN_TEST(test_kill_does_not_filter);
    return UNITY_END();
}
