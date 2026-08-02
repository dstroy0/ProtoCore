// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file df1.c
 * @brief Allen-Bradley DF1 full-duplex frame builder + parser (pure, host-tested).
 */

#include "services/fieldbus/df1/df1.h"
#include "shared_primitives/crc.h" // PC_CRC16_ARC

#if PC_ENABLE_DF1

uint8_t pc_df1_bcc(const uint8_t *data, size_t len)
{
    uint8_t s = 0;
    for (size_t i = 0; i < len; i++)
    {
        s = (uint8_t)(s + data[i]);
    }
    return (uint8_t)(0u - s); // 2's complement (modulo 256)
}

// DF1's block check is the reflected CRC-16 (poly 0xA001 = reflect(0x8005), init 0, no final XOR),
// cataloged as CRC-16/ARC. test_crc diffs the shared engine against the loop that used to live here
// over every length 0..64.
//
// The CRC covers the data *plus* a trailing ETX that is not adjacent to it in memory, so the callers
// below run the engine's begin/update/final split rather than assembling a scratch buffer. Note the
// running value they carry is the engine's internal register, which for a reflected CRC is held
// unreflected until final() - it is not interchangeable with the old right-shift intermediate, which
// is why those call sites convert as a whole rather than swapping one call.
static uint16_t df1_crc_data_plus_etx(const uint8_t *data, size_t len, uint8_t etx)
{
    uint32_t c = pc_crc_begin(&PC_CRC16_ARC);
    c = pc_crc_update(&PC_CRC16_ARC, c, data, len);
    c = pc_crc_update(&PC_CRC16_ARC, c, &etx, 1);
    return (uint16_t)pc_crc_final(&PC_CRC16_ARC, c);
}

uint16_t pc_df1_crc(const uint8_t *data, size_t len)
{
    return (uint16_t)pc_crc(&PC_CRC16_ARC, data, len);
}

size_t pc_df1_build_frame(uint8_t *buf, size_t cap, const uint8_t *data, size_t data_len, Df1Check check)
{
    if (!buf || (data_len && !data))
    {
        return 0;
    }
    size_t stuffed = data_len;
    for (size_t i = 0; i < data_len; i++)
    {
        if (data[i] == DF1_DLE)
        {
            stuffed++; // a DLE data byte is doubled on the wire
        }
    }
    size_t checklen = (check == DF1_CHECK_CRC) ? 2 : 1;
    size_t total = 2 + stuffed + 2 + checklen; // DLE STX + data + DLE ETX + check
    if (total > cap)
    {
        return 0;
    }

    size_t p = 0;
    buf[p++] = DF1_DLE;
    buf[p++] = DF1_STX;
    for (size_t i = 0; i < data_len; i++)
    {
        buf[p++] = data[i];
        if (data[i] == DF1_DLE)
        {
            buf[p++] = DF1_DLE;
        }
    }
    buf[p++] = DF1_DLE;
    buf[p++] = DF1_ETX;

    if (check == DF1_CHECK_CRC)
    {
        uint16_t c = df1_crc_data_plus_etx(data, data_len, DF1_ETX);
        buf[p++] = (uint8_t)(c & 0xFF); // low byte first
        buf[p++] = (uint8_t)(c >> 8);
    }
    else
    {
        buf[p++] = pc_df1_bcc(data, data_len); // BCC excludes the ETX
    }
    return p;
}

proto_bool pc_df1_parse_frame(const uint8_t *buf, size_t len, Df1Check check, uint8_t *out, size_t out_cap,
                              size_t *out_len)
{
    size_t checklen = (check == DF1_CHECK_CRC) ? 2 : 1;
    if (!buf || !out || len < 4 + checklen) // DLE STX DLE ETX + check
    {
        return PROTO_FALSE;
    }
    if (buf[0] != DF1_DLE || buf[1] != DF1_STX)
    {
        return PROTO_FALSE;
    }

    size_t i = 2;
    size_t o = 0;
    proto_bool ended = PROTO_FALSE;
    while (i < len)
    {
        if (buf[i] == DF1_DLE)
        {
            if (i + 1 >= len)
            {
                return PROTO_FALSE;
            }
            uint8_t next = buf[i + 1];
            if (next == DF1_DLE) // doubled DLE -> one 0x10 data byte
            {
                if (o >= out_cap)
                {
                    return PROTO_FALSE;
                }
                out[o++] = DF1_DLE;
                i += 2;
            }
            else if (next == DF1_ETX) // end of data
            {
                i += 2;
                ended = PROTO_TRUE;
                break;
            }
            else
            {
                return PROTO_FALSE; // an unexpected control symbol inside the data
            }
        }
        else
        {
            if (o >= out_cap)
            {
                return PROTO_FALSE;
            }
            out[o++] = buf[i++];
        }
    }
    if (!ended)
    {
        return PROTO_FALSE;
    }

    if (check == DF1_CHECK_CRC)
    {
        if (i + 2 > len)
        {
            return PROTO_FALSE;
        }
        uint16_t c = df1_crc_data_plus_etx(out, o, DF1_ETX);
        uint16_t got = (uint16_t)(buf[i] | ((uint16_t)buf[i + 1] << 8)); // low byte first
        if (c != got)
        {
            return PROTO_FALSE;
        }
    }
    else
    {
        if (i + 1 > len)
        {
            return PROTO_FALSE;
        }
        if (pc_df1_bcc(out, o) != buf[i])
        {
            return PROTO_FALSE;
        }
    }
    if (out_len)
    {
        *out_len = o;
    }
    return PROTO_TRUE;
}

#endif // PC_ENABLE_DF1
