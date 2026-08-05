// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file directnet.c
 * @brief AutomationDirect / Koyo DirectNET serial frame codec (see directnet.h).
 */

#include "services/fieldbus/directnet/directnet.h"

#if PC_ENABLE_DIRECTNET

static char hex_digit(uint8_t nibble)
{
    nibble &= 0x0F;
    return (char)(nibble < 10 ? ('0' + nibble) : ('A' + nibble - 10));
}
// Write @p value as @p digits ASCII-hex chars (big-endian) at p.
static void put_hex(uint8_t *p, uint32_t value, int digits)
{
    for (int i = 0; i < digits; i++)
    {
        p[i] = (uint8_t)hex_digit((uint8_t)(value >> (4 * (digits - 1 - i))));
    }
}

uint8_t pc_dnet_lrc(const uint8_t *bytes, size_t len)
{
    uint8_t lrc = 0;
    for (size_t i = 0; i < len; i++)
    {
        lrc ^= bytes[i];
    }
    return lrc;
}

size_t pc_dnet_header(uint8_t slave, uint8_t type, uint16_t address, uint8_t blocks, uint8_t *out, size_t cap)
{
    // SOH + slave(2) + type(1) + addr(4) + blocks(2) + ETB + LRC = 11 bytes.
    const size_t n = 1 + 2 + 1 + 4 + 2 + 1 + 1;
    if (!out || cap < n)
    {
        return 0;
    }
    size_t i = 0;
    out[i++] = DNET_SOH;
    put_hex(out + i, slave, 2);
    i += 2;
    out[i++] = type;
    put_hex(out + i, address, 4);
    i += 4;
    put_hex(out + i, blocks, 2);
    i += 2;
    out[i++] = DNET_ETB;
    // LRC over the framed body (slave..ETB), i.e. everything after SOH up to and including ETB.
    out[i] = pc_dnet_lrc(out + 1, i - 1);
    i++;
    return i;
}

size_t pc_dnet_data(const uint8_t *data, size_t data_len, uint8_t *out, size_t cap)
{
    if (!out || (data_len && !data))
    {
        return 0;
    }
    size_t n = 1 + data_len + 1 + 1; // STX + data + ETX + LRC
    if (n > cap)
    {
        return 0;
    }
    size_t i = 0;
    out[i++] = DNET_STX;
    if (data_len)
    {
        memcpy(out + i, data, data_len);
        i += data_len;
    }
    out[i++] = DNET_ETX;
    // LRC over data..ETX (everything after STX up to and including ETX).
    out[i] = pc_dnet_lrc(out + 1, i - 1);
    i++;
    return i;
}

proto_bool pc_dnet_data_parse(const uint8_t *frame, size_t len, const uint8_t **data, size_t *data_len)
{
    if (!frame || len < 3) // STX + ETX + LRC minimum
    {
        return PROTO_FALSE;
    }
    if (frame[0] != DNET_STX)
    {
        return PROTO_FALSE;
    }
    // The byte before the LRC must be ETX.
    size_t etx_idx = len - 2;
    if (frame[etx_idx] != DNET_ETX)
    {
        return PROTO_FALSE;
    }
    if (pc_dnet_lrc(frame + 1, len - 2) != frame[len - 1]) // over data..ETX
    {
        return PROTO_FALSE;
    }
    if (data)
    {
        *data = (etx_idx > 1) ? (frame + 1) : NULL;
    }
    if (data_len)
    {
        *data_len = etx_idx - 1;
    }
    return PROTO_TRUE;
}

#endif // PC_ENABLE_DIRECTNET
