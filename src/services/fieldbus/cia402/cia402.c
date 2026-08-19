// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cia402.c
 * @brief CiA 402 drive profile: state machine + Controlword/Statusword + CANopen object access.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_CIA402

#include "mmgr/protomem/protomem.h"
#include "services/fieldbus/canopen/canopen.h"
#include "services/fieldbus/cia402/cia402.h"
#include "shared/can/can.h"

static uint8_t canopen_work[16]; // the borrow an entry takes; Canopen never reads it

#include "mmgr/endian/endian.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void cia402_controlword(uint8_t *restrict work);

static void cia402_state(uint8_t *restrict work)
{
    (void)work;
    uint16_t sw = Cia402.state_args.statusword;

    // Mask/value table from IEC 61800-7-201 (CiA 402). Order matters where masks differ.
    if ((sw & 0x4F) == 0x00)
    {
        Cia402.value = CIA402_STATE_NOT_READY_TO_SWITCH_ON;
        return;
    }
    if ((sw & 0x4F) == 0x40)
    {
        Cia402.value = CIA402_STATE_SWITCH_ON_DISABLED;
        return;
    }
    if ((sw & 0x6F) == 0x21)
    {
        Cia402.value = CIA402_STATE_READY_TO_SWITCH_ON;
        return;
    }
    if ((sw & 0x6F) == 0x23)
    {
        Cia402.value = CIA402_STATE_SWITCHED_ON;
        return;
    }
    if ((sw & 0x6F) == 0x27)
    {
        Cia402.value = CIA402_STATE_OPERATION_ENABLED;
        return;
    }
    if ((sw & 0x6F) == 0x07)
    {
        Cia402.value = CIA402_STATE_QUICK_STOP_ACTIVE;
        return;
    }
    if ((sw & 0x4F) == 0x0F)
    {
        Cia402.value = CIA402_STATE_FAULT_REACTION_ACTIVE;
        return;
    }
    if ((sw & 0x4F) == 0x08)
    {
        Cia402.value = CIA402_STATE_FAULT;
        return;
    }
    Cia402.value = CIA402_STATE_UNKNOWN;
}

static void cia402_controlword(uint8_t *restrict work)
{
    (void)work;
    Cia402Command cmd = Cia402.controlword_args.cmd;

    switch (cmd)
    {
    case CIA402_COMMAND_SHUTDOWN:
        Cia402.u16 = 0x0006; // enable voltage + quick stop, switch-on 0
        return;
    case CIA402_COMMAND_SWITCH_ON:
    case CIA402_COMMAND_DISABLE_OPERATION:
        Cia402.u16 = 0x0007; // switch-on + enable voltage + quick stop
        return;
    case CIA402_COMMAND_ENABLE_OPERATION:
        Cia402.u16 = 0x000F; // + enable operation
        return;
    case CIA402_COMMAND_DISABLE_VOLTAGE:
        Cia402.u16 = 0x0000;
        return;
    case CIA402_COMMAND_QUICK_STOP:
        Cia402.u16 = 0x0002; // enable voltage, quick-stop bit cleared (active)
        return;
    case CIA402_COMMAND_FAULT_RESET:
        Cia402.u16 = 0x0080; // bit 7 rising edge
        return;
    }
    Cia402.u16 = 0x0000;
}

static void cia402_enable_sequence(uint8_t *restrict work)
{
    Cia402State state = Cia402.enable_sequence_args.state;

    switch (state)
    {
    case CIA402_STATE_FAULT:
    case CIA402_STATE_FAULT_REACTION_ACTIVE:
        Cia402.controlword_args.cmd = CIA402_COMMAND_FAULT_RESET;
        cia402_controlword(work);
        return;
    case CIA402_STATE_SWITCH_ON_DISABLED:
        Cia402.controlword_args.cmd = CIA402_COMMAND_SHUTDOWN;
        cia402_controlword(work);
        return;
    case CIA402_STATE_READY_TO_SWITCH_ON:
        Cia402.controlword_args.cmd = CIA402_COMMAND_SWITCH_ON;
        cia402_controlword(work);
        return;
    case CIA402_STATE_SWITCHED_ON:
    case CIA402_STATE_QUICK_STOP_ACTIVE:
    case CIA402_STATE_OPERATION_ENABLED:
        Cia402.controlword_args.cmd = CIA402_COMMAND_ENABLE_OPERATION;
        cia402_controlword(work);
        return;
    default: // not_ready_to_switch_on / unknown: wait, hold voltage off
        Cia402.controlword_args.cmd = CIA402_COMMAND_DISABLE_VOLTAGE;
        cia402_controlword(work);
        return;
    }
}

static void cia402_sdo_set_controlword(uint8_t *restrict work)
{
    (void)work;
    CanFrame *out = Cia402.sdo_set_controlword_args.out;
    uint8_t node = Cia402.sdo_set_controlword_args.node;
    uint16_t controlword = Cia402.sdo_set_controlword_args.controlword;

    uint8_t d[2];
    endian.wr16le(d, controlword);
    Canopen.build_sdo_write_args.out = out;
    Canopen.build_sdo_write_args.node_id = node;
    Canopen.build_sdo_write_args.index = CIA402_OD_CONTROLWORD;
    Canopen.build_sdo_write_args.sub = 0;
    Canopen.build_sdo_write_args.data = d;
    Canopen.build_sdo_write_args.len = 2;
    Canopen.build_sdo_write(canopen_work);
    Cia402.ok = Canopen.ok;
}

static void cia402_sdo_set_mode(uint8_t *restrict work)
{
    (void)work;
    CanFrame *out = Cia402.sdo_set_mode_args.out;
    uint8_t node = Cia402.sdo_set_mode_args.node;
    Cia402Mode mode = Cia402.sdo_set_mode_args.mode;

    uint8_t d = (uint8_t)(int8_t)mode; // wire byte
    Canopen.build_sdo_write_args.out = out;
    Canopen.build_sdo_write_args.node_id = node;
    Canopen.build_sdo_write_args.index = CIA402_OD_MODES_OF_OPERATION;
    Canopen.build_sdo_write_args.sub = 0;
    Canopen.build_sdo_write_args.data = &d;
    Canopen.build_sdo_write_args.len = 1;
    Canopen.build_sdo_write(canopen_work);
    Cia402.ok = Canopen.ok;
}

static void cia402_sdo_set_target_position(uint8_t *restrict work)
{
    (void)work;
    CanFrame *out = Cia402.sdo_set_target_position_args.out;
    uint8_t node = Cia402.sdo_set_target_position_args.node;
    int32_t position = Cia402.sdo_set_target_position_args.position;

    uint8_t d[4];
    endian.wr32le(d, (uint32_t)position);
    Canopen.build_sdo_write_args.out = out;
    Canopen.build_sdo_write_args.node_id = node;
    Canopen.build_sdo_write_args.index = CIA402_OD_TARGET_POSITION;
    Canopen.build_sdo_write_args.sub = 0;
    Canopen.build_sdo_write_args.data = d;
    Canopen.build_sdo_write_args.len = 4;
    Canopen.build_sdo_write(canopen_work);
    Cia402.ok = Canopen.ok;
}

static void cia402_sdo_set_target_velocity(uint8_t *restrict work)
{
    (void)work;
    CanFrame *out = Cia402.sdo_set_target_velocity_args.out;
    uint8_t node = Cia402.sdo_set_target_velocity_args.node;
    int32_t velocity = Cia402.sdo_set_target_velocity_args.velocity;

    uint8_t d[4];
    endian.wr32le(d, (uint32_t)velocity);
    Canopen.build_sdo_write_args.out = out;
    Canopen.build_sdo_write_args.node_id = node;
    Canopen.build_sdo_write_args.index = CIA402_OD_TARGET_VELOCITY;
    Canopen.build_sdo_write_args.sub = 0;
    Canopen.build_sdo_write_args.data = d;
    Canopen.build_sdo_write_args.len = 4;
    Canopen.build_sdo_write(canopen_work);
    Cia402.ok = Canopen.ok;
}

static void cia402_sdo_set_target_torque(uint8_t *restrict work)
{
    (void)work;
    CanFrame *out = Cia402.sdo_set_target_torque_args.out;
    uint8_t node = Cia402.sdo_set_target_torque_args.node;
    int16_t torque = Cia402.sdo_set_target_torque_args.torque;

    uint8_t d[2];
    endian.wr16le(d, (uint16_t)torque);
    Canopen.build_sdo_write_args.out = out;
    Canopen.build_sdo_write_args.node_id = node;
    Canopen.build_sdo_write_args.index = CIA402_OD_TARGET_TORQUE;
    Canopen.build_sdo_write_args.sub = 0;
    Canopen.build_sdo_write_args.data = d;
    Canopen.build_sdo_write_args.len = 2;
    Canopen.build_sdo_write(canopen_work);
    Cia402.ok = Canopen.ok;
}

static void cia402_sdo_read(uint8_t *restrict work)
{
    (void)work;
    CanFrame *out = Cia402.sdo_read_args.out;
    uint8_t node = Cia402.sdo_read_args.node;
    uint16_t index = Cia402.sdo_read_args.index;
    uint8_t sub = Cia402.sdo_read_args.sub;

    Canopen.build_sdo_read_args.out = out;
    Canopen.build_sdo_read_args.node_id = node;
    Canopen.build_sdo_read_args.index = index;
    Canopen.build_sdo_read_args.sub = sub;
    Canopen.build_sdo_read(canopen_work);
    Cia402.ok = Canopen.ok;
}

// Validate an expedited SDO upload response and copy its inline payload into @p out (>= need
// octets). No shared state - the parsed response lives on this call's stack.
static proto_bool sdo_upload_bytes(const CanFrame *f, uint16_t want_index, uint8_t need, uint8_t *out)
{
    CanopenSdoResponse resp;
    Canopen.parse_sdo_response_args.f = f;
    Canopen.parse_sdo_response_args.out = &resp;
    Canopen.parse_sdo_response(canopen_work);
    if (!Canopen.ok)
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
    mem.cpy(out, resp.data, need);
    return PROTO_TRUE;
}

static void cia402_sdo_get_u16(uint8_t *restrict work)
{
    (void)work;
    const CanFrame *f = Cia402.sdo_get_u16_args.f;
    uint16_t want_index = Cia402.sdo_get_u16_args.want_index;
    uint16_t *value = Cia402.sdo_get_u16_args.value;

    uint8_t d[2];
    if (!value || !sdo_upload_bytes(f, want_index, 2, d))
    {
        Cia402.ok = PROTO_FALSE;
        return;
    }
    *value = endian.rd16le(d);
    Cia402.ok = PROTO_TRUE;
}

static void cia402_sdo_get_i32(uint8_t *restrict work)
{
    (void)work;
    const CanFrame *f = Cia402.sdo_get_i32_args.f;
    uint16_t want_index = Cia402.sdo_get_i32_args.want_index;
    int32_t *value = Cia402.sdo_get_i32_args.value;

    uint8_t d[4];
    if (!value || !sdo_upload_bytes(f, want_index, 4, d))
    {
        Cia402.ok = PROTO_FALSE;
        return;
    }
    *value = (int32_t)endian.rd32le(d);
    Cia402.ok = PROTO_TRUE;
}

static void cia402_pack_command(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Cia402.pack_command_args.buf;
    size_t cap = Cia402.pack_command_args.cap;
    uint16_t controlword = Cia402.pack_command_args.controlword;
    int32_t target = Cia402.pack_command_args.target;

    if (!buf || cap < 6)
    {
        Cia402.n = 0;
        return;
    }
    size_t p = endian.wr16le(buf, controlword);
    p += endian.wr32le(buf + p, (uint32_t)target);
    Cia402.n = p; // 6
}

static void cia402_unpack_status(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = Cia402.unpack_status_args.buf;
    size_t len = Cia402.unpack_status_args.len;
    uint16_t *statusword = Cia402.unpack_status_args.statusword;
    int32_t *actual = Cia402.unpack_status_args.actual;

    if (!buf || !statusword || !actual || len < 6)
    {
        Cia402.ok = PROTO_FALSE;
        return;
    }
    *statusword = endian.rd16le(buf);
    *actual = (int32_t)endian.rd32le(buf + 2);
    Cia402.ok = PROTO_TRUE;
}

Cia402Ns Cia402 = {.state = cia402_state,
                   .controlword = cia402_controlword,
                   .enable_sequence = cia402_enable_sequence,
                   .sdo_set_controlword = cia402_sdo_set_controlword,
                   .sdo_set_mode = cia402_sdo_set_mode,
                   .sdo_set_target_position = cia402_sdo_set_target_position,
                   .sdo_set_target_velocity = cia402_sdo_set_target_velocity,
                   .sdo_set_target_torque = cia402_sdo_set_target_torque,
                   .sdo_read = cia402_sdo_read,
                   .sdo_get_u16 = cia402_sdo_get_u16,
                   .sdo_get_i32 = cia402_sdo_get_i32,
                   .pack_command = cia402_pack_command,
                   .unpack_status = cia402_unpack_status};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CIA402
