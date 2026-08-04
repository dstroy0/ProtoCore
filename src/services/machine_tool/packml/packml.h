// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file packml.h
 * @brief PackML / OMAC packaging-machine state model (ISA-TR88.00.02) - the state engine + PackTags.
 *
 * Reference standard: ISA-TR88.00.02-2022, "Machine and Unit States: An Implementation Example of ISA-88"
 * (ISBN 978-1-64331-224-8) - the OMAC PackML state model and PackTags. The 17-state model, the numeric
 * StateCurrent values, the command set, and the transition graph below follow that document.
 *
 * PackML is the OMAC / ISA-88 state model that packaging and process machines expose so a line
 * controller (usually over OPC UA) can command and observe them uniformly. Two pieces:
 *
 *  - the **state machine**: 17 states (Stopped/Idle/Execute/Held/Suspended/Complete/Aborted plus the
 *    transient "acting" states Starting/Resetting/Holding/... that auto-advance when their action
 *    finishes) driven by the control commands (Reset/Start/Stop/Hold/Unhold/Suspend/Unsuspend/Abort/
 *    Clear). Stop and Abort are the near-universal transitions to the shutdown / fault branches.
 *  - the **PackTags**: the standard Command / Status / Admin tag groups the model surfaces - the current
 *    state + unit mode, the requested/actual machine speed, and the production counters.
 *
 * This is a pure, fixed-BSS service: no heap, no I/O. The state engine (pc_packml_command /
 * pc_packml_state_complete) is a pure transition table; the owned service (pc_packml_svc_*) layers the
 * PackTags + counters + timers on top for a machine to run directly. Host-tested (native_packml).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PACKML_H
#define PROTOCORE_PACKML_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_PACKML

/**
 * @brief PackML unit / machine state. The underlying value is the ISA-TR88.00.02 StateCurrent wire number
 *        (1..17), so casting to the byte a PackTags Status.StateCurrent carries is a no-op at the boundary.
 */
typedef enum PROTO_ENUM_PACKED
{
    PACK_ML_STATE_UNDEFINED = 0,     ///< not yet initialized
    PACK_ML_STATE_CLEARING = 1,      ///< acting: clearing a fault (Aborted -> Stopped)
    PACK_ML_STATE_STOPPED = 2,       ///< wait: powered, safe, not producing
    PACK_ML_STATE_STARTING = 3,      ///< acting: Idle -> Execute
    PACK_ML_STATE_IDLE = 4,          ///< wait: ready to start
    PACK_ML_STATE_SUSPENDED = 5,     ///< wait: paused by an external condition (starved/blocked)
    PACK_ML_STATE_EXECUTE = 6,       ///< wait/producing: the machine is doing its work
    PACK_ML_STATE_STOPPING = 7,      ///< acting: -> Stopped
    PACK_ML_STATE_ABORTING = 8,      ///< acting: -> Aborted (fault)
    PACK_ML_STATE_ABORTED = 9,       ///< wait: faulted; needs Clear
    PACK_ML_STATE_HOLDING = 10,      ///< acting: Execute -> Held
    PACK_ML_STATE_HELD = 11,         ///< wait: production paused by the operator
    PACK_ML_STATE_UNHOLDING = 12,    ///< acting: Held -> Execute
    PACK_ML_STATE_SUSPENDING = 13,   ///< acting: Execute -> Suspended
    PACK_ML_STATE_UNSUSPENDING = 14, ///< acting: Suspended -> Execute
    PACK_ML_STATE_RESETTING = 15,    ///< acting: Stopped/Complete -> Idle
    PACK_ML_STATE_COMPLETING = 16,   ///< acting: Execute (production done) -> Complete
    PACK_ML_STATE_COMPLETE = 17      ///< wait: the production run finished
} PackMlState;

/** @brief PackML control command (the Command.CntrlCmd tag). Value is the conventional CntrlCmd number. */
typedef enum PROTO_ENUM_PACKED
{
    PACK_ML_COMMAND_PC_NONE = 0,
    PACK_ML_COMMAND_RESET = 1,
    PACK_ML_COMMAND_START = 2,
    PACK_ML_COMMAND_STOP = 3,
    PACK_ML_COMMAND_HOLD = 4,
    PACK_ML_COMMAND_UNHOLD = 5,
    PACK_ML_COMMAND_SUSPEND = 6,
    PACK_ML_COMMAND_UNSUSPEND = 7,
    PACK_ML_COMMAND_ABORT = 8,
    PACK_ML_COMMAND_CLEAR = 9
} PackMlCommand;

/** @brief PackML unit mode (Command.UnitMode / Status.UnitModeCurrent). Producing is the full state model. */
typedef enum PROTO_ENUM_PACKED
{
    PACK_ML_MODE_PRODUCING = 1,
    PACK_ML_MODE_MAINTENANCE = 2,
    PACK_ML_MODE_MANUAL = 3
} PackMlMode;

// ---------------------------------------------------------------------------
// Pure state engine (no state stored here - the caller holds the current state)
// ---------------------------------------------------------------------------

/**
 * @brief Apply a control command to a state and return the resulting state.
 *
 * Stop and Abort are the universal transitions (Abort from every state except the abort branch; Stop from
 * every state except the abort branch and the already-stopping/stopped/clearing states). Otherwise only the
 * command valid in @p s per the model transitions; an invalid command returns @p s unchanged.
 */
PackMlState pc_packml_command(PackMlState s, PackMlCommand c);

/**
 * @brief Advance an "acting" (transient) state to its target once its action completes (the SC / State
 *        Complete transition, e.g. Starting -> Execute, Aborting -> Aborted). A "wait" state is returned
 *        unchanged - only acting states auto-advance.
 */
PackMlState pc_packml_state_complete(PackMlState s);

/** @brief The production cycle finished: Execute -> Completing. Any other state is returned unchanged. */
PackMlState pc_packml_execute_complete(PackMlState s);

/** @brief True if @p s is an acting (transient, auto-advancing) state rather than a wait (stable) state. */
proto_bool pc_packml_is_acting(PackMlState s);

/** @brief True if @p c is a legal command in @p s (i.e. pc_packml_command would change the state). */
proto_bool pc_packml_command_valid(PackMlState s, PackMlCommand c);

/** @brief Human-readable state name (e.g. "Execute") for an HMI / log. Never null. */
const char *pc_packml_state_name(PackMlState s);

/** @brief Human-readable command name (e.g. "Start"). Never null. */
const char *pc_packml_command_name(PackMlCommand c);

// ---------------------------------------------------------------------------
// Owned service: the PackTags + counters + timers a machine runs directly (fixed BSS, one owner)
// ---------------------------------------------------------------------------

/** @brief Status PackTags snapshot (Status.*). */
typedef struct
{
    PackMlState state_current;        ///< Status.StateCurrent
    PackMlMode unit_mode_current;     ///< Status.UnitModeCurrent
    float mach_speed_actual;          ///< Status.MachSpeedActual
    uint32_t state_current_ms;        ///< time in the current state (ms), Admin.StateCurrentTime
    uint32_t acc_time_since_reset_ms; ///< Admin.AccTimeSinceReset
    uint32_t prod_processed;          ///< Admin.ProdProcessedCount
    uint32_t prod_defective;          ///< Admin.ProdDefectiveCount
} PackMlStatus;

/** @brief Initialize the service: state = Stopped, unit mode @p mode, counters/speed cleared. */
void pc_packml_svc_init(PackMlMode mode);

/**
 * @brief Apply a control command (Command.CntrlCmd) to the running service.
 * @return true if the command was legal in the current state and the state advanced.
 */
proto_bool pc_packml_svc_command(PackMlCommand c);

/**
 * @brief Signal that the current acting state's action has finished (the machine's State-Complete). Advances
 *        an acting state to its target; a no-op in a wait state.
 * @return the new state.
 */
PackMlState pc_packml_svc_state_complete(void);

/**
 * @brief Signal one production unit finished while in Execute: increments ProdProcessedCount (and, if
 *        @p defective, ProdDefectiveCount). Does not itself leave Execute (call pc_packml_svc_complete_run
 *        to end the run).
 */
void pc_packml_svc_count(proto_bool defective);

/** @brief End the production run: Execute -> Completing (then State-Complete carries it to Complete). */
proto_bool pc_packml_svc_complete_run(void);

/** @brief Request a unit-mode change. Allowed only in a stable, non-producing state (Stopped/Idle/Aborted). */
proto_bool pc_packml_svc_set_mode(PackMlMode mode);

/** @brief Set the commanded machine speed (Command.MachSpeed); reflected as MachSpeedActual while in Execute. */
void pc_packml_svc_set_speed(float mach_speed);

/** @brief Current state. */
PackMlState pc_packml_svc_state(void);

/** @brief Fill @p out with the current Status/Admin tag snapshot. */
void pc_packml_svc_status(PackMlStatus *out);

#endif // PC_ENABLE_PACKML

PROTO_END_DECLS

#endif // PROTOCORE_PACKML_H
