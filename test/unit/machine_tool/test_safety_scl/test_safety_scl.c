// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the IEC 61784-3 black-channel SCL primitives (services/machine_tool/safety_scl). The four ways
// a black channel can misbehave - corruption, loss, duplication, reordering - each have a case
// here, plus the receive watchdog (including a millis() rollover), the fail-safe latch, counter
// wrapping at a narrow modulus, and the explicit re-establish. Pure core, synthetic clock.

#include "services/machine_tool/safety_scl/safety_scl.h"
#include <unity.h>

static SclConn g;

// Full 32-bit counter, 100ms watchdog, first frame must carry counter 0.
void setUp(void)
{
    pc_scl_init(&g, 0, 0, 100, 1000);
}

void tearDown(void)
{
}

// Accept `n` good consecutive frames starting at `from`, one per millisecond.
static void run_ok(uint32_t from, uint32_t n, uint32_t t0)
{
    for (uint32_t i = 0; i < n; i++)
    {
        TEST_ASSERT_TRUE(pc_scl_on_frame(&g, PROTO_TRUE, from + i, t0 + i));
    }
}

void test_starts_in_init_and_is_usable(void)
{
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SCL_STATE_INIT, (uint8_t)pc_scl_state(&g));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SCL_FAULT_PC_NONE, (uint8_t)pc_scl_fault(&g));
    TEST_ASSERT_TRUE(pc_scl_ok(&g));
}

void test_good_frames_run(void)
{
    run_ok(0, 5, 1000);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SCL_STATE_RUNNING, (uint8_t)pc_scl_state(&g));
    TEST_ASSERT_EQUAL_UINT32(5, g.accepted);
    TEST_ASSERT_EQUAL_UINT32(0, g.rejected);
    TEST_ASSERT_TRUE(pc_scl_ok(&g));
}

// (1) corruption
void test_bad_signature_trips_signature_fault(void)
{
    run_ok(0, 2, 1000);
    TEST_ASSERT_FALSE(pc_scl_on_frame(&g, PROTO_FALSE, 2, 1002));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SCL_STATE_FAILSAFE, (uint8_t)pc_scl_state(&g));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SCL_FAULT_SIGNATURE, (uint8_t)pc_scl_fault(&g));
    TEST_ASSERT_FALSE(pc_scl_ok(&g));
}

// (2) loss - a dropped frame makes the next counter run ahead of the expected value
void test_lost_frame_trips_counter_fault(void)
{
    run_ok(0, 3, 1000);
    TEST_ASSERT_FALSE(pc_scl_on_frame(&g, PROTO_TRUE, 4, 1003)); // 3 was lost
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SCL_FAULT_COUNTER, (uint8_t)pc_scl_fault(&g));
}

// (3) duplication - a repeated frame carries a counter already consumed
void test_duplicate_frame_trips_counter_fault(void)
{
    run_ok(0, 3, 1000);
    TEST_ASSERT_FALSE(pc_scl_on_frame(&g, PROTO_TRUE, 2, 1003)); // 2 again
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SCL_FAULT_COUNTER, (uint8_t)pc_scl_fault(&g));
}

// (4) reordering - an out-of-order pair arrives with the counter going backwards
void test_reordered_frame_trips_counter_fault(void)
{
    run_ok(0, 4, 1000);
    TEST_ASSERT_FALSE(pc_scl_on_frame(&g, PROTO_TRUE, 1, 1004));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SCL_FAULT_COUNTER, (uint8_t)pc_scl_fault(&g));
}

// An inserted frame is the same single comparison: it is simply not the expected value.
void test_inserted_frame_trips_counter_fault(void)
{
    run_ok(0, 2, 1000);
    TEST_ASSERT_FALSE(pc_scl_on_frame(&g, PROTO_TRUE, 99, 1002));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SCL_FAULT_COUNTER, (uint8_t)pc_scl_fault(&g));
}

void test_watchdog_trips_on_a_silent_channel(void)
{
    run_ok(0, 1, 1000);
    TEST_ASSERT_TRUE(pc_scl_poll(&g, 1099));  // 99ms of silence - still inside the watchdog
    TEST_ASSERT_FALSE(pc_scl_poll(&g, 1100)); // 100ms - expired
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SCL_FAULT_TIMEOUT, (uint8_t)pc_scl_fault(&g));
}

// A connection that has never received a frame is starting up, not silent.
void test_watchdog_does_not_trip_before_the_first_frame(void)
{
    TEST_ASSERT_TRUE(pc_scl_poll(&g, 1000000));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SCL_STATE_INIT, (uint8_t)pc_scl_state(&g));
}

void test_watchdog_is_wrap_safe(void)
{
    pc_scl_init(&g, 0, 0, 100, 0xFFFFFFF0u);
    TEST_ASSERT_TRUE(pc_scl_on_frame(&g, PROTO_TRUE, 0, 0xFFFFFFF0u));
    TEST_ASSERT_TRUE(pc_scl_poll(&g, 0x00000050u));  // 0x60 = 96ms later, wrapped - still inside
    TEST_ASSERT_FALSE(pc_scl_poll(&g, 0x00000054u)); // 100ms later, wrapped - expired
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SCL_FAULT_TIMEOUT, (uint8_t)pc_scl_fault(&g));
}

void test_zero_watchdog_disables_the_timeout(void)
{
    pc_scl_init(&g, 0, 0, 0, 1000);
    TEST_ASSERT_TRUE(pc_scl_on_frame(&g, PROTO_TRUE, 0, 1000));
    TEST_ASSERT_TRUE(pc_scl_poll(&g, 0xFFFFFFFFu)); // arbitrarily long silence, never trips
}

// The core safety property: no silent self-healing.
void test_failsafe_latches_and_keeps_the_first_fault(void)
{
    run_ok(0, 2, 1000);
    TEST_ASSERT_FALSE(pc_scl_on_frame(&g, PROTO_FALSE, 2, 1002)); // SIGNATURE
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SCL_FAULT_SIGNATURE, (uint8_t)pc_scl_fault(&g));

    // A subsequent perfectly good frame must NOT bring it back, and must not rewrite the fault.
    TEST_ASSERT_FALSE(pc_scl_on_frame(&g, PROTO_TRUE, 2, 1003));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SCL_STATE_FAILSAFE, (uint8_t)pc_scl_state(&g));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SCL_FAULT_SIGNATURE, (uint8_t)pc_scl_fault(&g));
    TEST_ASSERT_FALSE(pc_scl_poll(&g, 1004));
}

void test_reset_re_establishes_and_preserves_tallies(void)
{
    run_ok(0, 2, 1000);
    TEST_ASSERT_FALSE(pc_scl_on_frame(&g, PROTO_TRUE, 77, 1002)); // COUNTER
    const uint32_t acc = g.accepted, rej = g.rejected;

    pc_scl_reset(&g, 10, 2000);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SCL_STATE_INIT, (uint8_t)pc_scl_state(&g));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SCL_FAULT_PC_NONE, (uint8_t)pc_scl_fault(&g));
    TEST_ASSERT_TRUE(pc_scl_ok(&g));
    TEST_ASSERT_EQUAL_UINT32(acc, g.accepted); // session tallies survive, so a flapping link shows
    TEST_ASSERT_EQUAL_UINT32(rej, g.rejected);

    TEST_ASSERT_TRUE(pc_scl_on_frame(&g, PROTO_TRUE, 10, 2000)); // armed for the new first counter
}

// Profiles use narrow consecutive numbers; sender and receiver must wrap identically.
void test_counter_wraps_at_the_modulus(void)
{
    pc_scl_init(&g, 254, 256, 0, 1000); // 8-bit consecutive number
    TEST_ASSERT_TRUE(pc_scl_on_frame(&g, PROTO_TRUE, 254, 1000));
    TEST_ASSERT_TRUE(pc_scl_on_frame(&g, PROTO_TRUE, 255, 1001));
    TEST_ASSERT_TRUE(pc_scl_on_frame(&g, PROTO_TRUE, 0, 1002)); // wrapped, not 256
    TEST_ASSERT_TRUE(pc_scl_on_frame(&g, PROTO_TRUE, 1, 1003));
    TEST_ASSERT_TRUE(pc_scl_ok(&g));

    // the sender helper wraps the same way, which is the point of exposing it
    TEST_ASSERT_EQUAL_UINT32(255, pc_scl_next_counter(254, 256));
    TEST_ASSERT_EQUAL_UINT32(0, pc_scl_next_counter(255, 256));
    TEST_ASSERT_EQUAL_UINT32(1, pc_scl_next_counter(0, 256));
    TEST_ASSERT_EQUAL_UINT32(0, pc_scl_next_counter(0xFFFFFFFFu, 0)); // full range wraps naturally
}

// An out-of-range first counter is normalised rather than desynchronising the connection.
void test_init_normalises_the_first_counter(void)
{
    pc_scl_init(&g, 300, 256, 0, 1000);
    TEST_ASSERT_TRUE(pc_scl_on_frame(&g, PROTO_TRUE, 44, 1000)); // 300 % 256
}

void test_null_guards(void)
{
    TEST_ASSERT_FALSE(pc_scl_on_frame(NULL, PROTO_TRUE, 0, 0));
    TEST_ASSERT_FALSE(pc_scl_poll(NULL, 0));
    TEST_ASSERT_FALSE(pc_scl_ok(NULL));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SCL_FAULT_PC_NONE, (uint8_t)pc_scl_fault(NULL));
    // a missing connection must never read as usable
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SCL_STATE_FAILSAFE, (uint8_t)pc_scl_state(NULL));
    pc_scl_init(NULL, 0, 0, 0, 0);
    pc_scl_reset(NULL, 0, 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_starts_in_init_and_is_usable);
    RUN_TEST(test_good_frames_run);
    RUN_TEST(test_bad_signature_trips_signature_fault);
    RUN_TEST(test_lost_frame_trips_counter_fault);
    RUN_TEST(test_duplicate_frame_trips_counter_fault);
    RUN_TEST(test_reordered_frame_trips_counter_fault);
    RUN_TEST(test_inserted_frame_trips_counter_fault);
    RUN_TEST(test_watchdog_trips_on_a_silent_channel);
    RUN_TEST(test_watchdog_does_not_trip_before_the_first_frame);
    RUN_TEST(test_watchdog_is_wrap_safe);
    RUN_TEST(test_zero_watchdog_disables_the_timeout);
    RUN_TEST(test_failsafe_latches_and_keeps_the_first_fault);
    RUN_TEST(test_reset_re_establishes_and_preserves_tallies);
    RUN_TEST(test_counter_wraps_at_the_modulus);
    RUN_TEST(test_init_normalises_the_first_counter);
    RUN_TEST(test_null_guards);
    return UNITY_END();
}
