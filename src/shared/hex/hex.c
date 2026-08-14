// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hex.c
 * @brief Hex digits, and the conversions both directions. See hex.h.
 *
 * The two digit tables are the module's compile-time storage: one definition rather than a copy per
 * translation unit. Nothing else is held - every conversion works in the caller's buffer.
 */

#include "shared/hex/hex.h"

/**
 * @brief The digit tables and the calls that read them - what HexNs points at.
 *
 * @var HexInternal::store  the two digit tables
 * @var HexInternal::ns     the handle a caller sets a call's members on
 */
struct HexInternal
{
    const HexStorage *store;
    HexNs *ns;
};

const HexStorage PROTOCORE_HEX = {"0123456789abcdef", "0123456789ABCDEF"};

static struct HexInternal s_hex = {.store = &PROTOCORE_HEX, .ns = &Hex};

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

static void hex_digit(struct HexInternal *restrict ctx)
{
    const char *digits = ctx->ns->args.upper ? ctx->store->upper : ctx->store->lower;
    ctx->ns->ch = digits[ctx->ns->args.nibble & 0x0Fu];
}

static void hex_val(struct HexInternal *restrict ctx)
{
    ctx->ns->i8 = hex_value(ctx->ns->args.ch);
}

static void hex_u32(struct HexInternal *restrict ctx)
{
    uint32_t v = ctx->ns->args.v;
    char *out = ctx->ns->io.out;

    // The width is counted before any digit is written so each digit lands at its final index,
    // which is why no reversal buffer is needed.
    uint8_t digits = 1;
    for (uint32_t probe = v; probe >= 16u; probe >>= 4)
    {
        digits++;
    }
    for (uint8_t i = digits; i > 0; i--)
    {
        out[i - 1] = ctx->store->lower[v & 0x0Fu];
        v >>= 4;
    }
    ctx->ns->u8 = digits;
}

static void hex_encode(struct HexInternal *restrict ctx)
{
    const uint8_t *in = ctx->ns->io.in;
    const uint32_t n = ctx->ns->io.n;
    char *out = ctx->ns->io.out;
    const char *digits = ctx->ns->args.upper ? ctx->store->upper : ctx->store->lower;

    for (uint32_t i = 0; i < n; i++)
    {
        out[2 * i] = digits[(in[i] >> 4) & 0x0Fu];
        out[2 * i + 1] = digits[in[i] & 0x0Fu];
    }
    out[2 * n] = '\0';
}

static void hex_decode(struct HexInternal *restrict ctx)
{
    const char *in = ctx->ns->io.text;
    const uint32_t hexlen = ctx->ns->io.n;
    uint8_t *out = ctx->ns->io.bytes;
    const uint32_t out_cap = ctx->ns->io.cap;

    ctx->ns->i32 = -1;
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
    ctx->ns->i32 = (int32_t)(hexlen / 2);
}

// Designated, so a member's position in the struct does not decide what it binds to.
HexNs Hex = {.digit = hex_digit,
             .val = hex_val,
             .u32 = hex_u32,
             .encode = hex_encode,
             .decode = hex_decode,
             .internal = &s_hex};
