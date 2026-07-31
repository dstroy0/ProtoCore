// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pn532.cpp
 * @brief PN532 NFC frame codec - implementation.
 *
 * Normal information frame: 00 | 00 FF | LEN | LCS | TFI | PData | DCS | 00, where
 * (LEN + LCS) & 0xFF == 0 and (TFI + sum(PData) + DCS) & 0xFF == 0 (PN532 User Manual).
 */

#include "services/peripherals/pn532/pn532.h"

#if PC_ENABLE_PN532

namespace
{
const uint8_t ACK[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
}

uint16_t pc_pn532_build_frame(uint8_t tfi, const uint8_t *data, uint8_t len, uint8_t *out, uint16_t cap)
{
    if (!out || len > PC_PN532_MAX_DATA || (data == nullptr && len > 0))
    {
        return 0;
    }
    uint16_t total = (uint16_t)(8 + len); // preamble+start(3) + LEN+LCS(2) + TFI + data + DCS + postamble
    if (total > cap)
    {
        return 0;
    }
    uint8_t frame_len = (uint8_t)(1 + len); // TFI + PData
    out[0] = 0x00;
    out[1] = 0x00;
    out[2] = 0xFF;
    out[3] = frame_len;
    out[4] = (uint8_t)(0x100 - frame_len); // LCS: LEN + LCS == 0
    out[5] = tfi;
    uint8_t sum = tfi;
    for (uint8_t i = 0; i < len; i++)
    {
        out[6 + i] = data[i];
        sum = (uint8_t)(sum + data[i]);
    }
    out[6 + len] = (uint8_t)(0x100 - sum); // DCS: TFI + sum(PData) + DCS == 0
    out[7 + len] = 0x00;                   // postamble
    return total;
}

int pc_pn532_parse_frame(const uint8_t *raw, uint16_t len, uint8_t *tfi, const uint8_t **pdata, uint8_t *pdata_len)
{
    if (!raw || len < 1)
    {
        return 0;
    }
    if (raw[0] != 0x00)
    {
        return -1; // preamble
    }
    if (len < 5)
    {
        return 0; // need 00 00 FF LEN LCS
    }
    if (raw[1] != 0x00 || raw[2] != 0xFF)
    {
        return -1; // start code
    }
    uint8_t frame_len = raw[3];
    if ((uint8_t)(frame_len + raw[4]) != 0)
    {
        return -1; // length checksum
    }
    if (frame_len == 0 || frame_len > PC_PN532_MAX_DATA + 1)
    {
        return -1; // no TFI, or implausible length
    }
    uint16_t total = (uint16_t)(7 + frame_len);
    if (len < total)
    {
        return 0; // wait for TFI + PData + DCS + postamble
    }
    uint8_t sum = 0;
    for (uint8_t i = 0; i < frame_len; i++)
    {
        sum = (uint8_t)(sum + raw[5 + i]); // TFI + PData
    }
    if ((uint8_t)(sum + raw[5 + frame_len]) != 0)
    {
        return -1; // data checksum
    }
    if (tfi)
    {
        *tfi = raw[5];
    }
    if (pdata)
    {
        *pdata = &raw[6];
    }
    if (pdata_len)
    {
        *pdata_len = (uint8_t)(frame_len - 1);
    }
    return (int)total;
}

bool pc_pn532_is_ack(const uint8_t *raw, uint16_t len)
{
    if (!raw || len < 6)
    {
        return false;
    }
    for (uint8_t i = 0; i < 6; i++)
    {
        if (raw[i] != ACK[i])
        {
            return false;
        }
    }
    return true;
}

uint16_t pc_pn532_build_ack(uint8_t *out, uint16_t cap)
{
    if (!out || cap < 6)
    {
        return 0;
    }
    for (uint8_t i = 0; i < 6; i++)
    {
        out[i] = ACK[i];
    }
    return 6;
}

#endif // PC_ENABLE_PN532
