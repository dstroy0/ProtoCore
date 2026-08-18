// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file zwave.c
 * @brief Z-Wave Serial API frame codec - implementation.
 *
 * Data frame: SOF | LEN | Type | Command | Data | Checksum, where LEN counts Type..Checksum
 * and Checksum = 0xFF XOR-folded over LEN..last-data (Silicon Labs Serial API spec).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_ZWAVE

#include "services/radio/zwave/zwave.h"

PROTOCORE_BEGIN_DECLS

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

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void zwave_build_frame(uint8_t *restrict work)
{
    (void)work;
    protocore_zwave_type type = Zwave.build_frame_args.type;
    uint8_t cmd = Zwave.build_frame_args.cmd;
    const uint8_t *data = Zwave.build_frame_args.data;
    uint8_t data_len = Zwave.build_frame_args.data_len;
    uint8_t *out = Zwave.build_frame_args.out;
    uint16_t cap = Zwave.build_frame_args.cap;

    if (!out || data_len > PROTOCORE_ZWAVE_MAX_DATA || (data == NULL && data_len > 0))
    {
        Zwave.value = 0;
        return;
    }
    uint8_t frame_len = (uint8_t)(data_len + 3); // Type + Command + Data + Checksum
    uint16_t total = (uint16_t)(2 + frame_len);  // SOF + LEN + frame_len bytes
    if (total > cap)
    {
        Zwave.value = 0;
        return;
    }
    out[0] = ZWAVE_SOF;
    out[1] = frame_len;
    out[2] = (uint8_t)type;
    out[3] = cmd;
    for (uint8_t i = 0; i < data_len; i++)
    {
        out[4 + i] = data[i];
    }
    // Checksum folds LEN..last-data = out[1 .. 1+frame_len-1] = out[1..frame_len].
    out[1 + frame_len] = checksum(&out[1], frame_len);
    Zwave.value = total;
}

static void zwave_parse_frame(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *raw = Zwave.parse_frame_args.raw;
    uint16_t len = Zwave.parse_frame_args.len;
    uint8_t *type = Zwave.parse_frame_args.type;
    uint8_t *cmd = Zwave.parse_frame_args.cmd;
    const uint8_t **pdata = Zwave.parse_frame_args.pdata;
    uint8_t *pdata_len = Zwave.parse_frame_args.pdata_len;

    if (!raw || len < 1)
    {
        Zwave.n = 0;
        return;
    }
    if (raw[0] != ZWAVE_SOF)
    {
        Zwave.n = -1; // not a data frame (could be a control byte - test those first)
        return;
    }
    if (len < 2)
    {
        Zwave.n = 0;
        return;
    }
    uint8_t frame_len = raw[1];
    if (frame_len < 3 || frame_len > PROTOCORE_ZWAVE_MAX_DATA + 3)
    {
        Zwave.n = -1; // too short for Type+Cmd+Checksum, or implausibly long
        return;
    }
    uint16_t total = (uint16_t)(2 + frame_len);
    if (len < total)
    {
        Zwave.n = 0; // wait for the rest
        return;
    }
    if (checksum(&raw[1], frame_len) != raw[1 + frame_len])
    {
        Zwave.n = -1; // checksum mismatch
        return;
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
    Zwave.n = (int)total;
}

static void zwave_is_ack(uint8_t *restrict work)
{
    (void)work;
    uint8_t b = Zwave.is_ack_args.b;

    Zwave.ok = b == ZWAVE_ACK;
}
static void zwave_is_nak(uint8_t *restrict work)
{
    (void)work;
    uint8_t b = Zwave.is_nak_args.b;

    Zwave.ok = b == ZWAVE_NAK;
}
static void zwave_is_can(uint8_t *restrict work)
{
    (void)work;
    uint8_t b = Zwave.is_can_args.b;

    Zwave.ok = b == ZWAVE_CAN;
}

static void zwave_build_ack(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = Zwave.build_ack_args.out;
    uint16_t cap = Zwave.build_ack_args.cap;

    if (!out || cap < 1)
    {
        Zwave.value = 0;
        return;
    }
    out[0] = ZWAVE_ACK;
    Zwave.value = 1;
}

ZwaveNs Zwave = {.build_frame = zwave_build_frame,
                 .parse_frame = zwave_parse_frame,
                 .is_ack = zwave_is_ack,
                 .is_nak = zwave_is_nak,
                 .is_can = zwave_is_can,
                 .build_ack = zwave_build_ack};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ZWAVE
