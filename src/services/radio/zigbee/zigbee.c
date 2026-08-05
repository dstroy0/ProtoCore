// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file zigbee.c
 * @brief Zigbee EZSP / ASH framing codec - implementation.
 *
 * ASH (UG101): [control | payload | CRC16] byte-stuffed and Flag-terminated. The CRC is
 * CRC-16/CCITT (poly 0x1021, init 0xFFFF); the reserved bytes 0x7E / 0x7D / 0x11 / 0x13 /
 * 0x18 / 0x1A are escaped as 0x7D, (byte XOR 0x20).
 */

#include "services/radio/zigbee/zigbee.h"
#include "shared_primitives/crc.h" // PC_CRC16_IBM_3740

#if PC_ENABLE_ZIGBEE

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

uint16_t pc_ash_crc16(const uint8_t *buf, uint16_t len)
{
    // ASH uses CRC-CCITT (poly 0x1021, init 0xFFFF, unreflected), cataloged as CRC-16/IBM-3740.
    return (uint16_t)pc_crc(&PC_CRC16_IBM_3740, buf, len);
}

uint16_t pc_ash_frame_encode(uint8_t control, const uint8_t *payload, uint16_t len, uint8_t *out, uint16_t cap)
{
    if (!out || len > PC_ZIGBEE_MAX_DATA || (payload == NULL && len > 0))
    {
        return 0;
    }
    // CRC over control + payload. They are not contiguous in memory, which is what the engine's
    // begin/update/final split is for - no scratch buffer to assemble them into.
    uint32_t c = pc_crc_begin(&PC_CRC16_IBM_3740);
    c = pc_crc_update(&PC_CRC16_IBM_3740, c, &control, 1);
    c = pc_crc_update(&PC_CRC16_IBM_3740, c, payload, len);
    const uint16_t crc = (uint16_t)pc_crc_final(&PC_CRC16_IBM_3740, c);

    uint16_t p = 0;
    if (!put_stuffed(out, &p, cap, control))
    {
        return 0;
    }
    for (uint16_t i = 0; i < len; i++)
    {
        if (!put_stuffed(out, &p, cap, payload[i]))
        {
            return 0;
        }
    }
    if (!put_stuffed(out, &p, cap, (uint8_t)(crc >> 8)) || !put_stuffed(out, &p, cap, (uint8_t)(crc & 0xFF)))
    {
        return 0;
    }
    if (p + 1 > cap)
    {
        return 0;
    }
    out[p++] = ASH_FLAG; // the delimiter is never stuffed
    return p;
}

int pc_ash_frame_decode(const uint8_t *raw, uint16_t len, uint8_t *control, uint8_t *payload, uint16_t pay_cap,
                        uint16_t *pay_len)
{
    if (!raw)
    {
        return 0;
    }
    // Find the frame delimiter.
    uint16_t flag = 0;
    while (flag < len && raw[flag] != ASH_FLAG)
    {
        flag++;
    }
    if (flag >= len)
    {
        return 0; // no complete frame yet
    }

    // Remove the byte-stuffing from raw[0, flag) into a fixed scratch: control + payload + CRC(2).
    uint8_t un[PC_ZIGBEE_MAX_DATA + 3];
    uint16_t n = 0;
    for (uint16_t i = 0; i < flag; i++)
    {
        uint8_t b = raw[i];
        if (b == ASH_ESCAPE)
        {
            if (++i >= flag)
            {
                return -1; // dangling escape
            }
            b = (uint8_t)(raw[i] ^ 0x20);
        }
        if (n >= sizeof(un))
        {
            return -1; // frame longer than we accept
        }
        un[n++] = b;
    }
    if (n < 3)
    {
        return -1; // need at least control + CRC(2)
    }
    uint16_t body = (uint16_t)(n - 2);
    uint16_t crc = pc_ash_crc16(un, body);
    if ((uint16_t)((un[n - 2] << 8) | un[n - 1]) != crc)
    {
        return -1; // CRC mismatch
    }

    uint16_t plen = (uint16_t)(body - 1); // minus the control byte
    if (plen > pay_cap)
    {
        return -1; // caller buffer too small
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
    return (int)(flag + 1); // consume up to and including the flag
}

#endif // PC_ENABLE_ZIGBEE
