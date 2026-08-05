// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ble_gatt.c
 * @brief Bluetooth ATT protocol codec + GATT characteristic bridge (see ble_gatt.h).
 */

#include "services/radio/ble_gatt/ble_gatt.h"
#include "mmgr/membuild.h"         // pc_sb frame builder
#include "shared_primitives/hex.h" // PC_HEX_LOWER - the shared digit table

#if PC_ENABLE_BLE_GATT

#include <string.h>

size_t att_read_req(uint16_t handle, uint8_t *out, size_t cap)
{
    if (!out || cap < 3)
    {
        return 0;
    }
    out[0] = ATT_OP_READ_REQ;
    out[1] = (uint8_t)handle;
    out[2] = (uint8_t)(handle >> 8);
    return 3;
}

size_t att_read_rsp(const uint8_t *val, size_t vlen, uint8_t *out, size_t cap)
{
    if (!out || (vlen && !val) || cap < 1 + vlen)
    {
        return 0;
    }
    out[0] = ATT_OP_READ_RSP;
    if (vlen)
    {
        memcpy(out + 1, val, vlen);
    }
    return 1 + vlen;
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
        memcpy(out + 3, val, vlen);
    }
    return 3 + vlen;
}

size_t att_write_req(uint16_t handle, const uint8_t *val, size_t vlen, uint8_t *out, size_t cap)
{
    return att_handle_value(ATT_OP_WRITE_REQ, handle, val, vlen, out, cap);
}

size_t att_notify(uint16_t handle, const uint8_t *val, size_t vlen, uint8_t *out, size_t cap)
{
    return att_handle_value(ATT_OP_HANDLE_VALUE_NTF, handle, val, vlen, out, cap);
}

size_t att_error_rsp(uint8_t req_op, uint16_t handle, uint8_t error, uint8_t *out, size_t cap)
{
    if (!out || cap < 5)
    {
        return 0;
    }
    out[0] = ATT_OP_ERROR_RSP;
    out[1] = req_op;
    out[2] = (uint8_t)handle;
    out[3] = (uint8_t)(handle >> 8);
    out[4] = error;
    return 5;
}

proto_bool att_parse(const uint8_t *pdu, size_t len, AttPdu *out)
{
    if (!pdu || !out || len < 1)
    {
        return PROTO_FALSE;
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
            return PROTO_FALSE;
        }
        out->req_op = pdu[1];
        out->handle = (uint16_t)(pdu[2] | (pdu[3] << 8));
        out->error = pdu[4];
        return PROTO_TRUE;
    case ATT_OP_READ_REQ:
        if (len < 3)
        {
            return PROTO_FALSE;
        }
        out->handle = (uint16_t)(pdu[1] | (pdu[2] << 8));
        return PROTO_TRUE;
    case ATT_OP_READ_RSP:
        if (len > 1)
        {
            out->value = pdu + 1;
            out->value_len = len - 1;
        }
        return PROTO_TRUE;
    case ATT_OP_WRITE_REQ:
    case ATT_OP_HANDLE_VALUE_NTF:
        if (len < 3)
        {
            return PROTO_FALSE;
        }
        out->handle = (uint16_t)(pdu[1] | (pdu[2] << 8));
        if (len > 3)
        {
            out->value = pdu + 3;
            out->value_len = len - 3;
        }
        return PROTO_TRUE;
    case ATT_OP_WRITE_RSP:
        return PROTO_TRUE;
    default:
        return PROTO_TRUE; // unknown opcode: still report it, no fixed fields
    }
}

static void put_hex16(pc_sb *b, uint16_t v)
{
    char t[7] = "0x0000";
    for (int i = 0; i < 4; i++)
    {
        t[2 + i] = PC_HEX_LOWER[(v >> ((3 - i) * 4)) & 0xF];
    }
    pc_sb_put(b, t);
}

size_t pc_gatt_char_json(const GattChar *chars, size_t n, char *out, size_t cap)
{
    if (!out || cap == 0 || (n && !chars))
    {
        return 0;
    }
    pc_sb b = {out, cap, 0, PROTO_TRUE};
    pc_sb_put(&b, "[");
    for (size_t i = 0; i < n; i++)
    {
        if (i)
        {
            pc_sb_put(&b, ",");
        }
        pc_sb_put(&b, "{\"handle\":");
        pc_sb_u32(&b, chars[i].handle);
        pc_sb_put(&b, ",\"uuid\":\"");
        put_hex16(&b, chars[i].uuid);
        pc_sb_put(&b, "\",\"props\":");
        pc_sb_u32(&b, chars[i].props);
        pc_sb_put(&b, "}");
    }
    pc_sb_put(&b, "]");
    if (!b.ok)
    {
        return 0;
    }
    out[b.len] = '\0';
    return b.len;
}

#endif // PC_ENABLE_BLE_GATT
