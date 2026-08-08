// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the one-GPIO presence facade (services/peripherals/rcwl0516): the debounce that swallows
// comparator chatter, the hold that bridges the RCWL-0516's retrigger gaps into one continuous
// presence span, the consume-once edge event, wrap-safety across a millis() rollover, and the
// degenerate zero-debounce / zero-hold configurations, on a synthetic clock. The binding is
// covered too: it samples the host pin table, which the test drives with pc_gpio_host_set().

#include "services/peripherals/rcwl0516/rcwl0516.h"
#include <unity.h>

static const uint32_t DEB = 50;
static const uint32_t HOLD = 2000;

static PresenceCore g_c;

void setUp(void)
{
    pc_presence_core_init(&g_c, DEB, HOLD, 1000);
}

void tearDown(void)
{
}

// Drive the pin at a fixed level from t0 to t1 inclusive, stepping by `step`.
static void drive(proto_bool level, uint32_t t0, uint32_t t1, uint32_t step)
{
    for (uint32_t t = t0; t <= t1; t += step)
    {
        pc_presence_core_update(&g_c, level, t);
    }
}

void test_starts_absent(void)
{
    TEST_ASSERT_FALSE(pc_presence_core_get(&g_c));
    TEST_ASSERT_FALSE(pc_presence_take_event(&g_c)); // no transition yet
}

void test_high_asserts_only_after_debounce(void)
{
    TEST_ASSERT_FALSE(pc_presence_core_update(&g_c, PROTO_TRUE, 1000)); // debounce starts
    TEST_ASSERT_FALSE(pc_presence_core_update(&g_c, PROTO_TRUE, 1049)); // 49ms - not yet believed
    TEST_ASSERT_TRUE(pc_presence_core_update(&g_c, PROTO_TRUE, 1050));  // 50ms - believed
    TEST_ASSERT_TRUE(pc_presence_core_get(&g_c));
}

// The failure this exists to prevent: comparator chatter around the threshold becoming a burst of
// presence events.
void test_chatter_shorter_than_debounce_never_asserts(void)
{
    uint32_t t = 1000;
    for (int i = 0; i < 20; i++)
    {
        pc_presence_core_update(&g_c, PROTO_TRUE, t);
        t += 20; // each level held only 20ms, under the 50ms debounce
        pc_presence_core_update(&g_c, PROTO_FALSE, t);
        t += 20;
    }
    TEST_ASSERT_FALSE(pc_presence_core_get(&g_c));
    TEST_ASSERT_FALSE(pc_presence_take_event(&g_c)); // and so no events at all
}

void test_hold_bridges_the_gap_after_pin_drops(void)
{
    drive(PROTO_TRUE, 1000, 1100, 10); // assert
    TEST_ASSERT_TRUE(pc_presence_core_get(&g_c));

    // Pin drops at t=2000. The believed level only follows after the debounce, so the last
    // believed-HIGH sample is at 2000 and presence must persist until 2000 + HOLD.
    pc_presence_core_update(&g_c, PROTO_FALSE, 2000);
    TEST_ASSERT_TRUE(pc_presence_core_get(&g_c));
    drive(PROTO_FALSE, 2050, 3999, 50);
    TEST_ASSERT_TRUE(pc_presence_core_get(&g_c)); // still held, just short of the deadline

    TEST_ASSERT_FALSE(pc_presence_core_update(&g_c, PROTO_FALSE, 4000)); // hold expires exactly here
    TEST_ASSERT_FALSE(pc_presence_core_get(&g_c));
}

// A person standing still retriggers the module intermittently; that must read as one continuous
// occupied span, not a flapping boolean.
void test_retrigger_gaps_stay_one_continuous_span(void)
{
    drive(PROTO_TRUE, 1000, 1100, 10);
    TEST_ASSERT_TRUE(pc_presence_core_get(&g_c));
    (void)pc_presence_take_event(&g_c); // consume the initial assert

    uint32_t t = 1100;
    for (int cycle = 0; cycle < 5; cycle++)
    {
        drive(PROTO_FALSE, t, t + 1500, 100); // a 1.5s gap - under the 2s hold
        t += 1500;
        drive(PROTO_TRUE, t, t + 200, 50); // retrigger
        t += 200;
        TEST_ASSERT_TRUE(pc_presence_core_get(&g_c));
    }
    // presence never dropped, so no further edges were reported
    TEST_ASSERT_FALSE(pc_presence_take_event(&g_c));
}

void test_event_fires_once_per_transition(void)
{
    drive(PROTO_TRUE, 1000, 1100, 10);
    TEST_ASSERT_TRUE(pc_presence_take_event(&g_c));  // rising edge
    TEST_ASSERT_FALSE(pc_presence_take_event(&g_c)); // consumed - not re-reported

    drive(PROTO_FALSE, 2000, 4000, 50);
    TEST_ASSERT_FALSE(pc_presence_core_get(&g_c));
    TEST_ASSERT_TRUE(pc_presence_take_event(&g_c));  // falling edge
    TEST_ASSERT_FALSE(pc_presence_take_event(&g_c)); // consumed
}

// Every elapsed test is an unsigned difference, so a millis() rollover mid-span must be invisible.
void test_wrap_safe_across_millis_rollover(void)
{
    pc_presence_core_init(&g_c, DEB, HOLD, 0xFFFFFF00u);

    pc_presence_core_update(&g_c, PROTO_TRUE, 0xFFFFFF00u);
    TEST_ASSERT_TRUE(pc_presence_core_update(&g_c, PROTO_TRUE, 0xFFFFFF50u)); // debounce elapsed
    TEST_ASSERT_TRUE(pc_presence_core_get(&g_c));

    // last believed-HIGH lands just before the wrap; the hold must expire 2000ms later in wrapped time
    pc_presence_core_update(&g_c, PROTO_FALSE, 0xFFFFFFF0u);
    TEST_ASSERT_TRUE(pc_presence_core_get(&g_c));
    TEST_ASSERT_TRUE(pc_presence_core_update(&g_c, PROTO_FALSE, 0x00000030u));  // wrapped, still inside hold
    TEST_ASSERT_FALSE(pc_presence_core_update(&g_c, PROTO_FALSE, 0x000007C0u)); // 0xFFFFFFF0 + 2000
    TEST_ASSERT_FALSE(pc_presence_core_get(&g_c));
}

void test_zero_debounce_and_zero_hold_are_pass_through(void)
{
    PresenceCore c;
    pc_presence_core_init(&c, 0, 0, 100);
    TEST_ASSERT_TRUE(pc_presence_core_update(&c, PROTO_TRUE, 100));   // believed immediately
    TEST_ASSERT_FALSE(pc_presence_core_update(&c, PROTO_FALSE, 101)); // and drops immediately
}

void test_repeated_and_static_now_is_harmless(void)
{
    // Polling faster than the clock ticks must not stall or double-count.
    for (int i = 0; i < 10; i++)
    {
        pc_presence_core_update(&g_c, PROTO_TRUE, 1000);
    }
    TEST_ASSERT_FALSE(pc_presence_core_get(&g_c)); // debounce never elapses at a frozen clock
    TEST_ASSERT_TRUE(pc_presence_core_update(&g_c, PROTO_TRUE, 1050));
}

void test_rcwl_defaults_and_null_guards(void)
{
    PresenceCore c;
    pc_rcwl0516_core_init(&c, 0);
    TEST_ASSERT_EQUAL_UINT32(PC_RCWL0516_DEBOUNCE_MS, c.debounce_ms);
    TEST_ASSERT_EQUAL_UINT32(PC_RCWL0516_HOLD_MS, c.hold_ms);
    TEST_ASSERT_FALSE(pc_presence_core_get(&c));

    pc_presence_core_init(NULL, 1, 1, 0); // must not fault
    TEST_ASSERT_FALSE(pc_presence_core_update(NULL, PROTO_TRUE, 0));
    TEST_ASSERT_FALSE(pc_presence_core_get(NULL));
    TEST_ASSERT_FALSE(pc_presence_take_event(NULL));
}

// The binding samples the real pin: begin() configures it as an input, and a poll after the pin has
// outlasted the debounce promotes presence. Levels come from the host pin table, so the composition
// the driver performs is what runs rather than a stub standing in for it.
void test_binding_samples_the_pin(void)
{
    const uint8_t PIN = 4;

    set_millis(1000);
    pc_gpio_host_set(PIN, 0);
    TEST_ASSERT_TRUE(pc_rcwl0516_begin((int)PIN));
    TEST_ASSERT_EQUAL_UINT8(PC_GPIO_IN, pc_gpio_host_mode(PIN));
    TEST_ASSERT_FALSE(pc_rcwl0516_present());

    // The pin goes high; the first poll only starts the steady-level timer.
    pc_gpio_host_set(PIN, 1);
    set_millis(1001);
    TEST_ASSERT_FALSE(pc_rcwl0516_poll());
    TEST_ASSERT_FALSE(pc_rcwl0516_present());

    // Past the debounce the believed level follows, and the edge event is consumed once.
    set_millis(1001 + PC_RCWL0516_DEBOUNCE_MS + 1);
    TEST_ASSERT_TRUE(pc_rcwl0516_poll());
    TEST_ASSERT_TRUE(pc_rcwl0516_present());
    TEST_ASSERT_FALSE(pc_rcwl0516_poll());
}

// Nothing is bound until begin() names a pin, so a poll before it reports absent.
void test_binding_refuses_before_begin(void)
{
    TEST_ASSERT_FALSE(pc_rcwl0516_poll());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_binding_refuses_before_begin); // first: begin() latches the pin for the run
    RUN_TEST(test_starts_absent);
    RUN_TEST(test_high_asserts_only_after_debounce);
    RUN_TEST(test_chatter_shorter_than_debounce_never_asserts);
    RUN_TEST(test_hold_bridges_the_gap_after_pin_drops);
    RUN_TEST(test_retrigger_gaps_stay_one_continuous_span);
    RUN_TEST(test_event_fires_once_per_transition);
    RUN_TEST(test_wrap_safe_across_millis_rollover);
    RUN_TEST(test_zero_debounce_and_zero_hold_are_pass_through);
    RUN_TEST(test_repeated_and_static_now_is_harmless);
    RUN_TEST(test_rcwl_defaults_and_null_guards);
    RUN_TEST(test_binding_samples_the_pin);
    return UNITY_END();
}
