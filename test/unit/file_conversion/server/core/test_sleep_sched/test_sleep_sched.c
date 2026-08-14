// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "server/core/sleep_sched.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static const protocore_sleep_cfg CFG = {
    .idle_ms = 1000,
    .min_ms = 100,
    .max_ms = 1600,
    .ramp_ms = 500,
};

static uint32_t window(const protocore_sleep_cfg *cfg, uint32_t now, uint32_t last_active_ms)
{
    SleepSched.ask.now = now;
    SleepSched.ask.last_active_ms = last_active_ms;
    SleepSched.ask.cfg = cfg;
    SleepSched.next(SleepSched.internal);
    return SleepSched.ms;
}

static uint32_t after_idle(uint32_t idle)
{
    return window(&CFG, idle, 0u);
}

void test_awake_until_the_idle_threshold(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, after_idle(0u));
    TEST_ASSERT_EQUAL_UINT32(0u, after_idle(999u));
    TEST_ASSERT_EQUAL_UINT32(CFG.min_ms, after_idle(1000u));
}

void test_the_window_doubles_every_ramp(void)
{
    TEST_ASSERT_EQUAL_UINT32(100u, after_idle(1000u));
    TEST_ASSERT_EQUAL_UINT32(100u, after_idle(1499u));
    TEST_ASSERT_EQUAL_UINT32(200u, after_idle(1500u));
    TEST_ASSERT_EQUAL_UINT32(400u, after_idle(2000u));
    TEST_ASSERT_EQUAL_UINT32(800u, after_idle(2500u));
    TEST_ASSERT_EQUAL_UINT32(1600u, after_idle(3000u));
    TEST_ASSERT_EQUAL_UINT32(1600u, after_idle(3500u));
}

void test_the_window_never_leaves_its_bounds(void)
{
    for (uint32_t idle = CFG.idle_ms; idle < 4000000u; idle += 9973u)
    {
        const uint32_t ms = after_idle(idle);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(CFG.min_ms, ms);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(CFG.max_ms, ms);
    }
    TEST_ASSERT_EQUAL_UINT32(CFG.max_ms, after_idle(0xFFFFFF00u));
}

void test_the_window_is_monotonic_in_the_idle_streak(void)
{
    uint32_t prev = 0;
    for (uint32_t idle = CFG.idle_ms; idle <= 6000u; idle += 100u)
    {
        const uint32_t ms = after_idle(idle);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(prev, ms);
        prev = ms;
    }
}

void test_no_ramp_goes_straight_to_the_ceiling(void)
{
    const protocore_sleep_cfg no_ramp = {.idle_ms = 1000, .min_ms = 100, .max_ms = 1600, .ramp_ms = 0};
    TEST_ASSERT_EQUAL_UINT32(0u, window(&no_ramp, 999u, 0u));
    TEST_ASSERT_EQUAL_UINT32(1600u, window(&no_ramp, 1000u, 0u));
    TEST_ASSERT_EQUAL_UINT32(1600u, window(&no_ramp, 100000u, 0u));
}

void test_a_ceiling_below_the_floor_clamps_to_the_floor(void)
{
    const protocore_sleep_cfg inverted = {.idle_ms = 0, .min_ms = 500, .max_ms = 100, .ramp_ms = 100};
    TEST_ASSERT_EQUAL_UINT32(500u, window(&inverted, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT32(500u, window(&inverted, 100000u, 0u));

    const protocore_sleep_cfg inverted_no_ramp = {.idle_ms = 0, .min_ms = 500, .max_ms = 100, .ramp_ms = 0};
    TEST_ASSERT_EQUAL_UINT32(500u, window(&inverted_no_ramp, 0u, 0u));
}

void test_the_idle_streak_is_wrap_safe(void)
{
    TEST_ASSERT_EQUAL_UINT32(200u, window(&CFG, 500u, 0xFFFFFC18u));

    TEST_ASSERT_EQUAL_UINT32(0u, window(&CFG, 0xFFFFFFFFu, 0xFFFFFC18u));
}

void test_a_null_config_stays_awake(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, window(NULL, 100000u, 0u));
}

void test_a_zero_floor_still_yields_a_window(void)
{
    const protocore_sleep_cfg zero_min = {.idle_ms = 0, .min_ms = 0, .max_ms = 8, .ramp_ms = 100};
    TEST_ASSERT_EQUAL_UINT32(1u, window(&zero_min, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT32(2u, window(&zero_min, 100u, 0u));
    TEST_ASSERT_EQUAL_UINT32(8u, window(&zero_min, 300u, 0u));
    TEST_ASSERT_EQUAL_UINT32(8u, window(&zero_min, 900u, 0u));
}
