// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file enocean.c
 * @brief EnOcean ESP3 serial codec - implementation.
 *
 * ESP3 telegram: 0x55 | data-len(2) | opt-len(1) | type(1) | CRC8H | data | opt | CRC8D.
 * CRC-8 is polynomial 0x07, MSB-first, init 0x00 (the ESP3 u8CRC8Table generator).
 */

#include "services/radio/enocean/enocean.h"

#if PC_ENABLE_ENOCEAN

#include <string.h>

#include "shared_primitives/crc.h" // PC_CRC8_SMBUS

uint8_t pc_esp3_crc8(const uint8_t *buf, uint16_t len)
{
    // The ESP3 CRC-8 (the u8CRC8Table generator) is the cataloge's CRC-8/SMBUS: poly 0x07,
    // MSB-first, init 0, no final XOR. test_crc diffs the shared engine against the loop that used
    // to live here over every length 0..64, so this is byte-identical to it.
    return (uint8_t)pc_crc(&PC_CRC8_SMBUS, buf, len);
}

int pc_esp3_parse(const uint8_t *raw, uint16_t len, pc_esp3_packet *out)
{
    if (!raw || len < 1)
    {
        return 0;
    }
    if (raw[0] != ESP3_SYNC)
    {
        return -1; // not a telegram start
    }
    if (len < 6)
    {
        return 0; // need sync + 4-byte header + CRC8H
    }
    uint16_t data_len = (uint16_t)((raw[1] << 8) | raw[2]);
    uint8_t opt_len = raw[3];
    uint8_t type = raw[4];
    if (data_len > PC_ENOCEAN_MAX_DATA)
    {
        return -1; // implausible length -> resynchronize
    }
    if (pc_esp3_crc8(&raw[1], 4) != raw[5])
    {
        return -1; // header CRC mismatch
    }
    uint32_t total = 6u + data_len + opt_len + 1u;
    if (len < total)
    {
        return 0; // wait for the rest of the telegram
    }
    if (pc_esp3_crc8(&raw[6], (uint16_t)(data_len + opt_len)) != raw[6 + data_len + opt_len])
    {
        return -1; // data CRC mismatch
    }
    if (out)
    {
        out->type = (pc_esp3_type)type;
        out->data = &raw[6];
        out->data_len = data_len;
        out->opt = &raw[6 + data_len];
        out->opt_len = opt_len;
    }
    return (int)total;
}

uint16_t pc_esp3_build(pc_esp3_type type, const uint8_t *data, uint16_t data_len, const uint8_t *opt, uint8_t opt_len,
                       uint8_t *out, uint16_t cap)
{
    if (!out || data_len > PC_ENOCEAN_MAX_DATA)
    {
        return 0;
    }
    uint32_t total = 6u + data_len + opt_len + 1u;
    if (total > cap)
    {
        return 0;
    }
    out[0] = ESP3_SYNC;
    out[1] = (uint8_t)(data_len >> 8);
    out[2] = (uint8_t)(data_len & 0xFF);
    out[3] = opt_len;
    out[4] = (uint8_t)type;
    out[5] = pc_esp3_crc8(&out[1], 4);
    for (uint16_t i = 0; i < data_len; i++)
    {
        out[6 + i] = data[i];
    }
    for (uint8_t i = 0; i < opt_len; i++)
    {
        out[6 + data_len + i] = opt[i];
    }
    out[6 + data_len + opt_len] = pc_esp3_crc8(&out[6], (uint16_t)(data_len + opt_len));
    return (uint16_t)total;
}

proto_bool pc_erp1_parse(const uint8_t *data, uint16_t len, pc_erp1 *out)
{
    if (!data || !out || len < 6) // RORG(1) + sender id(4) + status(1)
    {
        return PROTO_FALSE;
    }
    out->rorg = data[0];
    out->payload = (len > 6) ? data + 1 : NULL;
    out->payload_len = (uint8_t)(len - 6);
    const uint8_t *id = data + len - 5; // the 4-octet sender id precedes the status octet
    out->sender_id =
        ((uint32_t)id[0] << 24) | ((uint32_t)id[1] << 16) | ((uint32_t)id[2] << 8) | (uint32_t)id[3]; // big-endian
    out->status = data[len - 1];
    return PROTO_TRUE;
}

uint16_t pc_erp1_build(uint8_t *out, uint16_t cap, uint8_t rorg, const uint8_t *payload, uint8_t payload_len,
                       uint32_t sender_id, uint8_t status)
{
    if (!out || (payload_len && !payload))
    {
        return 0;
    }
    uint16_t total = (uint16_t)(1 + payload_len + 4 + 1); // RORG + payload + sender id + status
    if (total > cap)
    {
        return 0;
    }
    uint16_t p = 0;
    out[p++] = rorg;
    if (payload_len)
    {
        memcpy(out + p, payload, payload_len);
        p = (uint16_t)(p + payload_len);
    }
    out[p++] = (uint8_t)(sender_id >> 24); // 4-octet sender id, big-endian
    out[p++] = (uint8_t)(sender_id >> 16);
    out[p++] = (uint8_t)(sender_id >> 8);
    out[p++] = (uint8_t)sender_id;
    out[p++] = status;
    return p;
}

#endif // PC_ENABLE_ENOCEAN
