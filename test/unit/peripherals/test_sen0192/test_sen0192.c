// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the SEN0192 microwave motion sensor's pure presence state machine
// (services/peripherals/sen0192): presence asserts on an active sample, holds for the configured window after the
// last active sample, clears after it, counts clear->present edges, and honors OUT polarity. Host tests.

#include "services/peripherals/sen0192/sen0192.h"
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_asserts_on_active_and_counts_edge()
{
    Sen0192Motion m;
    pc_sen0192_motion_init(&m, 2000, PROTO_TRUE);
    TEST_ASSERT_FALSE(pc_sen0192_motion_present(&m));

    // An inactive (low) sample keeps it clear.
    TEST_ASSERT_FALSE(pc_sen0192_motion_update(&m, PROTO_FALSE, 500));
    TEST_ASSERT_FALSE(pc_sen0192_motion_present(&m));

    // First active (high) sample -> presence starts (returns the edge), one motion event.
    TEST_ASSERT_TRUE(pc_sen0192_motion_update(&m, PROTO_TRUE, 1000));
    TEST_ASSERT_TRUE(pc_sen0192_motion_present(&m));
    TEST_ASSERT_EQUAL_UINT32(1, pc_sen0192_motion_events(&m));

    // A second active sample while already present is not a new edge.
    TEST_ASSERT_FALSE(pc_sen0192_motion_update(&m, PROTO_TRUE, 1500));
    TEST_ASSERT_EQUAL_UINT32(1, pc_sen0192_motion_events(&m));
}

void test_holds_then_clears_after_window()
{
    Sen0192Motion m;
    pc_sen0192_motion_init(&m, 2000, PROTO_TRUE);
    pc_sen0192_motion_update(&m, PROTO_TRUE, 1000); // present, last_active=1000

    // Within the hold window (<= 2000 ms since last active): still present.
    TEST_ASSERT_TRUE(pc_sen0192_motion_tick(&m, 2999)); // 1999 ms later
    TEST_ASSERT_TRUE(pc_sen0192_motion_present(&m));
    TEST_ASSERT_TRUE(pc_sen0192_motion_tick(&m, 3000)); // exactly 2000 ms later (still within)
    TEST_ASSERT_TRUE(pc_sen0192_motion_present(&m));

    // Past the hold window: presence clears.
    TEST_ASSERT_FALSE(pc_sen0192_motion_tick(&m, 3001)); // 2001 ms later
    TEST_ASSERT_FALSE(pc_sen0192_motion_present(&m));
}

void test_reasserts_as_new_event()
{
    Sen0192Motion m;
    pc_sen0192_motion_init(&m, 1000, PROTO_TRUE);
    TEST_ASSERT_TRUE(pc_sen0192_motion_update(&m, PROTO_TRUE, 100)); // event 1
    pc_sen0192_motion_tick(&m, 1200);                                // clears (1100 > 1000)
    TEST_ASSERT_FALSE(pc_sen0192_motion_present(&m));
    TEST_ASSERT_TRUE(pc_sen0192_motion_update(&m, PROTO_TRUE, 2000)); // event 2 (new edge)
    TEST_ASSERT_EQUAL_UINT32(2, pc_sen0192_motion_events(&m));
}

void test_active_low_polarity()
{
    Sen0192Motion m;
    pc_sen0192_motion_init(&m, 1000, PROTO_FALSE);                    // OUT reads LOW on motion
    TEST_ASSERT_FALSE(pc_sen0192_motion_update(&m, PROTO_TRUE, 100)); // HIGH is inactive here
    TEST_ASSERT_FALSE(pc_sen0192_motion_present(&m));
    TEST_ASSERT_TRUE(pc_sen0192_motion_update(&m, PROTO_FALSE, 200)); // LOW is active
    TEST_ASSERT_TRUE(pc_sen0192_motion_present(&m));
    TEST_ASSERT_EQUAL_UINT32(1, pc_sen0192_motion_events(&m));
}

void test_active_age()
{
    Sen0192Motion m;
    pc_sen0192_motion_init(&m, 5000, PROTO_TRUE);
    TEST_ASSERT_EQUAL_UINT32(0, pc_sen0192_motion_active_age_ms(&m, 1234)); // no sample yet
    pc_sen0192_motion_update(&m, PROTO_TRUE, 1000);
    TEST_ASSERT_EQUAL_UINT32(750, pc_sen0192_motion_active_age_ms(&m, 1750));
}

void test_tick_present_unseeded_holds()
{
    // present && !seeded cannot occur through the public update()/tick() sequence (present is only ever
    // set true in the same branch that sets seeded true), but Sen0192Motion is a flat, unencapsulated
    // struct, so exercise the defensive combination directly: with no seeded timestamp to age against,
    // tick() must leave presence asserted rather than guess an age.
    Sen0192Motion m;
    pc_sen0192_motion_init(&m, 1000, PROTO_TRUE);
    m.present = PROTO_TRUE;
    m.seeded = PROTO_FALSE;
    TEST_ASSERT_TRUE(pc_sen0192_motion_tick(&m, 999999));
    TEST_ASSERT_TRUE(pc_sen0192_motion_present(&m));
}

// The binding samples the real pin: begin() configures PC_SEN0192_PIN as an input, a poll at the
// active level reports the clear -> present edge once, and presence ages out past the hold. Levels
// come from the host pin table, so what runs is the driver's own composition.
void test_binding_samples_the_pin(void)
{
    const uint8_t PIN = (uint8_t)PC_SEN0192_PIN;
    const uint8_t ACTIVE = (PC_SEN0192_ACTIVE_HIGH != 0) ? 1u : 0u;

    set_millis(5000);
    pc_gpio_host_set(PIN, ACTIVE ? 0u : 1u);
    TEST_ASSERT_TRUE(pc_sen0192_begin());
    TEST_ASSERT_EQUAL_UINT8(PC_GPIO_IN, pc_gpio_host_mode(PIN));
    TEST_ASSERT_FALSE(pc_sen0192_present());

    // Active level: the first poll is the clear -> present edge, the second is not.
    pc_gpio_host_set(PIN, ACTIVE);
    TEST_ASSERT_TRUE(pc_sen0192_poll());
    TEST_ASSERT_TRUE(pc_sen0192_present());
    TEST_ASSERT_FALSE(pc_sen0192_poll());
    TEST_ASSERT_EQUAL_UINT32(1, pc_sen0192_motion_count());

    // Inactive, and past the hold presence ages out.
    pc_gpio_host_set(PIN, ACTIVE ? 0u : 1u);
    set_millis(5000 + PC_SEN0192_HOLD_MS + 1);
    TEST_ASSERT_FALSE(pc_sen0192_poll());
    TEST_ASSERT_FALSE(pc_sen0192_present());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_asserts_on_active_and_counts_edge);
    RUN_TEST(test_holds_then_clears_after_window);
    RUN_TEST(test_reasserts_as_new_event);
    RUN_TEST(test_active_low_polarity);
    RUN_TEST(test_active_age);
    RUN_TEST(test_tick_present_unseeded_holds);
    RUN_TEST(test_binding_samples_the_pin);
    return UNITY_END();
}
