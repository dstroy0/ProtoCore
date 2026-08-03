// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wave.c
 * @brief IEEE 1609 WAVE codec (see wave.h).
 */

#include "services/transportation/wave/wave.h"

#if PC_ENABLE_WAVE

#include <string.h>

size_t pc_wave_encode_psid(uint32_t psid, uint8_t *out, size_t cap)
{
    // P-encoding: the number of leading 1 bits in the first octet gives the length.
    if (psid < 0x80)
    {
        if (cap < 1)
        {
            return 0;
        }
        out[0] = (uint8_t)psid;
        return 1;
    }
    if (psid < 0x4000)
    {
        if (cap < 2)
        {
            return 0;
        }
        out[0] = (uint8_t)(0x80 | (psid >> 8));
        out[1] = (uint8_t)psid;
        return 2;
    }
    if (psid < 0x200000)
    {
        if (cap < 3)
        {
            return 0;
        }
        out[0] = (uint8_t)(0xC0 | (psid >> 16));
        out[1] = (uint8_t)(psid >> 8);
        out[2] = (uint8_t)psid;
        return 3;
    }
    if (cap < 4)
    {
        return 0;
    }
    out[0] = (uint8_t)(0xE0 | (psid >> 24));
    out[1] = (uint8_t)(psid >> 16);
    out[2] = (uint8_t)(psid >> 8);
    out[3] = (uint8_t)psid;
    return 4;
}

size_t pc_wave_decode_psid(const uint8_t *in, size_t len, uint32_t *psid)
{
    if (!in || len < 1 || !psid)
    {
        return 0;
    }
    uint8_t b0 = in[0];
    if ((b0 & 0x80) == 0)
    {
        *psid = b0;
        return 1;
    }
    if ((b0 & 0xC0) == 0x80)
    {
        if (len < 2)
        {
            return 0;
        }
        *psid = (uint32_t)((b0 & 0x3F) << 8) | in[1];
        return 2;
    }
    if ((b0 & 0xE0) == 0xC0)
    {
        if (len < 3)
        {
            return 0;
        }
        *psid = (uint32_t)((b0 & 0x1F) << 16) | ((uint32_t)in[1] << 8) | in[2];
        return 3;
    }
    if ((b0 & 0xF0) == 0xE0)
    {
        if (len < 4)
        {
            return 0;
        }
        *psid = (uint32_t)((b0 & 0x0F) << 24) | ((uint32_t)in[1] << 16) | ((uint32_t)in[2] << 8) | in[3];
        return 4;
    }
    return 0;
}

size_t pc_wsmp_build(uint32_t psid, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t cap)
{
    if (!out || (payload_len && !payload) || payload_len > 255)
    {
        return 0;
    }
    if (cap < 1)
    {
        return 0;
    }
    size_t i = 0;
    out[i++] = WSMP_VERSION; // version/subtype (low nibble = version)
    size_t p = pc_wave_encode_psid(psid, out + i, cap - i);
    if (!p)
    {
        return 0;
    }
    i += p;
    if (i + 1 + payload_len > cap)
    {
        return 0;
    }
    out[i++] = (uint8_t)payload_len; // WSM length
    if (payload_len)
    {
        memcpy(out + i, payload, payload_len);
        i += payload_len;
    }
    return i;
}

proto_bool pc_wsmp_parse(const uint8_t *frame, size_t len, WsmpFrame *out)
{
    if (!frame || !out || len < 3)
    {
        return PROTO_FALSE;
    }
    if ((frame[0] & 0x0F) != WSMP_VERSION)
    {
        return PROTO_FALSE;
    }
    uint32_t psid = 0;
    size_t p = pc_wave_decode_psid(frame + 1, len - 1, &psid);
    if (!p)
    {
        return PROTO_FALSE;
    }
    size_t off = 1 + p;
    if (off + 1 > len)
    {
        return PROTO_FALSE;
    }
    uint8_t wlen = frame[off++];
    if (off + wlen > len)
    {
        return PROTO_FALSE;
    }
    out->psid = psid;
    out->payload = wlen ? (frame + off) : NULL;
    out->payload_len = wlen;
    return PROTO_TRUE;
}

size_t pc_wave_1609dot2_wrap(uint8_t content_type, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t cap)
{
    if (!out || (payload_len && !payload))
    {
        return 0;
    }
    size_t n = 2 + payload_len;
    if (n > cap)
    {
        return 0;
    }
    out[0] = WAVE_16092_VERSION;
    out[1] = content_type;
    if (payload_len)
    {
        memcpy(out + 2, payload, payload_len);
    }
    return n;
}

#endif // PC_ENABLE_WAVE
