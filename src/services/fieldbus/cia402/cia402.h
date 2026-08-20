// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cia402.h
 * @brief CiA 402 / IEC 61800-7-201 drive + motion profile (PROTOCORE_ENABLE_CIA402) over CANopen.
 *
 * The standardized servo / stepper drive profile: the power state machine (Controlword 0x6040 /
 * Statusword 0x6041), the Modes of Operation, and the target/actual position-velocity-torque
 * objects. This is the pure profile layer - the state decode + controlword commands are just
 * value logic, and the setters/getters wrap the shipped `services/canopen` SDO / PDO codec, so
 * the CAN stack (ESP32 TWAI or an MCP2515) becomes a motion master. Close the loop with a
 * `services/control` PID.
 *
 * Statusword state masks, Controlword command values, and the object indices are verified against
 * IEC 61800-7-201 (CiA 402) and multiple drive vendors' state-machine tables. Pure, host-tested.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CIA402_H
#define PROTOCORE_CIA402_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_CIA402

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

// --- object dictionary indices (sub-index 0 unless noted); the comment gives the CANopen type ---
#define CIA402_OD_ERROR_CODE 0x603Fu         ///< u16   last error code
#define CIA402_OD_CONTROLWORD 0x6040u        ///< u16   command word (drives the state machine)
#define CIA402_OD_STATUSWORD 0x6041u         ///< u16   status word (reports the state)
#define CIA402_OD_QUICK_STOP_OPTION 0x605Au  ///< i16   quick-stop option code
#define CIA402_OD_MODES_OF_OPERATION 0x6060u ///< i8    requested mode
#define CIA402_OD_MODES_DISPLAY 0x6061u      ///< i8    active mode (read-back)
#define CIA402_OD_POSITION_ACTUAL 0x6064u    ///< i32   position actual value
#define CIA402_OD_VELOCITY_ACTUAL 0x606Cu    ///< i32   velocity actual value
#define CIA402_OD_TARGET_TORQUE 0x6071u      ///< i16   target torque (per-mille of rated)
#define CIA402_OD_TORQUE_ACTUAL 0x6077u      ///< i16   torque actual value
#define CIA402_OD_TARGET_POSITION 0x607Au    ///< i32   target position (PP / CSP)
#define CIA402_OD_PROFILE_VELOCITY 0x6081u   ///< u32   profile velocity (PP)
#define CIA402_OD_PROFILE_ACCEL 0x6083u      ///< u32   profile acceleration
#define CIA402_OD_PROFILE_DECEL 0x6084u      ///< u32   profile deceleration
#define CIA402_OD_TARGET_VELOCITY 0x60FFu    ///< i32   target velocity (PV / CSV)
#define CIA402_OD_SUPPORTED_MODES 0x6502u    ///< u32   supported drive modes bitfield

/// Controlword bit masks (object 0x6040).
#define CIA402_CW_SWITCH_ON 0x0001
#define CIA402_CW_ENABLE_VOLTAGE 0x0002
#define CIA402_CW_QUICK_STOP 0x0004 ///< active-low: 0 requests quick stop
#define CIA402_CW_ENABLE_OPERATION 0x0008
#define CIA402_CW_FAULT_RESET 0x0080 ///< acts on the rising edge
#define CIA402_CW_HALT 0x0100

/// Statusword bit masks (object 0x6041).
#define CIA402_SW_READY_TO_SWITCH_ON 0x0001
#define CIA402_SW_SWITCHED_ON 0x0002
#define CIA402_SW_OPERATION_ENABLED 0x0004
#define CIA402_SW_FAULT 0x0008
#define CIA402_SW_VOLTAGE_ENABLED 0x0010
#define CIA402_SW_QUICK_STOP 0x0020 ///< 0 = quick stop active
#define CIA402_SW_SWITCH_ON_DISABLED 0x0040
#define CIA402_SW_WARNING 0x0080
#define CIA402_SW_REMOTE 0x0200
#define CIA402_SW_TARGET_REACHED 0x0400
#define CIA402_SW_INTERNAL_LIMIT 0x0800

/// @return true if the Statusword's Target Reached flag (bit 10) is set.
static inline proto_bool protocore_cia402_target_reached(uint16_t sw)
{
    return (sw & CIA402_SW_TARGET_REACHED) != 0;
}
/// @return true if the drive reports a fault (bit 3).
static inline proto_bool protocore_cia402_has_fault(uint16_t sw)
{
    return (sw & CIA402_SW_FAULT) != 0;
}
/// @return true if a warning is present (bit 7).
static inline proto_bool protocore_cia402_warning(uint16_t sw)
{
    return (sw & CIA402_SW_WARNING) != 0;
}
/// @return true if the drive's power stage voltage is applied (bit 4).
static inline proto_bool protocore_cia402_voltage_enabled(uint16_t sw)
{
    return (sw & CIA402_SW_VOLTAGE_ENABLED) != 0;
}
/// @return true if the drive follows the Controlword (bit 9 remote).
static inline proto_bool protocore_cia402_remote(uint16_t sw)
{
    return (sw & CIA402_SW_REMOTE) != 0;
}
/// @return true if a set-point was internally limited (bit 11).
static inline proto_bool protocore_cia402_internal_limit(uint16_t sw)
{
    return (sw & CIA402_SW_INTERNAL_LIMIT) != 0;
}

typedef enum PROTO_ENUM_PACKED
{
    CIA402_MODE_NO_MODE = 0,
    CIA402_MODE_PROFILE_POSITION = 1,      ///< PP
    CIA402_MODE_VELOCITY = 2,              ///< VL (frequency-converter CIA402_MODE_VELOCITY)
    CIA402_MODE_PROFILE_VELOCITY = 3,      ///< PV
    CIA402_MODE_PROFILE_TORQUE = 4,        ///< TQ
    CIA402_MODE_HOMING = 6,                ///< HM
    CIA402_MODE_INTERPOLATED_POSITION = 7, ///< IP
    CIA402_MODE_CYCLIC_SYNC_POSITION = 8,  ///< CSP
    CIA402_MODE_CYCLIC_SYNC_VELOCITY = 9,  ///< CSV
    CIA402_MODE_CYCLIC_SYNC_TORQUE = 10,   ///< CST
} Cia402Mode;

typedef enum PROTO_ENUM_PACKED
{
    CIA402_STATE_NOT_READY_TO_SWITCH_ON,
    CIA402_STATE_SWITCH_ON_DISABLED,
    CIA402_STATE_READY_TO_SWITCH_ON,
    CIA402_STATE_SWITCHED_ON,
    CIA402_STATE_OPERATION_ENABLED,
    CIA402_STATE_QUICK_STOP_ACTIVE,
    CIA402_STATE_FAULT_REACTION_ACTIVE,
    CIA402_STATE_FAULT,
    CIA402_STATE_UNKNOWN, ///< Statusword matched no defined state
} Cia402State;

typedef enum PROTO_ENUM_PACKED
{
    CIA402_COMMAND_SHUTDOWN,          ///< -> Ready to switch on
    CIA402_COMMAND_SWITCH_ON,         ///< -> Switched on
    CIA402_COMMAND_ENABLE_OPERATION,  ///< -> Operation enabled
    CIA402_COMMAND_DISABLE_VOLTAGE,   ///< -> Switch on disabled
    CIA402_COMMAND_QUICK_STOP,        ///< -> Quick stop active
    CIA402_COMMAND_DISABLE_OPERATION, ///< -> Switched on
    CIA402_COMMAND_FAULT_RESET,       ///< clear a fault (rising edge of bit 7)
} Cia402Command;

#include "shared/can/can.h" // CanFrame: the type a parameter points at

/** @brief What state takes: statusword. */
typedef struct
{
    uint16_t statusword;
} Cia402StateArgs;
/** @brief What controlword takes: cmd. */
typedef struct
{
    Cia402Command cmd;
} Cia402ControlwordArgs;
/** @brief What enable_sequence takes: state. */
typedef struct
{
    Cia402State state;
} Cia402EnableSequenceArgs;
/** @brief What sdo_set_controlword takes: out, node, controlword. */
typedef struct
{
    CanFrame *out;
    uint8_t node;
    uint16_t controlword;
} Cia402SdoSetControlwordArgs;
/** @brief What sdo_set_mode takes: out, node, mode. */
typedef struct
{
    CanFrame *out;
    uint8_t node;
    Cia402Mode mode;
} Cia402SdoSetModeArgs;
/** @brief What sdo_set_target_position takes: out, node, position. */
typedef struct
{
    CanFrame *out;
    uint8_t node;
    int32_t position;
} Cia402SdoSetTargetPositionArgs;
/** @brief What sdo_set_target_velocity takes: out, node, velocity. */
typedef struct
{
    CanFrame *out;
    uint8_t node;
    int32_t velocity;
} Cia402SdoSetTargetVelocityArgs;
/** @brief What sdo_set_target_torque takes: out, node, torque. */
typedef struct
{
    CanFrame *out;
    uint8_t node;
    int16_t torque;
} Cia402SdoSetTargetTorqueArgs;
/** @brief What sdo_read takes: out, node, index, sub. */
typedef struct
{
    CanFrame *out;
    uint8_t node;
    uint16_t index;
    uint8_t sub;
} Cia402SdoReadArgs;
/** @brief What sdo_get_u16 takes: f, want_index, value. */
typedef struct
{
    const CanFrame *f;
    uint16_t want_index;
    uint16_t *value;
} Cia402SdoGetU16Args;
/** @brief What sdo_get_i32 takes: f, want_index, value. */
typedef struct
{
    const CanFrame *f;
    uint16_t want_index;
    int32_t *value;
} Cia402SdoGetI32Args;
/** @brief What pack_command takes: buf, cap, controlword, target. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint16_t controlword;
    int32_t target;
} Cia402PackCommandArgs;
/** @brief What unpack_status takes: buf, len, statusword, actual. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    uint16_t *statusword;
    int32_t *actual;
} Cia402UnpackStatusArgs;
/**
 * @brief CiA 402 / IEC 61800-7-201 drive + motion profile (PROTOCORE_ENABLE_CIA402) over CANopen.
 *
 * A caller sets the members a call takes, invokes it through ::Cia402 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Cia402.state_args.statusword = ...;
 *   Cia402.state(work);
 *   // Cia402.value is what the call reports
 *
 * @var Cia402Ns::state_args  what state takes: statusword
 * @var Cia402Ns::controlword_args  what controlword takes: cmd
 * @var Cia402Ns::enable_sequence_args  what enable_sequence takes: state
 * @var Cia402Ns::sdo_set_controlword_args  what sdo_set_controlword takes: out, node, controlword
 * @var Cia402Ns::sdo_set_mode_args  what sdo_set_mode takes: out, node, mode
 * @var Cia402Ns::sdo_set_target_position_args  what sdo_set_target_position takes: out, node, position
 * @var Cia402Ns::sdo_set_target_velocity_args  what sdo_set_target_velocity takes: out, node, velocity
 * @var Cia402Ns::sdo_set_target_torque_args  what sdo_set_target_torque takes: out, node, torque
 * @var Cia402Ns::sdo_read_args  what sdo_read takes: out, node, index, sub
 * @var Cia402Ns::sdo_get_u16_args  what sdo_get_u16 takes: f, want_index, value
 * @var Cia402Ns::sdo_get_i32_args  what sdo_get_i32 takes: f, want_index, value
 * @var Cia402Ns::pack_command_args  what pack_command takes: buf, cap, controlword, target
 * @var Cia402Ns::unpack_status_args  what unpack_status takes: buf, len, statusword, actual
 * @var Cia402Ns::ok  a call's true/false outcome
 * @var Cia402Ns::value  the value a call reports
 * @var Cia402Ns::u16  what a call reports
 * @var Cia402Ns::n  the count a call reports
 * @var Cia402Ns::state  state
 * @var Cia402Ns::controlword  controlword
 * @var Cia402Ns::enable_sequence  enable_sequence
 * @var Cia402Ns::sdo_set_controlword  sdo_set_controlword
 * @var Cia402Ns::sdo_set_mode  sdo_set_mode
 * @var Cia402Ns::sdo_set_target_position  sdo_set_target_position
 * @var Cia402Ns::sdo_set_target_velocity  sdo_set_target_velocity
 * @var Cia402Ns::sdo_set_target_torque  sdo_set_target_torque
 * @var Cia402Ns::sdo_read  sdo_read
 * @var Cia402Ns::sdo_get_u16  sdo_get_u16
 * @var Cia402Ns::sdo_get_i32  sdo_get_i32
 * @var Cia402Ns::pack_command  pack_command
 * @var Cia402Ns::unpack_status  unpack_status
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    Cia402StateArgs state_args;
    Cia402ControlwordArgs controlword_args;
    Cia402EnableSequenceArgs enable_sequence_args;
    Cia402SdoSetControlwordArgs sdo_set_controlword_args;
    Cia402SdoSetModeArgs sdo_set_mode_args;
    Cia402SdoSetTargetPositionArgs sdo_set_target_position_args;
    Cia402SdoSetTargetVelocityArgs sdo_set_target_velocity_args;
    Cia402SdoSetTargetTorqueArgs sdo_set_target_torque_args;
    Cia402SdoReadArgs sdo_read_args;
    Cia402SdoGetU16Args sdo_get_u16_args;
    Cia402SdoGetI32Args sdo_get_i32_args;
    Cia402PackCommandArgs pack_command_args;
    Cia402UnpackStatusArgs unpack_status_args;
    proto_bool ok;
    Cia402State value;
    uint16_t u16;
    size_t n;
} Cia402Vars;

/** @brief The operands and the outcome. */
extern Cia402Vars Cia402V;

/** @brief The entries. */
typedef struct
{
    void (*const state)(uint8_t *restrict work);
    void (*const controlword)(uint8_t *restrict work);
    void (*const enable_sequence)(uint8_t *restrict work);
    void (*const sdo_set_controlword)(uint8_t *restrict work);
    void (*const sdo_set_mode)(uint8_t *restrict work);
    void (*const sdo_set_target_position)(uint8_t *restrict work);
    void (*const sdo_set_target_velocity)(uint8_t *restrict work);
    void (*const sdo_set_target_torque)(uint8_t *restrict work);
    void (*const sdo_read)(uint8_t *restrict work);
    void (*const sdo_get_u16)(uint8_t *restrict work);
    void (*const sdo_get_i32)(uint8_t *restrict work);
    void (*const pack_command)(uint8_t *restrict work);
    void (*const unpack_status)(uint8_t *restrict work);
} Cia402Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Cia402V or a region of the borrow at a fixed offset.
void protocore_cia402_state(uint8_t *restrict work);
void protocore_cia402_controlword(uint8_t *restrict work);
void protocore_cia402_enable_sequence(uint8_t *restrict work);
void protocore_cia402_sdo_set_controlword(uint8_t *restrict work);
void protocore_cia402_sdo_set_mode(uint8_t *restrict work);
void protocore_cia402_sdo_set_target_position(uint8_t *restrict work);
void protocore_cia402_sdo_set_target_velocity(uint8_t *restrict work);
void protocore_cia402_sdo_set_target_torque(uint8_t *restrict work);
void protocore_cia402_sdo_read(uint8_t *restrict work);
void protocore_cia402_sdo_get_u16(uint8_t *restrict work);
void protocore_cia402_sdo_get_i32(uint8_t *restrict work);
void protocore_cia402_pack_command(uint8_t *restrict work);
void protocore_cia402_unpack_status(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Cia402.state(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Cia402Ns Cia402 __attribute__((unused)) = {
    .state = protocore_cia402_state,
    .controlword = protocore_cia402_controlword,
    .enable_sequence = protocore_cia402_enable_sequence,
    .sdo_set_controlword = protocore_cia402_sdo_set_controlword,
    .sdo_set_mode = protocore_cia402_sdo_set_mode,
    .sdo_set_target_position = protocore_cia402_sdo_set_target_position,
    .sdo_set_target_velocity = protocore_cia402_sdo_set_target_velocity,
    .sdo_set_target_torque = protocore_cia402_sdo_set_target_torque,
    .sdo_read = protocore_cia402_sdo_read,
    .sdo_get_u16 = protocore_cia402_sdo_get_u16,
    .sdo_get_i32 = protocore_cia402_sdo_get_i32,
    .pack_command = protocore_cia402_pack_command,
    .unpack_status = protocore_cia402_unpack_status,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CIA402

#endif // PROTOCORE_CIA402_H
