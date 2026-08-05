// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mbplus.c
 * @brief Modbus Plus HDLC token-bus frame codec (see mbplus.h).
 */

#include "services/fieldbus/mbplus/mbplus.h"
#include "shared_primitives/crc.h" // PC_CRC16_X25

#if PC_ENABLE_MBPLUS

#include <string.h>

uint16_t pc_mbplus_crc(const uint8_t *bytes, size_t len)
{
    // CRC-16/X-25: reflected poly 0x8408, init 0xFFFF, xorout 0xFFFF.
    return (uint16_t)pc_crc(&PC_CRC16_X25, bytes, len);
}

size_t pc_mbplus_build(uint8_t address, uint8_t control, const uint8_t *payload, size_t payload_len, uint8_t *out,
                       size_t cap)
{
    if (!out || (payload_len && !payload) || address < 1 || address > MBPLUS_MAX_STATION)
    {
        return 0;
    }
    size_t n = 1 + 1 + 1 + payload_len + 2 + 1; // 7E addr ctrl payload CRClo CRChi 7E
    if (n > cap)
    {
        return 0;
    }
    size_t i = 0;
    out[i++] = MBPLUS_FLAG;
    size_t body = i;
    out[i++] = address;
    out[i++] = control;
    if (payload_len)
    {
        memcpy(out + i, payload, payload_len);
        i += payload_len;
    }
    uint16_t crc = pc_mbplus_crc(out + body, (i - body)); // over addr..last payload
    out[i++] = (uint8_t)crc;                              // CRC low byte first
    out[i++] = (uint8_t)(crc >> 8);
    out[i++] = MBPLUS_FLAG;
    return i;
}

proto_bool pc_mbplus_parse(const uint8_t *frame, size_t len, MbPlusFrame *out)
{
    // Min: 7E addr ctrl CRClo CRChi 7E = 6 bytes.
    if (!frame || !out || len < 6)
    {
        return PROTO_FALSE;
    }
    if (frame[0] != MBPLUS_FLAG || frame[len - 1] != MBPLUS_FLAG)
    {
        return PROTO_FALSE;
    }
    // Body is frame[1 .. len-2), the CRC is the last 2 body bytes.
    size_t body_end = len - 1;     // index of the trailing flag
    size_t crc_pos = body_end - 2; // low byte of the CRC
    size_t covered = crc_pos - 1;  // number of bytes (addr..payload) the CRC covers
    uint16_t want = pc_mbplus_crc(frame + 1, covered);
    uint16_t got = (uint16_t)(frame[crc_pos] | (frame[crc_pos + 1] << 8));
    if (want != got)
    {
        return PROTO_FALSE;
    }
    out->address = frame[1];
    out->control = frame[2];
    out->payload = (covered > 2) ? (frame + 3) : NULL;
    out->payload_len = covered - 2; // minus addr + ctrl
    return PROTO_TRUE;
}

uint8_t pc_mbplus_next_token(uint8_t current, uint8_t max_station)
{
    if (max_station < 1)
    {
        return 1;
    }
    return (current >= max_station) ? 1 : (uint8_t)(current + 1);
}

#endif // PC_ENABLE_MBPLUS
