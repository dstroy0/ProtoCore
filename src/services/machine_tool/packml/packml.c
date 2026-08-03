// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file packml.c
 * @brief PackML / OMAC state model (ISA-TR88.00.02) - state engine + owned PackTags service. See packml.h.
 */

#include "services/machine_tool/packml/packml.h"

#if PC_ENABLE_PACKML

#include "server/clock/clock.h" // pc_millis - the monotonic source

// ---------------------------------------------------------------------------
// Pure state engine
// ---------------------------------------------------------------------------

PackMlState pc_packml_command(PackMlState s, PackMlCommand c)
{
    // Abort is legal from every state except the abort branch itself -> Aborting.
    if (c == PACK_ML_COMMAND_ABORT && s != PACK_ML_STATE_ABORTING && s != PACK_ML_STATE_ABORTED)
    {
        return PACK_ML_STATE_ABORTING;
    }
    // Stop is legal from every state except the abort branch and the already-stopping/stopped/clearing
    // states -> Stopping.
    if (c == PACK_ML_COMMAND_STOP && s != PACK_ML_STATE_ABORTING && s != PACK_ML_STATE_ABORTED &&
        s != PACK_ML_STATE_STOPPING && s != PACK_ML_STATE_STOPPED && s != PACK_ML_STATE_CLEARING)
    {
        return PACK_ML_STATE_STOPPING;
    }

    switch (s)
    {
    case PACK_ML_STATE_STOPPED:
        if (c == PACK_ML_COMMAND_RESET)
        {
            return PACK_ML_STATE_RESETTING;
        }
        break;
    case PACK_ML_STATE_IDLE:
        if (c == PACK_ML_COMMAND_START)
        {
            return PACK_ML_STATE_STARTING;
        }
        break;
    case PACK_ML_STATE_EXECUTE:
        if (c == PACK_ML_COMMAND_HOLD)
        {
            return PACK_ML_STATE_HOLDING;
        }
        if (c == PACK_ML_COMMAND_SUSPEND)
        {
            return PACK_ML_STATE_SUSPENDING;
        }
        break;
    case PACK_ML_STATE_HELD:
        if (c == PACK_ML_COMMAND_UNHOLD)
        {
            return PACK_ML_STATE_UNHOLDING;
        }
        break;
    case PACK_ML_STATE_SUSPENDED:
        if (c == PACK_ML_COMMAND_UNSUSPEND)
        {
            return PACK_ML_STATE_UNSUSPENDING;
        }
        break;
    case PACK_ML_STATE_COMPLETE:
        if (c == PACK_ML_COMMAND_RESET)
        {
            return PACK_ML_STATE_RESETTING;
        }
        break;
    case PACK_ML_STATE_ABORTED:
        if (c == PACK_ML_COMMAND_CLEAR)
        {
            return PACK_ML_STATE_CLEARING;
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
    case PACK_ML_STATE_RESETTING:
        return PACK_ML_STATE_IDLE;
    case PACK_ML_STATE_STARTING:
        return PACK_ML_STATE_EXECUTE;
    case PACK_ML_STATE_HOLDING:
        return PACK_ML_STATE_HELD;
    case PACK_ML_STATE_UNHOLDING:
        return PACK_ML_STATE_EXECUTE;
    case PACK_ML_STATE_SUSPENDING:
        return PACK_ML_STATE_SUSPENDED;
    case PACK_ML_STATE_UNSUSPENDING:
        return PACK_ML_STATE_EXECUTE;
    case PACK_ML_STATE_COMPLETING:
        return PACK_ML_STATE_COMPLETE;
    case PACK_ML_STATE_STOPPING:
        return PACK_ML_STATE_STOPPED;
    case PACK_ML_STATE_ABORTING:
        return PACK_ML_STATE_ABORTED;
    case PACK_ML_STATE_CLEARING:
        return PACK_ML_STATE_STOPPED;
    default:
        return s; // wait states have no State-Complete transition
    }
}

PackMlState pc_packml_execute_complete(PackMlState s)
{
    return (s == PACK_ML_STATE_EXECUTE) ? PACK_ML_STATE_COMPLETING : s;
}

proto_bool pc_packml_is_acting(PackMlState s)
{
    switch (s)
    {
    case PACK_ML_STATE_CLEARING:
    case PACK_ML_STATE_STARTING:
    case PACK_ML_STATE_STOPPING:
    case PACK_ML_STATE_ABORTING:
    case PACK_ML_STATE_HOLDING:
    case PACK_ML_STATE_UNHOLDING:
    case PACK_ML_STATE_SUSPENDING:
    case PACK_ML_STATE_UNSUSPENDING:
    case PACK_ML_STATE_RESETTING:
    case PACK_ML_STATE_COMPLETING:
        return PROTO_TRUE;
    default:
        return PROTO_FALSE;
    }
}

proto_bool pc_packml_command_valid(PackMlState s, PackMlCommand c)
{
    return pc_packml_command(s, c) != s;
}

const char *pc_packml_state_name(PackMlState s)
{
    switch (s)
    {
    case PACK_ML_STATE_CLEARING:
        return "Clearing";
    case PACK_ML_STATE_STOPPED:
        return "Stopped";
    case PACK_ML_STATE_STARTING:
        return "Starting";
    case PACK_ML_STATE_IDLE:
        return "Idle";
    case PACK_ML_STATE_SUSPENDED:
        return "Suspended";
    case PACK_ML_STATE_EXECUTE:
        return "Execute";
    case PACK_ML_STATE_STOPPING:
        return "Stopping";
    case PACK_ML_STATE_ABORTING:
        return "Aborting";
    case PACK_ML_STATE_ABORTED:
        return "Aborted";
    case PACK_ML_STATE_HOLDING:
        return "Holding";
    case PACK_ML_STATE_HELD:
        return "Held";
    case PACK_ML_STATE_UNHOLDING:
        return "Unholding";
    case PACK_ML_STATE_SUSPENDING:
        return "Suspending";
    case PACK_ML_STATE_UNSUSPENDING:
        return "Unsuspending";
    case PACK_ML_STATE_RESETTING:
        return "Resetting";
    case PACK_ML_STATE_COMPLETING:
        return "Completing";
    case PACK_ML_STATE_COMPLETE:
        return "Complete";
    default:
        return "Undefined";
    }
}

const char *pc_packml_command_name(PackMlCommand c)
{
    switch (c)
    {
    case PACK_ML_COMMAND_RESET:
        return "Reset";
    case PACK_ML_COMMAND_START:
        return "Start";
    case PACK_ML_COMMAND_STOP:
        return "Stop";
    case PACK_ML_COMMAND_HOLD:
        return "Hold";
    case PACK_ML_COMMAND_UNHOLD:
        return "Unhold";
    case PACK_ML_COMMAND_SUSPEND:
        return "Suspend";
    case PACK_ML_COMMAND_UNSUSPEND:
        return "Unsuspend";
    case PACK_ML_COMMAND_ABORT:
        return "Abort";
    case PACK_ML_COMMAND_CLEAR:
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
typedef struct
{
    PackMlState state;
    PackMlMode mode;
    float mach_speed_cmd;
    uint32_t prod_processed;
    uint32_t prod_defective;
    uint32_t reset_ms;       // pc_millis at the last Reset -> AccTimeSinceReset base
    uint32_t state_entry_ms; // pc_millis at the last state change -> StateCurrentTime base
} PackMlSvcCtx;
// mode is the only member whose default is not the zero fill: PACK_ML_MODE_PRODUCING is 1, and
// nothing sets it before a read (pc_packml_set_mode is a caller-driven setter).
// PACK_ML_STATE_UNDEFINED is 0, so state needs no initializer.
static PackMlSvcCtx s_pml = {
    .mode = PACK_ML_MODE_PRODUCING,
};

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
    s_pml.state = PACK_ML_STATE_UNDEFINED; // force enter_state to stamp the entry time
    enter_state(PACK_ML_STATE_STOPPED);
}

proto_bool pc_packml_svc_command(PackMlCommand c)
{
    PackMlState next = pc_packml_command(s_pml.state, c);
    if (next == s_pml.state)
    {
        return PROTO_FALSE;
    }
    if (c == PACK_ML_COMMAND_RESET) // a fresh run: restart the accumulated-time clock
    {
        s_pml.reset_ms = pc_millis();
    }
    enter_state(next);
    return PROTO_TRUE;
}

PackMlState pc_packml_svc_state_complete(void)
{
    enter_state(pc_packml_state_complete(s_pml.state));
    return s_pml.state;
}

void pc_packml_svc_count(proto_bool defective)
{
    if (s_pml.state != PACK_ML_STATE_EXECUTE)
    {
        return; // units are only produced while executing
    }
    s_pml.prod_processed++;
    if (defective)
    {
        s_pml.prod_defective++;
    }
}

proto_bool pc_packml_svc_complete_run(void)
{
    PackMlState next = pc_packml_execute_complete(s_pml.state);
    if (next == s_pml.state)
    {
        return PROTO_FALSE;
    }
    enter_state(next);
    return PROTO_TRUE;
}

proto_bool pc_packml_svc_set_mode(PackMlMode mode)
{
    // A unit-mode change is only allowed in a stable, non-producing state (ISA-TR88.00.02 mode-change rules).
    if (s_pml.state != PACK_ML_STATE_STOPPED && s_pml.state != PACK_ML_STATE_IDLE &&
        s_pml.state != PACK_ML_STATE_ABORTED)
    {
        return PROTO_FALSE;
    }
    s_pml.mode = mode;
    return PROTO_TRUE;
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
    out->mach_speed_actual = (s_pml.state == PACK_ML_STATE_EXECUTE) ? s_pml.mach_speed_cmd : 0.0f;
    out->state_current_ms = now - s_pml.state_entry_ms;
    out->acc_time_since_reset_ms = now - s_pml.reset_ms;
    out->prod_processed = s_pml.prod_processed;
    out->prod_defective = s_pml.prod_defective;
}

#endif // PC_ENABLE_PACKML
