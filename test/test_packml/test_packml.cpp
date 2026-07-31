// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the PackML / OMAC state model (ISA-TR88.00.02): the pure transition engine
// (command / state-complete / execute-complete + command validity) and the owned PackTags service
// (state advance, production counters, unit-mode rules, machine speed, and the state/reset timers).

#include "services/machine_tool/packml/packml.h"
#include "services/system/clock.h"
#include <unity.h>

// Host clock seam so the timer tags (StateCurrentTime, AccTimeSinceReset) are deterministic.
static uint32_t g_ms = 0;
static uint32_t test_clock()
{
    return g_ms;
}

void setUp()
{
    g_ms = 0;
    pc_set_clock(test_clock, 1000);
    pc_packml_svc_init(PackMlMode::PRODUCING);
}
void tearDown()
{
}

// ---- Pure engine: happy production path -----------------------------------

void test_engine_startup_to_execute()
{
    PackMlState s = PackMlState::STOPPED;
    s = pc_packml_command(s, PackMlCommand::RESET);
    TEST_ASSERT_EQUAL(PackMlState::RESETTING, s);
    s = pc_packml_state_complete(s);
    TEST_ASSERT_EQUAL(PackMlState::IDLE, s);
    s = pc_packml_command(s, PackMlCommand::START);
    TEST_ASSERT_EQUAL(PackMlState::STARTING, s);
    s = pc_packml_state_complete(s);
    TEST_ASSERT_EQUAL(PackMlState::EXECUTE, s);
}

void test_engine_execute_to_complete_and_back()
{
    PackMlState s = PackMlState::EXECUTE;
    s = pc_packml_execute_complete(s);
    TEST_ASSERT_EQUAL(PackMlState::COMPLETING, s);
    s = pc_packml_state_complete(s);
    TEST_ASSERT_EQUAL(PackMlState::COMPLETE, s);
    // Complete -> Reset -> Resetting -> Idle, ready for the next run.
    s = pc_packml_command(s, PackMlCommand::RESET);
    TEST_ASSERT_EQUAL(PackMlState::RESETTING, s);
    TEST_ASSERT_EQUAL(PackMlState::IDLE, pc_packml_state_complete(s));
}

void test_engine_hold_unhold()
{
    PackMlState s = PackMlState::EXECUTE;
    s = pc_packml_command(s, PackMlCommand::HOLD);
    TEST_ASSERT_EQUAL(PackMlState::HOLDING, s);
    s = pc_packml_state_complete(s);
    TEST_ASSERT_EQUAL(PackMlState::HELD, s);
    s = pc_packml_command(s, PackMlCommand::UNHOLD);
    TEST_ASSERT_EQUAL(PackMlState::UNHOLDING, s);
    TEST_ASSERT_EQUAL(PackMlState::EXECUTE, pc_packml_state_complete(s));
}

void test_engine_suspend_unsuspend()
{
    PackMlState s = PackMlState::EXECUTE;
    s = pc_packml_command(s, PackMlCommand::SUSPEND);
    TEST_ASSERT_EQUAL(PackMlState::SUSPENDING, s);
    s = pc_packml_state_complete(s);
    TEST_ASSERT_EQUAL(PackMlState::SUSPENDED, s);
    s = pc_packml_command(s, PackMlCommand::UNSUSPEND);
    TEST_ASSERT_EQUAL(PackMlState::UNSUSPENDING, s);
    TEST_ASSERT_EQUAL(PackMlState::EXECUTE, pc_packml_state_complete(s));
}

// ---- Universal Stop / Abort branches --------------------------------------

void test_engine_stop_from_many_states()
{
    const PackMlState from[] = {PackMlState::IDLE,      PackMlState::EXECUTE,  PackMlState::HELD,
                                PackMlState::SUSPENDED, PackMlState::COMPLETE, PackMlState::RESETTING};
    for (unsigned i = 0; i < sizeof(from) / sizeof(from[0]); i++)
    {
        TEST_ASSERT_EQUAL(PackMlState::STOPPING, pc_packml_command(from[i], PackMlCommand::STOP));
    }
    // Stopping -> Stopped.
    TEST_ASSERT_EQUAL(PackMlState::STOPPED, pc_packml_state_complete(PackMlState::STOPPING));
    // Stop is a no-op once already Stopped / Stopping / in the abort branch.
    TEST_ASSERT_EQUAL(PackMlState::STOPPED, pc_packml_command(PackMlState::STOPPED, PackMlCommand::STOP));
    TEST_ASSERT_EQUAL(PackMlState::ABORTED, pc_packml_command(PackMlState::ABORTED, PackMlCommand::STOP));
}

void test_engine_abort_and_clear()
{
    // Abort from any non-abort state -> Aborting -> Aborted.
    TEST_ASSERT_EQUAL(PackMlState::ABORTING, pc_packml_command(PackMlState::EXECUTE, PackMlCommand::ABORT));
    TEST_ASSERT_EQUAL(PackMlState::ABORTING, pc_packml_command(PackMlState::STOPPED, PackMlCommand::ABORT));
    TEST_ASSERT_EQUAL(PackMlState::ABORTED, pc_packml_state_complete(PackMlState::ABORTING));
    // Abort is a no-op once aborting/aborted.
    TEST_ASSERT_EQUAL(PackMlState::ABORTED, pc_packml_command(PackMlState::ABORTED, PackMlCommand::ABORT));
    // Aborted -> Clear -> Clearing -> Stopped.
    PackMlState s = pc_packml_command(PackMlState::ABORTED, PackMlCommand::CLEAR);
    TEST_ASSERT_EQUAL(PackMlState::CLEARING, s);
    TEST_ASSERT_EQUAL(PackMlState::STOPPED, pc_packml_state_complete(s));
}

void test_engine_stop_and_abort_are_noops_inside_a_teardown()
{
    // Stop must not restart a teardown that is already running, and Abort must not
    // restart itself - otherwise the acting state would be re-entered forever.
    TEST_ASSERT_EQUAL(PackMlState::STOPPING, pc_packml_command(PackMlState::STOPPING, PackMlCommand::STOP));
    TEST_ASSERT_EQUAL(PackMlState::CLEARING, pc_packml_command(PackMlState::CLEARING, PackMlCommand::STOP));
    TEST_ASSERT_EQUAL(PackMlState::ABORTING, pc_packml_command(PackMlState::ABORTING, PackMlCommand::STOP));
    TEST_ASSERT_EQUAL(PackMlState::ABORTING, pc_packml_command(PackMlState::ABORTING, PackMlCommand::ABORT));
    // Abort still overrides a Stop already in progress: a fault outranks a stop.
    TEST_ASSERT_EQUAL(PackMlState::ABORTING, pc_packml_command(PackMlState::STOPPING, PackMlCommand::ABORT));
    TEST_ASSERT_EQUAL(PackMlState::ABORTING, pc_packml_command(PackMlState::CLEARING, PackMlCommand::ABORT));
}

void test_engine_wait_states_ignore_foreign_commands()
{
    // Each wait state accepts exactly one command; anything else leaves it untouched,
    // so a stray HMI button cannot shortcut the model.
    TEST_ASSERT_EQUAL(PackMlState::HELD, pc_packml_command(PackMlState::HELD, PackMlCommand::START));
    TEST_ASSERT_EQUAL(PackMlState::SUSPENDED, pc_packml_command(PackMlState::SUSPENDED, PackMlCommand::HOLD));
    TEST_ASSERT_EQUAL(PackMlState::COMPLETE, pc_packml_command(PackMlState::COMPLETE, PackMlCommand::START));
    TEST_ASSERT_EQUAL(PackMlState::ABORTED, pc_packml_command(PackMlState::ABORTED, PackMlCommand::RESET));
    TEST_ASSERT_EQUAL(PackMlState::STOPPED, pc_packml_command(PackMlState::STOPPED, PackMlCommand::CLEAR));
    TEST_ASSERT_EQUAL(PackMlState::IDLE, pc_packml_command(PackMlState::IDLE, PackMlCommand::UNHOLD));
    TEST_ASSERT_EQUAL(PackMlState::EXECUTE, pc_packml_command(PackMlState::EXECUTE, PackMlCommand::UNSUSPEND));
}

void test_engine_acting_states_accept_only_stop_and_abort()
{
    // Acting states are transient: nothing but the universal Stop / Abort may interrupt
    // one, and an uninitialized state accepts nothing else either.
    TEST_ASSERT_EQUAL(PackMlState::STARTING, pc_packml_command(PackMlState::STARTING, PackMlCommand::HOLD));
    TEST_ASSERT_EQUAL(PackMlState::CLEARING, pc_packml_command(PackMlState::CLEARING, PackMlCommand::RESET));
    TEST_ASSERT_EQUAL(PackMlState::COMPLETING, pc_packml_command(PackMlState::COMPLETING, PackMlCommand::START));
    TEST_ASSERT_EQUAL(PackMlState::UNHOLDING, pc_packml_command(PackMlState::UNHOLDING, PackMlCommand::UNHOLD));
    TEST_ASSERT_EQUAL(PackMlState::UNDEFINED, pc_packml_command(PackMlState::UNDEFINED, PackMlCommand::START));
    // ...but Stop and Abort do get through.
    TEST_ASSERT_EQUAL(PackMlState::STOPPING, pc_packml_command(PackMlState::STARTING, PackMlCommand::STOP));
    TEST_ASSERT_EQUAL(PackMlState::ABORTING, pc_packml_command(PackMlState::STARTING, PackMlCommand::ABORT));
}

void test_engine_execute_complete_only_from_execute()
{
    // "production done" is meaningless anywhere but Execute, so it must not move the state.
    TEST_ASSERT_EQUAL(PackMlState::IDLE, pc_packml_execute_complete(PackMlState::IDLE));
    TEST_ASSERT_EQUAL(PackMlState::HELD, pc_packml_execute_complete(PackMlState::HELD));
    TEST_ASSERT_EQUAL(PackMlState::COMPLETE, pc_packml_execute_complete(PackMlState::COMPLETE));
}

// ---- Command validity ------------------------------------------------------

void test_engine_invalid_commands_are_noops()
{
    // Start only from Idle; Hold only from Execute; Reset only from Stopped/Complete; etc.
    TEST_ASSERT_EQUAL(PackMlState::STOPPED, pc_packml_command(PackMlState::STOPPED, PackMlCommand::START));
    TEST_ASSERT_EQUAL(PackMlState::IDLE, pc_packml_command(PackMlState::IDLE, PackMlCommand::HOLD));
    TEST_ASSERT_EQUAL(PackMlState::EXECUTE, pc_packml_command(PackMlState::EXECUTE, PackMlCommand::RESET));
    TEST_ASSERT_EQUAL(PackMlState::IDLE, pc_packml_command(PackMlState::IDLE, PackMlCommand::RESET));
    TEST_ASSERT_FALSE(pc_packml_command_valid(PackMlState::STOPPED, PackMlCommand::START));
    TEST_ASSERT_TRUE(pc_packml_command_valid(PackMlState::STOPPED, PackMlCommand::RESET));
    TEST_ASSERT_TRUE(pc_packml_command_valid(PackMlState::EXECUTE, PackMlCommand::HOLD));
}

void test_engine_acting_classification()
{
    TEST_ASSERT_TRUE(pc_packml_is_acting(PackMlState::STARTING));
    TEST_ASSERT_TRUE(pc_packml_is_acting(PackMlState::ABORTING));
    TEST_ASSERT_TRUE(pc_packml_is_acting(PackMlState::COMPLETING));
    TEST_ASSERT_FALSE(pc_packml_is_acting(PackMlState::EXECUTE));
    TEST_ASSERT_FALSE(pc_packml_is_acting(PackMlState::STOPPED));
    TEST_ASSERT_FALSE(pc_packml_is_acting(PackMlState::ABORTED));
    // Wait states do not auto-advance.
    TEST_ASSERT_EQUAL(PackMlState::EXECUTE, pc_packml_state_complete(PackMlState::EXECUTE));
    TEST_ASSERT_EQUAL(PackMlState::IDLE, pc_packml_state_complete(PackMlState::IDLE));
}

void test_state_wire_numbers()
{
    // Status.StateCurrent carries the ISA-TR88.00.02 numbers an HMI expects.
    TEST_ASSERT_EQUAL_UINT8(2, (uint8_t)PackMlState::STOPPED);
    TEST_ASSERT_EQUAL_UINT8(6, (uint8_t)PackMlState::EXECUTE);
    TEST_ASSERT_EQUAL_UINT8(9, (uint8_t)PackMlState::ABORTED);
    TEST_ASSERT_EQUAL_UINT8(17, (uint8_t)PackMlState::COMPLETE);
    TEST_ASSERT_EQUAL_STRING("Execute", pc_packml_state_name(PackMlState::EXECUTE));
    TEST_ASSERT_EQUAL_STRING("Abort", pc_packml_command_name(PackMlCommand::ABORT));
}

void test_every_state_has_its_isa_name()
{
    // The names go straight onto an HMI / into a log line, so every one of the 17 states
    // needs the ISA-TR88.00.02 spelling - a wrong or missing arm reads as "Undefined".
    TEST_ASSERT_EQUAL_STRING("Clearing", pc_packml_state_name(PackMlState::CLEARING));
    TEST_ASSERT_EQUAL_STRING("Stopped", pc_packml_state_name(PackMlState::STOPPED));
    TEST_ASSERT_EQUAL_STRING("Starting", pc_packml_state_name(PackMlState::STARTING));
    TEST_ASSERT_EQUAL_STRING("Idle", pc_packml_state_name(PackMlState::IDLE));
    TEST_ASSERT_EQUAL_STRING("Suspended", pc_packml_state_name(PackMlState::SUSPENDED));
    TEST_ASSERT_EQUAL_STRING("Execute", pc_packml_state_name(PackMlState::EXECUTE));
    TEST_ASSERT_EQUAL_STRING("Stopping", pc_packml_state_name(PackMlState::STOPPING));
    TEST_ASSERT_EQUAL_STRING("Aborting", pc_packml_state_name(PackMlState::ABORTING));
    TEST_ASSERT_EQUAL_STRING("Aborted", pc_packml_state_name(PackMlState::ABORTED));
    TEST_ASSERT_EQUAL_STRING("Holding", pc_packml_state_name(PackMlState::HOLDING));
    TEST_ASSERT_EQUAL_STRING("Held", pc_packml_state_name(PackMlState::HELD));
    TEST_ASSERT_EQUAL_STRING("Unholding", pc_packml_state_name(PackMlState::UNHOLDING));
    TEST_ASSERT_EQUAL_STRING("Suspending", pc_packml_state_name(PackMlState::SUSPENDING));
    TEST_ASSERT_EQUAL_STRING("Unsuspending", pc_packml_state_name(PackMlState::UNSUSPENDING));
    TEST_ASSERT_EQUAL_STRING("Resetting", pc_packml_state_name(PackMlState::RESETTING));
    TEST_ASSERT_EQUAL_STRING("Completing", pc_packml_state_name(PackMlState::COMPLETING));
    TEST_ASSERT_EQUAL_STRING("Complete", pc_packml_state_name(PackMlState::COMPLETE));
    // Anything outside the model, including the pre-init value, is reported as such.
    TEST_ASSERT_EQUAL_STRING("Undefined", pc_packml_state_name(PackMlState::UNDEFINED));
    TEST_ASSERT_EQUAL_STRING("Undefined", pc_packml_state_name((PackMlState)200));
}

void test_every_command_has_its_isa_name()
{
    TEST_ASSERT_EQUAL_STRING("Reset", pc_packml_command_name(PackMlCommand::RESET));
    TEST_ASSERT_EQUAL_STRING("Start", pc_packml_command_name(PackMlCommand::START));
    TEST_ASSERT_EQUAL_STRING("Stop", pc_packml_command_name(PackMlCommand::STOP));
    TEST_ASSERT_EQUAL_STRING("Hold", pc_packml_command_name(PackMlCommand::HOLD));
    TEST_ASSERT_EQUAL_STRING("Unhold", pc_packml_command_name(PackMlCommand::UNHOLD));
    TEST_ASSERT_EQUAL_STRING("Suspend", pc_packml_command_name(PackMlCommand::SUSPEND));
    TEST_ASSERT_EQUAL_STRING("Unsuspend", pc_packml_command_name(PackMlCommand::UNSUSPEND));
    TEST_ASSERT_EQUAL_STRING("Abort", pc_packml_command_name(PackMlCommand::ABORT));
    TEST_ASSERT_EQUAL_STRING("Clear", pc_packml_command_name(PackMlCommand::CLEAR));
    // The idle CntrlCmd value (and anything unknown) is "None", not a stray pointer.
    TEST_ASSERT_EQUAL_STRING("None", pc_packml_command_name(PackMlCommand::PC_NONE));
    TEST_ASSERT_EQUAL_STRING("None", pc_packml_command_name((PackMlCommand)200));
}

// ---- Owned service ---------------------------------------------------------

void test_svc_init_is_stopped()
{
    TEST_ASSERT_EQUAL(PackMlState::STOPPED, pc_packml_svc_state());
    PackMlStatus st;
    pc_packml_svc_status(&st);
    TEST_ASSERT_EQUAL(PackMlState::STOPPED, st.state_current);
    TEST_ASSERT_EQUAL(PackMlMode::PRODUCING, st.unit_mode_current);
    TEST_ASSERT_EQUAL_UINT32(0, st.prod_processed);
}

void test_svc_full_run_with_counts()
{
    TEST_ASSERT_TRUE(pc_packml_svc_command(PackMlCommand::RESET));
    pc_packml_svc_state_complete(); // -> Idle
    TEST_ASSERT_EQUAL(PackMlState::IDLE, pc_packml_svc_state());
    TEST_ASSERT_TRUE(pc_packml_svc_command(PackMlCommand::START));
    pc_packml_svc_state_complete(); // -> Execute
    TEST_ASSERT_EQUAL(PackMlState::EXECUTE, pc_packml_svc_state());

    pc_packml_svc_count(false);
    pc_packml_svc_count(false);
    pc_packml_svc_count(true); // one defective
    PackMlStatus st;
    pc_packml_svc_status(&st);
    TEST_ASSERT_EQUAL_UINT32(3, st.prod_processed);
    TEST_ASSERT_EQUAL_UINT32(1, st.prod_defective);

    TEST_ASSERT_TRUE(pc_packml_svc_complete_run()); // -> Completing
    pc_packml_svc_state_complete();                 // -> Complete
    TEST_ASSERT_EQUAL(PackMlState::COMPLETE, pc_packml_svc_state());
}

void test_svc_count_only_in_execute()
{
    // Not executing (Stopped) -> counts are ignored.
    pc_packml_svc_count(false);
    PackMlStatus st;
    pc_packml_svc_status(&st);
    TEST_ASSERT_EQUAL_UINT32(0, st.prod_processed);
}

void test_svc_rejects_illegal_command()
{
    // Start is illegal in Stopped; the service reports no change.
    TEST_ASSERT_FALSE(pc_packml_svc_command(PackMlCommand::START));
    TEST_ASSERT_EQUAL(PackMlState::STOPPED, pc_packml_svc_state());
}

void test_svc_mode_change_rules()
{
    // Allowed in Stopped.
    TEST_ASSERT_TRUE(pc_packml_svc_set_mode(PackMlMode::MAINTENANCE));
    // Drive into Execute, where a mode change must be refused.
    pc_packml_svc_command(PackMlCommand::RESET);
    pc_packml_svc_state_complete();
    pc_packml_svc_command(PackMlCommand::START);
    pc_packml_svc_state_complete();
    TEST_ASSERT_EQUAL(PackMlState::EXECUTE, pc_packml_svc_state());
    TEST_ASSERT_FALSE(pc_packml_svc_set_mode(PackMlMode::MANUAL));
    PackMlStatus st;
    pc_packml_svc_status(&st);
    TEST_ASSERT_EQUAL(PackMlMode::MAINTENANCE, st.unit_mode_current); // unchanged
}

void test_svc_speed_actual_tracks_execute()
{
    pc_packml_svc_set_speed(120.0f);
    PackMlStatus st;
    pc_packml_svc_status(&st);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, st.mach_speed_actual); // Stopped -> 0
    pc_packml_svc_command(PackMlCommand::RESET);
    pc_packml_svc_state_complete();
    pc_packml_svc_command(PackMlCommand::START);
    pc_packml_svc_state_complete(); // Execute
    pc_packml_svc_status(&st);
    TEST_ASSERT_EQUAL_FLOAT(120.0f, st.mach_speed_actual);
}

void test_svc_timers()
{
    g_ms = 1000;
    pc_packml_svc_command(PackMlCommand::RESET); // reset stamps AccTimeSinceReset base + enters Resetting
    g_ms = 1500;
    PackMlStatus st;
    pc_packml_svc_status(&st);
    TEST_ASSERT_EQUAL_UINT32(500, st.state_current_ms);        // 1500 - 1000 (entered Resetting at 1000)
    TEST_ASSERT_EQUAL_UINT32(500, st.acc_time_since_reset_ms); // reset at 1000
    pc_packml_svc_state_complete();                            // -> Idle at 1500
    g_ms = 1800;
    pc_packml_svc_status(&st);
    TEST_ASSERT_EQUAL_UINT32(300, st.state_current_ms);        // 1800 - 1500
    TEST_ASSERT_EQUAL_UINT32(800, st.acc_time_since_reset_ms); // 1800 - 1000
}

void test_svc_abort_and_clear_cycle()
{
    // The fault branch driven through the owned service: Execute -> Aborting -> Aborted,
    // where only Clear (never Reset) is accepted, then Clearing -> Stopped.
    pc_packml_svc_command(PackMlCommand::RESET);
    pc_packml_svc_state_complete(); // Idle
    pc_packml_svc_command(PackMlCommand::START);
    pc_packml_svc_state_complete(); // Execute
    TEST_ASSERT_EQUAL(PackMlState::EXECUTE, pc_packml_svc_state());

    g_ms = 500;
    TEST_ASSERT_TRUE(pc_packml_svc_command(PackMlCommand::ABORT));
    TEST_ASSERT_EQUAL(PackMlState::ABORTING, pc_packml_svc_state());
    TEST_ASSERT_EQUAL(PackMlState::ABORTED, pc_packml_svc_state_complete());
    // Counting stops the moment production does.
    pc_packml_svc_count(false);
    PackMlStatus st;
    pc_packml_svc_status(&st);
    TEST_ASSERT_EQUAL_UINT32(0, st.prod_processed);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, st.mach_speed_actual);

    // Reset does not leave a fault; Clear does.
    TEST_ASSERT_FALSE(pc_packml_svc_command(PackMlCommand::RESET));
    TEST_ASSERT_EQUAL(PackMlState::ABORTED, pc_packml_svc_state());
    g_ms = 700;
    TEST_ASSERT_TRUE(pc_packml_svc_command(PackMlCommand::CLEAR));
    TEST_ASSERT_EQUAL(PackMlState::CLEARING, pc_packml_svc_state());
    // Entering Clearing restamped the state clock, and Clearing completes to Stopped.
    g_ms = 900;
    pc_packml_svc_status(&st);
    TEST_ASSERT_EQUAL_UINT32(200, st.state_current_ms);
    TEST_ASSERT_EQUAL(PackMlState::STOPPED, pc_packml_svc_state_complete());
}

void test_svc_stop_from_execute_lands_stopped()
{
    // The other teardown: Stop is legal mid-production and completes to Stopped, which
    // is a mode-changeable state again.
    pc_packml_svc_command(PackMlCommand::RESET);
    pc_packml_svc_state_complete();
    pc_packml_svc_command(PackMlCommand::START);
    pc_packml_svc_state_complete(); // Execute
    TEST_ASSERT_TRUE(pc_packml_svc_command(PackMlCommand::STOP));
    TEST_ASSERT_EQUAL(PackMlState::STOPPING, pc_packml_svc_state());
    // A second Stop while stopping changes nothing.
    TEST_ASSERT_FALSE(pc_packml_svc_command(PackMlCommand::STOP));
    TEST_ASSERT_EQUAL(PackMlState::STOPPED, pc_packml_svc_state_complete());
    TEST_ASSERT_TRUE(pc_packml_svc_set_mode(PackMlMode::MANUAL));
}

void test_svc_state_complete_in_a_wait_state_does_not_restamp()
{
    // Wait states have no State-Complete transition, so the call must be a true no-op -
    // in particular it must not reset the StateCurrentTime clock.
    g_ms = 400;
    TEST_ASSERT_EQUAL(PackMlState::STOPPED, pc_packml_svc_state_complete());
    g_ms = 900;
    PackMlStatus st;
    pc_packml_svc_status(&st);
    TEST_ASSERT_EQUAL_UINT32(900, st.state_current_ms); // still timed from the init at 0
}

void test_svc_complete_run_requires_execute()
{
    // ExecuteComplete outside Execute is not a state change and must report so.
    TEST_ASSERT_FALSE(pc_packml_svc_complete_run());
    TEST_ASSERT_EQUAL(PackMlState::STOPPED, pc_packml_svc_state());
    pc_packml_svc_command(PackMlCommand::RESET);
    pc_packml_svc_state_complete(); // Idle - still not producing
    TEST_ASSERT_FALSE(pc_packml_svc_complete_run());
    TEST_ASSERT_EQUAL(PackMlState::IDLE, pc_packml_svc_state());
}

void test_svc_mode_change_allowed_in_idle_and_aborted()
{
    // The mode-change rule is "stable and not producing", which is Stopped, Idle or Aborted.
    pc_packml_svc_command(PackMlCommand::RESET);
    pc_packml_svc_state_complete(); // Idle
    TEST_ASSERT_TRUE(pc_packml_svc_set_mode(PackMlMode::MANUAL));
    pc_packml_svc_command(PackMlCommand::ABORT);
    pc_packml_svc_state_complete(); // Aborted
    TEST_ASSERT_TRUE(pc_packml_svc_set_mode(PackMlMode::MAINTENANCE));
    PackMlStatus st;
    pc_packml_svc_status(&st);
    TEST_ASSERT_EQUAL(PackMlMode::MAINTENANCE, st.unit_mode_current);
    // But not while the fault is being cleared.
    pc_packml_svc_command(PackMlCommand::CLEAR); // Clearing
    TEST_ASSERT_FALSE(pc_packml_svc_set_mode(PackMlMode::PRODUCING));
    pc_packml_svc_status(&st);
    TEST_ASSERT_EQUAL(PackMlMode::MAINTENANCE, st.unit_mode_current);
}

void test_svc_status_null_out_is_ignored()
{
    // A null status buffer must be a no-op, not a write through nullptr.
    pc_packml_svc_status(nullptr);
    TEST_ASSERT_EQUAL(PackMlState::STOPPED, pc_packml_svc_state());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_engine_startup_to_execute);
    RUN_TEST(test_engine_execute_to_complete_and_back);
    RUN_TEST(test_engine_hold_unhold);
    RUN_TEST(test_engine_suspend_unsuspend);
    RUN_TEST(test_engine_stop_from_many_states);
    RUN_TEST(test_engine_abort_and_clear);
    RUN_TEST(test_engine_stop_and_abort_are_noops_inside_a_teardown);
    RUN_TEST(test_engine_wait_states_ignore_foreign_commands);
    RUN_TEST(test_engine_acting_states_accept_only_stop_and_abort);
    RUN_TEST(test_engine_execute_complete_only_from_execute);
    RUN_TEST(test_engine_invalid_commands_are_noops);
    RUN_TEST(test_engine_acting_classification);
    RUN_TEST(test_state_wire_numbers);
    RUN_TEST(test_every_state_has_its_isa_name);
    RUN_TEST(test_every_command_has_its_isa_name);
    RUN_TEST(test_svc_init_is_stopped);
    RUN_TEST(test_svc_full_run_with_counts);
    RUN_TEST(test_svc_count_only_in_execute);
    RUN_TEST(test_svc_rejects_illegal_command);
    RUN_TEST(test_svc_mode_change_rules);
    RUN_TEST(test_svc_speed_actual_tracks_execute);
    RUN_TEST(test_svc_timers);
    RUN_TEST(test_svc_abort_and_clear_cycle);
    RUN_TEST(test_svc_stop_from_execute_lands_stopped);
    RUN_TEST(test_svc_state_complete_in_a_wait_state_does_not_restamp);
    RUN_TEST(test_svc_complete_run_requires_execute);
    RUN_TEST(test_svc_mode_change_allowed_in_idle_and_aborted);
    RUN_TEST(test_svc_status_null_out_is_ignored);
    return UNITY_END();
}
