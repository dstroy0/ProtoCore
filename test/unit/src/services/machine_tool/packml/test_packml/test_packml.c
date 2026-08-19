// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the PackML / OMAC state model (services/machine_tool/packml/packml.h).
//
// The state model is ISA-TR88.00.02; the numbers it assigns are published verbatim by OPC 30050
// (OPC UA for PackML), whose PackMLMachineStateMachineType gives Clearing 1, Stopped 2, Stopping 7
// and PackMLExecuteStateMachineType gives Starting 3, Idle 4, Suspended 5, Execute 6, Holding 10,
// Held 11, Unholding 12, Suspending 13, Unsuspending 14, Resetting 15, Completing 16, Complete 17,
// with the flow Resetting -> Idle -> Starting -> Execute -> Completing -> Complete and the
// Execute <-> Holding <-> Held / Execute <-> Suspending <-> Suspended branches. The PackTags
// Command.CntrlCmd numbering (Reset 1, Start 2, Stop 3, Hold 4, Unhold 5, Suspend 6, Unsuspend 7,
// Abort 8, Clear 9) is the one published in the PackML PLC libraries (Siemens LPMLV30, Beckhoff
// Tc3_PackML_V2 E_PMLCommand).
//
// test_statecurrent_values_are_the_published_numbers is the load-bearing case: StateCurrent goes on
// the wire to a line controller, so a state whose number is off by one is not a local naming
// mistake, it is a machine reporting a different state than the one it is in.

#include "server/clock/clock.h"
#include "services/machine_tool/packml/packml.h"
#include <string.h>

#include <unity.h>

// A clock the test moves by hand, so the PackTags timers are arithmetic and not wall time.
static uint32_t s_now;

static uint32_t test_clock(void)
{
    return s_now;
}

// Move the clock and poll it. Clock.ms is what the module reads, and it only moves when
// Clock.millis() takes the source's value, so setting s_now alone changes nothing.
static void at_ms(uint32_t t)
{
    s_now = t;
    Clock.millis(Clock.internal);
}

void setUp(void)
{
    at_ms(0);
    Clock.src.fn = test_clock;
    Clock.src.ticks_per_second = 1000u;
    Clock.set_ms(Clock.internal);
}

void tearDown(void)
{
    Clock.src.fn = NULL;
    Clock.src.ticks_per_second = 0u;
    Clock.set_ms(Clock.internal);
}

// Every state's StateCurrent number, as OPC 30050 publishes them.
void test_statecurrent_values_are_the_published_numbers(void)
{
    TEST_ASSERT_EQUAL_INT(0, PACK_ML_STATE_UNDEFINED);
    TEST_ASSERT_EQUAL_INT(1, PACK_ML_STATE_CLEARING);
    TEST_ASSERT_EQUAL_INT(2, PACK_ML_STATE_STOPPED);
    TEST_ASSERT_EQUAL_INT(3, PACK_ML_STATE_STARTING);
    TEST_ASSERT_EQUAL_INT(4, PACK_ML_STATE_IDLE);
    TEST_ASSERT_EQUAL_INT(5, PACK_ML_STATE_SUSPENDED);
    TEST_ASSERT_EQUAL_INT(6, PACK_ML_STATE_EXECUTE);
    TEST_ASSERT_EQUAL_INT(7, PACK_ML_STATE_STOPPING);
    TEST_ASSERT_EQUAL_INT(8, PACK_ML_STATE_ABORTING);
    TEST_ASSERT_EQUAL_INT(9, PACK_ML_STATE_ABORTED);
    TEST_ASSERT_EQUAL_INT(10, PACK_ML_STATE_HOLDING);
    TEST_ASSERT_EQUAL_INT(11, PACK_ML_STATE_HELD);
    TEST_ASSERT_EQUAL_INT(12, PACK_ML_STATE_UNHOLDING);
    TEST_ASSERT_EQUAL_INT(13, PACK_ML_STATE_SUSPENDING);
    TEST_ASSERT_EQUAL_INT(14, PACK_ML_STATE_UNSUSPENDING);
    TEST_ASSERT_EQUAL_INT(15, PACK_ML_STATE_RESETTING);
    TEST_ASSERT_EQUAL_INT(16, PACK_ML_STATE_COMPLETING);
    TEST_ASSERT_EQUAL_INT(17, PACK_ML_STATE_COMPLETE);
}

// The PackTags Command.CntrlCmd numbering.
void test_control_command_numbers(void)
{
    TEST_ASSERT_EQUAL_INT(0, PACK_ML_COMMAND_PROTOCORE_NONE);
    TEST_ASSERT_EQUAL_INT(1, PACK_ML_COMMAND_RESET);
    TEST_ASSERT_EQUAL_INT(2, PACK_ML_COMMAND_START);
    TEST_ASSERT_EQUAL_INT(3, PACK_ML_COMMAND_STOP);
    TEST_ASSERT_EQUAL_INT(4, PACK_ML_COMMAND_HOLD);
    TEST_ASSERT_EQUAL_INT(5, PACK_ML_COMMAND_UNHOLD);
    TEST_ASSERT_EQUAL_INT(6, PACK_ML_COMMAND_SUSPEND);
    TEST_ASSERT_EQUAL_INT(7, PACK_ML_COMMAND_UNSUSPEND);
    TEST_ASSERT_EQUAL_INT(8, PACK_ML_COMMAND_ABORT);
    TEST_ASSERT_EQUAL_INT(9, PACK_ML_COMMAND_CLEAR);
    TEST_ASSERT_EQUAL_INT(1, PACK_ML_MODE_PRODUCING);
    TEST_ASSERT_EQUAL_INT(2, PACK_ML_MODE_MAINTENANCE);
    TEST_ASSERT_EQUAL_INT(3, PACK_ML_MODE_MANUAL);
}

// The production path: Stopped -Reset-> Resetting -SC-> Idle -Start-> Starting -SC-> Execute,
// then the run ends Execute -> Completing -SC-> Complete, and Reset starts the cycle again.
void test_production_path(void)
{
    PackMlState s = PACK_ML_STATE_STOPPED;
    s = protocore_packml_command(s, PACK_ML_COMMAND_RESET);
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_RESETTING, s);
    s = protocore_packml_state_complete(s);
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_IDLE, s);
    s = protocore_packml_command(s, PACK_ML_COMMAND_START);
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_STARTING, s);
    s = protocore_packml_state_complete(s);
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_EXECUTE, s);
    s = protocore_packml_execute_complete(s);
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_COMPLETING, s);
    s = protocore_packml_state_complete(s);
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_COMPLETE, s);
    s = protocore_packml_command(s, PACK_ML_COMMAND_RESET);
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_RESETTING, s);

    // execute_complete only fires from Execute
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_IDLE, protocore_packml_execute_complete(PACK_ML_STATE_IDLE));
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_HELD, protocore_packml_execute_complete(PACK_ML_STATE_HELD));
}

// Execute <-> Holding <-> Held: Hold leaves production, Unhold returns to it.
void test_hold_branch_returns_to_execute(void)
{
    PackMlState s = protocore_packml_command(PACK_ML_STATE_EXECUTE, PACK_ML_COMMAND_HOLD);
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_HOLDING, s);
    s = protocore_packml_state_complete(s);
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_HELD, s);
    s = protocore_packml_command(s, PACK_ML_COMMAND_UNHOLD);
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_UNHOLDING, s);
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_EXECUTE, protocore_packml_state_complete(s));
}

// Execute <-> Suspending <-> Suspended: the same shape, driven by an external condition.
void test_suspend_branch_returns_to_execute(void)
{
    PackMlState s = protocore_packml_command(PACK_ML_STATE_EXECUTE, PACK_ML_COMMAND_SUSPEND);
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_SUSPENDING, s);
    s = protocore_packml_state_complete(s);
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_SUSPENDED, s);
    s = protocore_packml_command(s, PACK_ML_COMMAND_UNSUSPEND);
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_UNSUSPENDING, s);
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_EXECUTE, protocore_packml_state_complete(s));
}

// Abort is the universal fault transition: legal from every state except the abort branch itself.
void test_abort_is_legal_everywhere_but_the_abort_branch(void)
{
    for (int v = PACK_ML_STATE_CLEARING; v <= PACK_ML_STATE_COMPLETE; v++)
    {
        PackMlState s = (PackMlState)v;
        PackMlState got = protocore_packml_command(s, PACK_ML_COMMAND_ABORT);
        if (s == PACK_ML_STATE_ABORTING || s == PACK_ML_STATE_ABORTED)
        {
            TEST_ASSERT_EQUAL_INT_MESSAGE(s, got, protocore_packml_state_name(s));
        }
        else
        {
            TEST_ASSERT_EQUAL_INT_MESSAGE(PACK_ML_STATE_ABORTING, got, protocore_packml_state_name(s));
        }
    }
    // Aborting -SC-> Aborted, and only Clear leaves it: Clearing -SC-> Stopped
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_ABORTED, protocore_packml_state_complete(PACK_ML_STATE_ABORTING));
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_CLEARING,
                          protocore_packml_command(PACK_ML_STATE_ABORTED, PACK_ML_COMMAND_CLEAR));
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_STOPPED, protocore_packml_state_complete(PACK_ML_STATE_CLEARING));
}

// Stop is universal too, minus the abort branch and the states already heading to Stopped.
void test_stop_is_legal_everywhere_but_the_stop_and_abort_branches(void)
{
    for (int v = PACK_ML_STATE_CLEARING; v <= PACK_ML_STATE_COMPLETE; v++)
    {
        PackMlState s = (PackMlState)v;
        proto_bool exempt = (s == PACK_ML_STATE_ABORTING || s == PACK_ML_STATE_ABORTED || s == PACK_ML_STATE_STOPPING ||
                             s == PACK_ML_STATE_STOPPED || s == PACK_ML_STATE_CLEARING);
        PackMlState want = exempt ? s : PACK_ML_STATE_STOPPING;
        TEST_ASSERT_EQUAL_INT_MESSAGE(want, protocore_packml_command(s, PACK_ML_COMMAND_STOP),
                                      protocore_packml_state_name(s));
    }
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_STOPPED, protocore_packml_state_complete(PACK_ML_STATE_STOPPING));
}

// Clear applies only in Aborted, and Reset only from the two run-boundary wait states.
void test_clear_and_reset_are_state_specific(void)
{
    for (int v = PACK_ML_STATE_CLEARING; v <= PACK_ML_STATE_COMPLETE; v++)
    {
        PackMlState s = (PackMlState)v;
        proto_bool want_clear = (s == PACK_ML_STATE_ABORTED);
        proto_bool want_reset = (s == PACK_ML_STATE_STOPPED || s == PACK_ML_STATE_COMPLETE);
        TEST_ASSERT_EQUAL_INT_MESSAGE(want_clear, protocore_packml_command_valid(s, PACK_ML_COMMAND_CLEAR),
                                      protocore_packml_state_name(s));
        TEST_ASSERT_EQUAL_INT_MESSAGE(want_reset, protocore_packml_command_valid(s, PACK_ML_COMMAND_RESET),
                                      protocore_packml_state_name(s));
    }
}

// An illegal command is a no-op, and command_valid says exactly that.
void test_illegal_commands_leave_the_state_unchanged(void)
{
    for (int v = PACK_ML_STATE_CLEARING; v <= PACK_ML_STATE_COMPLETE; v++)
    {
        for (int c = PACK_ML_COMMAND_PROTOCORE_NONE; c <= PACK_ML_COMMAND_CLEAR; c++)
        {
            PackMlState s = (PackMlState)v;
            PackMlCommand cmd = (PackMlCommand)c;
            PackMlState got = protocore_packml_command(s, cmd);
            proto_bool changed = (got != s);
            TEST_ASSERT_EQUAL_INT_MESSAGE(changed, protocore_packml_command_valid(s, cmd),
                                          protocore_packml_command_name(cmd));
        }
    }
    // the null command never moves anything
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_EXECUTE,
                          protocore_packml_command(PACK_ML_STATE_EXECUTE, PACK_ML_COMMAND_PROTOCORE_NONE));
    // Start out of Held is not a transition; Unhold is
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_HELD, protocore_packml_command(PACK_ML_STATE_HELD, PACK_ML_COMMAND_START));
}

// Ten states act and auto-advance; the seven wait states stand still under State-Complete.
void test_acting_states_advance_and_wait_states_do_not(void)
{
    for (int v = PACK_ML_STATE_CLEARING; v <= PACK_ML_STATE_COMPLETE; v++)
    {
        PackMlState s = (PackMlState)v;
        PackMlState after = protocore_packml_state_complete(s);
        if (protocore_packml_is_acting(s))
        {
            TEST_ASSERT_NOT_EQUAL_INT_MESSAGE(s, after, protocore_packml_state_name(s));
        }
        else
        {
            TEST_ASSERT_EQUAL_INT_MESSAGE(s, after, protocore_packml_state_name(s));
        }
    }
    // the seven wait states, named: Stopped, Idle, Suspended, Execute, Aborted, Held, Complete
    TEST_ASSERT_FALSE(protocore_packml_is_acting(PACK_ML_STATE_STOPPED));
    TEST_ASSERT_FALSE(protocore_packml_is_acting(PACK_ML_STATE_IDLE));
    TEST_ASSERT_FALSE(protocore_packml_is_acting(PACK_ML_STATE_SUSPENDED));
    TEST_ASSERT_FALSE(protocore_packml_is_acting(PACK_ML_STATE_EXECUTE));
    TEST_ASSERT_FALSE(protocore_packml_is_acting(PACK_ML_STATE_ABORTED));
    TEST_ASSERT_FALSE(protocore_packml_is_acting(PACK_ML_STATE_HELD));
    TEST_ASSERT_FALSE(protocore_packml_is_acting(PACK_ML_STATE_COMPLETE));
    TEST_ASSERT_FALSE(protocore_packml_is_acting(PACK_ML_STATE_UNDEFINED));
}

// The names are the standard's spellings, and an out-of-range value is named, never null.
void test_names(void)
{
    TEST_ASSERT_EQUAL_STRING("Stopped", protocore_packml_state_name(PACK_ML_STATE_STOPPED));
    TEST_ASSERT_EQUAL_STRING("Execute", protocore_packml_state_name(PACK_ML_STATE_EXECUTE));
    TEST_ASSERT_EQUAL_STRING("Unsuspending", protocore_packml_state_name(PACK_ML_STATE_UNSUSPENDING));
    TEST_ASSERT_EQUAL_STRING("Undefined", protocore_packml_state_name((PackMlState)99));
    TEST_ASSERT_EQUAL_STRING("Start", protocore_packml_command_name(PACK_ML_COMMAND_START));
    TEST_ASSERT_EQUAL_STRING("Unsuspend", protocore_packml_command_name(PACK_ML_COMMAND_UNSUSPEND));
    TEST_ASSERT_EQUAL_STRING("None", protocore_packml_command_name((PackMlCommand)99));
}

// The service starts Stopped in the requested mode, with the counters cleared.
void test_service_initializes_stopped(void)
{
    protocore_packml_svc_init(PACK_ML_MODE_MAINTENANCE);
    PackMlStatus st;
    protocore_packml_svc_status(&st);
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_STOPPED, protocore_packml_svc_state());
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_STOPPED, st.state_current);
    TEST_ASSERT_EQUAL_INT(PACK_ML_MODE_MAINTENANCE, st.unit_mode_current);
    TEST_ASSERT_EQUAL_UINT32(0u, st.prod_processed);
    TEST_ASSERT_EQUAL_UINT32(0u, st.prod_defective);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, st.mach_speed_actual);
}

// The service follows the same transition table, and reports whether the command was legal.
void test_service_follows_the_engine(void)
{
    protocore_packml_svc_init(PACK_ML_MODE_PRODUCING);
    TEST_ASSERT_FALSE(protocore_packml_svc_command(PACK_ML_COMMAND_START)); // not legal in Stopped
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_STOPPED, protocore_packml_svc_state());

    TEST_ASSERT_TRUE(protocore_packml_svc_command(PACK_ML_COMMAND_RESET));
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_RESETTING, protocore_packml_svc_state());
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_IDLE, protocore_packml_svc_state_complete());
    TEST_ASSERT_TRUE(protocore_packml_svc_command(PACK_ML_COMMAND_START));
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_EXECUTE, protocore_packml_svc_state_complete());
    // State-Complete in a wait state is a no-op
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_EXECUTE, protocore_packml_svc_state_complete());
}

// Units are only produced while executing, and a defective one counts in both totals.
void test_service_counts_only_while_executing(void)
{
    protocore_packml_svc_init(PACK_ML_MODE_PRODUCING);
    protocore_packml_svc_count(PROTO_FALSE); // Stopped: ignored
    PackMlStatus st;
    protocore_packml_svc_status(&st);
    TEST_ASSERT_EQUAL_UINT32(0u, st.prod_processed);

    TEST_ASSERT_TRUE(protocore_packml_svc_command(PACK_ML_COMMAND_RESET));
    protocore_packml_svc_state_complete();
    TEST_ASSERT_TRUE(protocore_packml_svc_command(PACK_ML_COMMAND_START));
    protocore_packml_svc_state_complete();
    protocore_packml_svc_count(PROTO_FALSE);
    protocore_packml_svc_count(PROTO_TRUE);
    protocore_packml_svc_count(PROTO_FALSE);
    protocore_packml_svc_status(&st);
    TEST_ASSERT_EQUAL_UINT32(3u, st.prod_processed);
    TEST_ASSERT_EQUAL_UINT32(1u, st.prod_defective);

    // ending the run leaves Execute, so a further unit is not counted
    TEST_ASSERT_TRUE(protocore_packml_svc_complete_run());
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_COMPLETING, protocore_packml_svc_state());
    protocore_packml_svc_count(PROTO_FALSE);
    protocore_packml_svc_status(&st);
    TEST_ASSERT_EQUAL_UINT32(3u, st.prod_processed);
    TEST_ASSERT_FALSE(protocore_packml_svc_complete_run()); // not in Execute any more
}

// A unit-mode change is only accepted in a stable, non-producing state.
void test_service_mode_change_is_restricted(void)
{
    protocore_packml_svc_init(PACK_ML_MODE_PRODUCING);
    TEST_ASSERT_TRUE(protocore_packml_svc_set_mode(PACK_ML_MODE_MANUAL)); // Stopped

    TEST_ASSERT_TRUE(protocore_packml_svc_command(PACK_ML_COMMAND_RESET));
    TEST_ASSERT_FALSE(protocore_packml_svc_set_mode(PACK_ML_MODE_PRODUCING)); // Resetting is acting
    protocore_packml_svc_state_complete();
    TEST_ASSERT_TRUE(protocore_packml_svc_set_mode(PACK_ML_MODE_PRODUCING)); // Idle

    TEST_ASSERT_TRUE(protocore_packml_svc_command(PACK_ML_COMMAND_START));
    protocore_packml_svc_state_complete();
    TEST_ASSERT_FALSE(protocore_packml_svc_set_mode(PACK_ML_MODE_MANUAL)); // Execute is producing

    TEST_ASSERT_TRUE(protocore_packml_svc_command(PACK_ML_COMMAND_ABORT));
    protocore_packml_svc_state_complete();
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_ABORTED, protocore_packml_svc_state());
    TEST_ASSERT_TRUE(protocore_packml_svc_set_mode(PACK_ML_MODE_MAINTENANCE));

    PackMlStatus st;
    protocore_packml_svc_status(&st);
    TEST_ASSERT_EQUAL_INT(PACK_ML_MODE_MAINTENANCE, st.unit_mode_current);
}

// MachSpeedActual is the commanded speed while producing and zero everywhere else.
void test_service_speed_is_reported_only_while_executing(void)
{
    protocore_packml_svc_init(PACK_ML_MODE_PRODUCING);
    protocore_packml_svc_set_speed(120.5f);
    PackMlStatus st;
    protocore_packml_svc_status(&st);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, st.mach_speed_actual);

    TEST_ASSERT_TRUE(protocore_packml_svc_command(PACK_ML_COMMAND_RESET));
    protocore_packml_svc_state_complete();
    TEST_ASSERT_TRUE(protocore_packml_svc_command(PACK_ML_COMMAND_START));
    protocore_packml_svc_state_complete();
    protocore_packml_svc_status(&st);
    TEST_ASSERT_EQUAL_FLOAT(120.5f, st.mach_speed_actual);

    TEST_ASSERT_TRUE(protocore_packml_svc_command(PACK_ML_COMMAND_HOLD));
    protocore_packml_svc_state_complete();
    protocore_packml_svc_status(&st);
    TEST_ASSERT_EQUAL_INT(PACK_ML_STATE_HELD, st.state_current);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, st.mach_speed_actual);
}

// StateCurrentTime counts from the last state change; AccTimeSinceReset counts from the last Reset.
// Both are read off a clock the test steps, so the expected values are subtraction.
void test_service_timers_measure_from_their_own_marks(void)
{
    at_ms(1000);
    protocore_packml_svc_init(PACK_ML_MODE_PRODUCING); // reset_ms = state_entry_ms = 1000

    at_ms(1500);
    PackMlStatus st;
    protocore_packml_svc_status(&st);
    TEST_ASSERT_EQUAL_UINT32(500u, st.state_current_ms);        // 1500 - 1000
    TEST_ASSERT_EQUAL_UINT32(500u, st.acc_time_since_reset_ms); // 1500 - 1000

    at_ms(2000);
    TEST_ASSERT_TRUE(protocore_packml_svc_command(PACK_ML_COMMAND_RESET)); // both marks move to 2000
    at_ms(2300);
    protocore_packml_svc_status(&st);
    TEST_ASSERT_EQUAL_UINT32(300u, st.state_current_ms);
    TEST_ASSERT_EQUAL_UINT32(300u, st.acc_time_since_reset_ms);

    at_ms(2400);
    protocore_packml_svc_state_complete(); // Resetting -> Idle: only the state mark moves
    at_ms(2450);
    protocore_packml_svc_status(&st);
    TEST_ASSERT_EQUAL_UINT32(50u, st.state_current_ms);         // 2450 - 2400
    TEST_ASSERT_EQUAL_UINT32(450u, st.acc_time_since_reset_ms); // 2450 - 2000
}
