// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file zwave.c
 * @brief Z-Wave Serial API frame codec - implementation.
 *
 * Data frame: SOF | LEN | Type | Command | Data | Checksum, where LEN counts Type..Checksum
 * and Checksum = 0xFF XOR-folded over LEN..last-data (Silicon Labs Serial API spec).
 */

#include "services/radio/zwave/zwave.h"

#if PC_ENABLE_ZWAVE

// Checksum: 0xFF XORed with every byte from LEN through the last data byte.
static uint8_t checksum(const uint8_t *from_len, uint16_t n)
{
    uint8_t c = 0xFF;
    for (uint16_t i = 0; i < n; i++)
    {
        c = (uint8_t)(c ^ from_len[i]);
    }
    return c;
}

uint16_t pc_zwave_build_frame(pc_zwave_type type, uint8_t cmd, const uint8_t *data, uint8_t data_len, uint8_t *out,
                              uint16_t cap)
{
    if (!out || data_len > PC_ZWAVE_MAX_DATA || (data == NULL && data_len > 0))
    {
        return 0;
    }
    uint8_t frame_len = (uint8_t)(data_len + 3); // Type + Command + Data + Checksum
    uint16_t total = (uint16_t)(2 + frame_len);  // SOF + LEN + frame_len bytes
    if (total > cap)
    {
        return 0;
    }
    out[0] = Zwave::ZWAVE_SOF;
    out[1] = frame_len;
    out[2] = (uint8_t)type;
    out[3] = cmd;
    for (uint8_t i = 0; i < data_len; i++)
    {
        out[4 + i] = data[i];
    }
    // Checksum folds LEN..last-data = out[1 .. 1+frame_len-1] = out[1..frame_len].
    out[1 + frame_len] = checksum(&out[1], frame_len);
    return total;
}

int pc_zwave_parse_frame(const uint8_t *raw, uint16_t len, uint8_t *type, uint8_t *cmd, const uint8_t **pdata,
                         uint8_t *pdata_len)
{
    if (!raw || len < 1)
    {
        return 0;
    }
    if (raw[0] != Zwave::ZWAVE_SOF)
    {
        return -1; // not a data frame (could be a control byte - test those first)
    }
    if (len < 2)
    {
        return 0;
    }
    uint8_t frame_len = raw[1];
    if (frame_len < 3 || frame_len > PC_ZWAVE_MAX_DATA + 3)
    {
        return -1; // too short for Type+Cmd+Checksum, or implausibly long
    }
    uint16_t total = (uint16_t)(2 + frame_len);
    if (len < total)
    {
        return 0; // wait for the rest
    }
    if (checksum(&raw[1], frame_len) != raw[1 + frame_len])
    {
        return -1; // checksum mismatch
    }
    if (type)
    {
        *type = raw[2];
    }
    if (cmd)
    {
        *cmd = raw[3];
    }
    if (pdata)
    {
        *pdata = &raw[4];
    }
    if (pdata_len)
    {
        *pdata_len = (uint8_t)(frame_len - 3);
    }
    return (int)total;
}

proto_bool pc_zwave_is_ack(uint8_t b)
{
    return b == Zwave::ZWAVE_ACK;
}
proto_bool pc_zwave_is_nak(uint8_t b)
{
    return b == Zwave::ZWAVE_NAK;
}
proto_bool pc_zwave_is_can(uint8_t b)
{
    return b == Zwave::ZWAVE_CAN;
}

uint16_t pc_zwave_build_ack(uint8_t *out, uint16_t cap)
{
    if (!out || cap < 1)
    {
        return 0;
    }
    out[0] = Zwave::ZWAVE_ACK;
    return 1;
}

#endif // PC_ENABLE_ZWAVE
