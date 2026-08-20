// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sercos.c
 * @brief SERCOS III telegram + IDN codec (see sercos.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SERCOS

#include "mmgr/protomem/protomem.h"
#include "services/fieldbus/sercos/sercos.h"

PROTOCORE_BEGIN_DECLS

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void sercos_idn(uint8_t *restrict work)
{
    (void)work;
    proto_bool is_product = Sercos.idn_args.is_product;
    uint8_t param_set = Sercos.idn_args.param_set;
    uint16_t data_block = Sercos.idn_args.data_block;

    Sercos.value =
        (uint16_t)(((is_product ? 1u : 0u) << 15) | ((uint32_t)(param_set & 0x7) << 12) | (data_block & 0x0FFF));
}

static void sercos_idn_parse(uint8_t *restrict work)
{
    (void)work;
    uint16_t idn = Sercos.idn_parse_args.idn;
    proto_bool *is_product = Sercos.idn_parse_args.is_product;
    uint8_t *param_set = Sercos.idn_parse_args.param_set;
    uint16_t *data_block = Sercos.idn_parse_args.data_block;

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

static void sercos_build(uint8_t *restrict work)
{
    (void)work;
    uint8_t type = Sercos.build_args.type;
    uint8_t phase = Sercos.build_args.phase;
    uint16_t cycle = Sercos.build_args.cycle;
    const uint8_t *data = Sercos.build_args.data;
    size_t data_len = Sercos.build_args.data_len;
    uint8_t *out = Sercos.build_args.out;
    size_t cap = Sercos.build_args.cap;

    if (!out || (data_len && !data) || (type != SERCOS_TEL_MDT && type != SERCOS_TEL_AT))
    {
        Sercos.n = 0;
        return;
    }
    size_t n = SERCOS_HDR_LEN + data_len;
    if (n > cap)
    {
        Sercos.n = 0;
        return;
    }
    out[0] = type;
    out[1] = phase;
    out[2] = (uint8_t)cycle; // little-endian cycle count
    out[3] = (uint8_t)(cycle >> 8);
    if (data_len)
    {
        mem.cpy(out + SERCOS_HDR_LEN, data, data_len);
    }
    Sercos.n = n;
}

static void sercos_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *frame = Sercos.parse_args.frame;
    size_t len = Sercos.parse_args.len;
    SercosTelegram *out = Sercos.parse_args.out;

    if (!frame || !out || len < SERCOS_HDR_LEN)
    {
        Sercos.ok = PROTO_FALSE;
        return;
    }
    if (frame[0] != SERCOS_TEL_MDT && frame[0] != SERCOS_TEL_AT)
    {
        Sercos.ok = PROTO_FALSE;
        return;
    }
    out->type = frame[0];
    out->phase = frame[1];
    out->cycle = (uint16_t)(frame[2] | (frame[3] << 8));
    out->data = (len > SERCOS_HDR_LEN) ? (frame + SERCOS_HDR_LEN) : NULL;
    out->data_len = len - SERCOS_HDR_LEN;
    Sercos.ok = PROTO_TRUE;
}

SercosNs Sercos = {.idn = sercos_idn, .idn_parse = sercos_idn_parse, .build = sercos_build, .parse = sercos_parse};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SERCOS
