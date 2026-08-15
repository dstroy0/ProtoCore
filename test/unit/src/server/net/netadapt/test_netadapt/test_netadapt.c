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
    TEST_ASSERT_EQUAL_UINT32(1024u, protocore_netadapt_window(0u, 8000u, 1024u, 16384u));
    TEST_ASSERT_EQUAL_UINT32(1024u, protocore_netadapt_window(7999u, 8000u, 1024u, 16384u));
    TEST_ASSERT_EQUAL_UINT32(1024u, protocore_netadapt_window(8000u, 8000u, 1024u, 16384u));
    // one octet of spare: (8001-8000)/4 = 0, below the floor, so still the floor
    TEST_ASSERT_EQUAL_UINT32(1024u, protocore_netadapt_window(8001u, 8000u, 1024u, 16384u));
}

// Header: "a quarter of the heap above the reserve". Each expectation is that division written out.
void test_window_is_a_quarter_of_the_spare_heap(void)
{
    // (40000 - 8000) / 4 = 32000 / 4 = 8000
    TEST_ASSERT_EQUAL_UINT32(8000u, protocore_netadapt_window(40000u, 8000u, 1024u, 16384u));
    // (12096 - 8000) / 4 = 4096 / 4 = 1024, exactly the floor
    TEST_ASSERT_EQUAL_UINT32(1024u, protocore_netadapt_window(12096u, 8000u, 1024u, 16384u));
    // (12092 - 8000) / 4 = 4092 / 4 = 1023, one below the floor, so the floor wins
    TEST_ASSERT_EQUAL_UINT32(1024u, protocore_netadapt_window(12092u, 8000u, 1024u, 16384u));
    // (73536 - 8000) / 4 = 65536 / 4 = 16384, exactly the ceiling
    TEST_ASSERT_EQUAL_UINT32(16384u, protocore_netadapt_window(73536u, 8000u, 1024u, 16384u));
    // a reserve of zero makes the whole heap spare: 4096 / 4 = 1024
    TEST_ASSERT_EQUAL_UINT32(1024u, protocore_netadapt_window(4096u, 0u, 256u, 16384u));
}

// One past the ceiling boundary, and far past it, both clamp.
void test_window_clamps_to_the_ceiling(void)
{
    // (73540 - 8000) / 4 = 16385, one above the ceiling
    TEST_ASSERT_EQUAL_UINT32(16384u, protocore_netadapt_window(73540u, 8000u, 1024u, 16384u));
    TEST_ASSERT_EQUAL_UINT32(16384u, protocore_netadapt_window(4000000u, 8000u, 1024u, 16384u));
    // the widest heap a uint32_t can name still lands on the ceiling, not on an overflowed value
    TEST_ASSERT_EQUAL_UINT32(16384u, protocore_netadapt_window(0xFFFFFFFFu, 8000u, 1024u, 16384u));
}

// Header: "If max_win < min_win the result is min_win" - for a heap with spare and without.
void test_window_inverted_bounds_yield_the_floor(void)
{
    TEST_ASSERT_EQUAL_UINT32(4096u, protocore_netadapt_window(100000u, 8000u, 4096u, 1024u));
    TEST_ASSERT_EQUAL_UINT32(4096u, protocore_netadapt_window(100u, 8000u, 4096u, 1024u));
}

// The result is never outside [min_win, max_win]: swept across the reserve boundary and the
// quarter-of-spare range, which is what an lwIP netif is handed unchecked.
void test_window_stays_inside_the_stated_bounds(void)
{
    static const uint32_t MIN = 1024u;
    static const uint32_t MAX = 16384u;
    for (uint32_t heap = 0u; heap <= 120000u; heap += 137u)
    {
        uint32_t w = protocore_netadapt_window(heap, 8000u, MIN, MAX);
        TEST_ASSERT_TRUE_MESSAGE(w >= MIN, "below the floor");
        TEST_ASSERT_TRUE_MESSAGE(w <= MAX, "above the ceiling");
    }
}

// More free heap never asks for a smaller window: the decision has no local dips a caller would
// see as flapping.
void test_window_never_shrinks_as_the_heap_grows(void)
{
    uint32_t prev = protocore_netadapt_window(0u, 8000u, 1024u, 16384u);
    for (uint32_t heap = 0u; heap <= 120000u; heap += 61u)
    {
        uint32_t w = protocore_netadapt_window(heap, 8000u, 1024u, 16384u);
        TEST_ASSERT_TRUE_MESSAGE(w >= prev, "window shrank as the heap grew");
        prev = w;
    }
}

// Header: "once the elapsed wait exceeds timeout_ms". The trigger is at the timeout, not past it.
void test_dhcp_fallback_timeout_boundary(void)
{
    TEST_ASSERT_FALSE(protocore_netadapt_dhcp_fallback(9999u, 1u, 10000u, 5u));
    TEST_ASSERT_TRUE(protocore_netadapt_dhcp_fallback(10000u, 1u, 10000u, 5u));
    TEST_ASSERT_TRUE(protocore_netadapt_dhcp_fallback(10001u, 1u, 10000u, 5u));
    // a zero timeout fires immediately
    TEST_ASSERT_TRUE(protocore_netadapt_dhcp_fallback(0u, 0u, 0u, 0u));
}

// Header: "(when max_attempts > 0) the attempts reach max_attempts"; 0 ignores the attempt count.
void test_dhcp_fallback_attempt_budget(void)
{
    TEST_ASSERT_FALSE(protocore_netadapt_dhcp_fallback(1000u, 4u, 10000u, 5u));
    TEST_ASSERT_TRUE(protocore_netadapt_dhcp_fallback(1000u, 5u, 10000u, 5u));
    TEST_ASSERT_TRUE(protocore_netadapt_dhcp_fallback(1000u, 6u, 10000u, 5u));
    TEST_ASSERT_FALSE(protocore_netadapt_dhcp_fallback(1000u, 0xFFFFFFFFu, 10000u, 0u));
}

// Neither counter runs backwards on a device, so once the fallback is due it stays due: no
// oscillation between DHCP and the static address.
void test_dhcp_fallback_latches_as_the_counters_advance(void)
{
    proto_bool seen = PROTO_FALSE;
    for (uint32_t ms = 0u; ms <= 20000u; ms += 250u)
    {
        proto_bool now = protocore_netadapt_dhcp_fallback(ms, 1u, 10000u, 5u);
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
        proto_bool now = protocore_netadapt_dhcp_fallback(0u, n, 10000u, 5u);
        if (seen)
        {
            TEST_ASSERT_TRUE_MESSAGE(now, "fallback un-triggered as attempts advanced");
        }
        seen = now;
    }
    TEST_ASSERT_TRUE(seen);
}
