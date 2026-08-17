// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dshot.c
 * @brief DShot ESC throttle protocol codec (see dshot.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_DSHOT

#include "server/peripherals/dshot/dshot.h"

PROTOCORE_BEGIN_DECLS

// The DShot CRC: xor of the three 4-bit nibbles of the 12-bit (value<<1 | telemetry) word.
static uint8_t dshot_crc(uint16_t v12, proto_bool bidirectional)
{
    uint8_t crc = (uint8_t)((v12 ^ (v12 >> 4) ^ (v12 >> 8)) & 0x0F);
    if (bidirectional)
    {
        crc = (uint8_t)((~crc) & 0x0F); // bidirectional/extended DShot inverts the CRC
    }
    return crc;
}

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void dshot_encode(uint8_t *restrict work)
{
    (void)work;
    uint16_t value11 = Dshot.encode_args.value11;
    proto_bool telemetry = Dshot.encode_args.telemetry;
    proto_bool bidirectional = Dshot.encode_args.bidirectional;

    value11 &= 0x07FF; // 11-bit value field
    uint16_t v12 = (uint16_t)((value11 << 1) | (telemetry ? 1 : 0));
    uint8_t crc = dshot_crc(v12, bidirectional);
    Dshot.frame = (uint16_t)((v12 << 4) | crc);
}

static void dshot_decode(uint8_t *restrict work)
{
    (void)work;
    uint16_t frame = Dshot.decode_args.frame;
    uint16_t *value11 = Dshot.decode_args.value11;
    proto_bool *telemetry = Dshot.decode_args.telemetry;
    proto_bool bidirectional = Dshot.decode_args.bidirectional;

    uint16_t v12 = (uint16_t)(frame >> 4);
    uint8_t got = (uint8_t)(frame & 0x0F);
    if (got != dshot_crc(v12, bidirectional))
    {
        Dshot.ok = PROTO_FALSE;
        return;
    }
    if (value11)
    {
        *value11 = (uint16_t)(v12 >> 1);
    }
    if (telemetry)
    {
        *telemetry = (v12 & 1) != 0;
    }
    Dshot.ok = PROTO_TRUE;
}

static void dshot_bit_ns(uint8_t *restrict work)
{
    (void)work;
    uint16_t rate_kbit = Dshot.bit_ns_args.rate_kbit;
    proto_bool bit = Dshot.bit_ns_args.bit;

    uint32_t period_ns; // one bit-period, in ns, at rate_kbit kbit/s
    switch (rate_kbit)
    {
    case 150:
        period_ns = 6667;
        break;
    case 300:
        period_ns = 3333;
        break;
    case 600:
        period_ns = 1667;
        break;
    case 1200:
        period_ns = 833;
        break;
    default:
        Dshot.ns = 0;
        return;
    }
    // A "1" holds high ~3/4 of the period, a "0" ~3/8 (T1H = 2 * T0H); the ESC samples the pulse width.
    Dshot.ns = bit ? (period_ns * 3 / 4) : (period_ns * 3 / 8);
}

static void dshot_esc_pwm_ns(uint8_t *restrict work)
{
    (void)work;
    uint16_t throttle_1000 = Dshot.esc_pwm_ns_args.throttle_1000;
    protocore_esc_pwm mode = Dshot.esc_pwm_ns_args.mode;

    uint32_t lo;
    uint32_t hi; // pulse width range in ns
    switch (mode)
    {
    case PROTOCORE_ESC_ONESHOT125:
        lo = 125000;
        hi = 250000;
        break;
    case PROTOCORE_ESC_ONESHOT42:
        lo = 42000;
        hi = 84000;
        break;
    case PROTOCORE_ESC_MULTISHOT:
        lo = 5000;
        hi = 25000;
        break;
    case PROTOCORE_ESC_PWM:
    default:
        lo = 1000000;
        hi = 2000000;
        break;
    }
    if (throttle_1000 > 1000)
    {
        throttle_1000 = 1000;
    }
    // Linear map, computed in 64-bit to avoid overflow (hi-lo up to 1e6, * 1000).
    Dshot.ns = lo + (uint32_t)(((uint64_t)(hi - lo) * throttle_1000) / 1000);
}

DshotNs Dshot = {
    .encode = dshot_encode, .decode = dshot_decode, .bit_ns = dshot_bit_ns, .esc_pwm_ns = dshot_esc_pwm_ns};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DSHOT
