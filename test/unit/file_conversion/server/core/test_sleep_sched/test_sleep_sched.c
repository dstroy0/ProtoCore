// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the dynamic sleep-cycle scheduler (server/core/sleep_sched.h).
//
// No external standard governs this scheduler: it is a ProtoCore policy core, so the only document
// that can supply an expected value is the contract sleep_sched.h publishes. Every numeric
// expectation below is arithmetic derived from these four published sentences, with the derivation
// written into the case:
//
//   sleep_sched.h:63-64  "Reports 0 while idle < idle_ms; otherwise a window clamped to
//                         [min_ms, max_ms] that grows with the idle streak (doubling every
//                         cfg.ramp_ms, or straight to max_ms when ramp_ms is 0). If max_ms < min_ms
//                         the result is clamped to min_ms."
//   sleep_sched.h:61-62  "Wrap-safe: uses the unsigned delta now - last_active_ms".
//   sleep_sched.h:33-36  idle_ms "stay fully awake until idle at least this long", min_ms "first
//                         sleep window once idle (also the floor)", ramp_ms "every additional
//                         ramp_ms of idle doubles the window".
//   sleep_sched.h:57     ms "milliseconds to sleep, or 0 to stay awake".
//
// Those two field notes together fix the doubling count: min_ms is the window at idle == idle_ms,
// and each further ramp_ms doubles it, so
//
//     ms = clamp(min_ms * 2^floor((idle - idle_ms) / ramp_ms), min_ms, max(max_ms, min_ms))
//
// which is the closed form every ramp case checks a point of.
//
// The load-bearing case is test_the_window_doubles_every_ramp: it walks that formula across the
// whole ramp, from the threshold to past the ceiling, so a wrong doubling count or a missing clamp
// cannot survive it.
//
// PROPERTIES ONLY, NO PUBLISHED VALUE: test_a_zero_floor_stays_inside_its_bounds. With min_ms == 0
// the formula above degenerates - 0 doubles to 0 forever - and the header publishes no other base,
// so no document in this repo fixes what the window is when the floor is zero. That case therefore
// asserts only what holds whatever the base is: the bounds and the monotonic growth. It does not
// assert the base of 1 that sleep_sched.c:51 substitutes, because that 1 appears nowhere but the
// implementation. test_a_null_config_stays_awake is likewise a refusal property, not a published
// value: the header documents no behavior for a null cfg, and 0 is the only report that does not
// invent thresholds the caller never supplied.

#include "server/core/sleep_sched.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// idle_ms 1000, min_ms 100, max_ms 1600, ramp_ms 500: the ceiling sits exactly 4 doublings above the
// floor (100 * 2^4 = 1600), so every step of the ramp lands on a value the formula names.
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

// last_active_ms 0, so now is the idle streak itself.
static uint32_t after_idle(uint32_t idle)
{
    return window(&CFG, idle, 0u);
}

// "Reports 0 while idle < idle_ms" and idle_ms is "stay fully awake until idle at least this long",
// so 999 is still awake and 1000 is not. min_ms is "first sleep window once idle", which fixes the
// value at the threshold without appealing to the ramp.
void test_stays_awake_until_the_idle_threshold(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, after_idle(0u));
    TEST_ASSERT_EQUAL_UINT32(0u, after_idle(999u));
    TEST_ASSERT_EQUAL_UINT32(CFG.min_ms, after_idle(1000u));
}

// ms = clamp(100 * 2^floor((idle - 1000) / 500), 100, 1600):
//   idle 1000 -> floor(   0/500) = 0 -> 100 * 1  =  100
//   idle 1499 -> floor( 499/500) = 0 -> 100 * 1  =  100
//   idle 1500 -> floor( 500/500) = 1 -> 100 * 2  =  200
//   idle 2000 -> floor(1000/500) = 2 -> 100 * 4  =  400
//   idle 2500 -> floor(1500/500) = 3 -> 100 * 8  =  800
//   idle 3000 -> floor(2000/500) = 4 -> 100 * 16 = 1600
//   idle 3500 -> floor(2500/500) = 5 -> 100 * 32 = 3200, clamped to max_ms = 1600
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

// A ceiling that is not a power-of-two multiple of the floor, so the clamp is the only thing that
// can produce the answer. min_ms 100, max_ms 250, ramp_ms 500, idle_ms 0:
//   idle    0 -> 0 doublings -> 100
//   idle  500 -> 1 doubling  -> 200        (200 <= 250, no clamp)
//   idle 1000 -> 2 doublings -> 400 -> 250 (clamped to max_ms, not to the 200 below it)
//   idle 9000 -> 18 doublings                -> 250
void test_the_ceiling_clamps_off_the_doubling_grid(void)
{
    const protocore_sleep_cfg odd_ceiling = {.idle_ms = 0, .min_ms = 100, .max_ms = 250, .ramp_ms = 500};
    TEST_ASSERT_EQUAL_UINT32(100u, window(&odd_ceiling, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT32(200u, window(&odd_ceiling, 500u, 0u));
    TEST_ASSERT_EQUAL_UINT32(250u, window(&odd_ceiling, 1000u, 0u));
    TEST_ASSERT_EQUAL_UINT32(250u, window(&odd_ceiling, 9000u, 0u));
}

// "clamped to [min_ms, max_ms]" holds for every idle streak past the threshold, including the
// largest one a uint32 can name.
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

// Doubling a floor that is already past half of a uint32 ceiling must clamp, not wrap:
// min_ms 0x40000000, max_ms 0xFFFFFFFF, ramp_ms 1, idle 100 -> 100 doublings. 0x40000000 * 2^2 is
// 0x100000000, which does not fit, so every idle past 2 ramps reports the ceiling.
void test_the_doubling_clamps_instead_of_overflowing(void)
{
    const protocore_sleep_cfg wide = {.idle_ms = 0, .min_ms = 0x40000000u, .max_ms = 0xFFFFFFFFu, .ramp_ms = 1};
    TEST_ASSERT_EQUAL_UINT32(0x40000000u, window(&wide, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT32(0x80000000u, window(&wide, 1u, 0u));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, window(&wide, 2u, 0u));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, window(&wide, 100u, 0u));
}

// "grows with the idle streak": the window is non-decreasing in idle, whatever the config.
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

// "or straight to max_ms when ramp_ms is 0" - the threshold still applies, then the ceiling, with no
// intermediate value at any idle streak.
void test_no_ramp_goes_straight_to_the_ceiling(void)
{
    const protocore_sleep_cfg no_ramp = {.idle_ms = 1000, .min_ms = 100, .max_ms = 1600, .ramp_ms = 0};
    TEST_ASSERT_EQUAL_UINT32(0u, window(&no_ramp, 999u, 0u));
    TEST_ASSERT_EQUAL_UINT32(1600u, window(&no_ramp, 1000u, 0u));
    TEST_ASSERT_EQUAL_UINT32(1600u, window(&no_ramp, 100000u, 0u));
}

// "If max_ms < min_ms the result is clamped to min_ms" - min_ms 500 over max_ms 100 reports 500 at
// every idle streak, on both the ramped and the ramp_ms == 0 path.
void test_a_ceiling_below_the_floor_clamps_to_the_floor(void)
{
    const protocore_sleep_cfg inverted = {.idle_ms = 0, .min_ms = 500, .max_ms = 100, .ramp_ms = 100};
    TEST_ASSERT_EQUAL_UINT32(500u, window(&inverted, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT32(500u, window(&inverted, 100000u, 0u));

    const protocore_sleep_cfg inverted_no_ramp = {.idle_ms = 0, .min_ms = 500, .max_ms = 100, .ramp_ms = 0};
    TEST_ASSERT_EQUAL_UINT32(500u, window(&inverted_no_ramp, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT32(500u, window(&inverted_no_ramp, 100000u, 0u));
}

// "uses the unsigned delta now - last_active_ms": the streak is computed mod 2^32, so a timestamp
// taken before a millis() rollover still reads as a short streak.
//   0x00000000 - 0xFFFFFC18 = 0x3E8 = 1000, so now 500 is idle 1000 + 500 = 1500
//     -> floor((1500 - 1000) / 500) = 1 doubling -> 100 * 2 = 200
//   0xFFFFFFFF - 0xFFFFFC18 = 0x3E7 =  999, one short of idle_ms -> stay awake
void test_the_idle_streak_is_wrap_safe(void)
{
    TEST_ASSERT_EQUAL_UINT32(200u, window(&CFG, 500u, 0xFFFFFC18u));
    TEST_ASSERT_EQUAL_UINT32(0u, window(&CFG, 0xFFFFFFFFu, 0xFFFFFC18u));
}

// Refusal property, not a published value: with no cfg there are no thresholds to read, and the
// only report that invents none is 0 - "stay awake" (sleep_sched.h:57).
void test_a_null_config_stays_awake(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, window(NULL, 100000u, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, window(NULL, 0u, 0u));
}

// PROPERTIES ONLY. min_ms 0 leaves the doubling base undocumented (0 * 2^n is 0 at every n, and the
// header names no substitute), so this case asserts only what holds for any base: the report stays
// inside [min_ms, max_ms] = [0, 8] and never decreases as the streak grows. The 1, 2, 8 chain the
// implementation produces is not asserted - sleep_sched.c:51 is its only source.
void test_a_zero_floor_stays_inside_its_bounds(void)
{
    const protocore_sleep_cfg zero_min = {.idle_ms = 0, .min_ms = 0, .max_ms = 8, .ramp_ms = 100};
    uint32_t prev = 0;
    for (uint32_t idle = 0; idle <= 2000u; idle += 50u)
    {
        const uint32_t ms = window(&zero_min, idle, 0u);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(zero_min.max_ms, ms);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(prev, ms);
        prev = ms;
    }
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(zero_min.max_ms, window(&zero_min, 0xFFFFFF00u, 0u));
}
