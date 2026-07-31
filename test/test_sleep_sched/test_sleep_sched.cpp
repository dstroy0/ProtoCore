// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/sleep_sched: the dynamic sleep-cycle decision core. Pure, synthetic clock.

#include "services/system/sleep_sched/sleep_sched.h"
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// idle_ms=1000, ramp doubles min=100 every 500ms of idle-past-threshold, up to max=2000.
static const pc_sleep_cfg CFG = {1000, 100, 2000, 500};

void test_awake_when_recent(void)
{
    // idle 999 < 1000 -> stay awake.
    TEST_ASSERT_EQUAL_UINT32(0, pc_sleep_next(1999, 1000, &CFG));
}

void test_min_window_at_threshold(void)
{
    // idle exactly 1000: past threshold, 0 doublings -> the floor.
    TEST_ASSERT_EQUAL_UINT32(100, pc_sleep_next(2000, 1000, &CFG));
    // idle 1499: still < one full ramp period past threshold -> still the floor.
    TEST_ASSERT_EQUAL_UINT32(100, pc_sleep_next(2499, 1000, &CFG));
}

void test_ramp_doubles(void)
{
    // idle 1500: one ramp period (500) past threshold -> 100<<1 = 200.
    TEST_ASSERT_EQUAL_UINT32(200, pc_sleep_next(2500, 1000, &CFG));
    // idle 2000: two ramp periods -> 400.
    TEST_ASSERT_EQUAL_UINT32(400, pc_sleep_next(3000, 1000, &CFG));
    // idle 3000: four periods -> 100<<4 = 1600.
    TEST_ASSERT_EQUAL_UINT32(1600, pc_sleep_next(4000, 1000, &CFG));
}

void test_clamps_to_ceiling(void)
{
    // idle 10000: many periods, clamped to max_ms = 2000 (not 100<<18).
    TEST_ASSERT_EQUAL_UINT32(2000, pc_sleep_next(11000, 1000, &CFG));
}

void test_no_ramp_jumps_to_ceiling(void)
{
    pc_sleep_cfg cfg = {1000, 100, 2000, 0};                         // ramp_ms 0
    TEST_ASSERT_EQUAL_UINT32(0, pc_sleep_next(1500, 1000, &cfg));    // still awake
    TEST_ASSERT_EQUAL_UINT32(2000, pc_sleep_next(2000, 1000, &cfg)); // straight to max
}

void test_degenerate_max_below_min(void)
{
    pc_sleep_cfg cfg = {1000, 500, 100, 0}; // max < min -> clamp to min
    TEST_ASSERT_EQUAL_UINT32(500, pc_sleep_next(2000, 1000, &cfg));
}

void test_wrap_safe(void)
{
    // last_active just before the millis() rollover, now just after: real idle 1284 >= 1000.
    uint32_t w = pc_sleep_next(0x00000400u, 0xFFFFFF00u, &CFG);
    TEST_ASSERT_TRUE(w >= 100 && w <= 2000);
    // real idle 306 < 1000 -> awake.
    TEST_ASSERT_EQUAL_UINT32(0, pc_sleep_next(50, 0xFFFFFF00u, &CFG));
}

void test_null_cfg(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, pc_sleep_next(5000, 0, nullptr));
}

void test_zero_min_and_max_clamps_seed_window_down(void)
{
    // min_ms=0 -> the "or 1" seed kicks in (window starts at 1); max_ms=0 too, so ceil_ms=0.
    // With 0 doublings the seeded window (1) never gets a chance to double, so it is still > ceil_ms
    // (0) after the loop and must be clamped down to the ceiling.
    pc_sleep_cfg cfg = {1000, 0, 0, 500};
    TEST_ASSERT_EQUAL_UINT32(0, pc_sleep_next(2000, 1000, &cfg));
}

void test_window_hits_ceiling_exactly_before_doubling(void)
{
    // min_ms=4, max_ms=8: window doubles 4 -> 8 on the first iteration (4 is not > ceil_ms/2 == 4,
    // so it doubles instead of clamping), then the second iteration's pre-doubling guard sees
    // window (8) already >= ceil_ms (8) and breaks on that arm directly.
    pc_sleep_cfg cfg = {1000, 4, 8, 500};
    TEST_ASSERT_EQUAL_UINT32(8, pc_sleep_next(3000, 1000, &cfg));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_awake_when_recent);
    RUN_TEST(test_min_window_at_threshold);
    RUN_TEST(test_ramp_doubles);
    RUN_TEST(test_clamps_to_ceiling);
    RUN_TEST(test_no_ramp_jumps_to_ceiling);
    RUN_TEST(test_degenerate_max_below_min);
    RUN_TEST(test_wrap_safe);
    RUN_TEST(test_null_cfg);
    RUN_TEST(test_zero_min_and_max_clamps_seed_window_down);
    RUN_TEST(test_window_hits_ceiling_exactly_before_doubling);
    return UNITY_END();
}
