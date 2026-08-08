// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file nema_ts2.c
 * @brief NEMA TS 2 SDLC frame codec (see nema_ts2.h).
 */

#include "services/transportation/nema_ts2/nema_ts2.h"
#include "shared_primitives/crc.h" // PC_CRC16_X25

#if PC_ENABLE_NEMA_TS2

uint16_t pc_nema_ts2_crc(const uint8_t *bytes, size_t len)
{
    // CRC-16/X-25: reflected poly 0x8408 (reverse of 0x1021), init 0xFFFF, xorout 0xFFFF.
    return (uint16_t)pc_crc(&PC_CRC16_X25, bytes, len);
}

size_t pc_nema_ts2_build(uint8_t address, uint8_t control, uint8_t frame_type, const uint8_t *data, size_t data_len,
                         uint8_t *out, size_t cap)
{
    if (!out || (data_len && !data))
    {
        return 0;
    }
    size_t n = 3 + data_len + 2;
    if (n > cap)
    {
        return 0;
    }
    out[0] = address;
    out[1] = control;
    out[2] = frame_type;
    if (data_len)
    {
        memcpy(out + 3, data, data_len);
    }
    uint16_t crc = pc_nema_ts2_crc(out, 3 + data_len);
    out[3 + data_len] = (uint8_t)crc; // FCS low byte first
    out[3 + data_len + 1] = (uint8_t)(crc >> 8);
    return n;
}

proto_bool pc_nema_ts2_parse(const uint8_t *frame, size_t len, NemaTs2Frame *out)
{
    if (!frame || !out || len < 5) // address + control + frame_type + 2-byte FCS
    {
        return PROTO_FALSE;
    }
    size_t body = len - 2;
    uint16_t want = pc_nema_ts2_crc(frame, body);
    uint16_t got = (uint16_t)(frame[body] | (frame[body + 1] << 8));
    if (want != got)
    {
        return PROTO_FALSE;
    }
    out->address = frame[0];
    out->control = frame[1];
    out->frame_type = frame[2];
    out->data = (body > 3) ? (frame + 3) : NULL;
    out->data_len = body - 3;
    return PROTO_TRUE;
}

#endif // PC_ENABLE_NEMA_TS2
