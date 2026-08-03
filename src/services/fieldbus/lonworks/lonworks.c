// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file lonworks.c
 * @brief LonWorks / LON-IP network-variable codec (see lonworks.h).
 */

#include "services/fieldbus/lonworks/lonworks.h"

#if PC_ENABLE_LONWORKS

#include <string.h>

size_t pc_lon_build_nv(uint8_t msg_code, uint16_t selector, const uint8_t *value, size_t value_len, uint8_t *out,
                       size_t cap)
{
    if (!out || (value_len && !value) || selector > LON_NV_SELECTOR_MAX)
    {
        return 0;
    }
    size_t n = 3 + value_len;
    if (n > cap)
    {
        return 0;
    }
    out[0] = msg_code;
    out[1] = (uint8_t)(selector >> 8); // 14-bit selector, big-endian
    out[2] = (uint8_t)selector;
    if (value_len)
    {
        memcpy(out + 3, value, value_len);
    }
    return n;
}

proto_bool pc_lon_parse_nv(const uint8_t *pdu, size_t len, LonNv *out)
{
    if (!pdu || !out || len < 3)
    {
        return PROTO_FALSE;
    }
    out->msg_code = pdu[0];
    out->selector = (uint16_t)(((pdu[1] & 0x3F) << 8) | pdu[2]); // 14-bit
    out->value = (len > 3) ? (pdu + 3) : NULL;
    out->value_len = len - 3;
    return PROTO_TRUE;
}

void pc_lon_snvt_temp_encode(double celsius, uint8_t out[2])
{
    // SNVT_temp: kelvin in 0.01 K steps -> (celsius + 273.15) * 100, as a signed 16-bit big-endian.
    double kelvin_hundredths = (celsius + 273.15) * 100.0;
    int32_t v = (int32_t)(kelvin_hundredths >= 0 ? kelvin_hundredths + 0.5 : kelvin_hundredths - 0.5);
    if (v > 32767)
    {
        v = 32767;
    }
    if (v < -32768)
    {
        v = -32768;
    }
    uint16_t u = (uint16_t)v;
    out[0] = (uint8_t)(u >> 8);
    out[1] = (uint8_t)u;
}

double pc_lon_snvt_temp_decode(const uint8_t in[2])
{
    int16_t v = (int16_t)((in[0] << 8) | in[1]);
    return (double)v / 100.0 - 273.15;
}

void pc_lon_snvt_switch_encode(double percent, uint8_t state, uint8_t out[2])
{
    // SNVT_switch: value is 0..200 in 0.5% steps (0..100.5%), state is 0/1/0xFF.
    if (percent < 0)
    {
        percent = 0;
    }
    if (percent > 100.5)
    {
        percent = 100.5;
    }
    uint8_t v = (uint8_t)(percent * 2.0 + 0.5);
    out[0] = v;
    out[1] = state;
}

void pc_lon_snvt_switch_decode(const uint8_t in[2], double *percent, uint8_t *state)
{
    if (percent)
    {
        *percent = (double)in[0] / 2.0;
    }
    if (state)
    {
        *state = in[1];
    }
}

#endif // PC_ENABLE_LONWORKS
