// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file zigbee.c
 * @brief Zigbee EZSP / ASH framing codec - implementation.
 *
 * ASH (UG101): [control | payload | CRC16] byte-stuffed and Flag-terminated. The CRC is
 * CRC-16/CCITT (poly 0x1021, init 0xFFFF); the reserved bytes 0x7E / 0x7D / 0x11 / 0x13 /
 * 0x18 / 0x1A are escaped as 0x7D, (byte XOR 0x20).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t crc_work[16]; // the borrow an entry takes; Crc never reads it

#if PROTOCORE_ENABLE_ZIGBEE

#include "services/radio/zigbee/zigbee.h"
#include "shared/crc/crc.h" // PROTOCORE_CRC16_IBM_3740

PROTOCORE_BEGIN_DECLS

static proto_bool is_reserved(uint8_t b)
{
    return b == 0x7E || b == 0x7D || b == 0x11 || b == 0x13 || b == 0x18 || b == 0x1A;
}

// Append a byte to out with ASH stuffing; return false if it would overflow cap.
static proto_bool put_stuffed(uint8_t *out, uint16_t *p, uint16_t cap, uint8_t b)
{
    if (is_reserved(b))
    {
        if (*p + 2 > cap)
        {
            return PROTO_FALSE;
        }
        out[(*p)++] = ASH_ESCAPE;
        out[(*p)++] = (uint8_t)(b ^ 0x20);
    }
    else
    {
        if (*p + 1 > cap)
        {
            return PROTO_FALSE;
        }
        out[(*p)++] = b;
    }
    return PROTO_TRUE;
}

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void zigbee_ash_crc16(uint8_t *restrict work);

static void zigbee_ash_crc16(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = Zigbee.ash_crc16_args.buf;
    uint16_t len = Zigbee.ash_crc16_args.len;

    // ASH uses CRC-CCITT (poly 0x1021, init 0xFFFF, unreflected), cataloged as CRC-16/IBM-3740.
    Crc.args.params = &PROTOCORE_CRC16_IBM_3740;
    Crc.args.data = buf;
    Crc.args.len = len;
    Crc.compute(crc_work);
    Zigbee.value = (uint16_t)Crc.value;
}

static void zigbee_ash_frame_encode(uint8_t *restrict work)
{
    (void)work;
    uint8_t control = Zigbee.ash_frame_encode_args.control;
    const uint8_t *payload = Zigbee.ash_frame_encode_args.payload;
    uint16_t len = Zigbee.ash_frame_encode_args.len;
    uint8_t *out = Zigbee.ash_frame_encode_args.out;
    uint16_t cap = Zigbee.ash_frame_encode_args.cap;

    if (!out || len > PROTOCORE_ZIGBEE_MAX_DATA || (payload == NULL && len > 0))
    {
        Zigbee.value = 0;
        return;
    }
    // CRC over control + payload. They are not contiguous in memory, which is what the engine's
    // begin/update/final split is for - no scratch buffer to assemble them into.
    Crc.args.params = &PROTOCORE_CRC16_IBM_3740;
    Crc.begin(crc_work);
    Crc.args.crc = Crc.value;
    Crc.args.data = &control;
    Crc.args.len = 1;
    Crc.update(crc_work);
    Crc.args.crc = Crc.value;
    Crc.args.data = payload;
    Crc.args.len = len;
    Crc.update(crc_work);
    Crc.args.crc = Crc.value;
    Crc.final(crc_work);
    const uint16_t crc = (uint16_t)Crc.value;

    uint16_t p = 0;
    if (!put_stuffed(out, &p, cap, control))
    {
        Zigbee.value = 0;
        return;
    }
    for (uint16_t i = 0; i < len; i++)
    {
        if (!put_stuffed(out, &p, cap, payload[i]))
        {
            Zigbee.value = 0;
            return;
        }
    }
    if (!put_stuffed(out, &p, cap, (uint8_t)(crc >> 8)) || !put_stuffed(out, &p, cap, (uint8_t)(crc & 0xFF)))
    {
        Zigbee.value = 0;
        return;
    }
    if (p + 1 > cap)
    {
        Zigbee.value = 0;
        return;
    }
    out[p++] = ASH_FLAG; // the delimiter is never stuffed
    Zigbee.value = p;
}

static void zigbee_ash_frame_decode(uint8_t *restrict work)
{
    const uint8_t *raw = Zigbee.ash_frame_decode_args.raw;
    uint16_t len = Zigbee.ash_frame_decode_args.len;
    uint8_t *control = Zigbee.ash_frame_decode_args.control;
    uint8_t *payload = Zigbee.ash_frame_decode_args.payload;
    uint16_t pay_cap = Zigbee.ash_frame_decode_args.pay_cap;
    uint16_t *pay_len = Zigbee.ash_frame_decode_args.pay_len;

    if (!raw)
    {
        Zigbee.n = 0;
        return;
    }
    // Find the frame delimiter.
    uint16_t flag = 0;
    while (flag < len && raw[flag] != ASH_FLAG)
    {
        flag++;
    }
    if (flag >= len)
    {
        Zigbee.n = 0; // no complete frame yet
        return;
    }

    // Remove the byte-stuffing from raw[0, flag) into a fixed scratch: control + payload + CRC(2).
    uint8_t un[PROTOCORE_ZIGBEE_MAX_DATA + 3];
    uint16_t n = 0;
    for (uint16_t i = 0; i < flag; i++)
    {
        uint8_t b = raw[i];
        if (b == ASH_ESCAPE)
        {
            if (++i >= flag)
            {
                Zigbee.n = -1; // dangling escape
                return;
            }
            b = (uint8_t)(raw[i] ^ 0x20);
        }
        if (n >= sizeof(un))
        {
            Zigbee.n = -1; // frame longer than we accept
            return;
        }
        un[n++] = b;
    }
    if (n < 3)
    {
        Zigbee.n = -1; // need at least control + CRC(2)
        return;
    }
    uint16_t body = (uint16_t)(n - 2);
    Zigbee.ash_crc16_args.buf = un;
    Zigbee.ash_crc16_args.len = body;
    zigbee_ash_crc16(work);
    uint16_t crc = Zigbee.value;
    if ((uint16_t)((un[n - 2] << 8) | un[n - 1]) != crc)
    {
        Zigbee.n = -1; // CRC mismatch
        return;
    }

    uint16_t plen = (uint16_t)(body - 1); // minus the control byte
    if (plen > pay_cap)
    {
        Zigbee.n = -1; // caller buffer too small
        return;
    }
    if (control)
    {
        *control = un[0];
    }
    for (uint16_t i = 0; i < plen; i++)
    {
        payload[i] = un[1 + i];
    }
    if (pay_len)
    {
        *pay_len = plen;
    }
    Zigbee.n = (int)(flag + 1); // consume up to and including the flag
}

ZigbeeNs Zigbee = {.ash_crc16 = zigbee_ash_crc16,
                   .ash_frame_encode = zigbee_ash_frame_encode,
                   .ash_frame_decode = zigbee_ash_frame_decode};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ZIGBEE
