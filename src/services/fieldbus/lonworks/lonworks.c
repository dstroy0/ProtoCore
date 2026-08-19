// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file lonworks.c
 * @brief LonWorks / LON-IP network-variable codec (see lonworks.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_LONWORKS

#include "mmgr/protomem/protomem.h"
#include "services/fieldbus/lonworks/lonworks.h"

PROTOCORE_BEGIN_DECLS

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void lonworks_build_nv(uint8_t *restrict work)
{
    (void)work;
    uint8_t msg_code = Lonworks.build_nv_args.msg_code;
    uint16_t selector = Lonworks.build_nv_args.selector;
    const uint8_t *value = Lonworks.build_nv_args.value;
    size_t value_len = Lonworks.build_nv_args.value_len;
    uint8_t *out = Lonworks.build_nv_args.out;
    size_t cap = Lonworks.build_nv_args.cap;

    if (!out || (value_len && !value) || selector > LON_NV_SELECTOR_MAX)
    {
        Lonworks.n = 0;
        return;
    }
    size_t n = LON_NV_HDR_LEN + value_len;
    if (n > cap)
    {
        Lonworks.n = 0;
        return;
    }
    // Octet 0 is the message bit and the direction bit off msg_code, then selector bits 13..8;
    // octet 1 is selector bits 7..0.
    out[0] = (uint8_t)((msg_code & LON_NV_TYPE_MASK) | (uint8_t)(selector >> 8));
    out[1] = (uint8_t)selector;
    if (value_len)
    {
        mem.cpy(out + LON_NV_HDR_LEN, value, value_len);
    }
    Lonworks.n = n;
}

static void lonworks_parse_nv(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *pdu = Lonworks.parse_nv_args.pdu;
    size_t len = Lonworks.parse_nv_args.len;
    LonNv *out = Lonworks.parse_nv_args.out;

    if (!pdu || !out || len < LON_NV_HDR_LEN)
    {
        Lonworks.ok = PROTO_FALSE;
        return;
    }
    out->msg_code = (uint8_t)(pdu[0] & LON_NV_TYPE_MASK);
    out->selector = (uint16_t)(((uint16_t)(pdu[0] & LON_NV_SEL_HI_MASK) << 8) | pdu[1]);
    out->value = (len > LON_NV_HDR_LEN) ? (pdu + LON_NV_HDR_LEN) : NULL;
    out->value_len = len - LON_NV_HDR_LEN;
    Lonworks.ok = PROTO_TRUE;
}

static void lonworks_snvt_temp_encode(uint8_t *restrict work)
{
    (void)work;
    double celsius = Lonworks.snvt_temp_encode_args.celsius;
    uint8_t *out = Lonworks.snvt_temp_encode_args.out;

    // SNVT_temp: tenths of a degree Celsius above -274, as an unsigned 16-bit big-endian.
    // Scaled value = 1 * 10^-1 * (raw - 2740), raw 0..65535.
    double tenths = celsius * 10.0 + 2740.0;
    if (tenths < 0.0)
    {
        tenths = 0.0;
    }
    if (tenths > 65535.0)
    {
        tenths = 65535.0;
    }
    uint16_t u = (uint16_t)(tenths + 0.5);
    out[0] = (uint8_t)(u >> 8);
    out[1] = (uint8_t)u;
}

static void lonworks_snvt_temp_decode(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *in = Lonworks.snvt_temp_decode_args.in;

    uint16_t v = (uint16_t)(((uint16_t)in[0] << 8) | in[1]);
    Lonworks.value = ((double)v - 2740.0) / 10.0;
}

static void lonworks_snvt_switch_encode(uint8_t *restrict work)
{
    (void)work;
    double percent = Lonworks.snvt_switch_encode_args.percent;
    uint8_t state = Lonworks.snvt_switch_encode_args.state;
    uint8_t *out = Lonworks.snvt_switch_encode_args.out;

    // SNVT_switch: value is 0..200 in 0.5 % steps (0..100 %), state is 0 OFF / 1 ON / 0xFF NULL.
    if (percent < 0)
    {
        percent = 0;
    }
    if (percent > 100.0)
    {
        percent = 100.0;
    }
    uint8_t v = (uint8_t)(percent * 2.0 + 0.5);
    out[0] = v;
    out[1] = state;
}

static void lonworks_snvt_switch_decode(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *in = Lonworks.snvt_switch_decode_args.in;
    double *percent = Lonworks.snvt_switch_decode_args.percent;
    uint8_t *state = Lonworks.snvt_switch_decode_args.state;

    if (percent)
    {
        *percent = (double)in[0] / 2.0;
    }
    if (state)
    {
        *state = in[1];
    }
}

LonworksNs Lonworks = {.build_nv = lonworks_build_nv,
                       .parse_nv = lonworks_parse_nv,
                       .snvt_temp_encode = lonworks_snvt_temp_encode,
                       .snvt_temp_decode = lonworks_snvt_temp_decode,
                       .snvt_switch_encode = lonworks_snvt_switch_encode,
                       .snvt_switch_decode = lonworks_snvt_switch_decode};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_LONWORKS
