// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the network adaptation decisions (server/net/netadapt/netadapt.h).
//
// No standard governs either decision, so every expectation here is PROPERTIES: arithmetic derived
// from the two formulas the header itself publishes, plus the invariants that hold whatever the
// implementation. test_window_stays_inside_the_stated_bounds is the load-bearing case: a window
// outside [min_win, max_win] is handed straight to an lwIP netif, so the clamp is the property the
// caller relies on and it is swept across the whole reserve boundary rather than sampled.

#include "server/net/netadapt/netadapt.h"

#include <unity.h>

static uint8_t netadapt_work[16]; // the borrow an entry takes; Netadapt never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// Header: "min_win if the heap at/below the reserve". The boundary is inclusive, so a heap of
// exactly the reserve is still the floor and reserve+1 is the first heap with spare.
void test_window_floor_at_and_below_the_reserve(void)
{
    Netadapt.window_args.free_heap = 0u;
    Netadapt.window_args.reserve = 8000u;
    Netadapt.window_args.min_win = 1024u;
    Netadapt.window_args.max_win = 16384u;
    Netadapt.window(netadapt_work);
    TEST_ASSERT_EQUAL_UINT32(1024u, Netadapt.u32);
    Netadapt.window_args.free_heap = 7999u;
    Netadapt.window_args.reserve = 8000u;
    Netadapt.window_args.min_win = 1024u;
    Netadapt.window_args.max_win = 16384u;
    Netadapt.window(netadapt_work);
    TEST_ASSERT_EQUAL_UINT32(1024u, Netadapt.u32);
    Netadapt.window_args.free_heap = 8000u;
    Netadapt.window_args.reserve = 8000u;
    Netadapt.window_args.min_win = 1024u;
    Netadapt.window_args.max_win = 16384u;
    Netadapt.window(netadapt_work);
    TEST_ASSERT_EQUAL_UINT32(1024u, Netadapt.u32);
    // one octet of spare: (8001-8000)/4 = 0, below the floor, so still the floor
    Netadapt.window_args.free_heap = 8001u;
    Netadapt.window_args.reserve = 8000u;
    Netadapt.window_args.min_win = 1024u;
    Netadapt.window_args.max_win = 16384u;
    Netadapt.window(netadapt_work);
    TEST_ASSERT_EQUAL_UINT32(1024u, Netadapt.u32);
}

// Header: "a quarter of the heap above the reserve". Each expectation is that division written out.
void test_window_is_a_quarter_of_the_spare_heap(void)
{
    // (40000 - 8000) / 4 = 32000 / 4 = 8000
    Netadapt.window_args.free_heap = 40000u;
    Netadapt.window_args.reserve = 8000u;
    Netadapt.window_args.min_win = 1024u;
    Netadapt.window_args.max_win = 16384u;
    Netadapt.window(netadapt_work);
    TEST_ASSERT_EQUAL_UINT32(8000u, Netadapt.u32);
    // (12096 - 8000) / 4 = 4096 / 4 = 1024, exactly the floor
    Netadapt.window_args.free_heap = 12096u;
    Netadapt.window_args.reserve = 8000u;
    Netadapt.window_args.min_win = 1024u;
    Netadapt.window_args.max_win = 16384u;
    Netadapt.window(netadapt_work);
    TEST_ASSERT_EQUAL_UINT32(1024u, Netadapt.u32);
    // (12092 - 8000) / 4 = 4092 / 4 = 1023, one below the floor, so the floor wins
    Netadapt.window_args.free_heap = 12092u;
    Netadapt.window_args.reserve = 8000u;
    Netadapt.window_args.min_win = 1024u;
    Netadapt.window_args.max_win = 16384u;
    Netadapt.window(netadapt_work);
    TEST_ASSERT_EQUAL_UINT32(1024u, Netadapt.u32);
    // (73536 - 8000) / 4 = 65536 / 4 = 16384, exactly the ceiling
    Netadapt.window_args.free_heap = 73536u;
    Netadapt.window_args.reserve = 8000u;
    Netadapt.window_args.min_win = 1024u;
    Netadapt.window_args.max_win = 16384u;
    Netadapt.window(netadapt_work);
    TEST_ASSERT_EQUAL_UINT32(16384u, Netadapt.u32);
    // a reserve of zero makes the whole heap spare: 4096 / 4 = 1024
    Netadapt.window_args.free_heap = 4096u;
    Netadapt.window_args.reserve = 0u;
    Netadapt.window_args.min_win = 256u;
    Netadapt.window_args.max_win = 16384u;
    Netadapt.window(netadapt_work);
    TEST_ASSERT_EQUAL_UINT32(1024u, Netadapt.u32);
}

// One past the ceiling boundary, and far past it, both clamp.
void test_window_clamps_to_the_ceiling(void)
{
    // (73540 - 8000) / 4 = 16385, one above the ceiling
    Netadapt.window_args.free_heap = 73540u;
    Netadapt.window_args.reserve = 8000u;
    Netadapt.window_args.min_win = 1024u;
    Netadapt.window_args.max_win = 16384u;
    Netadapt.window(netadapt_work);
    TEST_ASSERT_EQUAL_UINT32(16384u, Netadapt.u32);
    Netadapt.window_args.free_heap = 4000000u;
    Netadapt.window_args.reserve = 8000u;
    Netadapt.window_args.min_win = 1024u;
    Netadapt.window_args.max_win = 16384u;
    Netadapt.window(netadapt_work);
    TEST_ASSERT_EQUAL_UINT32(16384u, Netadapt.u32);
    // the widest heap a uint32_t can name still lands on the ceiling, not on an overflowed value
    Netadapt.window_args.free_heap = 0xFFFFFFFFu;
    Netadapt.window_args.reserve = 8000u;
    Netadapt.window_args.min_win = 1024u;
    Netadapt.window_args.max_win = 16384u;
    Netadapt.window(netadapt_work);
    TEST_ASSERT_EQUAL_UINT32(16384u, Netadapt.u32);
}

// Header: "If max_win < min_win the result is min_win" - for a heap with spare and without.
void test_window_inverted_bounds_yield_the_floor(void)
{
    Netadapt.window_args.free_heap = 100000u;
    Netadapt.window_args.reserve = 8000u;
    Netadapt.window_args.min_win = 4096u;
    Netadapt.window_args.max_win = 1024u;
    Netadapt.window(netadapt_work);
    TEST_ASSERT_EQUAL_UINT32(4096u, Netadapt.u32);
    Netadapt.window_args.free_heap = 100u;
    Netadapt.window_args.reserve = 8000u;
    Netadapt.window_args.min_win = 4096u;
    Netadapt.window_args.max_win = 1024u;
    Netadapt.window(netadapt_work);
    TEST_ASSERT_EQUAL_UINT32(4096u, Netadapt.u32);
}

// The result is never outside [min_win, max_win]: swept across the reserve boundary and the
// quarter-of-spare range, which is what an lwIP netif is handed unchecked.
void test_window_stays_inside_the_stated_bounds(void)
{
    static const uint32_t MIN = 1024u;
    static const uint32_t MAX = 16384u;
    for (uint32_t heap = 0u; heap <= 120000u; heap += 137u)
    {
        Netadapt.window_args.free_heap = heap;
        Netadapt.window_args.reserve = 8000u;
        Netadapt.window_args.min_win = MIN;
        Netadapt.window_args.max_win = MAX;
        Netadapt.window(netadapt_work);
        uint32_t w = Netadapt.u32;
        TEST_ASSERT_TRUE_MESSAGE(w >= MIN, "below the floor");
        TEST_ASSERT_TRUE_MESSAGE(w <= MAX, "above the ceiling");
    }
}

// More free heap never asks for a smaller window: the decision has no local dips a caller would
// see as flapping.
void test_window_never_shrinks_as_the_heap_grows(void)
{
    Netadapt.window_args.free_heap = 0u;
    Netadapt.window_args.reserve = 8000u;
    Netadapt.window_args.min_win = 1024u;
    Netadapt.window_args.max_win = 16384u;
    Netadapt.window(netadapt_work);
    uint32_t prev = Netadapt.u32;
    for (uint32_t heap = 0u; heap <= 120000u; heap += 61u)
    {
        Netadapt.window_args.free_heap = heap;
        Netadapt.window_args.reserve = 8000u;
        Netadapt.window_args.min_win = 1024u;
        Netadapt.window_args.max_win = 16384u;
        Netadapt.window(netadapt_work);
        uint32_t w = Netadapt.u32;
        TEST_ASSERT_TRUE_MESSAGE(w >= prev, "window shrank as the heap grew");
        prev = w;
    }
}

// Header: "once the elapsed wait exceeds timeout_ms". The trigger is at the timeout, not past it.
void test_dhcp_fallback_timeout_boundary(void)
{
    Netadapt.dhcp_fallback_args.elapsed_ms = 9999u;
    Netadapt.dhcp_fallback_args.attempts = 1u;
    Netadapt.dhcp_fallback_args.timeout_ms = 10000u;
    Netadapt.dhcp_fallback_args.max_attempts = 5u;
    Netadapt.dhcp_fallback(netadapt_work);
    TEST_ASSERT_FALSE(Netadapt.ok);
    Netadapt.dhcp_fallback_args.elapsed_ms = 10000u;
    Netadapt.dhcp_fallback_args.attempts = 1u;
    Netadapt.dhcp_fallback_args.timeout_ms = 10000u;
    Netadapt.dhcp_fallback_args.max_attempts = 5u;
    Netadapt.dhcp_fallback(netadapt_work);
    TEST_ASSERT_TRUE(Netadapt.ok);
    Netadapt.dhcp_fallback_args.elapsed_ms = 10001u;
    Netadapt.dhcp_fallback_args.attempts = 1u;
    Netadapt.dhcp_fallback_args.timeout_ms = 10000u;
    Netadapt.dhcp_fallback_args.max_attempts = 5u;
    Netadapt.dhcp_fallback(netadapt_work);
    TEST_ASSERT_TRUE(Netadapt.ok);
    // a zero timeout fires immediately
    Netadapt.dhcp_fallback_args.elapsed_ms = 0u;
    Netadapt.dhcp_fallback_args.attempts = 0u;
    Netadapt.dhcp_fallback_args.timeout_ms = 0u;
    Netadapt.dhcp_fallback_args.max_attempts = 0u;
    Netadapt.dhcp_fallback(netadapt_work);
    TEST_ASSERT_TRUE(Netadapt.ok);
}

// Header: "(when max_attempts > 0) the attempts reach max_attempts"; 0 ignores the attempt count.
void test_dhcp_fallback_attempt_budget(void)
{
    Netadapt.dhcp_fallback_args.elapsed_ms = 1000u;
    Netadapt.dhcp_fallback_args.attempts = 4u;
    Netadapt.dhcp_fallback_args.timeout_ms = 10000u;
    Netadapt.dhcp_fallback_args.max_attempts = 5u;
    Netadapt.dhcp_fallback(netadapt_work);
    TEST_ASSERT_FALSE(Netadapt.ok);
    Netadapt.dhcp_fallback_args.elapsed_ms = 1000u;
    Netadapt.dhcp_fallback_args.attempts = 5u;
    Netadapt.dhcp_fallback_args.timeout_ms = 10000u;
    Netadapt.dhcp_fallback_args.max_attempts = 5u;
    Netadapt.dhcp_fallback(netadapt_work);
    TEST_ASSERT_TRUE(Netadapt.ok);
    Netadapt.dhcp_fallback_args.elapsed_ms = 1000u;
    Netadapt.dhcp_fallback_args.attempts = 6u;
    Netadapt.dhcp_fallback_args.timeout_ms = 10000u;
    Netadapt.dhcp_fallback_args.max_attempts = 5u;
    Netadapt.dhcp_fallback(netadapt_work);
    TEST_ASSERT_TRUE(Netadapt.ok);
    Netadapt.dhcp_fallback_args.elapsed_ms = 1000u;
    Netadapt.dhcp_fallback_args.attempts = 0xFFFFFFFFu;
    Netadapt.dhcp_fallback_args.timeout_ms = 10000u;
    Netadapt.dhcp_fallback_args.max_attempts = 0u;
    Netadapt.dhcp_fallback(netadapt_work);
    TEST_ASSERT_FALSE(Netadapt.ok);
}

// Neither counter runs backwards on a device, so once the fallback is due it stays due: no
// oscillation between DHCP and the static address.
void test_dhcp_fallback_latches_as_the_counters_advance(void)
{
    proto_bool seen = PROTO_FALSE;
    for (uint32_t ms = 0u; ms <= 20000u; ms += 250u)
    {
        Netadapt.dhcp_fallback_args.elapsed_ms = ms;
        Netadapt.dhcp_fallback_args.attempts = 1u;
        Netadapt.dhcp_fallback_args.timeout_ms = 10000u;
        Netadapt.dhcp_fallback_args.max_attempts = 5u;
        Netadapt.dhcp_fallback(netadapt_work);
        proto_bool now = Netadapt.ok;
        if (seen)
        {
            TEST_ASSERT_TRUE_MESSAGE(now, "fallback un-triggered as time advanced");
        }
        seen = now;
    }
    TEST_ASSERT_TRUE(seen);

    seen = PROTO_FALSE;
    for (uint32_t n = 0u; n <= 12u; n++)
    {
        Netadapt.dhcp_fallback_args.elapsed_ms = 0u;
        Netadapt.dhcp_fallback_args.attempts = n;
        Netadapt.dhcp_fallback_args.timeout_ms = 10000u;
        Netadapt.dhcp_fallback_args.max_attempts = 5u;
        Netadapt.dhcp_fallback(netadapt_work);
        proto_bool now = Netadapt.ok;
        if (seen)
        {
            TEST_ASSERT_TRUE_MESSAGE(now, "fallback un-triggered as attempts advanced");
        }
        seen = now;
    }
    TEST_ASSERT_TRUE(seen);
}
