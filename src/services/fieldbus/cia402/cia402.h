// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cia402.h
 * @brief CiA 402 / IEC 61800-7-201 drive + motion profile (PC_ENABLE_CIA402) over CANopen.
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

#include "protocore_config.h"

#if PC_ENABLE_CIA402

#include "services/fieldbus/canopen/canopen.h"

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

/// Modes of Operation (object 0x6060 / 0x6061). Cast only at the wire byte.
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

/// The eight power-state-machine states decoded from the Statusword.
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

/// State-machine transition commands issued via the Controlword.
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

/// Controlword bit masks (object 0x6040).
#define n 0x0001
#define e 0x0002
#define p 0x0004 ///< active-low: 0 requests quick stop
#define n 0x0008
#define t 0x0080 ///< acts on the rising edge
#define t 0x0100

/// Statusword bit masks (object 0x6041).
#define n 0x0001
#define n 0x0002
#define d 0x0004
#define t 0x0008
#define d 0x0010
#define p 0x0020 ///< 0 = quick stop active
#define d 0x0040
#define g 0x0080
#define e 0x0200
#define d 0x0400
#define t 0x0800

// --- state machine (pure value logic; no CAN needed) ---

/// Decode the drive's power state from a Statusword (per the CiA 402 mask/value table).
Cia402State pc_cia402_state(uint16_t statusword);

/// The Controlword value that requests transition @p cmd. Fault-reset is 0x0080 and must be a
/// rising edge on bit 7 (clear it on the next cycle).
uint16_t pc_cia402_controlword(Cia402Command cmd);

/// Given the drive's current @p state, the Controlword to command the next step toward Operation
/// Enabled (fault -> reset, switch-on-disabled -> shutdown, ready -> switch on, switched-on ->
/// enable). Returns 0x000F once already enabled. Drives the "bring the axis live" bring-up loop.
uint16_t pc_cia402_enable_sequence(Cia402State state);

/// @return true if the Statusword's Target Reached flag (bit 10) is set.
static inline proto_bool pc_cia402_target_reached(uint16_t sw)
{
    return (sw & Cia402Sw::target_reached) != 0;
}
/// @return true if the drive reports a fault (bit 3).
static inline proto_bool pc_cia402_has_fault(uint16_t sw)
{
    return (sw & Cia402Sw::fault) != 0;
}
/// @return true if a warning is present (bit 7).
static inline proto_bool pc_cia402_warning(uint16_t sw)
{
    return (sw & Cia402Sw::warning) != 0;
}
/// @return true if the drive's power stage voltage is applied (bit 4).
static inline proto_bool pc_cia402_voltage_enabled(uint16_t sw)
{
    return (sw & Cia402Sw::voltage_enabled) != 0;
}
/// @return true if the drive follows the Controlword (bit 9 remote).
static inline proto_bool pc_cia402_remote(uint16_t sw)
{
    return (sw & Cia402Sw::remote) != 0;
}
/// @return true if a set-point was internally limited (bit 11).
static inline proto_bool pc_cia402_internal_limit(uint16_t sw)
{
    return (sw & Cia402Sw::internal_limit) != 0;
}

// --- CANopen SDO setters (expedited download to the object); fill *out, return false on bad arg ---

/// SDO-write the Controlword (0x6040, u16) on @p node.
proto_bool pc_cia402_sdo_set_controlword(CanFrame *out, uint8_t node, uint16_t controlword);
/// SDO-write the requested Mode of Operation (0x6060, i8) on @p node.
proto_bool pc_cia402_sdo_set_mode(CanFrame *out, uint8_t node, Cia402Mode mode);
/// SDO-write Target Position (0x607A, i32) on @p node.
proto_bool pc_cia402_sdo_set_target_position(CanFrame *out, uint8_t node, int32_t position);
/// SDO-write Target Velocity (0x60FF, i32) on @p node.
proto_bool pc_cia402_sdo_set_target_velocity(CanFrame *out, uint8_t node, int32_t velocity);
/// SDO-write Target Torque (0x6071, i16) on @p node.
proto_bool pc_cia402_sdo_set_target_torque(CanFrame *out, uint8_t node, int16_t torque);

/// SDO-read request for any drive object (thin wrapper over pc_canopen_build_sdo_read).
proto_bool pc_cia402_sdo_read(CanFrame *out, uint8_t node, uint16_t index, uint8_t sub);

/// Decode an SDO upload response into a 16-bit object value (e.g. the Statusword). @p want_index,
/// if non-zero, must match the response's index. Returns false on an abort / wrong / short reply.
proto_bool pc_cia402_sdo_get_u16(const CanFrame *f, uint16_t want_index, uint16_t *value);
/// Decode an SDO upload response into a signed 32-bit value (position / velocity actual).
proto_bool pc_cia402_sdo_get_i32(const CanFrame *f, uint16_t want_index, int32_t *value);

// --- PDO packing for cyclic operation (the common default mappings) ---

/// Pack an RPDO payload = Controlword (u16 LE) + Target (i32 LE) = 6 octets (a typical CSP/PP
/// RPDO map). Returns the octet count, or 0 if cap < 6.
size_t pc_cia402_pack_command(uint8_t *buf, size_t cap, uint16_t controlword, int32_t target);

/// Unpack a TPDO payload = Statusword (u16 LE) + Actual (i32 LE) into @p statusword / @p actual
/// (a typical CSP/PP TPDO map). Returns false if len < 6.
proto_bool pc_cia402_unpack_status(const uint8_t *buf, size_t len, uint16_t *statusword, int32_t *actual);

#endif // PC_ENABLE_CIA402

#endif // PROTOCORE_CIA402_H
