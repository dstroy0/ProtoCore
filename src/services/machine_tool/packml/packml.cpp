// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file packml.cpp
 * @brief PackML / OMAC state model (ISA-TR88.00.02) - state engine + owned PackTags service. See packml.h.
 */

#include "services/machine_tool/packml/packml.h"

#if PC_ENABLE_PACKML

#include "services/system/clock.h" // pc_millis - the monotonic source

// ---------------------------------------------------------------------------
// Pure state engine
// ---------------------------------------------------------------------------

PackMlState pc_packml_command(PackMlState s, PackMlCommand c)
{
    // Abort is legal from every state except the abort branch itself -> Aborting.
    if (c == PackMlCommand::ABORT && s != PackMlState::ABORTING && s != PackMlState::ABORTED)
    {
        return PackMlState::ABORTING;
    }
    // Stop is legal from every state except the abort branch and the already-stopping/stopped/clearing
    // states -> Stopping.
    if (c == PackMlCommand::STOP && s != PackMlState::ABORTING && s != PackMlState::ABORTED &&
        s != PackMlState::STOPPING && s != PackMlState::STOPPED && s != PackMlState::CLEARING)
    {
        return PackMlState::STOPPING;
    }

    switch (s)
    {
    case PackMlState::STOPPED:
        if (c == PackMlCommand::RESET)
        {
            return PackMlState::RESETTING;
        }
        break;
    case PackMlState::IDLE:
        if (c == PackMlCommand::START)
        {
            return PackMlState::STARTING;
        }
        break;
    case PackMlState::EXECUTE:
        if (c == PackMlCommand::HOLD)
        {
            return PackMlState::HOLDING;
        }
        if (c == PackMlCommand::SUSPEND)
        {
            return PackMlState::SUSPENDING;
        }
        break;
    case PackMlState::HELD:
        if (c == PackMlCommand::UNHOLD)
        {
            return PackMlState::UNHOLDING;
        }
        break;
    case PackMlState::SUSPENDED:
        if (c == PackMlCommand::UNSUSPEND)
        {
            return PackMlState::UNSUSPENDING;
        }
        break;
    case PackMlState::COMPLETE:
        if (c == PackMlCommand::RESET)
        {
            return PackMlState::RESETTING;
        }
        break;
    case PackMlState::ABORTED:
        if (c == PackMlCommand::CLEAR)
        {
            return PackMlState::CLEARING;
        }
        break;
    default:
        break; // acting states accept only the Stop/Abort handled above
    }
    return s; // command not legal in this state: no transition
}

PackMlState pc_packml_state_complete(PackMlState s)
{
    switch (s)
    {
    case PackMlState::RESETTING:
        return PackMlState::IDLE;
    case PackMlState::STARTING:
        return PackMlState::EXECUTE;
    case PackMlState::HOLDING:
        return PackMlState::HELD;
    case PackMlState::UNHOLDING:
        return PackMlState::EXECUTE;
    case PackMlState::SUSPENDING:
        return PackMlState::SUSPENDED;
    case PackMlState::UNSUSPENDING:
        return PackMlState::EXECUTE;
    case PackMlState::COMPLETING:
        return PackMlState::COMPLETE;
    case PackMlState::STOPPING:
        return PackMlState::STOPPED;
    case PackMlState::ABORTING:
        return PackMlState::ABORTED;
    case PackMlState::CLEARING:
        return PackMlState::STOPPED;
    default:
        return s; // wait states have no State-Complete transition
    }
}

PackMlState pc_packml_execute_complete(PackMlState s)
{
    return (s == PackMlState::EXECUTE) ? PackMlState::COMPLETING : s;
}

bool pc_packml_is_acting(PackMlState s)
{
    switch (s)
    {
    case PackMlState::CLEARING:
    case PackMlState::STARTING:
    case PackMlState::STOPPING:
    case PackMlState::ABORTING:
    case PackMlState::HOLDING:
    case PackMlState::UNHOLDING:
    case PackMlState::SUSPENDING:
    case PackMlState::UNSUSPENDING:
    case PackMlState::RESETTING:
    case PackMlState::COMPLETING:
        return true;
    default:
        return false;
    }
}

bool pc_packml_command_valid(PackMlState s, PackMlCommand c)
{
    return pc_packml_command(s, c) != s;
}

const char *pc_packml_state_name(PackMlState s)
{
    switch (s)
    {
    case PackMlState::CLEARING:
        return "Clearing";
    case PackMlState::STOPPED:
        return "Stopped";
    case PackMlState::STARTING:
        return "Starting";
    case PackMlState::IDLE:
        return "Idle";
    case PackMlState::SUSPENDED:
        return "Suspended";
    case PackMlState::EXECUTE:
        return "Execute";
    case PackMlState::STOPPING:
        return "Stopping";
    case PackMlState::ABORTING:
        return "Aborting";
    case PackMlState::ABORTED:
        return "Aborted";
    case PackMlState::HOLDING:
        return "Holding";
    case PackMlState::HELD:
        return "Held";
    case PackMlState::UNHOLDING:
        return "Unholding";
    case PackMlState::SUSPENDING:
        return "Suspending";
    case PackMlState::UNSUSPENDING:
        return "Unsuspending";
    case PackMlState::RESETTING:
        return "Resetting";
    case PackMlState::COMPLETING:
        return "Completing";
    case PackMlState::COMPLETE:
        return "Complete";
    default:
        return "Undefined";
    }
}

const char *pc_packml_command_name(PackMlCommand c)
{
    switch (c)
    {
    case PackMlCommand::RESET:
        return "Reset";
    case PackMlCommand::START:
        return "Start";
    case PackMlCommand::STOP:
        return "Stop";
    case PackMlCommand::HOLD:
        return "Hold";
    case PackMlCommand::UNHOLD:
        return "Unhold";
    case PackMlCommand::SUSPEND:
        return "Suspend";
    case PackMlCommand::UNSUSPEND:
        return "Unsuspend";
    case PackMlCommand::ABORT:
        return "Abort";
    case PackMlCommand::CLEAR:
        return "Clear";
    default:
        return "None";
    }
}

// ---------------------------------------------------------------------------
// Owned service (one named owner, internal linkage)
// ---------------------------------------------------------------------------

// All PackML service state - the current state, unit mode, commanded speed, the production counters, and the
// state/reset timestamps - grouped in one owner (BSS, no heap), unreachable from any other translation unit.
struct PackMlSvcCtx
{
    PackMlState state = PackMlState::UNDEFINED;
    PackMlMode mode = PackMlMode::PRODUCING;
    float mach_speed_cmd = 0.0f;
    uint32_t prod_processed = 0;
    uint32_t prod_defective = 0;
    uint32_t reset_ms = 0;       // pc_millis at the last Reset -> AccTimeSinceReset base
    uint32_t state_entry_ms = 0; // pc_millis at the last state change -> StateCurrentTime base
};
static PackMlSvcCtx s_pml;

static void enter_state(PackMlState s)
{
    if (s == s_pml.state)
    {
        return;
    }
    s_pml.state = s;
    s_pml.state_entry_ms = pc_millis();
}

void pc_packml_svc_init(PackMlMode mode)
{
    s_pml.mode = mode;
    s_pml.mach_speed_cmd = 0.0f;
    s_pml.prod_processed = 0;
    s_pml.prod_defective = 0;
    s_pml.reset_ms = pc_millis();
    s_pml.state = PackMlState::UNDEFINED; // force enter_state to stamp the entry time
    enter_state(PackMlState::STOPPED);
}

bool pc_packml_svc_command(PackMlCommand c)
{
    PackMlState next = pc_packml_command(s_pml.state, c);
    if (next == s_pml.state)
    {
        return false;
    }
    if (c == PackMlCommand::RESET) // a fresh run: restart the accumulated-time clock
    {
        s_pml.reset_ms = pc_millis();
    }
    enter_state(next);
    return true;
}

PackMlState pc_packml_svc_state_complete(void)
{
    enter_state(pc_packml_state_complete(s_pml.state));
    return s_pml.state;
}

void pc_packml_svc_count(bool defective)
{
    if (s_pml.state != PackMlState::EXECUTE)
    {
        return; // units are only produced while executing
    }
    s_pml.prod_processed++;
    if (defective)
    {
        s_pml.prod_defective++;
    }
}

bool pc_packml_svc_complete_run(void)
{
    PackMlState next = pc_packml_execute_complete(s_pml.state);
    if (next == s_pml.state)
    {
        return false;
    }
    enter_state(next);
    return true;
}

bool pc_packml_svc_set_mode(PackMlMode mode)
{
    // A unit-mode change is only allowed in a stable, non-producing state (ISA-TR88.00.02 mode-change rules).
    if (s_pml.state != PackMlState::STOPPED && s_pml.state != PackMlState::IDLE && s_pml.state != PackMlState::ABORTED)
    {
        return false;
    }
    s_pml.mode = mode;
    return true;
}

void pc_packml_svc_set_speed(float mach_speed)
{
    s_pml.mach_speed_cmd = mach_speed;
}

PackMlState pc_packml_svc_state(void)
{
    return s_pml.state;
}

void pc_packml_svc_status(PackMlStatus *out)
{
    if (!out)
    {
        return;
    }
    uint32_t now = pc_millis();
    out->state_current = s_pml.state;
    out->unit_mode_current = s_pml.mode;
    // MachSpeedActual is the commanded speed while producing, otherwise zero.
    out->mach_speed_actual = (s_pml.state == PackMlState::EXECUTE) ? s_pml.mach_speed_cmd : 0.0f;
    out->state_current_ms = now - s_pml.state_entry_ms;
    out->acc_time_since_reset_ms = now - s_pml.reset_ms;
    out->prod_processed = s_pml.prod_processed;
    out->prod_defective = s_pml.prod_defective;
}

#endif // PC_ENABLE_PACKML
