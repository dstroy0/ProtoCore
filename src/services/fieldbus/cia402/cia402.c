// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cia402.c
 * @brief CiA 402 drive profile: state machine + Controlword/Statusword + CANopen object access.
 */

#include "services/fieldbus/cia402/cia402.h"

#if PC_ENABLE_CIA402

#include <string.h>

#include "shared_primitives/endian.h"

Cia402State pc_cia402_state(uint16_t sw)
{
    // Mask/value table from IEC 61800-7-201 (CiA 402). Order matters where masks differ.
    if ((sw & 0x4F) == 0x00)
    {
        return CIA402_STATE_NOT_READY_TO_SWITCH_ON;
    }
    if ((sw & 0x4F) == 0x40)
    {
        return CIA402_STATE_SWITCH_ON_DISABLED;
    }
    if ((sw & 0x6F) == 0x21)
    {
        return CIA402_STATE_READY_TO_SWITCH_ON;
    }
    if ((sw & 0x6F) == 0x23)
    {
        return CIA402_STATE_SWITCHED_ON;
    }
    if ((sw & 0x6F) == 0x27)
    {
        return CIA402_STATE_OPERATION_ENABLED;
    }
    if ((sw & 0x6F) == 0x07)
    {
        return CIA402_STATE_QUICK_STOP_ACTIVE;
    }
    if ((sw & 0x4F) == 0x0F)
    {
        return CIA402_STATE_FAULT_REACTION_ACTIVE;
    }
    if ((sw & 0x4F) == 0x08)
    {
        return CIA402_STATE_FAULT;
    }
    return CIA402_STATE_UNKNOWN;
}

uint16_t pc_cia402_controlword(Cia402Command cmd)
{
    switch (cmd)
    {
    case CIA402_COMMAND_SHUTDOWN:
        return 0x0006; // enable voltage + quick stop, switch-on 0
    case CIA402_COMMAND_SWITCH_ON:
    case CIA402_COMMAND_DISABLE_OPERATION:
        return 0x0007; // switch-on + enable voltage + quick stop
    case CIA402_COMMAND_ENABLE_OPERATION:
        return 0x000F; // + enable operation
    case CIA402_COMMAND_DISABLE_VOLTAGE:
        return 0x0000;
    case CIA402_COMMAND_QUICK_STOP:
        return 0x0002; // enable voltage, quick-stop bit cleared (active)
    case CIA402_COMMAND_FAULT_RESET:
        return 0x0080; // bit 7 rising edge
    }
    return 0x0000;
}

uint16_t pc_cia402_enable_sequence(Cia402State state)
{
    switch (state)
    {
    case CIA402_STATE_FAULT:
    case CIA402_STATE_FAULT_REACTION_ACTIVE:
        return pc_cia402_controlword(CIA402_COMMAND_FAULT_RESET);
    case CIA402_STATE_SWITCH_ON_DISABLED:
        return pc_cia402_controlword(CIA402_COMMAND_SHUTDOWN);
    case CIA402_STATE_READY_TO_SWITCH_ON:
        return pc_cia402_controlword(CIA402_COMMAND_SWITCH_ON);
    case CIA402_STATE_SWITCHED_ON:
    case CIA402_STATE_QUICK_STOP_ACTIVE:
    case CIA402_STATE_OPERATION_ENABLED:
        return pc_cia402_controlword(CIA402_COMMAND_ENABLE_OPERATION);
    default: // not_ready_to_switch_on / unknown: wait, hold voltage off
        return pc_cia402_controlword(CIA402_COMMAND_DISABLE_VOLTAGE);
    }
}

proto_bool pc_cia402_sdo_set_controlword(CanFrame *out, uint8_t node, uint16_t controlword)
{
    uint8_t d[2];
    pc_wr16le(d, controlword);
    return pc_canopen_build_sdo_write(out, node, CIA402_OD_CONTROLWORD, 0, d, 2);
}

proto_bool pc_cia402_sdo_set_mode(CanFrame *out, uint8_t node, Cia402Mode mode)
{
    uint8_t d = (uint8_t)(int8_t)mode; // wire byte
    return pc_canopen_build_sdo_write(out, node, CIA402_OD_MODES_OF_OPERATION, 0, &d, 1);
}

proto_bool pc_cia402_sdo_set_target_position(CanFrame *out, uint8_t node, int32_t position)
{
    uint8_t d[4];
    pc_wr32le(d, (uint32_t)position);
    return pc_canopen_build_sdo_write(out, node, CIA402_OD_TARGET_POSITION, 0, d, 4);
}

proto_bool pc_cia402_sdo_set_target_velocity(CanFrame *out, uint8_t node, int32_t velocity)
{
    uint8_t d[4];
    pc_wr32le(d, (uint32_t)velocity);
    return pc_canopen_build_sdo_write(out, node, CIA402_OD_TARGET_VELOCITY, 0, d, 4);
}

proto_bool pc_cia402_sdo_set_target_torque(CanFrame *out, uint8_t node, int16_t torque)
{
    uint8_t d[2];
    pc_wr16le(d, (uint16_t)torque);
    return pc_canopen_build_sdo_write(out, node, CIA402_OD_TARGET_TORQUE, 0, d, 2);
}

proto_bool pc_cia402_sdo_read(CanFrame *out, uint8_t node, uint16_t index, uint8_t sub)
{
    return pc_canopen_build_sdo_read(out, node, index, sub);
}

// Validate an expedited SDO upload response and copy its inline payload into @p out (>= need
// octets). No shared state - the parsed response lives on this call's stack.
static proto_bool sdo_upload_bytes(const CanFrame *f, uint16_t want_index, uint8_t need, uint8_t *out)
{
    CanopenSdoResponse resp;
    if (!pc_canopen_parse_sdo_response(f, &resp))
    {
        return PROTO_FALSE;
    }
    if (resp.is_abort || !resp.is_upload || !resp.expedited || resp.len < need)
    {
        return PROTO_FALSE;
    }
    if (want_index != 0 && resp.index != want_index)
    {
        return PROTO_FALSE;
    }
    memcpy(out, resp.data, need);
    return PROTO_TRUE;
}

proto_bool pc_cia402_sdo_get_u16(const CanFrame *f, uint16_t want_index, uint16_t *value)
{
    uint8_t d[2];
    if (!value || !sdo_upload_bytes(f, want_index, 2, d))
    {
        return PROTO_FALSE;
    }
    *value = pc_rd16le(d);
    return PROTO_TRUE;
}

proto_bool pc_cia402_sdo_get_i32(const CanFrame *f, uint16_t want_index, int32_t *value)
{
    uint8_t d[4];
    if (!value || !sdo_upload_bytes(f, want_index, 4, d))
    {
        return PROTO_FALSE;
    }
    *value = (int32_t)pc_rd32le(d);
    return PROTO_TRUE;
}

size_t pc_cia402_pack_command(uint8_t *buf, size_t cap, uint16_t controlword, int32_t target)
{
    if (!buf || cap < 6)
    {
        return 0;
    }
    size_t p = pc_wr16le(buf, controlword);
    p += pc_wr32le(buf + p, (uint32_t)target);
    return p; // 6
}

proto_bool pc_cia402_unpack_status(const uint8_t *buf, size_t len, uint16_t *statusword, int32_t *actual)
{
    if (!buf || !statusword || !actual || len < 6)
    {
        return PROTO_FALSE;
    }
    *statusword = pc_rd16le(buf);
    *actual = (int32_t)pc_rd32le(buf + 2);
    return PROTO_TRUE;
}

#endif // PC_ENABLE_CIA402
