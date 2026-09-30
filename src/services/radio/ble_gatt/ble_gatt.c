// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ble_gatt.c
 * @brief Bluetooth ATT protocol codec + GATT characteristic bridge (see ble_gatt.h).
 */

#include "protocore_config.h" // the entry point: the widths

#if PROTOCORE_ENABLE_BLE_GATT

#include "memoria_operor/memoria_operor.h"
#include "services/radio/ble_gatt/ble_gatt.h"
#include "shared/hex/hex.h"            // PROTOCORE_HEX: the shared digit tables
#include "verba_scribo/verba_scribo.h" // verba_*: the text and number writers the builders chain

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

size_t protocore_ble_gatt_att_read_req(uint8_t *work, uint16_t handle, uint8_t *out, size_t cap)
{
    (void)work;

    if (!out || cap < 3)
    {
        return 0;
    }
    out[0] = ATT_OP_READ_REQ;
    out[1] = (uint8_t)handle;
    out[2] = (uint8_t)(handle >> 8);
    return 3;
}

size_t protocore_ble_gatt_att_read_rsp(uint8_t *work, const uint8_t *val, size_t vlen, uint8_t *out, size_t cap)
{
    (void)work;

    if (!out || (vlen && !val) || cap < 1 + vlen)
    {
        return 0;
    }
    out[0] = ATT_OP_READ_RSP;
    if (vlen)
    {
        EMBED_CALL(memor.cpy, MemoriaCfg, .dst = out + 1, .src = val, .bytes = vlen);
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
        EMBED_CALL(memor.cpy, MemoriaCfg, .dst = out + 3, .src = val, .bytes = vlen);
    }
    return 3 + vlen;
}

size_t protocore_ble_gatt_att_write_req(uint8_t *work, uint16_t handle, const uint8_t *val, size_t vlen, uint8_t *out,
                                        size_t cap)
{
    (void)work;

    return att_handle_value(ATT_OP_WRITE_REQ, handle, val, vlen, out, cap);
}

size_t protocore_ble_gatt_att_notify(uint8_t *work, uint16_t handle, const uint8_t *val, size_t vlen, uint8_t *out,
                                     size_t cap)
{
    (void)work;

    return att_handle_value(ATT_OP_HANDLE_VALUE_NTF, handle, val, vlen, out, cap);
}

size_t protocore_ble_gatt_att_error_rsp(uint8_t *work, uint8_t req_op, uint16_t handle, uint8_t error, uint8_t *out,
                                        size_t cap)
{
    (void)work;

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

proto_bool protocore_ble_gatt_att_parse(uint8_t *work, const uint8_t *pdu, size_t len, AttPdu *out)
{
    proto_bool ok = PROTO_FALSE;
    (void)work;

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
        ok = PROTO_TRUE; // unknown opcode: still report it, no fixed fields
        break;
    }
    return ok;
}

static size_t put_hex16(char *out, size_t cap, size_t at, uint16_t v)
{
    char t[7] = "0x0000";
    for (int i = 0; i < 4; i++)
    {
        t[2 + i] = PROTOCORE_HEX.lower[(v >> ((3 - i) * 4)) & 0xF];
    }
    return EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = t);
}

size_t protocore_ble_gatt_char_json(uint8_t *work, const GattChar *chars, size_t n, char *out, size_t cap)
{
    (void)work;

    if (!out || cap == 0 || (n && !chars))
    {
        return 0;
    }
    size_t b = 0;
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "[");
    for (size_t i = 0; i < n; i++)
    {
        if (i)
        {
            b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = ",");
        }
        b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "{\"handle\":");
        b = EMBED_CALL(verba_numerus.u32, VerbaNumerusCfg, .out = out, .cap = cap, .at = b, .val = chars[i].handle);
        b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = ",\"uuid\":\"");
        b = put_hex16(out, cap, b, chars[i].uuid);
        b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "\",\"props\":");
        b = EMBED_CALL(verba_numerus.u32, VerbaNumerusCfg, .out = out, .cap = cap, .at = b, .val = chars[i].props);
        b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "}");
    }
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "]");
    if (!EMBED_CALL(verba_finis.ok, VerbaFinisCfg, .cap = cap, .at = b))
    {
        return 0;
    }
    out[b] = '\0';
    return b;
}

#endif // PROTOCORE_ENABLE_BLE_GATT
