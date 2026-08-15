// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the IEC 61784-3 black-channel Safety Communication Layer primitives
// (services/machine_tool/safety_scl/safety_scl.h).
//
// IEC 61784-3 is a paid IEC standard and is NOT obtainable here, so no published vector backs this
// suite. What the standard names, and what safety_scl.h states it implements, is the set of failure
// modes a black channel may exhibit - corruption, loss, unacceptable delay, duplication, insertion
// and incorrect sequence - so every case below is a PROPERTY: each of those failures must be
// detected, and the fail-safe state must latch. The CRC is deliberately not this module's (the
// profile computes it and hands the verdict in), so nothing here asserts a polynomial.
//
// test_every_black_channel_failure_is_detected is the load-bearing case: it drives one connection
// through each failure mode in turn and requires each to be caught with the right diagnosis. A
// safety layer that catches four of the five is not four fifths as safe, it is unsafe.

#include "services/machine_tool/safety_scl/safety_scl.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// A connection armed for counter 1, no modulus, a 100 ms watchdog, at t = 1000.
static void arm(SclConn *c)
{
    protocore_scl_init(c, 1, 0, 100, 1000);
}

// A fresh connection is neither running nor faulted: it is starting up.
void test_init_starts_in_init_with_no_fault(void)
{
    SclConn c;
    memset(&c, 0xEE, sizeof(c));
    arm(&c);
    TEST_ASSERT_EQUAL_INT(SCL_STATE_INIT, protocore_scl_state(&c));
    TEST_ASSERT_EQUAL_INT(SCL_FAULT_PROTOCORE_NONE, protocore_scl_fault(&c));
    TEST_ASSERT_TRUE(protocore_scl_ok(&c));
    TEST_ASSERT_EQUAL_UINT32(0u, c.accepted);
    TEST_ASSERT_EQUAL_UINT32(0u, c.rejected);
    TEST_ASSERT_EQUAL_UINT32(1u, c.expected);
}

// The first frame carrying the armed counter, with a good signature, starts the connection.
void test_first_valid_frame_runs_the_connection(void)
{
    SclConn c;
    arm(&c);
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 1, 1010));
    TEST_ASSERT_EQUAL_INT(SCL_STATE_RUNNING, protocore_scl_state(&c));
    TEST_ASSERT_EQUAL_UINT32(1u, c.accepted);
    TEST_ASSERT_EQUAL_UINT32(2u, c.expected);
    TEST_ASSERT_EQUAL_UINT32(1010u, c.last_ok_ms);

    // and the run continues, one counter at a time
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 2, 1020));
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 3, 1030));
    TEST_ASSERT_EQUAL_UINT32(3u, c.accepted);
    TEST_ASSERT_EQUAL_UINT32(0u, c.rejected);
}

// Each way a black channel misbehaves, on its own connection, with the fault it must be diagnosed
// as. Corruption is the caller's signature verdict; the other four all present as a counter that is
// not the expected next value.
void test_every_black_channel_failure_is_detected(void)
{
    SclConn c;

    // corruption: the profile's CRC rejected the frame
    arm(&c);
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 1, 1010));
    TEST_ASSERT_FALSE(protocore_scl_on_frame(&c, PROTO_FALSE, 2, 1020));
    TEST_ASSERT_EQUAL_INT(SCL_FAULT_SIGNATURE, protocore_scl_fault(&c));

    // loss: frame 2 never arrived, so 3 turns up where 2 was expected
    arm(&c);
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 1, 1010));
    TEST_ASSERT_FALSE(protocore_scl_on_frame(&c, PROTO_TRUE, 3, 1020));
    TEST_ASSERT_EQUAL_INT(SCL_FAULT_COUNTER, protocore_scl_fault(&c));

    // duplication: frame 1 arrives twice
    arm(&c);
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 1, 1010));
    TEST_ASSERT_FALSE(protocore_scl_on_frame(&c, PROTO_TRUE, 1, 1020));
    TEST_ASSERT_EQUAL_INT(SCL_FAULT_COUNTER, protocore_scl_fault(&c));

    // incorrect sequence: 3 overtakes 2
    arm(&c);
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 1, 1010));
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 2, 1020));
    TEST_ASSERT_FALSE(protocore_scl_on_frame(&c, PROTO_TRUE, 2, 1030)); // the reordered pair replays 2
    TEST_ASSERT_EQUAL_INT(SCL_FAULT_COUNTER, protocore_scl_fault(&c));

    // insertion: a frame the sender never sent, with a counter of its own
    arm(&c);
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 1, 1010));
    TEST_ASSERT_FALSE(protocore_scl_on_frame(&c, PROTO_TRUE, 0x1234, 1020));
    TEST_ASSERT_EQUAL_INT(SCL_FAULT_COUNTER, protocore_scl_fault(&c));

    // unacceptable delay: nothing at all inside the watchdog
    arm(&c);
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 1, 1010));
    TEST_ASSERT_FALSE(protocore_scl_poll(&c, 1110));
    TEST_ASSERT_EQUAL_INT(SCL_FAULT_TIMEOUT, protocore_scl_fault(&c));
}

// A corrupt frame's counter is not trusted either: the diagnosis is SIGNATURE even when the counter
// is also wrong, because a frame that failed its CRC carries no meaningful counter.
void test_corruption_is_diagnosed_before_the_counter(void)
{
    SclConn c;
    arm(&c);
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 1, 1010));
    TEST_ASSERT_FALSE(protocore_scl_on_frame(&c, PROTO_FALSE, 99, 1020));
    TEST_ASSERT_EQUAL_INT(SCL_FAULT_SIGNATURE, protocore_scl_fault(&c));
}

// Once fail-safe, a perfectly good frame does not bring the connection back, and the first fault
// stands: recovering on its own is what would let an intermittent fault look like a working link.
void test_failsafe_never_self_heals(void)
{
    SclConn c;
    arm(&c);
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 1, 1010));
    TEST_ASSERT_FALSE(protocore_scl_on_frame(&c, PROTO_FALSE, 2, 1020)); // corruption trips it
    TEST_ASSERT_EQUAL_INT(SCL_STATE_FAILSAFE, protocore_scl_state(&c));
    TEST_ASSERT_FALSE(protocore_scl_ok(&c));

    // the frame that should have arrived, arriving correctly, is still refused
    TEST_ASSERT_FALSE(protocore_scl_on_frame(&c, PROTO_TRUE, 2, 1030));
    TEST_ASSERT_EQUAL_INT(SCL_STATE_FAILSAFE, protocore_scl_state(&c));
    TEST_ASSERT_EQUAL_INT(SCL_FAULT_SIGNATURE, protocore_scl_fault(&c)); // not overwritten
    TEST_ASSERT_EQUAL_UINT32(1u, c.accepted);
    TEST_ASSERT_EQUAL_UINT32(2u, c.rejected);

    // and a poll neither heals it nor re-diagnoses it
    TEST_ASSERT_FALSE(protocore_scl_poll(&c, 1040));
    TEST_ASSERT_EQUAL_INT(SCL_FAULT_SIGNATURE, protocore_scl_fault(&c));
}

// The watchdog fires exactly at the limit, not one millisecond early.
void test_watchdog_fires_at_the_limit(void)
{
    SclConn c;
    arm(&c);
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 1, 1000));
    TEST_ASSERT_TRUE(protocore_scl_poll(&c, 1099)); // 99 ms of silence: still inside
    TEST_ASSERT_EQUAL_INT(SCL_STATE_RUNNING, protocore_scl_state(&c));
    TEST_ASSERT_FALSE(protocore_scl_poll(&c, 1100)); // 100 ms: the limit itself
    TEST_ASSERT_EQUAL_INT(SCL_FAULT_TIMEOUT, protocore_scl_fault(&c));

    // an accepted frame restarts the interval
    arm(&c);
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 1, 1000));
    TEST_ASSERT_TRUE(protocore_scl_poll(&c, 1090));
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 2, 1090));
    TEST_ASSERT_TRUE(protocore_scl_poll(&c, 1189));
    TEST_ASSERT_FALSE(protocore_scl_poll(&c, 1190));
}

// A connection that has not yet received a frame is starting up, not silent, so it never times out.
void test_watchdog_does_not_run_before_the_first_frame(void)
{
    SclConn c;
    arm(&c);
    TEST_ASSERT_TRUE(protocore_scl_poll(&c, 1100));
    TEST_ASSERT_TRUE(protocore_scl_poll(&c, 100000));
    TEST_ASSERT_EQUAL_INT(SCL_STATE_INIT, protocore_scl_state(&c));
    TEST_ASSERT_EQUAL_INT(SCL_FAULT_PROTOCORE_NONE, protocore_scl_fault(&c));
}

// A zero watchdog disables the check: silence is then the application's problem, not this layer's.
void test_zero_watchdog_disables_the_check(void)
{
    SclConn c;
    protocore_scl_init(&c, 1, 0, 0, 1000);
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 1, 1000));
    TEST_ASSERT_TRUE(protocore_scl_poll(&c, 0xFFFFFFFFu));
    TEST_ASSERT_EQUAL_INT(SCL_STATE_RUNNING, protocore_scl_state(&c));
}

// The elapsed test is an unsigned difference, so a millis() rollover between the last frame and the
// poll yields the true interval rather than a huge one that trips instantly.
void test_watchdog_is_rollover_safe(void)
{
    SclConn c;
    // last frame at 16 ms before the wrap, polled 16 ms after it: 32 ms elapsed, inside a 100 ms
    // watchdog even though the raw numbers went backwards.
    protocore_scl_init(&c, 1, 0, 100, 0xFFFFFFF0u);
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 1, 0xFFFFFFF0u));
    TEST_ASSERT_TRUE(protocore_scl_poll(&c, 0x00000010u)); // (0x10 - 0xFFFFFFF0) mod 2^32 = 32
    TEST_ASSERT_EQUAL_INT(SCL_STATE_RUNNING, protocore_scl_state(&c));

    // 0x60 past the wrap is 112 ms, past the limit
    TEST_ASSERT_FALSE(protocore_scl_poll(&c, 0x00000060u));
    TEST_ASSERT_EQUAL_INT(SCL_FAULT_TIMEOUT, protocore_scl_fault(&c));
}

// A narrow counter wraps at its modulus, and sender and receiver wrap the same way: walking a full
// cycle plus one keeps the connection accepted throughout.
void test_counter_wraps_at_the_modulus(void)
{
    SclConn c;
    protocore_scl_init(&c, 0, 4, 0, 0);
    uint32_t sender = 0;
    for (uint32_t i = 0; i < 9; i++) // more than two full cycles of a modulus-4 counter
    {
        TEST_ASSERT_TRUE_MESSAGE(protocore_scl_on_frame(&c, PROTO_TRUE, sender, i), "wrapped counter refused");
        sender = protocore_scl_next_counter(sender, 4);
    }
    TEST_ASSERT_EQUAL_UINT32(9u, c.accepted);
    TEST_ASSERT_EQUAL_UINT32(0u, c.rejected);
    TEST_ASSERT_EQUAL_INT(SCL_STATE_RUNNING, protocore_scl_state(&c));

    // the sender's own sequence over one cycle
    TEST_ASSERT_EQUAL_UINT32(1u, protocore_scl_next_counter(0, 4));
    TEST_ASSERT_EQUAL_UINT32(2u, protocore_scl_next_counter(1, 4));
    TEST_ASSERT_EQUAL_UINT32(3u, protocore_scl_next_counter(2, 4));
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_scl_next_counter(3, 4));

    // with no modulus the counter runs the full 32 bits and wraps there
    TEST_ASSERT_EQUAL_UINT32(1u, protocore_scl_next_counter(0, 0));
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_scl_next_counter(0xFFFFFFFFu, 0));

    // an armed counter above the modulus is itself wrapped, so the two sides cannot disagree
    protocore_scl_init(&c, 6, 4, 0, 0);
    TEST_ASSERT_EQUAL_UINT32(2u, c.expected);
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 6, 0)); // 6 mod 4 == 2
}

// A wrapped counter still catches a skip: with modulus 4, jumping two ahead is not the next value.
void test_a_narrow_counter_still_catches_a_skip(void)
{
    SclConn c;
    protocore_scl_init(&c, 0, 4, 0, 0);
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 0, 0));
    TEST_ASSERT_FALSE(protocore_scl_on_frame(&c, PROTO_TRUE, 2, 1));
    TEST_ASSERT_EQUAL_INT(SCL_FAULT_COUNTER, protocore_scl_fault(&c));
}

// Re-establishing is explicit and it arms a fresh counter, but the session tallies are preserved so
// a flapping link stays visible instead of being reset away.
void test_reset_re_establishes_and_keeps_the_tallies(void)
{
    SclConn c;
    arm(&c);
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 1, 1010));
    TEST_ASSERT_FALSE(protocore_scl_on_frame(&c, PROTO_TRUE, 7, 1020)); // a skip trips it
    TEST_ASSERT_EQUAL_UINT32(1u, c.accepted);
    TEST_ASSERT_EQUAL_UINT32(1u, c.rejected);

    protocore_scl_reset(&c, 50, 2000);
    TEST_ASSERT_EQUAL_INT(SCL_STATE_INIT, protocore_scl_state(&c));
    TEST_ASSERT_EQUAL_INT(SCL_FAULT_PROTOCORE_NONE, protocore_scl_fault(&c));
    TEST_ASSERT_TRUE(protocore_scl_ok(&c));
    TEST_ASSERT_EQUAL_UINT32(50u, c.expected);
    TEST_ASSERT_EQUAL_UINT32(1u, c.accepted); // preserved
    TEST_ASSERT_EQUAL_UINT32(1u, c.rejected); // preserved

    // the re-established connection runs again, and its watchdog measures from the reset
    TEST_ASSERT_TRUE(protocore_scl_poll(&c, 100000)); // still INIT: no watchdog yet
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 50, 2010));
    TEST_ASSERT_EQUAL_INT(SCL_STATE_RUNNING, protocore_scl_state(&c));
    TEST_ASSERT_EQUAL_UINT32(2u, c.accepted);
}

// A missing connection is never a usable one, and no call writes through the null.
void test_a_null_connection_is_not_usable(void)
{
    TEST_ASSERT_FALSE(protocore_scl_ok(NULL));
    TEST_ASSERT_EQUAL_INT(SCL_STATE_FAILSAFE, protocore_scl_state(NULL));
    TEST_ASSERT_EQUAL_INT(SCL_FAULT_PROTOCORE_NONE, protocore_scl_fault(NULL));
    TEST_ASSERT_FALSE(protocore_scl_on_frame(NULL, PROTO_TRUE, 1, 0));
    TEST_ASSERT_FALSE(protocore_scl_poll(NULL, 0));
    protocore_scl_init(NULL, 1, 0, 100, 0); // no crash
    protocore_scl_reset(NULL, 1, 0);
}

// The reset arms the counter through the same modulus the connection was initialized with.
void test_reset_honours_the_counter_modulus(void)
{
    SclConn c;
    protocore_scl_init(&c, 0, 4, 0, 0);
    TEST_ASSERT_FALSE(protocore_scl_on_frame(&c, PROTO_FALSE, 0, 0));
    protocore_scl_reset(&c, 9, 100);
    TEST_ASSERT_EQUAL_UINT32(1u, c.expected); // 9 mod 4
    TEST_ASSERT_TRUE(protocore_scl_on_frame(&c, PROTO_TRUE, 9, 110));
}
