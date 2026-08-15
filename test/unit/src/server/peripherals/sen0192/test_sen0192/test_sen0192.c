// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the SEN0192 motion-presence tracker (server/peripherals/sen0192/sen0192.h).
//
// The SEN0192 is a 10.525 GHz Doppler module with one digital OUT line and no protocol at all.
// There is no standard to quote and no published vector to reproduce, so every expectation here is
// a PROPERTY of the state machine the header specifies: presence asserts on an active-level
// sample, is held for hold_ms past the last active sample, clears once that window is exceeded,
// counts one event per clear-to-present edge, and honors the configured OUT polarity.
//
// test_presence_is_held_then_clears_at_the_window is the load-bearing case. It pins both sides of
// the hold boundary, which is what turns a stream of Doppler returns into one occupancy span
// instead of a flapping boolean, and what stops an empty room reading as occupied forever.

#include "server/peripherals/sen0192/sen0192.h"

#include <unity.h>

#define HOLD 2000u

void setUp(void)
{
}
void tearDown(void)
{
}

static Sen0192Motion g_m;

// A tracker that has sampled nothing reports absent and has seen no events, so a poll loop never
// publishes presence it has not measured.
void test_fresh_tracker_is_absent(void)
{
    protocore_sen0192_motion_init(&g_m, HOLD, PROTO_TRUE);
    TEST_ASSERT_FALSE(protocore_sen0192_motion_present(&g_m));
    TEST_ASSERT_EQUAL_UINT32(0, protocore_sen0192_motion_events(&g_m));
    TEST_ASSERT_EQUAL_UINT32(HOLD, g_m.hold_ms);

    // and a tick before any sample cannot assert or clear anything
    TEST_ASSERT_FALSE(protocore_sen0192_motion_tick(&g_m, 100000));
    TEST_ASSERT_FALSE(protocore_sen0192_motion_present(&g_m));
    TEST_ASSERT_EQUAL_UINT32(0, protocore_sen0192_motion_events(&g_m));
}

// The first active sample raises presence and reports the edge; further active samples while
// already present report no edge, so a caller publishes one event per arrival.
void test_first_active_sample_is_the_only_edge(void)
{
    protocore_sen0192_motion_init(&g_m, HOLD, PROTO_TRUE);
    TEST_ASSERT_TRUE(protocore_sen0192_motion_update(&g_m, PROTO_TRUE, 1000));
    TEST_ASSERT_TRUE(protocore_sen0192_motion_present(&g_m));
    TEST_ASSERT_EQUAL_UINT32(1, protocore_sen0192_motion_events(&g_m));

    TEST_ASSERT_FALSE(protocore_sen0192_motion_update(&g_m, PROTO_TRUE, 1100));
    TEST_ASSERT_FALSE(protocore_sen0192_motion_update(&g_m, PROTO_TRUE, 1200));
    TEST_ASSERT_TRUE(protocore_sen0192_motion_present(&g_m));
    TEST_ASSERT_EQUAL_UINT32(1, protocore_sen0192_motion_events(&g_m));
}

// Presence survives the whole hold window measured from the last active sample and clears once it
// is exceeded - at hold + 1, not at hold.
void test_presence_is_held_then_clears_at_the_window(void)
{
    protocore_sen0192_motion_init(&g_m, HOLD, PROTO_TRUE);
    TEST_ASSERT_TRUE(protocore_sen0192_motion_update(&g_m, PROTO_TRUE, 1000));

    TEST_ASSERT_TRUE(protocore_sen0192_motion_tick(&g_m, 1000 + HOLD - 1));
    TEST_ASSERT_TRUE(protocore_sen0192_motion_tick(&g_m, 1000 + HOLD));
    TEST_ASSERT_FALSE(protocore_sen0192_motion_tick(&g_m, 1000 + HOLD + 1));
    TEST_ASSERT_FALSE(protocore_sen0192_motion_present(&g_m));

    // an inactive sample ages presence out the same way a bare tick does
    protocore_sen0192_motion_init(&g_m, HOLD, PROTO_TRUE);
    TEST_ASSERT_TRUE(protocore_sen0192_motion_update(&g_m, PROTO_TRUE, 1000));
    TEST_ASSERT_FALSE(protocore_sen0192_motion_update(&g_m, PROTO_FALSE, 1000 + HOLD));
    TEST_ASSERT_TRUE(protocore_sen0192_motion_present(&g_m));
    TEST_ASSERT_FALSE(protocore_sen0192_motion_update(&g_m, PROTO_FALSE, 1000 + HOLD + 1));
    TEST_ASSERT_FALSE(protocore_sen0192_motion_present(&g_m));
}

// Each active sample restarts the window, so a gap shorter than the hold does not break presence
// and does not raise a second event.
void test_active_samples_extend_one_span(void)
{
    protocore_sen0192_motion_init(&g_m, HOLD, PROTO_TRUE);
    TEST_ASSERT_TRUE(protocore_sen0192_motion_update(&g_m, PROTO_TRUE, 0));
    for (uint32_t t = 1500; t <= 15000; t += 1500)
    {
        TEST_ASSERT_FALSE(protocore_sen0192_motion_update(&g_m, PROTO_TRUE, t));
        TEST_ASSERT_TRUE(protocore_sen0192_motion_present(&g_m));
    }
    TEST_ASSERT_EQUAL_UINT32(1, protocore_sen0192_motion_events(&g_m));
    // and the window still runs from the last of them
    TEST_ASSERT_TRUE(protocore_sen0192_motion_tick(&g_m, 15000 + HOLD));
    TEST_ASSERT_FALSE(protocore_sen0192_motion_tick(&g_m, 15000 + HOLD + 1));
}

// A second arrival after presence has cleared is a second event, so the count tracks arrivals
// rather than samples.
void test_events_count_arrivals(void)
{
    protocore_sen0192_motion_init(&g_m, HOLD, PROTO_TRUE);
    for (uint32_t k = 0; k < 4; k++)
    {
        const uint32_t base = k * 100000u;
        TEST_ASSERT_TRUE(protocore_sen0192_motion_update(&g_m, PROTO_TRUE, base));
        TEST_ASSERT_EQUAL_UINT32(k + 1, protocore_sen0192_motion_events(&g_m));
        TEST_ASSERT_FALSE(protocore_sen0192_motion_tick(&g_m, base + HOLD + 1));
    }
}

// The OUT polarity is configurable, so with active_high false a LOW sample is the motion one and a
// HIGH sample is the idle one. A tracker wired the wrong way round reports the room occupied
// whenever nothing is moving.
void test_polarity_selects_the_active_level(void)
{
    protocore_sen0192_motion_init(&g_m, HOLD, PROTO_FALSE);
    TEST_ASSERT_FALSE(protocore_sen0192_motion_update(&g_m, PROTO_TRUE, 0)); // HIGH is idle here
    TEST_ASSERT_FALSE(protocore_sen0192_motion_present(&g_m));
    TEST_ASSERT_EQUAL_UINT32(0, protocore_sen0192_motion_events(&g_m));

    TEST_ASSERT_TRUE(protocore_sen0192_motion_update(&g_m, PROTO_FALSE, 100)); // LOW is motion
    TEST_ASSERT_TRUE(protocore_sen0192_motion_present(&g_m));
    TEST_ASSERT_EQUAL_UINT32(1, protocore_sen0192_motion_events(&g_m));

    TEST_ASSERT_FALSE(protocore_sen0192_motion_update(&g_m, PROTO_TRUE, 100 + HOLD + 1));
    TEST_ASSERT_FALSE(protocore_sen0192_motion_present(&g_m));
}

// The age is time since the last active sample, and it is 0 rather than a huge number before any
// sample has been taken - a caller reading it as "seconds since motion" must not be told the
// sensor has been quiet since the epoch when it has simply never been read.
void test_active_age(void)
{
    protocore_sen0192_motion_init(&g_m, HOLD, PROTO_TRUE);
    TEST_ASSERT_EQUAL_UINT32(0, protocore_sen0192_motion_active_age_ms(&g_m, 500000));

    protocore_sen0192_motion_update(&g_m, PROTO_TRUE, 1000);
    TEST_ASSERT_EQUAL_UINT32(0, protocore_sen0192_motion_active_age_ms(&g_m, 1000));
    TEST_ASSERT_EQUAL_UINT32(750, protocore_sen0192_motion_active_age_ms(&g_m, 1750));

    // an inactive sample does not reset it
    protocore_sen0192_motion_update(&g_m, PROTO_FALSE, 1750);
    TEST_ASSERT_EQUAL_UINT32(900, protocore_sen0192_motion_active_age_ms(&g_m, 1900));
}

// Every elapsed-time test is an unsigned difference, so a millis() rollover between the last
// active sample and the tick still yields the true interval instead of clearing presence 49 days
// early or holding it for 49 days.
void test_timing_survives_the_millis_rollover(void)
{
    const uint32_t t0 = 0xFFFFFF00u; // 256 ms of the 32-bit range left
    protocore_sen0192_motion_init(&g_m, HOLD, PROTO_TRUE);
    TEST_ASSERT_TRUE(protocore_sen0192_motion_update(&g_m, PROTO_TRUE, t0));

    TEST_ASSERT_EQUAL_UINT32(HOLD, protocore_sen0192_motion_active_age_ms(&g_m, t0 + HOLD));
    TEST_ASSERT_TRUE(protocore_sen0192_motion_tick(&g_m, t0 + HOLD)); // 0x000006D0, already wrapped
    TEST_ASSERT_FALSE(protocore_sen0192_motion_tick(&g_m, t0 + HOLD + 1));
}

// A zero hold makes presence last exactly as long as the samples do: the next tick at any later
// instant clears it.
void test_zero_hold_clears_on_the_next_tick(void)
{
    protocore_sen0192_motion_init(&g_m, 0, PROTO_TRUE);
    TEST_ASSERT_TRUE(protocore_sen0192_motion_update(&g_m, PROTO_TRUE, 1000));
    TEST_ASSERT_TRUE(protocore_sen0192_motion_tick(&g_m, 1000)); // same instant, nothing elapsed
    TEST_ASSERT_FALSE(protocore_sen0192_motion_tick(&g_m, 1001));
}

// A repeated timestamp is harmless: it neither clears presence nor counts a second arrival.
void test_repeated_timestamps_are_harmless(void)
{
    protocore_sen0192_motion_init(&g_m, HOLD, PROTO_TRUE);
    TEST_ASSERT_TRUE(protocore_sen0192_motion_update(&g_m, PROTO_TRUE, 5000));
    for (int i = 0; i < 5; i++)
    {
        TEST_ASSERT_FALSE(protocore_sen0192_motion_update(&g_m, PROTO_TRUE, 5000));
        TEST_ASSERT_TRUE(protocore_sen0192_motion_tick(&g_m, 5000));
    }
    TEST_ASSERT_TRUE(protocore_sen0192_motion_present(&g_m));
    TEST_ASSERT_EQUAL_UINT32(1, protocore_sen0192_motion_events(&g_m));
}
