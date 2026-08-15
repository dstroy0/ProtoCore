// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the one-GPIO presence facade (server/peripherals/rcwl0516/rcwl0516.h).
//
// The RCWL-0516 carries no protocol: one OUT pin that latches HIGH on a Doppler return. There is no
// standard to quote and no published vector to reproduce, so every expectation here is a PROPERTY
// of the state machine the header specifies - fail-safe start, a level believed only after it
// outlasts the debounce, presence extended by exactly the hold past the last believed-HIGH sample,
// one event per edge, and unsigned time arithmetic that survives a millis() rollover.
//
// test_presence_clears_exactly_one_hold_after_the_last_high is the load-bearing case: it pins the
// boundary at both sides. Presence that decays early turns one person standing still into a stream
// of arrive/leave events, and presence that never decays reports an empty room as occupied forever.

#include "server/peripherals/rcwl0516/rcwl0516.h"

#include <unity.h>

#define DEBOUNCE 50u
#define HOLD 2000u

void setUp(void)
{
}
void tearDown(void)
{
}

static PresenceCore g_core;

static proto_bool feed(proto_bool level, uint32_t now)
{
    return protocore_presence_core_update(&g_core, level, now);
}

// A core that has sampled nothing reports absent, and treats the pin as idle rather than assuming
// whatever it happens to be reading.
void test_fresh_core_is_absent(void)
{
    protocore_presence_core_init(&g_core, DEBOUNCE, HOLD, 1000);
    TEST_ASSERT_FALSE(protocore_presence_core_get(&g_core));
    TEST_ASSERT_FALSE(protocore_presence_take_event(&g_core));
    TEST_ASSERT_EQUAL_UINT32(DEBOUNCE, g_core.debounce_ms);
    TEST_ASSERT_EQUAL_UINT32(HOLD, g_core.hold_ms);
    TEST_ASSERT_EQUAL_UINT8(0, g_core.raw);
    TEST_ASSERT_EQUAL_UINT8(0, g_core.stable);
}

// A HIGH pin is not believed until it has held for the debounce, and then it is - at that sample,
// not one later.
void test_a_level_is_believed_only_after_the_debounce(void)
{
    protocore_presence_core_init(&g_core, DEBOUNCE, HOLD, 0);
    TEST_ASSERT_FALSE(feed(PROTO_TRUE, 0));
    TEST_ASSERT_FALSE(feed(PROTO_TRUE, 1));
    TEST_ASSERT_FALSE(feed(PROTO_TRUE, DEBOUNCE - 1));
    TEST_ASSERT_TRUE(feed(PROTO_TRUE, DEBOUNCE));
    TEST_ASSERT_TRUE(protocore_presence_core_get(&g_core));
}

// Comparator chatter around the detection threshold never holds one level long enough to be
// believed, so it produces no presence at all rather than a burst of events.
void test_chatter_below_the_debounce_is_swallowed(void)
{
    protocore_presence_core_init(&g_core, DEBOUNCE, HOLD, 0);
    const uint32_t step = DEBOUNCE - 10u; // flips before any level can be believed
    for (uint32_t t = 0; t < 10u * DEBOUNCE; t += step)
    {
        TEST_ASSERT_FALSE(feed(((t / step) % 2u) == 0u ? PROTO_TRUE : PROTO_FALSE, t));
    }
    TEST_ASSERT_FALSE(protocore_presence_core_get(&g_core));
    TEST_ASSERT_FALSE(protocore_presence_take_event(&g_core));
}

// The module drops OUT between retriggers. Presence must not follow it down, so a gap shorter than
// the hold stays one continuous occupied span.
void test_hold_bridges_the_retrigger_gap(void)
{
    protocore_presence_core_init(&g_core, DEBOUNCE, HOLD, 0);
    TEST_ASSERT_FALSE(feed(PROTO_TRUE, 0));
    TEST_ASSERT_TRUE(feed(PROTO_TRUE, DEBOUNCE)); // present at t = 50
    TEST_ASSERT_TRUE(protocore_presence_take_event(&g_core));

    // OUT drops, and is sampled LOW across a gap well inside the hold
    for (uint32_t t = 100; t <= 1500; t += 100)
    {
        TEST_ASSERT_TRUE_MESSAGE(feed(PROTO_FALSE, t), "presence dropped inside the hold");
    }
    // it retriggers, and the whole span was one presence: no edge to publish
    TEST_ASSERT_TRUE(feed(PROTO_TRUE, 1600));
    TEST_ASSERT_TRUE(feed(PROTO_TRUE, 1700));
    TEST_ASSERT_FALSE(protocore_presence_take_event(&g_core));
}

// The hold is measured from the last believed-HIGH sample. Feeding LOW from t = 100, the level is
// believed LOW at t = 150 (one debounce later) but t = 100 was the last sample at which the
// believed level was still HIGH, so presence must survive to t = 2099 and be gone at t = 2100.
void test_presence_clears_exactly_one_hold_after_the_last_high(void)
{
    protocore_presence_core_init(&g_core, DEBOUNCE, HOLD, 0);
    TEST_ASSERT_FALSE(feed(PROTO_TRUE, 0));
    TEST_ASSERT_TRUE(feed(PROTO_TRUE, DEBOUNCE));
    TEST_ASSERT_TRUE(protocore_presence_take_event(&g_core));

    TEST_ASSERT_TRUE(feed(PROTO_FALSE, 100));
    TEST_ASSERT_EQUAL_UINT32(100, g_core.last_high_ms);
    TEST_ASSERT_TRUE(feed(PROTO_FALSE, 150)); // the LOW is believed here, presence held
    TEST_ASSERT_EQUAL_UINT8(0, g_core.stable);

    TEST_ASSERT_TRUE(feed(PROTO_FALSE, 100 + HOLD - 1));
    TEST_ASSERT_FALSE(protocore_presence_take_event(&g_core)); // no edge yet
    TEST_ASSERT_FALSE(feed(PROTO_FALSE, 100 + HOLD));
    TEST_ASSERT_FALSE(protocore_presence_core_get(&g_core));
    TEST_ASSERT_TRUE(protocore_presence_take_event(&g_core));
}

// One event per transition, consumed once: a caller publishes an edge rather than re-publishing a
// level every poll.
void test_event_is_taken_once_per_edge(void)
{
    protocore_presence_core_init(&g_core, DEBOUNCE, HOLD, 0);
    TEST_ASSERT_FALSE(protocore_presence_take_event(&g_core));

    TEST_ASSERT_FALSE(feed(PROTO_TRUE, 0));
    TEST_ASSERT_TRUE(feed(PROTO_TRUE, DEBOUNCE));
    TEST_ASSERT_TRUE(protocore_presence_take_event(&g_core));
    TEST_ASSERT_FALSE(protocore_presence_take_event(&g_core));

    // polling on while present raises no new event
    TEST_ASSERT_TRUE(feed(PROTO_TRUE, 200));
    TEST_ASSERT_TRUE(feed(PROTO_TRUE, 300));
    TEST_ASSERT_FALSE(protocore_presence_take_event(&g_core));

    // the falling edge does, once: t = 400 is the last believed-HIGH sample, so presence ends a
    // hold later at t = 2400.
    TEST_ASSERT_TRUE(feed(PROTO_FALSE, 400));
    TEST_ASSERT_TRUE(feed(PROTO_FALSE, 450));
    TEST_ASSERT_FALSE(feed(PROTO_FALSE, 400 + HOLD));
    TEST_ASSERT_TRUE(protocore_presence_take_event(&g_core));
    TEST_ASSERT_FALSE(protocore_presence_take_event(&g_core));
}

// A zero debounce believes every sample immediately, and a zero hold makes presence follow the
// believed level exactly - the degenerate configuration the header documents.
void test_zero_debounce_and_zero_hold_follow_the_level(void)
{
    protocore_presence_core_init(&g_core, 0, 0, 0);
    TEST_ASSERT_TRUE(feed(PROTO_TRUE, 0));
    TEST_ASSERT_TRUE(protocore_presence_core_get(&g_core));
    TEST_ASSERT_FALSE(feed(PROTO_FALSE, 1));
    TEST_ASSERT_FALSE(protocore_presence_core_get(&g_core));
    TEST_ASSERT_TRUE(feed(PROTO_TRUE, 2));
    TEST_ASSERT_FALSE(feed(PROTO_FALSE, 3));
}

// A zero hold with a real debounce still debounces: the two limits are independent.
void test_zero_hold_still_debounces(void)
{
    protocore_presence_core_init(&g_core, DEBOUNCE, 0, 0);
    TEST_ASSERT_FALSE(feed(PROTO_TRUE, 0));
    TEST_ASSERT_FALSE(feed(PROTO_TRUE, DEBOUNCE - 1));
    TEST_ASSERT_TRUE(feed(PROTO_TRUE, DEBOUNCE));
    // the LOW is not believed until it too has outlasted the debounce; then presence goes at once
    TEST_ASSERT_TRUE(feed(PROTO_FALSE, 100));
    TEST_ASSERT_FALSE(feed(PROTO_FALSE, 150));
}

// Every elapsed-time test is an unsigned difference, so a millis() rollover between two samples
// still yields the true interval. Started 16 ms before the wrap, the debounce completes 34 ms after
// it and the hold expires 2000 ms after the last believed-HIGH sample, both across the seam.
void test_timing_survives_the_millis_rollover(void)
{
    const uint32_t t0 = 0xFFFFFFF0u; // 16 ms of the 32-bit range left
    protocore_presence_core_init(&g_core, DEBOUNCE, HOLD, t0);

    TEST_ASSERT_FALSE(feed(PROTO_TRUE, t0));
    TEST_ASSERT_FALSE(feed(PROTO_TRUE, t0 + DEBOUNCE - 1)); // 0x00000021, already wrapped
    TEST_ASSERT_TRUE(feed(PROTO_TRUE, t0 + DEBOUNCE));      // 0x00000022

    const uint32_t high = t0 + DEBOUNCE;
    TEST_ASSERT_TRUE(feed(PROTO_FALSE, high));
    TEST_ASSERT_TRUE(feed(PROTO_FALSE, high + HOLD - 1));
    TEST_ASSERT_FALSE(feed(PROTO_FALSE, high + HOLD));
}

// The RCWL-0516 convenience initializer supplies the module's own two constants: a debounce long
// enough to swallow comparator chatter, and a hold at least as long as its ~2 s retrigger window.
void test_rcwl0516_defaults(void)
{
    protocore_presence_core_init(&g_core, 1, 1, 0); // deliberately wrong, then overwritten
    protocore_rcwl0516_core_init(&g_core, 12345);
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_RCWL0516_DEBOUNCE_MS, g_core.debounce_ms);
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_RCWL0516_HOLD_MS, g_core.hold_ms);
    TEST_ASSERT_EQUAL_UINT32(2000, PROTOCORE_RCWL0516_HOLD_MS);
    TEST_ASSERT_EQUAL_UINT32(50, PROTOCORE_RCWL0516_DEBOUNCE_MS);
    TEST_ASSERT_FALSE(protocore_presence_core_get(&g_core)); // still fail-safe
}

// A repeated or non-monotonic timestamp is harmless: it neither asserts presence nor loses it.
void test_repeated_timestamps_are_harmless(void)
{
    protocore_presence_core_init(&g_core, DEBOUNCE, HOLD, 0);
    for (int i = 0; i < 5; i++)
    {
        TEST_ASSERT_FALSE(feed(PROTO_TRUE, 0));
    }
    TEST_ASSERT_TRUE(feed(PROTO_TRUE, DEBOUNCE));
    for (int i = 0; i < 5; i++)
    {
        TEST_ASSERT_TRUE(feed(PROTO_TRUE, DEBOUNCE));
    }
    TEST_ASSERT_TRUE(protocore_presence_core_get(&g_core));
}

// A null core is reported, not written through.
void test_null_core_is_refused(void)
{
    protocore_presence_core_init(NULL, DEBOUNCE, HOLD, 0); // must not fault
    TEST_ASSERT_FALSE(protocore_presence_core_update(NULL, PROTO_TRUE, 0));
    TEST_ASSERT_FALSE(protocore_presence_core_get(NULL));
    TEST_ASSERT_FALSE(protocore_presence_take_event(NULL));
}
