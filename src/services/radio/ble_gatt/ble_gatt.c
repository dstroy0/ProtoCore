// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ble_gatt.c
 * @brief Bluetooth ATT protocol codec + GATT characteristic bridge (see ble_gatt.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_BLE_GATT

#include "mmgr/membuild/membuild.h" // protocore_sb frame builder
#include "mmgr/protomem/protomem.h"
#include "services/radio/ble_gatt/ble_gatt.h"
#include "shared/hex/hex.h" // PROTOCORE_HEX: the shared digit tables

PROTOCORE_BEGIN_DECLS

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_ble_gatt_att_read_req(uint8_t *restrict work)
{
    (void)work;
    uint16_t handle = BleGattV.att_read_req_args.handle;
    uint8_t *out = BleGattV.att_read_req_args.out;
    size_t cap = BleGattV.att_read_req_args.cap;

    if (!out || cap < 3)
    {
        BleGattV.n = 0;
        return;
    }
    out[0] = ATT_OP_READ_REQ;
    out[1] = (uint8_t)handle;
    out[2] = (uint8_t)(handle >> 8);
    BleGattV.n = 3;
}

void protocore_ble_gatt_att_read_rsp(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *val = BleGattV.att_read_rsp_args.val;
    size_t vlen = BleGattV.att_read_rsp_args.vlen;
    uint8_t *out = BleGattV.att_read_rsp_args.out;
    size_t cap = BleGattV.att_read_rsp_args.cap;

    if (!out || (vlen && !val) || cap < 1 + vlen)
    {
        BleGattV.n = 0;
        return;
    }
    out[0] = ATT_OP_READ_RSP;
    if (vlen)
    {
        mem.cpy(out + 1, val, vlen);
    }
    BleGattV.n = 1 + vlen;
}

static size_t att_handle_value(uint8_t op, uint16_t handle, const uint8_t *val, size_t vlen, uint8_t *out, size_t cap)
{
    if (!out || (vlen && !val) || cap < 3 + vlen)
    {
        return 0;
    }
    out[0] = op;
    out[1] = (uint8_t)handle;
    out[2] = (uint8_t)(handle >> 8);
    if (vlen)
    {
        mem.cpy(out + 3, val, vlen);
    }
    return 3 + vlen;
}

void protocore_ble_gatt_att_write_req(uint8_t *restrict work)
{
    (void)work;
    uint16_t handle = BleGattV.att_write_req_args.handle;
    const uint8_t *val = BleGattV.att_write_req_args.val;
    size_t vlen = BleGattV.att_write_req_args.vlen;
    uint8_t *out = BleGattV.att_write_req_args.out;
    size_t cap = BleGattV.att_write_req_args.cap;

    BleGattV.n = att_handle_value(ATT_OP_WRITE_REQ, handle, val, vlen, out, cap);
}

void protocore_ble_gatt_att_notify(uint8_t *restrict work)
{
    (void)work;
    uint16_t handle = BleGattV.att_notify_args.handle;
    const uint8_t *val = BleGattV.att_notify_args.val;
    size_t vlen = BleGattV.att_notify_args.vlen;
    uint8_t *out = BleGattV.att_notify_args.out;
    size_t cap = BleGattV.att_notify_args.cap;

    BleGattV.n = att_handle_value(ATT_OP_HANDLE_VALUE_NTF, handle, val, vlen, out, cap);
}

void protocore_ble_gatt_att_error_rsp(uint8_t *restrict work)
{
    (void)work;
    uint8_t req_op = BleGattV.att_error_rsp_args.req_op;
    uint16_t handle = BleGattV.att_error_rsp_args.handle;
    uint8_t error = BleGattV.att_error_rsp_args.error;
    uint8_t *out = BleGattV.att_error_rsp_args.out;
    size_t cap = BleGattV.att_error_rsp_args.cap;

    if (!out || cap < 5)
    {
        BleGattV.n = 0;
        return;
    }
    out[0] = ATT_OP_ERROR_RSP;
    out[1] = req_op;
    out[2] = (uint8_t)handle;
    out[3] = (uint8_t)(handle >> 8);
    out[4] = error;
    BleGattV.n = 5;
}

void protocore_ble_gatt_att_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *pdu = BleGattV.att_parse_args.pdu;
    size_t len = BleGattV.att_parse_args.len;
    AttPdu *out = BleGattV.att_parse_args.out;

    if (!pdu || !out || len < 1)
    {
        BleGattV.ok = PROTO_FALSE;
        return;
    }
    out->opcode = pdu[0];
    out->handle = 0;
    out->req_op = 0;
    out->error = 0;
    out->value = NULL;
    out->value_len = 0;

    switch (pdu[0])
    {
    case ATT_OP_ERROR_RSP:
        if (len < 5)
        {
            BleGattV.ok = PROTO_FALSE;
            return;
        }
        out->req_op = pdu[1];
        out->handle = (uint16_t)(pdu[2] | (pdu[3] << 8));
        out->error = pdu[4];
        BleGattV.ok = PROTO_TRUE;
        return;
    case ATT_OP_READ_REQ:
        if (len < 3)
        {
            BleGattV.ok = PROTO_FALSE;
            return;
        }
        out->handle = (uint16_t)(pdu[1] | (pdu[2] << 8));
        BleGattV.ok = PROTO_TRUE;
        return;
    case ATT_OP_READ_RSP:
        if (len > 1)
        {
            out->value = pdu + 1;
            out->value_len = len - 1;
        }
        BleGattV.ok = PROTO_TRUE;
        return;
    case ATT_OP_WRITE_REQ:
    case ATT_OP_HANDLE_VALUE_NTF:
        if (len < 3)
        {
            BleGattV.ok = PROTO_FALSE;
            return;
        }
        out->handle = (uint16_t)(pdu[1] | (pdu[2] << 8));
        if (len > 3)
        {
            out->value = pdu + 3;
            out->value_len = len - 3;
        }
        BleGattV.ok = PROTO_TRUE;
        return;
    case ATT_OP_WRITE_RSP:
        BleGattV.ok = PROTO_TRUE;
        return;
    default:
        BleGattV.ok = PROTO_TRUE; // unknown opcode: still report it, no fixed fields
        return;
    }
}

static void put_hex16(protocore_sb *b, uint16_t v)
{
    char t[7] = "0x0000";
    for (int i = 0; i < 4; i++)
    {
        t[2 + i] = PROTOCORE_HEX.lower[(v >> ((3 - i) * 4)) & 0xF];
    }
    Sb.put(b, t);
}

void protocore_ble_gatt_char_json(uint8_t *restrict work)
{
    (void)work;
    const GattChar *chars = BleGattV.char_json_args.chars;
    size_t n = BleGattV.char_json_args.n;
    char *out = BleGattV.char_json_args.out;
    size_t cap = BleGattV.char_json_args.cap;

    if (!out || cap == 0 || (n && !chars))
    {
        BleGattV.n = 0;
        return;
    }
    protocore_sb b = {out, cap, 0, PROTO_TRUE};
    Sb.put(&b, "[");
    for (size_t i = 0; i < n; i++)
    {
        if (i)
        {
            Sb.put(&b, ",");
        }
        Sb.put(&b, "{\"handle\":");
        Sb.u32(&b, chars[i].handle);
        Sb.put(&b, ",\"uuid\":\"");
        put_hex16(&b, chars[i].uuid);
        Sb.put(&b, "\",\"props\":");
        Sb.u32(&b, chars[i].props);
        Sb.put(&b, "}");
    }
    Sb.put(&b, "]");
    if (!b.ok)
    {
        BleGattV.n = 0;
        return;
    }
    out[b.len] = '\0';
    BleGattV.n = b.len;
}

/** @brief The operands and the outcome. */
BleGattVars BleGattV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_BLE_GATT
