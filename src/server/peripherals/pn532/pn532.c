// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pn532.c
 * @brief PN532 NFC frame codec - implementation.
 *
 * Normal information frame: 00 | 00 FF | LEN | LCS | TFI | PData | DCS | 00, where
 * (LEN + LCS) & 0xFF == 0 and (TFI + sum(PData) + DCS) & 0xFF == 0 (PN532 User Manual).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_PN532

#include "server/peripherals/pn532/pn532.h"

PROTOCORE_BEGIN_DECLS

static const uint8_t ACK[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_pn532_build_frame(uint8_t *restrict work)
{
    (void)work;
    uint8_t tfi = Pn532V.build_frame_args.tfi;
    const uint8_t *data = Pn532V.build_frame_args.data;
    uint8_t len = Pn532V.build_frame_args.len;
    uint8_t *out = Pn532V.build_frame_args.out;
    uint16_t cap = Pn532V.build_frame_args.cap;

    if (!out || len > PROTOCORE_PN532_MAX_DATA || (data == NULL && len > 0))
    {
        Pn532V.len = 0;
        return;
    }
    uint16_t total = (uint16_t)(8 + len); // preamble+start(3) + LEN+LCS(2) + TFI + data + DCS + postamble
    if (total > cap)
    {
        Pn532V.len = 0;
        return;
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
    Pn532V.len = total;
}

void protocore_pn532_parse_frame(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *raw = Pn532V.parse_frame_args.raw;
    uint16_t len = Pn532V.parse_frame_args.len;
    uint8_t *tfi = Pn532V.parse_frame_args.tfi;
    const uint8_t **pdata = Pn532V.parse_frame_args.pdata;
    uint8_t *pdata_len = Pn532V.parse_frame_args.pdata_len;

    if (!raw || len < 1)
    {
        Pn532V.n = 0;
        return;
    }
    if (raw[0] != 0x00)
    {
        Pn532V.n = -1;
        return; // preamble
    }
    if (len < 5)
    {
        Pn532V.n = 0;
        return; // need 00 00 FF LEN LCS
    }
    if (raw[1] != 0x00 || raw[2] != 0xFF)
    {
        Pn532V.n = -1;
        return; // start code
    }
    uint8_t frame_len = raw[3];
    if ((uint8_t)(frame_len + raw[4]) != 0)
    {
        Pn532V.n = -1;
        return; // length checksum
    }
    if (frame_len == 0 || frame_len > PROTOCORE_PN532_MAX_DATA + 1)
    {
        Pn532V.n = -1;
        return; // no TFI, or implausible length
    }
    uint16_t total = (uint16_t)(7 + frame_len);
    if (len < total)
    {
        Pn532V.n = 0;
        return; // wait for TFI + PData + DCS + postamble
    }
    uint8_t sum = 0;
    for (uint8_t i = 0; i < frame_len; i++)
    {
        sum = (uint8_t)(sum + raw[5 + i]); // TFI + PData
    }
    if ((uint8_t)(sum + raw[5 + frame_len]) != 0)
    {
        Pn532V.n = -1;
        return; // data checksum
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
    Pn532V.n = (int)total;
}

void protocore_pn532_is_ack(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *raw = Pn532V.is_ack_args.raw;
    uint16_t len = Pn532V.is_ack_args.len;

    if (!raw || len < 6)
    {
        Pn532V.ok = PROTO_FALSE;
        return;
    }
    for (uint8_t i = 0; i < 6; i++)
    {
        if (raw[i] != ACK[i])
        {
            Pn532V.ok = PROTO_FALSE;
            return;
        }
    }
    Pn532V.ok = PROTO_TRUE;
}

void protocore_pn532_build_ack(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = Pn532V.build_ack_args.out;
    uint16_t cap = Pn532V.build_ack_args.cap;

    if (!out || cap < 6)
    {
        Pn532V.len = 0;
        return;
    }
    for (uint8_t i = 0; i < 6; i++)
    {
        out[i] = ACK[i];
    }
    Pn532V.len = 6;
}

/** @brief The operands and the outcome. */
Pn532Vars Pn532V;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_PN532
