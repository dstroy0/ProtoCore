// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sercos.c
 * @brief SERCOS III telegram + IDN codec (see sercos.h).
 */

#include "services/fieldbus/sercos/sercos.h"

#if PC_ENABLE_SERCOS

uint16_t pc_sercos_idn(proto_bool is_product, uint8_t param_set, uint16_t data_block)
{
    return (uint16_t)(((is_product ? 1u : 0u) << 15) | ((uint32_t)(param_set & 0x7) << 12) | (data_block & 0x0FFF));
}

void pc_sercos_idn_parse(uint16_t idn, proto_bool *is_product, uint8_t *param_set, uint16_t *data_block)
{
    if (is_product)
    {
        *is_product = (idn & 0x8000) != 0;
    }
    if (param_set)
    {
        *param_set = (uint8_t)((idn >> 12) & 0x7);
    }
    if (data_block)
    {
        *data_block = (uint16_t)(idn & 0x0FFF);
    }
}

size_t pc_sercos_build(uint8_t type, uint8_t phase, uint16_t cycle, const uint8_t *data, size_t data_len, uint8_t *out,
                       size_t cap)
{
    if (!out || (data_len && !data) || (type != SERCOS_TEL_MDT && type != SERCOS_TEL_AT))
    {
        return 0;
    }
    size_t n = SERCOS_HDR_LEN + data_len;
    if (n > cap)
    {
        return 0;
    }
    out[0] = type;
    out[1] = phase;
    out[2] = (uint8_t)cycle; // little-endian cycle count
    out[3] = (uint8_t)(cycle >> 8);
    if (data_len)
    {
        memcpy(out + SERCOS_HDR_LEN, data, data_len);
    }
    return n;
}

proto_bool pc_sercos_parse(const uint8_t *frame, size_t len, SercosTelegram *out)
{
    if (!frame || !out || len < SERCOS_HDR_LEN)
    {
        return PROTO_FALSE;
    }
    if (frame[0] != SERCOS_TEL_MDT && frame[0] != SERCOS_TEL_AT)
    {
        return PROTO_FALSE;
    }
    out->type = frame[0];
    out->phase = frame[1];
    out->cycle = (uint16_t)(frame[2] | (frame[3] << 8));
    out->data = (len > SERCOS_HDR_LEN) ? (frame + SERCOS_HDR_LEN) : NULL;
    out->data_len = len - SERCOS_HDR_LEN;
    return PROTO_TRUE;
}

#endif // PC_ENABLE_SERCOS
