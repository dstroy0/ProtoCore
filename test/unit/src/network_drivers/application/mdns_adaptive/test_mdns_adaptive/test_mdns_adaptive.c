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
    TEST_ASSERT_EQUAL_UINT32(60000u, protocore_mdns_refresh_interval(120));
    TEST_ASSERT_EQUAL_UINT32(2250000u, protocore_mdns_refresh_interval(4500));
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_mdns_refresh_interval(0));
    TEST_ASSERT_EQUAL_UINT32(500u, protocore_mdns_refresh_interval(1));
    TEST_ASSERT_EQUAL_UINT32(4294967000u, protocore_mdns_refresh_interval(8589934));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, protocore_mdns_refresh_interval(8589935));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, protocore_mdns_refresh_interval(0xFFFFFFFFu));
}

// The floor is the nominal cadence and the ceiling can never sit below it, so a misconfigured pair
// cannot produce an interval that skips the refresher entirely. A zero threshold would make every
// sample count as contention, so it is raised to one.
void test_init_cannot_produce_a_ceiling_below_the_floor(void)
{
    MdnsBeacon b;
    protocore_mdns_beacon_init(&b, 60000u, 240000u, 40u);
    TEST_ASSERT_EQUAL_UINT32(60000u, b.base_ms);
    TEST_ASSERT_EQUAL_UINT32(240000u, b.max_ms);
    TEST_ASSERT_EQUAL_UINT32(60000u, b.cur_ms); // starts at the nominal cadence
    TEST_ASSERT_EQUAL_UINT16(40u, b.hi_thresh);

    protocore_mdns_beacon_init(&b, 60000u, 1000u, 0u);
    TEST_ASSERT_EQUAL_UINT32(60000u, b.max_ms); // the ceiling is raised to the floor
    TEST_ASSERT_EQUAL_UINT32(60000u, b.cur_ms);
    TEST_ASSERT_EQUAL_UINT16(1u, b.hi_thresh);
}

// Contention at or above the threshold doubles the interval, and the doubling stops at the ceiling
// rather than running past it or wrapping.
void test_contention_backs_the_interval_off_to_the_ceiling(void)
{
    MdnsBeacon b;
    protocore_mdns_beacon_init(&b, 1000u, 8000u, 40u);

    TEST_ASSERT_EQUAL_UINT32(2000u, protocore_mdns_beacon_adapt(&b, 40u)); // at the threshold
    TEST_ASSERT_EQUAL_UINT32(4000u, protocore_mdns_beacon_adapt(&b, 100u));
    TEST_ASSERT_EQUAL_UINT32(8000u, protocore_mdns_beacon_adapt(&b, 100u));
    TEST_ASSERT_EQUAL_UINT32(8000u, protocore_mdns_beacon_adapt(&b, 100u)); // held at the ceiling
    TEST_ASSERT_EQUAL_UINT32(8000u, protocore_mdns_beacon_adapt(&b, 0xFFFFu));

    // A ceiling that is not twice the floor is still exact: the double is clamped, not rounded up.
    protocore_mdns_beacon_init(&b, 1000u, 1500u, 1u);
    TEST_ASSERT_EQUAL_UINT32(1500u, protocore_mdns_beacon_adapt(&b, 1u));

    // A base near the top of the range doubles into an overflow, which lands on the ceiling too.
    protocore_mdns_beacon_init(&b, 0x90000000u, 0xF0000000u, 1u);
    TEST_ASSERT_EQUAL_UINT32(0xF0000000u, protocore_mdns_beacon_adapt(&b, 1u));
}

// Quiet air halves the interval back toward the nominal cadence and stops there: the recovery never
// announces faster than the base the caller asked for.
void test_quiet_air_recovers_the_interval_to_the_floor(void)
{
    MdnsBeacon b;
    protocore_mdns_beacon_init(&b, 1000u, 8000u, 40u);
    (void)protocore_mdns_beacon_adapt(&b, 100u);
    (void)protocore_mdns_beacon_adapt(&b, 100u);
    (void)protocore_mdns_beacon_adapt(&b, 100u);
    TEST_ASSERT_EQUAL_UINT32(8000u, b.cur_ms);

    TEST_ASSERT_EQUAL_UINT32(4000u, protocore_mdns_beacon_adapt(&b, 0u));
    TEST_ASSERT_EQUAL_UINT32(2000u, protocore_mdns_beacon_adapt(&b, 0u));
    TEST_ASSERT_EQUAL_UINT32(1000u, protocore_mdns_beacon_adapt(&b, 0u));
    TEST_ASSERT_EQUAL_UINT32(1000u, protocore_mdns_beacon_adapt(&b, 0u)); // held at the floor

    // A halving that would land below the floor is clamped to it, not rounded down.
    protocore_mdns_beacon_init(&b, 1500u, 8000u, 40u);
    (void)protocore_mdns_beacon_adapt(&b, 100u); // 3000
    TEST_ASSERT_EQUAL_UINT32(1500u, protocore_mdns_beacon_adapt(&b, 0u));
}

// Between silence and the threshold the interval is left where it is, so a moderately busy channel
// neither backs off nor recovers.
void test_moderate_contention_holds_the_interval(void)
{
    MdnsBeacon b;
    protocore_mdns_beacon_init(&b, 1000u, 8000u, 40u);
    (void)protocore_mdns_beacon_adapt(&b, 100u); // 2000
    for (uint16_t c = 1; c < 40u; c++)
    {
        TEST_ASSERT_EQUAL_UINT32(2000u, protocore_mdns_beacon_adapt(&b, c));
    }
}

// The due test is modular subtraction, so it survives the millis() rollover: an announce scheduled
// across the wrap fires at the same elapsed time it would anywhere else.
void test_due_is_wrap_safe_across_the_millis_rollover(void)
{
    MdnsBeacon b;
    protocore_mdns_beacon_init(&b, 1000u, 8000u, 40u);

    TEST_ASSERT_FALSE(protocore_mdns_beacon_due(&b, 5000u, 5999u));
    TEST_ASSERT_TRUE(protocore_mdns_beacon_due(&b, 5000u, 6000u)); // exactly the interval
    TEST_ASSERT_TRUE(protocore_mdns_beacon_due(&b, 5000u, 60000u));

    // The last announce sits 256 ms before the wrap, so the 1000 ms interval expires 744 ms after
    // the counter restarts: 0xFFFFFFFF - 0xFFFFFF00 + 1 = 256, and 1000 - 256 = 744 = 0x2E8.
    const uint32_t last = 0xFFFFFF00u;
    TEST_ASSERT_FALSE(protocore_mdns_beacon_due(&b, last, 0x000002E7u)); //  999 ms elapsed
    TEST_ASSERT_TRUE(protocore_mdns_beacon_due(&b, last, 0x000002E8u));  // 1000 ms elapsed
}

// A sleep that would carry the record past its refresh point announces first, so the record
// survives the sleep instead of lapsing with the radio off.
void test_a_sleep_that_would_lapse_the_record_announces_first(void)
{
    MdnsBeacon b;
    protocore_mdns_beacon_init(&b, 1000u, 8000u, 40u);

    // 200 ms since the last announce: a 799 ms sleep lands short of the interval, an 800 ms sleep
    // reaches it exactly.
    TEST_ASSERT_FALSE(protocore_mdns_beacon_presleep_due(&b, 1000u, 1200u, 799u));
    TEST_ASSERT_TRUE(protocore_mdns_beacon_presleep_due(&b, 1000u, 1200u, 800u));
    TEST_ASSERT_TRUE(protocore_mdns_beacon_presleep_due(&b, 1000u, 1200u, 100000u));

    // No sleep at all is the plain due test.
    TEST_ASSERT_FALSE(protocore_mdns_beacon_presleep_due(&b, 1000u, 1200u, 0u));
    TEST_ASSERT_TRUE(protocore_mdns_beacon_presleep_due(&b, 1000u, 2000u, 0u));

    // The elapsed-plus-sleep sum is taken wider than the counter, so a sleep near the full range
    // cannot wrap back under the interval and skip the announce.
    TEST_ASSERT_TRUE(protocore_mdns_beacon_presleep_due(&b, 1000u, 1200u, 0xFFFFFFFFu));
}

// A window reports the frames counted in it and starts the next one; before the window closes there
// is nothing to report.
void test_the_sampling_window_reports_the_frames_it_counted(void)
{
    MdnsContentionWindow w;
    uint16_t got = 0xAAAA;

    protocore_mdns_contention_init(&w, 1000u, 500u, 10000u);
    TEST_ASSERT_EQUAL_UINT32(1000u, w.window_ms);

    TEST_ASSERT_FALSE(protocore_mdns_contention_sample(&w, 700u, 10999u, &got));
    TEST_ASSERT_EQUAL_UINT16(0xAAAA, got); // untouched while the window is open

    // 700 - 500 = 200 frames in the window that just closed.
    TEST_ASSERT_TRUE(protocore_mdns_contention_sample(&w, 700u, 11000u, &got));
    TEST_ASSERT_EQUAL_UINT16(200u, got);

    // The next window is anchored on what was just reported, so counts do not accumulate.
    TEST_ASSERT_TRUE(protocore_mdns_contention_sample(&w, 730u, 12000u, &got));
    TEST_ASSERT_EQUAL_UINT16(30u, got);

    // More frames than the uint16 the adapt step takes: saturated, never truncated to the low bits.
    TEST_ASSERT_TRUE(protocore_mdns_contention_sample(&w, 730u + 0x10000u, 13000u, &got));
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, got);

    // A zero window length falls back to one second.
    protocore_mdns_contention_init(&w, 0u, 0u, 0u);
    TEST_ASSERT_EQUAL_UINT32(1000u, w.window_ms);
}

// Both the frame counter and the clock wrap, and the modular difference reports the true count
// across either one.
void test_the_sampling_window_is_wrap_safe(void)
{
    MdnsContentionWindow w;
    uint16_t got = 0;

    // The frame counter wraps inside the window: 0x20 - 0xFFFFFFF0 = 0x30 frames.
    protocore_mdns_contention_init(&w, 1000u, 0xFFFFFFF0u, 0u);
    TEST_ASSERT_TRUE(protocore_mdns_contention_sample(&w, 0x20u, 1000u, &got));
    TEST_ASSERT_EQUAL_UINT16(0x30u, got);

    // The clock wraps inside the window: the sample was taken 256 ms before the wrap, so the 1000 ms
    // window closes 744 ms after the counter restarts (1000 - 256 = 744 = 0x2E8).
    protocore_mdns_contention_init(&w, 1000u, 0u, 0xFFFFFF00u);
    TEST_ASSERT_FALSE(protocore_mdns_contention_sample(&w, 5u, 0x000002E7u, &got)); //  999 ms
    TEST_ASSERT_TRUE(protocore_mdns_contention_sample(&w, 5u, 0x000002E8u, &got));  // 1000 ms
    TEST_ASSERT_EQUAL_UINT16(5u, got);
}

// A null handle is reported rather than dereferenced, on every entry point.
void test_null_handles_are_refused(void)
{
    uint16_t got = 0;
    MdnsContentionWindow w;
    protocore_mdns_contention_init(&w, 1000u, 0u, 0u);

    protocore_mdns_beacon_init(NULL, 1000u, 8000u, 40u);
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_mdns_beacon_adapt(NULL, 100u));
    TEST_ASSERT_FALSE(protocore_mdns_beacon_due(NULL, 0u, 100000u));
    TEST_ASSERT_FALSE(protocore_mdns_beacon_presleep_due(NULL, 0u, 0u, 100000u));

    protocore_mdns_contention_init(NULL, 1000u, 0u, 0u);
    TEST_ASSERT_FALSE(protocore_mdns_contention_sample(NULL, 100u, 100000u, &got));
    TEST_ASSERT_FALSE(protocore_mdns_contention_sample(&w, 100u, 100000u, NULL));
}
