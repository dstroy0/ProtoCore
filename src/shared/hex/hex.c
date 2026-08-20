// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hex.c
 * @brief Hex digits, and the conversions both directions. See hex.h.
 *
 * The two digit tables are the module's compile-time storage: one definition rather than a copy per
 * translation unit. Nothing else is held - every conversion works in the caller's buffer.
 */

#include "shared/hex/hex.h"

const HexStorage PROTOCORE_HEX = {"0123456789abcdef", "0123456789ABCDEF"};

// A hex character as 0..15, or -1 when it is not a hex digit of either case.
static int8_t hex_value(char c)
{
    if (c >= '0' && c <= '9')
    {
        return (int8_t)(c - '0');
    }
    if (c >= 'a' && c <= 'f')
    {
        return (int8_t)(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F')
    {
        return (int8_t)(c - 'A' + 10);
    }
    return -1;
}

void protocore_hex_digit(uint8_t *restrict work)
{
    const char *digits = HexV.args.upper ? PROTOCORE_HEX.upper : PROTOCORE_HEX.lower;
    HexV.ch = digits[HexV.args.nibble & 0x0Fu];
}

void protocore_hex_val(uint8_t *restrict work)
{
    (void)work;
    HexV.i8 = hex_value(HexV.args.ch);
}

void protocore_hex_u32(uint8_t *restrict work)
{
    uint32_t v = HexV.args.v;
    char *out = HexV.io.out;

    // The width is counted before any digit is written so each digit lands at its final index,
    // which is why no reversal buffer is needed.
    uint8_t digits = 1;
    for (uint32_t probe = v; probe >= 16u; probe >>= 4)
    {
        digits++;
    }
    for (uint8_t i = digits; i > 0; i--)
    {
        out[i - 1] = PROTOCORE_HEX.lower[v & 0x0Fu];
        v >>= 4;
    }
    HexV.u8 = digits;
}

void protocore_hex_encode(uint8_t *restrict work)
{
    const uint8_t *in = HexV.io.in;
    const uint32_t n = HexV.io.n;
    char *out = HexV.io.out;
    const char *digits = HexV.args.upper ? PROTOCORE_HEX.upper : PROTOCORE_HEX.lower;

    for (uint32_t i = 0; i < n; i++)
    {
        out[2 * i] = digits[(in[i] >> 4) & 0x0Fu];
        out[2 * i + 1] = digits[in[i] & 0x0Fu];
    }
    out[2 * n] = '\0';
}

void protocore_hex_decode(uint8_t *restrict work)
{
    (void)work;
    const char *in = HexV.io.text;
    const uint32_t hexlen = HexV.io.n;
    uint8_t *out = HexV.io.bytes;
    const uint32_t out_cap = HexV.io.cap;

    HexV.i32 = -1;
    if ((hexlen % 2) != 0 || (hexlen / 2) > out_cap)
    {
        return;
    }
    for (uint32_t i = 0; i < hexlen; i += 2)
    {
        int8_t hi = hex_value(in[i]);
        int8_t lo = hex_value(in[i + 1]);
        if (hi < 0 || lo < 0)
        {
            return; // a bad digit stops the run where it is found
        }
        out[i / 2] = (uint8_t)((hi << 4) | lo);
    }
    HexV.i32 = (int32_t)(hexlen / 2);
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
HexVars HexV;
