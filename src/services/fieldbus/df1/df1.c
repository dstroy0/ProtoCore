// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file df1.c
 * @brief Allen-Bradley DF1 full-duplex frame builder + parser (pure, host-tested).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_DF1

#include "services/fieldbus/df1/df1.h"
#include "shared/crc/crc.h" // PROTOCORE_CRC16_ARC

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void df1_bcc(uint8_t *restrict work);

static void df1_bcc(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *data = Df1.bcc_args.data;
    size_t len = Df1.bcc_args.len;

    uint8_t s = 0;
    for (size_t i = 0; i < len; i++)
    {
        s = (uint8_t)(s + data[i]);
    }
    Df1.value = (uint8_t)(0u - s); // 2's complement (modulo 256)
}

// DF1's block check is the reflected CRC-16 (poly 0xA001 = reflect(0x8005), init 0, no final XOR), cataloged
// as CRC-16/ARC. The data and the ETX are two runs, folded into one register.
static uint16_t df1_crc_data_plus_etx(const uint8_t *data, size_t len, uint8_t etx)
{
    Crc.args.params = &PROTOCORE_CRC16_ARC;
    Crc.begin(Crc.internal);
    Crc.args.crc = Crc.value;
    Crc.args.data = data;
    Crc.args.len = len;
    Crc.update(Crc.internal);
    Crc.args.crc = Crc.value;
    Crc.args.data = &etx;
    Crc.args.len = 1;
    Crc.update(Crc.internal);
    Crc.args.crc = Crc.value;
    Crc.final(Crc.internal);
    return (uint16_t)Crc.value;
}

static void df1_crc(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *data = Df1.crc_args.data;
    size_t len = Df1.crc_args.len;

    Crc.args.params = &PROTOCORE_CRC16_ARC;
    Crc.args.data = data;
    Crc.args.len = len;
    Crc.compute(Crc.internal);
    Df1.u16 = (uint16_t)Crc.value;
}

static void df1_build_frame(uint8_t *restrict work)
{
    uint8_t *buf = Df1.build_frame_args.buf;
    size_t cap = Df1.build_frame_args.cap;
    const uint8_t *data = Df1.build_frame_args.data;
    size_t data_len = Df1.build_frame_args.data_len;
    Df1Check check = Df1.build_frame_args.check;

    if (!buf || (data_len && !data))
    {
        Df1.n = 0;
        return;
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
        Df1.n = 0;
        return;
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
        Df1.bcc_args.data = data;
        Df1.bcc_args.len = data_len;
        df1_bcc(work);
        buf[p++] = Df1.value; // BCC excludes the ETX
    }
    Df1.n = p;
}

static void df1_parse_frame(uint8_t *restrict work)
{
    const uint8_t *buf = Df1.parse_frame_args.buf;
    size_t len = Df1.parse_frame_args.len;
    Df1Check check = Df1.parse_frame_args.check;
    uint8_t *out = Df1.parse_frame_args.out;
    size_t out_cap = Df1.parse_frame_args.out_cap;
    size_t *out_len = Df1.parse_frame_args.out_len;

    size_t checklen = (check == DF1_CHECK_CRC) ? 2 : 1;
    if (!buf || !out || len < 4 + checklen) // DLE STX DLE ETX + check
    {
        Df1.ok = PROTO_FALSE;
        return;
    }
    if (buf[0] != DF1_DLE || buf[1] != DF1_STX)
    {
        Df1.ok = PROTO_FALSE;
        return;
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
                Df1.ok = PROTO_FALSE;
                return;
            }
            uint8_t next = buf[i + 1];
            if (next == DF1_DLE) // doubled DLE -> one 0x10 data byte
            {
                if (o >= out_cap)
                {
                    Df1.ok = PROTO_FALSE;
                    return;
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
                Df1.ok = PROTO_FALSE;
                return; // an unexpected control symbol inside the data
            }
        }
        else
        {
            if (o >= out_cap)
            {
                Df1.ok = PROTO_FALSE;
                return;
            }
            out[o++] = buf[i++];
        }
    }
    if (!ended)
    {
        Df1.ok = PROTO_FALSE;
        return;
    }

    if (check == DF1_CHECK_CRC)
    {
        if (i + 2 > len)
        {
            Df1.ok = PROTO_FALSE;
            return;
        }
        uint16_t c = df1_crc_data_plus_etx(out, o, DF1_ETX);
        uint16_t got = (uint16_t)(buf[i] | ((uint16_t)buf[i + 1] << 8)); // low byte first
        if (c != got)
        {
            Df1.ok = PROTO_FALSE;
            return;
        }
    }
    else
    {
        if (i + 1 > len)
        {
            Df1.ok = PROTO_FALSE;
            return;
        }
        Df1.bcc_args.data = out;
        Df1.bcc_args.len = o;
        df1_bcc(work);
        if (Df1.value != buf[i])
        {
            Df1.ok = PROTO_FALSE;
            return;
        }
    }
    if (out_len)
    {
        *out_len = o;
    }
    Df1.ok = PROTO_TRUE;
}

Df1Ns Df1 = {.bcc = df1_bcc, .crc = df1_crc, .build_frame = df1_build_frame, .parse_frame = df1_parse_frame};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DF1
