// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for adaptive mDNS beacon scheduling
// (network_drivers/application/mdns_adaptive/mdns_adaptive.h).
//
// RFC 6762 sec 10 publishes the record lifetimes this schedule is built around: a Multicast DNS
// record naming a host SHOULD carry a TTL of 120 seconds, and every other record 75 minutes. The
// backoff and recovery rules are the module's own, so those cases are PROPERTIES.
//
// test_the_refresher_is_half_the_rfc6762_record_lifetime is the load-bearing case. A cache evicts a
// record when its TTL lapses, so the refresher has to fire inside the lifetime the RFC names, and
// the two published TTLs are turned into milliseconds here by arithmetic written out in the comment
// rather than by asking the code what it produces.

#include "network_drivers/application/mdns_adaptive/mdns_adaptive.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// RFC 6762 sec 10: 120 s for a record naming a host, 75 minutes for every other record. Half of
// each, in milliseconds:
//   120 s          -> 120 * 1000 / 2                 =      60000 ms
//   75 min = 4500 s -> 4500 * 1000 / 2               =    2250000 ms
// The saturation boundary comes from the return width: half_ms = ttl_s * 500 must fit a uint32, so
// the largest exact answer is at ttl_s = floor(0xFFFFFFFF / 500) = 8589934, giving 4294967000, and
// one second more overflows and clamps.
void test_the_refresher_is_half_the_rfc6762_record_lifetime(void)
{
    MdnsAdaptiveV.refresh_interval_args.ttl_s = 120;
    MdnsAdaptive.refresh_interval(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(60000u, MdnsAdaptiveV.ms);
    MdnsAdaptiveV.refresh_interval_args.ttl_s = 4500;
    MdnsAdaptive.refresh_interval(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(2250000u, MdnsAdaptiveV.ms);
    MdnsAdaptiveV.refresh_interval_args.ttl_s = 0;
    MdnsAdaptive.refresh_interval(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(0u, MdnsAdaptiveV.ms);
    MdnsAdaptiveV.refresh_interval_args.ttl_s = 1;
    MdnsAdaptive.refresh_interval(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(500u, MdnsAdaptiveV.ms);
    MdnsAdaptiveV.refresh_interval_args.ttl_s = 8589934;
    MdnsAdaptive.refresh_interval(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(4294967000u, MdnsAdaptiveV.ms);
    MdnsAdaptiveV.refresh_interval_args.ttl_s = 8589935;
    MdnsAdaptive.refresh_interval(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, MdnsAdaptiveV.ms);
    MdnsAdaptiveV.refresh_interval_args.ttl_s = 0xFFFFFFFFu;
    MdnsAdaptive.refresh_interval(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, MdnsAdaptiveV.ms);
}

// The floor is the nominal cadence and the ceiling can never sit below it, so a misconfigured pair
// cannot produce an interval that skips the refresher entirely. A zero threshold would make every
// sample count as contention, so it is raised to one.
void test_init_cannot_produce_a_ceiling_below_the_floor(void)
{
    MdnsBeacon b;
    MdnsAdaptiveV.beacon_init_args.b = &b;
    MdnsAdaptiveV.beacon_init_args.base_ms = 60000u;
    MdnsAdaptiveV.beacon_init_args.max_ms = 240000u;
    MdnsAdaptiveV.beacon_init_args.hi_thresh = 40u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(60000u, b.base_ms);
    TEST_ASSERT_EQUAL_UINT32(240000u, b.max_ms);
    TEST_ASSERT_EQUAL_UINT32(60000u, b.cur_ms); // starts at the nominal cadence
    TEST_ASSERT_EQUAL_UINT16(40u, b.hi_thresh);

    MdnsAdaptiveV.beacon_init_args.b = &b;
    MdnsAdaptiveV.beacon_init_args.base_ms = 60000u;
    MdnsAdaptiveV.beacon_init_args.max_ms = 1000u;
    MdnsAdaptiveV.beacon_init_args.hi_thresh = 0u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(60000u, b.max_ms); // the ceiling is raised to the floor
    TEST_ASSERT_EQUAL_UINT32(60000u, b.cur_ms);
    TEST_ASSERT_EQUAL_UINT16(1u, b.hi_thresh);
}

// Contention at or above the threshold doubles the interval, and the doubling stops at the ceiling
// rather than running past it or wrapping.
void test_contention_backs_the_interval_off_to_the_ceiling(void)
{
    MdnsBeacon b;
    MdnsAdaptiveV.beacon_init_args.b = &b;
    MdnsAdaptiveV.beacon_init_args.base_ms = 1000u;
    MdnsAdaptiveV.beacon_init_args.max_ms = 8000u;
    MdnsAdaptiveV.beacon_init_args.hi_thresh = 40u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());

    MdnsAdaptiveV.beacon_adapt_args.b = &b;
    MdnsAdaptiveV.beacon_adapt_args.contention = 40u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(2000u, MdnsAdaptiveV.ms); // at the threshold
    MdnsAdaptiveV.beacon_adapt_args.b = &b;
    MdnsAdaptiveV.beacon_adapt_args.contention = 100u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(4000u, MdnsAdaptiveV.ms);
    MdnsAdaptiveV.beacon_adapt_args.b = &b;
    MdnsAdaptiveV.beacon_adapt_args.contention = 100u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(8000u, MdnsAdaptiveV.ms);
    MdnsAdaptiveV.beacon_adapt_args.b = &b;
    MdnsAdaptiveV.beacon_adapt_args.contention = 100u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(8000u, MdnsAdaptiveV.ms); // held at the ceiling
    MdnsAdaptiveV.beacon_adapt_args.b = &b;
    MdnsAdaptiveV.beacon_adapt_args.contention = 0xFFFFu;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(8000u, MdnsAdaptiveV.ms);

    // A ceiling that is not twice the floor is still exact: the double is clamped, not rounded up.
    MdnsAdaptiveV.beacon_init_args.b = &b;
    MdnsAdaptiveV.beacon_init_args.base_ms = 1000u;
    MdnsAdaptiveV.beacon_init_args.max_ms = 1500u;
    MdnsAdaptiveV.beacon_init_args.hi_thresh = 1u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());
    MdnsAdaptiveV.beacon_adapt_args.b = &b;
    MdnsAdaptiveV.beacon_adapt_args.contention = 1u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(1500u, MdnsAdaptiveV.ms);

    // A base near the top of the range doubles into an overflow, which lands on the ceiling too.
    MdnsAdaptiveV.beacon_init_args.b = &b;
    MdnsAdaptiveV.beacon_init_args.base_ms = 0x90000000u;
    MdnsAdaptiveV.beacon_init_args.max_ms = 0xF0000000u;
    MdnsAdaptiveV.beacon_init_args.hi_thresh = 1u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());
    MdnsAdaptiveV.beacon_adapt_args.b = &b;
    MdnsAdaptiveV.beacon_adapt_args.contention = 1u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(0xF0000000u, MdnsAdaptiveV.ms);
}

// Quiet air halves the interval back toward the nominal cadence and stops there: the recovery never
// announces faster than the base the caller asked for.
void test_quiet_air_recovers_the_interval_to_the_floor(void)
{
    MdnsBeacon b;
    MdnsAdaptiveV.beacon_init_args.b = &b;
    MdnsAdaptiveV.beacon_init_args.base_ms = 1000u;
    MdnsAdaptiveV.beacon_init_args.max_ms = 8000u;
    MdnsAdaptiveV.beacon_init_args.hi_thresh = 40u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());
    MdnsAdaptiveV.beacon_adapt_args.b = &b;
    MdnsAdaptiveV.beacon_adapt_args.contention = 100u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    (void)MdnsAdaptiveV.ms;
    MdnsAdaptiveV.beacon_adapt_args.b = &b;
    MdnsAdaptiveV.beacon_adapt_args.contention = 100u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    (void)MdnsAdaptiveV.ms;
    MdnsAdaptiveV.beacon_adapt_args.b = &b;
    MdnsAdaptiveV.beacon_adapt_args.contention = 100u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    (void)MdnsAdaptiveV.ms;
    TEST_ASSERT_EQUAL_UINT32(8000u, b.cur_ms);

    MdnsAdaptiveV.beacon_adapt_args.b = &b;
    MdnsAdaptiveV.beacon_adapt_args.contention = 0u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(4000u, MdnsAdaptiveV.ms);
    MdnsAdaptiveV.beacon_adapt_args.b = &b;
    MdnsAdaptiveV.beacon_adapt_args.contention = 0u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(2000u, MdnsAdaptiveV.ms);
    MdnsAdaptiveV.beacon_adapt_args.b = &b;
    MdnsAdaptiveV.beacon_adapt_args.contention = 0u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(1000u, MdnsAdaptiveV.ms);
    MdnsAdaptiveV.beacon_adapt_args.b = &b;
    MdnsAdaptiveV.beacon_adapt_args.contention = 0u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(1000u, MdnsAdaptiveV.ms); // held at the floor

    // A halving that would land below the floor is clamped to it, not rounded down.
    MdnsAdaptiveV.beacon_init_args.b = &b;
    MdnsAdaptiveV.beacon_init_args.base_ms = 1500u;
    MdnsAdaptiveV.beacon_init_args.max_ms = 8000u;
    MdnsAdaptiveV.beacon_init_args.hi_thresh = 40u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());
    MdnsAdaptiveV.beacon_adapt_args.b = &b;
    MdnsAdaptiveV.beacon_adapt_args.contention = 100u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    (void)MdnsAdaptiveV.ms; // 3000
    MdnsAdaptiveV.beacon_adapt_args.b = &b;
    MdnsAdaptiveV.beacon_adapt_args.contention = 0u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(1500u, MdnsAdaptiveV.ms);
}

// Between silence and the threshold the interval is left where it is, so a moderately busy channel
// neither backs off nor recovers.
void test_moderate_contention_holds_the_interval(void)
{
    MdnsBeacon b;
    MdnsAdaptiveV.beacon_init_args.b = &b;
    MdnsAdaptiveV.beacon_init_args.base_ms = 1000u;
    MdnsAdaptiveV.beacon_init_args.max_ms = 8000u;
    MdnsAdaptiveV.beacon_init_args.hi_thresh = 40u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());
    MdnsAdaptiveV.beacon_adapt_args.b = &b;
    MdnsAdaptiveV.beacon_adapt_args.contention = 100u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    (void)MdnsAdaptiveV.ms; // 2000
    for (uint16_t c = 1; c < 40u; c++)
    {
        MdnsAdaptiveV.beacon_adapt_args.b = &b;
        MdnsAdaptiveV.beacon_adapt_args.contention = c;
        MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
        TEST_ASSERT_EQUAL_UINT32(2000u, MdnsAdaptiveV.ms);
    }
}

// The due test is modular subtraction, so it survives the millis() rollover: an announce scheduled
// across the wrap fires at the same elapsed time it would anywhere else.
void test_due_is_wrap_safe_across_the_millis_rollover(void)
{
    MdnsBeacon b;
    MdnsAdaptiveV.beacon_init_args.b = &b;
    MdnsAdaptiveV.beacon_init_args.base_ms = 1000u;
    MdnsAdaptiveV.beacon_init_args.max_ms = 8000u;
    MdnsAdaptiveV.beacon_init_args.hi_thresh = 40u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());

    MdnsAdaptiveV.beacon_due_args.b = &b;
    MdnsAdaptiveV.beacon_due_args.last_ms = 5000u;
    MdnsAdaptiveV.beacon_due_args.now_ms = 5999u;
    MdnsAdaptive.beacon_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_FALSE(MdnsAdaptiveV.ok);
    MdnsAdaptiveV.beacon_due_args.b = &b;
    MdnsAdaptiveV.beacon_due_args.last_ms = 5000u;
    MdnsAdaptiveV.beacon_due_args.now_ms = 6000u;
    MdnsAdaptive.beacon_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptiveV.ok); // exactly the interval
    MdnsAdaptiveV.beacon_due_args.b = &b;
    MdnsAdaptiveV.beacon_due_args.last_ms = 5000u;
    MdnsAdaptiveV.beacon_due_args.now_ms = 60000u;
    MdnsAdaptive.beacon_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptiveV.ok);

    // The last announce sits 256 ms before the wrap, so the 1000 ms interval expires 744 ms after
    // the counter restarts: 0xFFFFFFFF - 0xFFFFFF00 + 1 = 256, and 1000 - 256 = 744 = 0x2E8.
    const uint32_t last = 0xFFFFFF00u;
    MdnsAdaptiveV.beacon_due_args.b = &b;
    MdnsAdaptiveV.beacon_due_args.last_ms = last;
    MdnsAdaptiveV.beacon_due_args.now_ms = 0x000002E7u;
    MdnsAdaptive.beacon_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_FALSE(MdnsAdaptiveV.ok); //  999 ms elapsed
    MdnsAdaptiveV.beacon_due_args.b = &b;
    MdnsAdaptiveV.beacon_due_args.last_ms = last;
    MdnsAdaptiveV.beacon_due_args.now_ms = 0x000002E8u;
    MdnsAdaptive.beacon_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptiveV.ok); // 1000 ms elapsed
}

// A sleep that would carry the record past its refresh point announces first, so the record
// survives the sleep instead of lapsing with the radio off.
void test_a_sleep_that_would_lapse_the_record_announces_first(void)
{
    MdnsBeacon b;
    MdnsAdaptiveV.beacon_init_args.b = &b;
    MdnsAdaptiveV.beacon_init_args.base_ms = 1000u;
    MdnsAdaptiveV.beacon_init_args.max_ms = 8000u;
    MdnsAdaptiveV.beacon_init_args.hi_thresh = 40u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());

    // 200 ms since the last announce: a 799 ms sleep lands short of the interval, an 800 ms sleep
    // reaches it exactly.
    MdnsAdaptiveV.beacon_presleep_due_args.b = &b;
    MdnsAdaptiveV.beacon_presleep_due_args.last_ms = 1000u;
    MdnsAdaptiveV.beacon_presleep_due_args.now_ms = 1200u;
    MdnsAdaptiveV.beacon_presleep_due_args.sleep_ms = 799u;
    MdnsAdaptive.beacon_presleep_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_FALSE(MdnsAdaptiveV.ok);
    MdnsAdaptiveV.beacon_presleep_due_args.b = &b;
    MdnsAdaptiveV.beacon_presleep_due_args.last_ms = 1000u;
    MdnsAdaptiveV.beacon_presleep_due_args.now_ms = 1200u;
    MdnsAdaptiveV.beacon_presleep_due_args.sleep_ms = 800u;
    MdnsAdaptive.beacon_presleep_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptiveV.ok);
    MdnsAdaptiveV.beacon_presleep_due_args.b = &b;
    MdnsAdaptiveV.beacon_presleep_due_args.last_ms = 1000u;
    MdnsAdaptiveV.beacon_presleep_due_args.now_ms = 1200u;
    MdnsAdaptiveV.beacon_presleep_due_args.sleep_ms = 100000u;
    MdnsAdaptive.beacon_presleep_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptiveV.ok);

    // No sleep at all is the plain due test.
    MdnsAdaptiveV.beacon_presleep_due_args.b = &b;
    MdnsAdaptiveV.beacon_presleep_due_args.last_ms = 1000u;
    MdnsAdaptiveV.beacon_presleep_due_args.now_ms = 1200u;
    MdnsAdaptiveV.beacon_presleep_due_args.sleep_ms = 0u;
    MdnsAdaptive.beacon_presleep_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_FALSE(MdnsAdaptiveV.ok);
    MdnsAdaptiveV.beacon_presleep_due_args.b = &b;
    MdnsAdaptiveV.beacon_presleep_due_args.last_ms = 1000u;
    MdnsAdaptiveV.beacon_presleep_due_args.now_ms = 2000u;
    MdnsAdaptiveV.beacon_presleep_due_args.sleep_ms = 0u;
    MdnsAdaptive.beacon_presleep_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptiveV.ok);

    // The elapsed-plus-sleep sum is taken wider than the counter, so a sleep near the full range
    // cannot wrap back under the interval and skip the announce.
    MdnsAdaptiveV.beacon_presleep_due_args.b = &b;
    MdnsAdaptiveV.beacon_presleep_due_args.last_ms = 1000u;
    MdnsAdaptiveV.beacon_presleep_due_args.now_ms = 1200u;
    MdnsAdaptiveV.beacon_presleep_due_args.sleep_ms = 0xFFFFFFFFu;
    MdnsAdaptive.beacon_presleep_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptiveV.ok);
}

// A window reports the frames counted in it and starts the next one; before the window closes there
// is nothing to report.
void test_the_sampling_window_reports_the_frames_it_counted(void)
{
    MdnsContentionWindow w;
    uint16_t got = 0xAAAA;

    MdnsAdaptiveV.contention_init_args.w = &w;
    MdnsAdaptiveV.contention_init_args.window_ms = 1000u;
    MdnsAdaptiveV.contention_init_args.frames_now = 500u;
    MdnsAdaptiveV.contention_init_args.now_ms = 10000u;
    MdnsAdaptive.contention_init(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(1000u, w.window_ms);

    MdnsAdaptiveV.contention_sample_args.w = &w;
    MdnsAdaptiveV.contention_sample_args.frames_now = 700u;
    MdnsAdaptiveV.contention_sample_args.now_ms = 10999u;
    MdnsAdaptiveV.contention_sample_args.out = &got;
    MdnsAdaptive.contention_sample(protocore_mdns_adaptive_span());
    TEST_ASSERT_FALSE(MdnsAdaptiveV.ok);
    TEST_ASSERT_EQUAL_UINT16(0xAAAA, got); // untouched while the window is open

    // 700 - 500 = 200 frames in the window that just closed.
    MdnsAdaptiveV.contention_sample_args.w = &w;
    MdnsAdaptiveV.contention_sample_args.frames_now = 700u;
    MdnsAdaptiveV.contention_sample_args.now_ms = 11000u;
    MdnsAdaptiveV.contention_sample_args.out = &got;
    MdnsAdaptive.contention_sample(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptiveV.ok);
    TEST_ASSERT_EQUAL_UINT16(200u, got);

    // The next window is anchored on what was just reported, so counts do not accumulate.
    MdnsAdaptiveV.contention_sample_args.w = &w;
    MdnsAdaptiveV.contention_sample_args.frames_now = 730u;
    MdnsAdaptiveV.contention_sample_args.now_ms = 12000u;
    MdnsAdaptiveV.contention_sample_args.out = &got;
    MdnsAdaptive.contention_sample(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptiveV.ok);
    TEST_ASSERT_EQUAL_UINT16(30u, got);

    // More frames than the uint16 the adapt step takes: saturated, never truncated to the low bits.
    MdnsAdaptiveV.contention_sample_args.w = &w;
    MdnsAdaptiveV.contention_sample_args.frames_now = 730u + 0x10000u;
    MdnsAdaptiveV.contention_sample_args.now_ms = 13000u;
    MdnsAdaptiveV.contention_sample_args.out = &got;
    MdnsAdaptive.contention_sample(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptiveV.ok);
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, got);

    // A zero window length falls back to one second.
    MdnsAdaptiveV.contention_init_args.w = &w;
    MdnsAdaptiveV.contention_init_args.window_ms = 0u;
    MdnsAdaptiveV.contention_init_args.frames_now = 0u;
    MdnsAdaptiveV.contention_init_args.now_ms = 0u;
    MdnsAdaptive.contention_init(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(1000u, w.window_ms);
}

// Both the frame counter and the clock wrap, and the modular difference reports the true count
// across either one.
void test_the_sampling_window_is_wrap_safe(void)
{
    MdnsContentionWindow w;
    uint16_t got = 0;

    // The frame counter wraps inside the window: 0x20 - 0xFFFFFFF0 = 0x30 frames.
    MdnsAdaptiveV.contention_init_args.w = &w;
    MdnsAdaptiveV.contention_init_args.window_ms = 1000u;
    MdnsAdaptiveV.contention_init_args.frames_now = 0xFFFFFFF0u;
    MdnsAdaptiveV.contention_init_args.now_ms = 0u;
    MdnsAdaptive.contention_init(protocore_mdns_adaptive_span());
    MdnsAdaptiveV.contention_sample_args.w = &w;
    MdnsAdaptiveV.contention_sample_args.frames_now = 0x20u;
    MdnsAdaptiveV.contention_sample_args.now_ms = 1000u;
    MdnsAdaptiveV.contention_sample_args.out = &got;
    MdnsAdaptive.contention_sample(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptiveV.ok);
    TEST_ASSERT_EQUAL_UINT16(0x30u, got);

    // The clock wraps inside the window: the sample was taken 256 ms before the wrap, so the 1000 ms
    // window closes 744 ms after the counter restarts (1000 - 256 = 744 = 0x2E8).
    MdnsAdaptiveV.contention_init_args.w = &w;
    MdnsAdaptiveV.contention_init_args.window_ms = 1000u;
    MdnsAdaptiveV.contention_init_args.frames_now = 0u;
    MdnsAdaptiveV.contention_init_args.now_ms = 0xFFFFFF00u;
    MdnsAdaptive.contention_init(protocore_mdns_adaptive_span());
    MdnsAdaptiveV.contention_sample_args.w = &w;
    MdnsAdaptiveV.contention_sample_args.frames_now = 5u;
    MdnsAdaptiveV.contention_sample_args.now_ms = 0x000002E7u;
    MdnsAdaptiveV.contention_sample_args.out = &got;
    MdnsAdaptive.contention_sample(protocore_mdns_adaptive_span());
    TEST_ASSERT_FALSE(MdnsAdaptiveV.ok); //  999 ms
    MdnsAdaptiveV.contention_sample_args.w = &w;
    MdnsAdaptiveV.contention_sample_args.frames_now = 5u;
    MdnsAdaptiveV.contention_sample_args.now_ms = 0x000002E8u;
    MdnsAdaptiveV.contention_sample_args.out = &got;
    MdnsAdaptive.contention_sample(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptiveV.ok); // 1000 ms
    TEST_ASSERT_EQUAL_UINT16(5u, got);
}

// A null handle is reported rather than dereferenced, on every entry point.
void test_null_handles_are_refused(void)
{
    uint16_t got = 0;
    MdnsContentionWindow w;
    MdnsAdaptiveV.contention_init_args.w = &w;
    MdnsAdaptiveV.contention_init_args.window_ms = 1000u;
    MdnsAdaptiveV.contention_init_args.frames_now = 0u;
    MdnsAdaptiveV.contention_init_args.now_ms = 0u;
    MdnsAdaptive.contention_init(protocore_mdns_adaptive_span());

    MdnsAdaptiveV.beacon_init_args.b = NULL;
    MdnsAdaptiveV.beacon_init_args.base_ms = 1000u;
    MdnsAdaptiveV.beacon_init_args.max_ms = 8000u;
    MdnsAdaptiveV.beacon_init_args.hi_thresh = 40u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());
    MdnsAdaptiveV.beacon_adapt_args.b = NULL;
    MdnsAdaptiveV.beacon_adapt_args.contention = 100u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(0u, MdnsAdaptiveV.ms);
    MdnsAdaptiveV.beacon_due_args.b = NULL;
    MdnsAdaptiveV.beacon_due_args.last_ms = 0u;
    MdnsAdaptiveV.beacon_due_args.now_ms = 100000u;
    MdnsAdaptive.beacon_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_FALSE(MdnsAdaptiveV.ok);
    MdnsAdaptiveV.beacon_presleep_due_args.b = NULL;
    MdnsAdaptiveV.beacon_presleep_due_args.last_ms = 0u;
    MdnsAdaptiveV.beacon_presleep_due_args.now_ms = 0u;
    MdnsAdaptiveV.beacon_presleep_due_args.sleep_ms = 100000u;
    MdnsAdaptive.beacon_presleep_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_FALSE(MdnsAdaptiveV.ok);

    MdnsAdaptiveV.contention_init_args.w = NULL;
    MdnsAdaptiveV.contention_init_args.window_ms = 1000u;
    MdnsAdaptiveV.contention_init_args.frames_now = 0u;
    MdnsAdaptiveV.contention_init_args.now_ms = 0u;
    MdnsAdaptive.contention_init(protocore_mdns_adaptive_span());
    MdnsAdaptiveV.contention_sample_args.w = NULL;
    MdnsAdaptiveV.contention_sample_args.frames_now = 100u;
    MdnsAdaptiveV.contention_sample_args.now_ms = 100000u;
    MdnsAdaptiveV.contention_sample_args.out = &got;
    MdnsAdaptive.contention_sample(protocore_mdns_adaptive_span());
    TEST_ASSERT_FALSE(MdnsAdaptiveV.ok);
    MdnsAdaptiveV.contention_sample_args.w = &w;
    MdnsAdaptiveV.contention_sample_args.frames_now = 100u;
    MdnsAdaptiveV.contention_sample_args.now_ms = 100000u;
    MdnsAdaptiveV.contention_sample_args.out = NULL;
    MdnsAdaptive.contention_sample(protocore_mdns_adaptive_span());
    TEST_ASSERT_FALSE(MdnsAdaptiveV.ok);
}
