// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/mdns_adaptive: RF-aware backoff, TTL refresher, auto-sleep beacon.

#include "network_drivers/application/mdns_adaptive/mdns_adaptive.h"
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_refresh_interval(void)
{
    TEST_ASSERT_EQUAL_UINT32(60000, pc_mdns_refresh_interval(120)); // 120s TTL -> refresh at 60s
    TEST_ASSERT_EQUAL_UINT32(0, pc_mdns_refresh_interval(0));
}

void test_backoff_and_recover(void)
{
    MdnsBeacon b;
    pc_mdns_beacon_init(&b, 60000, 480000, 5); // base 60s, ceil 480s, backoff at 5 contenders
    // Crowded: double each window, capped at ceiling.
    TEST_ASSERT_EQUAL_UINT32(120000, pc_mdns_beacon_adapt(&b, 8));
    TEST_ASSERT_EQUAL_UINT32(240000, pc_mdns_beacon_adapt(&b, 8));
    TEST_ASSERT_EQUAL_UINT32(480000, pc_mdns_beacon_adapt(&b, 8));
    TEST_ASSERT_EQUAL_UINT32(480000, pc_mdns_beacon_adapt(&b, 8)); // clamped at ceiling
    // Moderate contention (below threshold, nonzero): hold.
    TEST_ASSERT_EQUAL_UINT32(480000, pc_mdns_beacon_adapt(&b, 3));
    // Quiet air: halve back toward base.
    TEST_ASSERT_EQUAL_UINT32(240000, pc_mdns_beacon_adapt(&b, 0));
    TEST_ASSERT_EQUAL_UINT32(120000, pc_mdns_beacon_adapt(&b, 0));
    TEST_ASSERT_EQUAL_UINT32(60000, pc_mdns_beacon_adapt(&b, 0));
    TEST_ASSERT_EQUAL_UINT32(60000, pc_mdns_beacon_adapt(&b, 0)); // clamped at base
}

void test_due(void)
{
    MdnsBeacon b;
    pc_mdns_beacon_init(&b, 60000, 480000, 5);
    TEST_ASSERT_FALSE(pc_mdns_beacon_due(&b, 1000, 1000 + 59999));
    TEST_ASSERT_TRUE(pc_mdns_beacon_due(&b, 1000, 1000 + 60000));
    TEST_ASSERT_TRUE(pc_mdns_beacon_due(&b, 1000, 1000 + 120000));
    // Wrap-safe: last just below the uint32 rollover, now just after.
    TEST_ASSERT_TRUE(pc_mdns_beacon_due(&b, 0xFFFFFF00u, 0xFFFFFF00u + 60000));
}

void test_presleep(void)
{
    MdnsBeacon b;
    pc_mdns_beacon_init(&b, 60000, 480000, 5);
    // 10s since last announce; a 40s sleep would leave us at 50s < 60s -> not due yet.
    TEST_ASSERT_FALSE(pc_mdns_beacon_presleep_due(&b, 0, 10000, 40000));
    // 10s since last; a 60s sleep would reach 70s >= 60s -> announce before sleeping.
    TEST_ASSERT_TRUE(pc_mdns_beacon_presleep_due(&b, 0, 10000, 60000));
    // Huge sleep must not overflow the elapsed+sleep sum.
    TEST_ASSERT_TRUE(pc_mdns_beacon_presleep_due(&b, 0, 0xFFFFFFF0u, 0xFFFFFFF0u));
}

void test_refresh_interval_overflow(void)
{
    // ttl_s large enough that ttl_s * 1000 / 2 overflows a uint32_t -> clamp to UINT32_MAX.
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, pc_mdns_refresh_interval(0xFFFFFFFFu));
}

void test_beacon_init_clamps_and_defaults(void)
{
    MdnsBeacon b;
    // max_ms below base_ms: the ceiling clamps up to the floor.
    pc_mdns_beacon_init(&b, 1000, 500, 5);
    TEST_ASSERT_EQUAL_UINT32(1000, b.max_ms);
    // hi_thresh of 0 defaults to 1 (else nothing would ever count as "high contention").
    MdnsBeacon b2;
    pc_mdns_beacon_init(&b2, 1000, 5000, 0);
    TEST_ASSERT_EQUAL_UINT16(1, b2.hi_thresh);
}

void test_beacon_adapt_overflow_clamps_to_ceiling(void)
{
    MdnsBeacon b;
    // base_ms picked so doubling overflows a uint32_t (the shifted value wraps below cur_ms).
    pc_mdns_beacon_init(&b, 0xC0000000u, 0xFFFFFFFFu, 1);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, pc_mdns_beacon_adapt(&b, 1));
}

void test_beacon_null_guards(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, pc_mdns_beacon_adapt(NULL, 5));
    TEST_ASSERT_FALSE(pc_mdns_beacon_due(NULL, 0, 1000));
    TEST_ASSERT_FALSE(pc_mdns_beacon_presleep_due(NULL, 0, 1000, 500));
}

void test_refresh_interval_and_beacon()
{
    (void)pc_mdns_refresh_interval(0); // ttl 0 edge
    (void)pc_mdns_refresh_interval(3600);
    MdnsBeacon b;
    pc_mdns_beacon_init(&b, 1000, 60000, 3);
    pc_mdns_beacon_adapt(&b, 5);                           // high contention
    pc_mdns_beacon_adapt(&b, 0);                           // no contention
    TEST_ASSERT_FALSE(pc_mdns_beacon_due(&b, 1000, 1000)); // not yet due
    pc_mdns_beacon_init(NULL, 0, 0, 0);                    // null guard
}

// --- contention window sampler --------------------------------------------

void test_contention_no_sample_before_the_window(void)
{
    MdnsContentionWindow w;
    pc_mdns_contention_init(&w, 1000, 0, 100000);
    uint16_t c = 0xAB;
    // Window is 1000 ms; 999 ms in, nothing is reported and the out-param is untouched.
    TEST_ASSERT_FALSE(pc_mdns_contention_sample(&w, 50, 100999, &c));
    TEST_ASSERT_EQUAL_UINT16(0xAB, c);
}

void test_contention_reports_the_window_delta(void)
{
    MdnsContentionWindow w;
    pc_mdns_contention_init(&w, 1000, 0, 100000);
    uint16_t c = 0;
    TEST_ASSERT_TRUE(pc_mdns_contention_sample(&w, 42, 101000, &c));
    TEST_ASSERT_EQUAL_UINT16(42, c); // 42 frames counted over the window
}

void test_contention_delta_is_per_window_not_cumulative(void)
{
    MdnsContentionWindow w;
    pc_mdns_contention_init(&w, 1000, 0, 100000);
    uint16_t c = 0;
    TEST_ASSERT_TRUE(pc_mdns_contention_sample(&w, 100, 101000, &c));
    TEST_ASSERT_EQUAL_UINT16(100, c);
    // The running total keeps climbing; the next window reports only its own share.
    TEST_ASSERT_TRUE(pc_mdns_contention_sample(&w, 130, 102000, &c));
    TEST_ASSERT_EQUAL_UINT16(30, c);
}

void test_contention_saturates_at_uint16(void)
{
    MdnsContentionWindow w;
    pc_mdns_contention_init(&w, 1000, 0, 100000);
    uint16_t c = 0;
    TEST_ASSERT_TRUE(pc_mdns_contention_sample(&w, 70000, 101000, &c)); // > 0xFFFF
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, c);
}

void test_contention_frame_counter_wrap(void)
{
    // The promiscuous counter is uint32 and will eventually wrap. A window straddling the wrap must
    // still yield the true count via modular subtraction.
    MdnsContentionWindow w;
    pc_mdns_contention_init(&w, 1000, 0xFFFFFFF0u, 100000);
    uint16_t c = 0;
    TEST_ASSERT_TRUE(pc_mdns_contention_sample(&w, 0x0000000Fu, 101000, &c)); // wrapped by 0x1F
    TEST_ASSERT_EQUAL_UINT16(0x1F, c);
}

void test_contention_clock_wrap(void)
{
    // The millis clock wraps too; the window-elapsed test is modular, so a window straddling the
    // rollover still closes on time.
    MdnsContentionWindow w;
    pc_mdns_contention_init(&w, 1000, 0, 0xFFFFFC00u);
    uint16_t c = 0;
    TEST_ASSERT_FALSE(pc_mdns_contention_sample(&w, 5, 0xFFFFFE00u, &c)); // 512 ms in
    TEST_ASSERT_TRUE(pc_mdns_contention_sample(&w, 9, 0x00000000u, &c));  // 1024 ms in, past the wrap
    TEST_ASSERT_EQUAL_UINT16(9, c);
}

void test_contention_zero_window_defaults(void)
{
    MdnsContentionWindow w;
    pc_mdns_contention_init(&w, 0, 0, 100000); // 0 -> a 1000 ms default
    TEST_ASSERT_EQUAL_UINT32(1000, w.window_ms);
}

void test_contention_null_is_safe(void)
{
    uint16_t c = 0;
    pc_mdns_contention_init(NULL, 1000, 0, 0);
    MdnsContentionWindow w;
    pc_mdns_contention_init(&w, 1000, 0, 0);
    TEST_ASSERT_FALSE(pc_mdns_contention_sample(NULL, 0, 2000, &c));
    TEST_ASSERT_FALSE(pc_mdns_contention_sample(&w, 0, 2000, NULL));
}

// The whole loop, in the pure domain: a busy window backs the interval off, a quiet one recovers it.
void test_contention_drives_the_beacon(void)
{
    MdnsBeacon b;
    pc_mdns_beacon_init(&b, 1000, 8000, 50); // base 1s, ceiling 8s, back off at >=50 frames/window
    MdnsContentionWindow w;
    pc_mdns_contention_init(&w, 1000, 0, 0);

    uint16_t c = 0;
    // Busy window: 200 frames -> back off.
    TEST_ASSERT_TRUE(pc_mdns_contention_sample(&w, 200, 1000, &c));
    pc_mdns_beacon_adapt(&b, c);
    TEST_ASSERT_EQUAL_UINT32(2000, b.cur_ms);

    // Still busy -> back off again.
    TEST_ASSERT_TRUE(pc_mdns_contention_sample(&w, 400, 2000, &c));
    pc_mdns_beacon_adapt(&b, c);
    TEST_ASSERT_EQUAL_UINT32(4000, b.cur_ms);

    // Silent window (no new frames) -> recover.
    TEST_ASSERT_TRUE(pc_mdns_contention_sample(&w, 400, 3000, &c));
    TEST_ASSERT_EQUAL_UINT16(0, c);
    pc_mdns_beacon_adapt(&b, c);
    TEST_ASSERT_EQUAL_UINT32(2000, b.cur_ms);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_refresh_interval);
    RUN_TEST(test_backoff_and_recover);
    RUN_TEST(test_due);
    RUN_TEST(test_presleep);
    RUN_TEST(test_refresh_interval_overflow);
    RUN_TEST(test_beacon_init_clamps_and_defaults);
    RUN_TEST(test_beacon_adapt_overflow_clamps_to_ceiling);
    RUN_TEST(test_beacon_null_guards);
    RUN_TEST(test_refresh_interval_and_beacon);
    RUN_TEST(test_contention_no_sample_before_the_window);
    RUN_TEST(test_contention_reports_the_window_delta);
    RUN_TEST(test_contention_delta_is_per_window_not_cumulative);
    RUN_TEST(test_contention_saturates_at_uint16);
    RUN_TEST(test_contention_frame_counter_wrap);
    RUN_TEST(test_contention_clock_wrap);
    RUN_TEST(test_contention_zero_window_defaults);
    RUN_TEST(test_contention_null_is_safe);
    RUN_TEST(test_contention_drives_the_beacon);
    return UNITY_END();
}
