// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/netadapt: TCP window sizing by free RAM + DHCP->static fallback.

#include "services/net/netadapt/netadapt.h"
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_window_floor_when_low_heap(void)
{
    // heap at or below the reserve -> the floor.
    TEST_ASSERT_EQUAL_UINT32(1024, pc_netadapt_window(8000, 8000, 1024, 16384));
    TEST_ASSERT_EQUAL_UINT32(1024, pc_netadapt_window(5000, 8000, 1024, 16384));
}

void test_window_scales_with_heap(void)
{
    // (free - reserve)/4, clamped. free=40000, reserve=8000 -> 32000/4 = 8000.
    TEST_ASSERT_EQUAL_UINT32(8000, pc_netadapt_window(40000, 8000, 1024, 16384));
    // Below the floor: free=12000 -> 4000/4=1000 < 1024 floor.
    TEST_ASSERT_EQUAL_UINT32(1024, pc_netadapt_window(12000, 8000, 1024, 16384));
}

void test_window_clamps_to_ceiling(void)
{
    // Huge heap -> clamped to max_win.
    TEST_ASSERT_EQUAL_UINT32(16384, pc_netadapt_window(200000, 8000, 1024, 16384));
}

void test_window_degenerate_max_below_min(void)
{
    TEST_ASSERT_EQUAL_UINT32(4096, pc_netadapt_window(100000, 8000, 4096, 1024));
}

void test_dhcp_fallback_on_timeout(void)
{
    TEST_ASSERT_FALSE(pc_netadapt_dhcp_fallback(9000, 1, 10000, 5)); // still within budget
    TEST_ASSERT_TRUE(pc_netadapt_dhcp_fallback(10000, 1, 10000, 5)); // timed out
}

void test_dhcp_fallback_on_attempts(void)
{
    TEST_ASSERT_TRUE(pc_netadapt_dhcp_fallback(1000, 5, 10000, 5)); // hit the attempt budget
    TEST_ASSERT_FALSE(pc_netadapt_dhcp_fallback(1000, 4, 10000, 5));
    // max_attempts 0 disables the attempt trigger.
    TEST_ASSERT_FALSE(pc_netadapt_dhcp_fallback(1000, 99, 10000, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_window_floor_when_low_heap);
    RUN_TEST(test_window_scales_with_heap);
    RUN_TEST(test_window_clamps_to_ceiling);
    RUN_TEST(test_window_degenerate_max_below_min);
    RUN_TEST(test_dhcp_fallback_on_timeout);
    RUN_TEST(test_dhcp_fallback_on_attempts);
    return UNITY_END();
}
