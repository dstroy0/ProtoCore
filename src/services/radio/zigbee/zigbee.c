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

#include "protocore_config.h" // the entry point: the widths

#include "services/radio/zigbee/zigbee.h"
#include "shared/crc/crc.h" // PROTOCORE_CRC16_IBM_3740

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

uint16_t protocore_zigbee_ash_crc16(uint8_t *work, const uint8_t *buf, uint16_t len)
{
    // ASH uses CRC-CCITT (poly 0x1021, init 0xFFFF, unreflected), cataloged as CRC-16/IBM-3740.
    CrcV.args.params = &PROTOCORE_CRC16_IBM_3740;
    CrcV.args.data = buf;
    CrcV.args.len = len;
    Crc.compute(work);
    return (uint16_t)CrcV.value;
}

uint16_t protocore_zigbee_ash_frame_encode(uint8_t *work, uint8_t control, const uint8_t *payload, uint16_t len,
                                           uint8_t *out, uint16_t cap)
{
    if (!out || len > PROTOCORE_ZIGBEE_MAX_DATA || (payload == NULL && len > 0))
    {
        return 0;
    }
    // CRC over control + payload. They are not contiguous in memory, which is what the engine's
    // begin/update/final split is for - no scratch buffer to assemble them into.
    CrcV.args.params = &PROTOCORE_CRC16_IBM_3740;
    Crc.begin(work);
    CrcV.args.crc = CrcV.value;
    CrcV.args.data = &control;
    CrcV.args.len = 1;
    Crc.update(work);
    CrcV.args.crc = CrcV.value;
    CrcV.args.data = payload;
    CrcV.args.len = len;
    Crc.update(work);
    CrcV.args.crc = CrcV.value;
    Crc.final(work);
    const uint16_t crc = (uint16_t)CrcV.value;

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

int protocore_zigbee_ash_frame_decode(uint8_t *work, const uint8_t *raw, uint16_t len, uint8_t *control,
                                      uint8_t *payload, uint16_t pay_cap, uint16_t *pay_len)
{
    int n_result = 0;
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
        n_result = 0; // no complete frame yet
        return n_result;
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
                n_result = -1; // dangling escape
                return n_result;
            }
            b = (uint8_t)(raw[i] ^ 0x20);
        }
        if (n >= sizeof(un))
        {
            n_result = -1; // frame longer than we accept
            return n_result;
        }
        un[n++] = b;
    }
    if (n < 3)
    {
        n_result = -1; // need at least control + CRC(2)
        return n_result;
    }
    uint16_t body = (uint16_t)(n - 2);
    uint16_t zigbee_value = Zigbee.ash_crc16(work, un, body);
    uint16_t crc = zigbee_value;
    if ((uint16_t)((un[n - 2] << 8) | un[n - 1]) != crc)
    {
        n_result = -1; // CRC mismatch
        return n_result;
    }

    uint16_t plen = (uint16_t)(body - 1); // minus the control byte
    if (plen > pay_cap)
    {
        n_result = -1; // caller buffer too small
        return n_result;
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
    n_result = (int)(flag + 1); // consume up to and including the flag
    return n_result;
}
