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
    MdnsAdaptive.refresh_interval_args.ttl_s = 120;
    MdnsAdaptive.refresh_interval(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(60000u, MdnsAdaptive.ms);
    MdnsAdaptive.refresh_interval_args.ttl_s = 4500;
    MdnsAdaptive.refresh_interval(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(2250000u, MdnsAdaptive.ms);
    MdnsAdaptive.refresh_interval_args.ttl_s = 0;
    MdnsAdaptive.refresh_interval(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(0u, MdnsAdaptive.ms);
    MdnsAdaptive.refresh_interval_args.ttl_s = 1;
    MdnsAdaptive.refresh_interval(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(500u, MdnsAdaptive.ms);
    MdnsAdaptive.refresh_interval_args.ttl_s = 8589934;
    MdnsAdaptive.refresh_interval(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(4294967000u, MdnsAdaptive.ms);
    MdnsAdaptive.refresh_interval_args.ttl_s = 8589935;
    MdnsAdaptive.refresh_interval(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, MdnsAdaptive.ms);
    MdnsAdaptive.refresh_interval_args.ttl_s = 0xFFFFFFFFu;
    MdnsAdaptive.refresh_interval(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, MdnsAdaptive.ms);
}

// The floor is the nominal cadence and the ceiling can never sit below it, so a misconfigured pair
// cannot produce an interval that skips the refresher entirely. A zero threshold would make every
// sample count as contention, so it is raised to one.
void test_init_cannot_produce_a_ceiling_below_the_floor(void)
{
    MdnsBeacon b;
    MdnsAdaptive.beacon_init_args.b = &b;
    MdnsAdaptive.beacon_init_args.base_ms = 60000u;
    MdnsAdaptive.beacon_init_args.max_ms = 240000u;
    MdnsAdaptive.beacon_init_args.hi_thresh = 40u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(60000u, b.base_ms);
    TEST_ASSERT_EQUAL_UINT32(240000u, b.max_ms);
    TEST_ASSERT_EQUAL_UINT32(60000u, b.cur_ms); // starts at the nominal cadence
    TEST_ASSERT_EQUAL_UINT16(40u, b.hi_thresh);

    MdnsAdaptive.beacon_init_args.b = &b;
    MdnsAdaptive.beacon_init_args.base_ms = 60000u;
    MdnsAdaptive.beacon_init_args.max_ms = 1000u;
    MdnsAdaptive.beacon_init_args.hi_thresh = 0u;
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
    MdnsAdaptive.beacon_init_args.b = &b;
    MdnsAdaptive.beacon_init_args.base_ms = 1000u;
    MdnsAdaptive.beacon_init_args.max_ms = 8000u;
    MdnsAdaptive.beacon_init_args.hi_thresh = 40u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());

    MdnsAdaptive.beacon_adapt_args.b = &b;
    MdnsAdaptive.beacon_adapt_args.contention = 40u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(2000u, MdnsAdaptive.ms); // at the threshold
    MdnsAdaptive.beacon_adapt_args.b = &b;
    MdnsAdaptive.beacon_adapt_args.contention = 100u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(4000u, MdnsAdaptive.ms);
    MdnsAdaptive.beacon_adapt_args.b = &b;
    MdnsAdaptive.beacon_adapt_args.contention = 100u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(8000u, MdnsAdaptive.ms);
    MdnsAdaptive.beacon_adapt_args.b = &b;
    MdnsAdaptive.beacon_adapt_args.contention = 100u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(8000u, MdnsAdaptive.ms); // held at the ceiling
    MdnsAdaptive.beacon_adapt_args.b = &b;
    MdnsAdaptive.beacon_adapt_args.contention = 0xFFFFu;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(8000u, MdnsAdaptive.ms);

    // A ceiling that is not twice the floor is still exact: the double is clamped, not rounded up.
    MdnsAdaptive.beacon_init_args.b = &b;
    MdnsAdaptive.beacon_init_args.base_ms = 1000u;
    MdnsAdaptive.beacon_init_args.max_ms = 1500u;
    MdnsAdaptive.beacon_init_args.hi_thresh = 1u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());
    MdnsAdaptive.beacon_adapt_args.b = &b;
    MdnsAdaptive.beacon_adapt_args.contention = 1u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(1500u, MdnsAdaptive.ms);

    // A base near the top of the range doubles into an overflow, which lands on the ceiling too.
    MdnsAdaptive.beacon_init_args.b = &b;
    MdnsAdaptive.beacon_init_args.base_ms = 0x90000000u;
    MdnsAdaptive.beacon_init_args.max_ms = 0xF0000000u;
    MdnsAdaptive.beacon_init_args.hi_thresh = 1u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());
    MdnsAdaptive.beacon_adapt_args.b = &b;
    MdnsAdaptive.beacon_adapt_args.contention = 1u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(0xF0000000u, MdnsAdaptive.ms);
}

// Quiet air halves the interval back toward the nominal cadence and stops there: the recovery never
// announces faster than the base the caller asked for.
void test_quiet_air_recovers_the_interval_to_the_floor(void)
{
    MdnsBeacon b;
    MdnsAdaptive.beacon_init_args.b = &b;
    MdnsAdaptive.beacon_init_args.base_ms = 1000u;
    MdnsAdaptive.beacon_init_args.max_ms = 8000u;
    MdnsAdaptive.beacon_init_args.hi_thresh = 40u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());
    MdnsAdaptive.beacon_adapt_args.b = &b;
    MdnsAdaptive.beacon_adapt_args.contention = 100u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    (void)MdnsAdaptive.ms;
    MdnsAdaptive.beacon_adapt_args.b = &b;
    MdnsAdaptive.beacon_adapt_args.contention = 100u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    (void)MdnsAdaptive.ms;
    MdnsAdaptive.beacon_adapt_args.b = &b;
    MdnsAdaptive.beacon_adapt_args.contention = 100u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    (void)MdnsAdaptive.ms;
    TEST_ASSERT_EQUAL_UINT32(8000u, b.cur_ms);

    MdnsAdaptive.beacon_adapt_args.b = &b;
    MdnsAdaptive.beacon_adapt_args.contention = 0u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(4000u, MdnsAdaptive.ms);
    MdnsAdaptive.beacon_adapt_args.b = &b;
    MdnsAdaptive.beacon_adapt_args.contention = 0u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(2000u, MdnsAdaptive.ms);
    MdnsAdaptive.beacon_adapt_args.b = &b;
    MdnsAdaptive.beacon_adapt_args.contention = 0u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(1000u, MdnsAdaptive.ms);
    MdnsAdaptive.beacon_adapt_args.b = &b;
    MdnsAdaptive.beacon_adapt_args.contention = 0u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(1000u, MdnsAdaptive.ms); // held at the floor

    // A halving that would land below the floor is clamped to it, not rounded down.
    MdnsAdaptive.beacon_init_args.b = &b;
    MdnsAdaptive.beacon_init_args.base_ms = 1500u;
    MdnsAdaptive.beacon_init_args.max_ms = 8000u;
    MdnsAdaptive.beacon_init_args.hi_thresh = 40u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());
    MdnsAdaptive.beacon_adapt_args.b = &b;
    MdnsAdaptive.beacon_adapt_args.contention = 100u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    (void)MdnsAdaptive.ms; // 3000
    MdnsAdaptive.beacon_adapt_args.b = &b;
    MdnsAdaptive.beacon_adapt_args.contention = 0u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(1500u, MdnsAdaptive.ms);
}

// Between silence and the threshold the interval is left where it is, so a moderately busy channel
// neither backs off nor recovers.
void test_moderate_contention_holds_the_interval(void)
{
    MdnsBeacon b;
    MdnsAdaptive.beacon_init_args.b = &b;
    MdnsAdaptive.beacon_init_args.base_ms = 1000u;
    MdnsAdaptive.beacon_init_args.max_ms = 8000u;
    MdnsAdaptive.beacon_init_args.hi_thresh = 40u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());
    MdnsAdaptive.beacon_adapt_args.b = &b;
    MdnsAdaptive.beacon_adapt_args.contention = 100u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    (void)MdnsAdaptive.ms; // 2000
    for (uint16_t c = 1; c < 40u; c++)
    {
        MdnsAdaptive.beacon_adapt_args.b = &b;
        MdnsAdaptive.beacon_adapt_args.contention = c;
        MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
        TEST_ASSERT_EQUAL_UINT32(2000u, MdnsAdaptive.ms);
    }
}

// The due test is modular subtraction, so it survives the millis() rollover: an announce scheduled
// across the wrap fires at the same elapsed time it would anywhere else.
void test_due_is_wrap_safe_across_the_millis_rollover(void)
{
    MdnsBeacon b;
    MdnsAdaptive.beacon_init_args.b = &b;
    MdnsAdaptive.beacon_init_args.base_ms = 1000u;
    MdnsAdaptive.beacon_init_args.max_ms = 8000u;
    MdnsAdaptive.beacon_init_args.hi_thresh = 40u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());

    MdnsAdaptive.beacon_due_args.b = &b;
    MdnsAdaptive.beacon_due_args.last_ms = 5000u;
    MdnsAdaptive.beacon_due_args.now_ms = 5999u;
    MdnsAdaptive.beacon_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_FALSE(MdnsAdaptive.ok);
    MdnsAdaptive.beacon_due_args.b = &b;
    MdnsAdaptive.beacon_due_args.last_ms = 5000u;
    MdnsAdaptive.beacon_due_args.now_ms = 6000u;
    MdnsAdaptive.beacon_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptive.ok); // exactly the interval
    MdnsAdaptive.beacon_due_args.b = &b;
    MdnsAdaptive.beacon_due_args.last_ms = 5000u;
    MdnsAdaptive.beacon_due_args.now_ms = 60000u;
    MdnsAdaptive.beacon_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptive.ok);

    // The last announce sits 256 ms before the wrap, so the 1000 ms interval expires 744 ms after
    // the counter restarts: 0xFFFFFFFF - 0xFFFFFF00 + 1 = 256, and 1000 - 256 = 744 = 0x2E8.
    const uint32_t last = 0xFFFFFF00u;
    MdnsAdaptive.beacon_due_args.b = &b;
    MdnsAdaptive.beacon_due_args.last_ms = last;
    MdnsAdaptive.beacon_due_args.now_ms = 0x000002E7u;
    MdnsAdaptive.beacon_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_FALSE(MdnsAdaptive.ok); //  999 ms elapsed
    MdnsAdaptive.beacon_due_args.b = &b;
    MdnsAdaptive.beacon_due_args.last_ms = last;
    MdnsAdaptive.beacon_due_args.now_ms = 0x000002E8u;
    MdnsAdaptive.beacon_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptive.ok); // 1000 ms elapsed
}

// A sleep that would carry the record past its refresh point announces first, so the record
// survives the sleep instead of lapsing with the radio off.
void test_a_sleep_that_would_lapse_the_record_announces_first(void)
{
    MdnsBeacon b;
    MdnsAdaptive.beacon_init_args.b = &b;
    MdnsAdaptive.beacon_init_args.base_ms = 1000u;
    MdnsAdaptive.beacon_init_args.max_ms = 8000u;
    MdnsAdaptive.beacon_init_args.hi_thresh = 40u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());

    // 200 ms since the last announce: a 799 ms sleep lands short of the interval, an 800 ms sleep
    // reaches it exactly.
    MdnsAdaptive.beacon_presleep_due_args.b = &b;
    MdnsAdaptive.beacon_presleep_due_args.last_ms = 1000u;
    MdnsAdaptive.beacon_presleep_due_args.now_ms = 1200u;
    MdnsAdaptive.beacon_presleep_due_args.sleep_ms = 799u;
    MdnsAdaptive.beacon_presleep_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_FALSE(MdnsAdaptive.ok);
    MdnsAdaptive.beacon_presleep_due_args.b = &b;
    MdnsAdaptive.beacon_presleep_due_args.last_ms = 1000u;
    MdnsAdaptive.beacon_presleep_due_args.now_ms = 1200u;
    MdnsAdaptive.beacon_presleep_due_args.sleep_ms = 800u;
    MdnsAdaptive.beacon_presleep_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptive.ok);
    MdnsAdaptive.beacon_presleep_due_args.b = &b;
    MdnsAdaptive.beacon_presleep_due_args.last_ms = 1000u;
    MdnsAdaptive.beacon_presleep_due_args.now_ms = 1200u;
    MdnsAdaptive.beacon_presleep_due_args.sleep_ms = 100000u;
    MdnsAdaptive.beacon_presleep_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptive.ok);

    // No sleep at all is the plain due test.
    MdnsAdaptive.beacon_presleep_due_args.b = &b;
    MdnsAdaptive.beacon_presleep_due_args.last_ms = 1000u;
    MdnsAdaptive.beacon_presleep_due_args.now_ms = 1200u;
    MdnsAdaptive.beacon_presleep_due_args.sleep_ms = 0u;
    MdnsAdaptive.beacon_presleep_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_FALSE(MdnsAdaptive.ok);
    MdnsAdaptive.beacon_presleep_due_args.b = &b;
    MdnsAdaptive.beacon_presleep_due_args.last_ms = 1000u;
    MdnsAdaptive.beacon_presleep_due_args.now_ms = 2000u;
    MdnsAdaptive.beacon_presleep_due_args.sleep_ms = 0u;
    MdnsAdaptive.beacon_presleep_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptive.ok);

    // The elapsed-plus-sleep sum is taken wider than the counter, so a sleep near the full range
    // cannot wrap back under the interval and skip the announce.
    MdnsAdaptive.beacon_presleep_due_args.b = &b;
    MdnsAdaptive.beacon_presleep_due_args.last_ms = 1000u;
    MdnsAdaptive.beacon_presleep_due_args.now_ms = 1200u;
    MdnsAdaptive.beacon_presleep_due_args.sleep_ms = 0xFFFFFFFFu;
    MdnsAdaptive.beacon_presleep_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptive.ok);
}

// A window reports the frames counted in it and starts the next one; before the window closes there
// is nothing to report.
void test_the_sampling_window_reports_the_frames_it_counted(void)
{
    MdnsContentionWindow w;
    uint16_t got = 0xAAAA;

    MdnsAdaptive.contention_init_args.w = &w;
    MdnsAdaptive.contention_init_args.window_ms = 1000u;
    MdnsAdaptive.contention_init_args.frames_now = 500u;
    MdnsAdaptive.contention_init_args.now_ms = 10000u;
    MdnsAdaptive.contention_init(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(1000u, w.window_ms);

    MdnsAdaptive.contention_sample_args.w = &w;
    MdnsAdaptive.contention_sample_args.frames_now = 700u;
    MdnsAdaptive.contention_sample_args.now_ms = 10999u;
    MdnsAdaptive.contention_sample_args.out = &got;
    MdnsAdaptive.contention_sample(protocore_mdns_adaptive_span());
    TEST_ASSERT_FALSE(MdnsAdaptive.ok);
    TEST_ASSERT_EQUAL_UINT16(0xAAAA, got); // untouched while the window is open

    // 700 - 500 = 200 frames in the window that just closed.
    MdnsAdaptive.contention_sample_args.w = &w;
    MdnsAdaptive.contention_sample_args.frames_now = 700u;
    MdnsAdaptive.contention_sample_args.now_ms = 11000u;
    MdnsAdaptive.contention_sample_args.out = &got;
    MdnsAdaptive.contention_sample(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptive.ok);
    TEST_ASSERT_EQUAL_UINT16(200u, got);

    // The next window is anchored on what was just reported, so counts do not accumulate.
    MdnsAdaptive.contention_sample_args.w = &w;
    MdnsAdaptive.contention_sample_args.frames_now = 730u;
    MdnsAdaptive.contention_sample_args.now_ms = 12000u;
    MdnsAdaptive.contention_sample_args.out = &got;
    MdnsAdaptive.contention_sample(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptive.ok);
    TEST_ASSERT_EQUAL_UINT16(30u, got);

    // More frames than the uint16 the adapt step takes: saturated, never truncated to the low bits.
    MdnsAdaptive.contention_sample_args.w = &w;
    MdnsAdaptive.contention_sample_args.frames_now = 730u + 0x10000u;
    MdnsAdaptive.contention_sample_args.now_ms = 13000u;
    MdnsAdaptive.contention_sample_args.out = &got;
    MdnsAdaptive.contention_sample(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptive.ok);
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, got);

    // A zero window length falls back to one second.
    MdnsAdaptive.contention_init_args.w = &w;
    MdnsAdaptive.contention_init_args.window_ms = 0u;
    MdnsAdaptive.contention_init_args.frames_now = 0u;
    MdnsAdaptive.contention_init_args.now_ms = 0u;
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
    MdnsAdaptive.contention_init_args.w = &w;
    MdnsAdaptive.contention_init_args.window_ms = 1000u;
    MdnsAdaptive.contention_init_args.frames_now = 0xFFFFFFF0u;
    MdnsAdaptive.contention_init_args.now_ms = 0u;
    MdnsAdaptive.contention_init(protocore_mdns_adaptive_span());
    MdnsAdaptive.contention_sample_args.w = &w;
    MdnsAdaptive.contention_sample_args.frames_now = 0x20u;
    MdnsAdaptive.contention_sample_args.now_ms = 1000u;
    MdnsAdaptive.contention_sample_args.out = &got;
    MdnsAdaptive.contention_sample(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptive.ok);
    TEST_ASSERT_EQUAL_UINT16(0x30u, got);

    // The clock wraps inside the window: the sample was taken 256 ms before the wrap, so the 1000 ms
    // window closes 744 ms after the counter restarts (1000 - 256 = 744 = 0x2E8).
    MdnsAdaptive.contention_init_args.w = &w;
    MdnsAdaptive.contention_init_args.window_ms = 1000u;
    MdnsAdaptive.contention_init_args.frames_now = 0u;
    MdnsAdaptive.contention_init_args.now_ms = 0xFFFFFF00u;
    MdnsAdaptive.contention_init(protocore_mdns_adaptive_span());
    MdnsAdaptive.contention_sample_args.w = &w;
    MdnsAdaptive.contention_sample_args.frames_now = 5u;
    MdnsAdaptive.contention_sample_args.now_ms = 0x000002E7u;
    MdnsAdaptive.contention_sample_args.out = &got;
    MdnsAdaptive.contention_sample(protocore_mdns_adaptive_span());
    TEST_ASSERT_FALSE(MdnsAdaptive.ok); //  999 ms
    MdnsAdaptive.contention_sample_args.w = &w;
    MdnsAdaptive.contention_sample_args.frames_now = 5u;
    MdnsAdaptive.contention_sample_args.now_ms = 0x000002E8u;
    MdnsAdaptive.contention_sample_args.out = &got;
    MdnsAdaptive.contention_sample(protocore_mdns_adaptive_span());
    TEST_ASSERT_TRUE(MdnsAdaptive.ok); // 1000 ms
    TEST_ASSERT_EQUAL_UINT16(5u, got);
}

// A null handle is reported rather than dereferenced, on every entry point.
void test_null_handles_are_refused(void)
{
    uint16_t got = 0;
    MdnsContentionWindow w;
    MdnsAdaptive.contention_init_args.w = &w;
    MdnsAdaptive.contention_init_args.window_ms = 1000u;
    MdnsAdaptive.contention_init_args.frames_now = 0u;
    MdnsAdaptive.contention_init_args.now_ms = 0u;
    MdnsAdaptive.contention_init(protocore_mdns_adaptive_span());

    MdnsAdaptive.beacon_init_args.b = NULL;
    MdnsAdaptive.beacon_init_args.base_ms = 1000u;
    MdnsAdaptive.beacon_init_args.max_ms = 8000u;
    MdnsAdaptive.beacon_init_args.hi_thresh = 40u;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());
    MdnsAdaptive.beacon_adapt_args.b = NULL;
    MdnsAdaptive.beacon_adapt_args.contention = 100u;
    MdnsAdaptive.beacon_adapt(protocore_mdns_adaptive_span());
    TEST_ASSERT_EQUAL_UINT32(0u, MdnsAdaptive.ms);
    MdnsAdaptive.beacon_due_args.b = NULL;
    MdnsAdaptive.beacon_due_args.last_ms = 0u;
    MdnsAdaptive.beacon_due_args.now_ms = 100000u;
    MdnsAdaptive.beacon_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_FALSE(MdnsAdaptive.ok);
    MdnsAdaptive.beacon_presleep_due_args.b = NULL;
    MdnsAdaptive.beacon_presleep_due_args.last_ms = 0u;
    MdnsAdaptive.beacon_presleep_due_args.now_ms = 0u;
    MdnsAdaptive.beacon_presleep_due_args.sleep_ms = 100000u;
    MdnsAdaptive.beacon_presleep_due(protocore_mdns_adaptive_span());
    TEST_ASSERT_FALSE(MdnsAdaptive.ok);

    MdnsAdaptive.contention_init_args.w = NULL;
    MdnsAdaptive.contention_init_args.window_ms = 1000u;
    MdnsAdaptive.contention_init_args.frames_now = 0u;
    MdnsAdaptive.contention_init_args.now_ms = 0u;
    MdnsAdaptive.contention_init(protocore_mdns_adaptive_span());
    MdnsAdaptive.contention_sample_args.w = NULL;
    MdnsAdaptive.contention_sample_args.frames_now = 100u;
    MdnsAdaptive.contention_sample_args.now_ms = 100000u;
    MdnsAdaptive.contention_sample_args.out = &got;
    MdnsAdaptive.contention_sample(protocore_mdns_adaptive_span());
    TEST_ASSERT_FALSE(MdnsAdaptive.ok);
    MdnsAdaptive.contention_sample_args.w = &w;
    MdnsAdaptive.contention_sample_args.frames_now = 100u;
    MdnsAdaptive.contention_sample_args.now_ms = 100000u;
    MdnsAdaptive.contention_sample_args.out = NULL;
    MdnsAdaptive.contention_sample(protocore_mdns_adaptive_span());
    TEST_ASSERT_FALSE(MdnsAdaptive.ok);
}
