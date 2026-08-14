// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file crc.c
 * @brief The Rocksoft CRC model, and the catalogue presets. See crc.h.
 *
 * Bitwise, not table-driven: a 256-entry table per polynomial would cost more flash than the frames
 * are worth on this class of device, and every caller here checksums tens to hundreds of octets, not
 * megabytes. Nothing is held between calls - the running register is the caller's, carried on the
 * handle - so there is no storage member.
 */

#include "shared/crc/crc.h"

// --- Catalogue presets ------------------------------------------------------------------------
// Each carries its published check value: the CRC of the ASCII octets "123456789". test_crc asserts
// every one of them, so an incorrect parameter here fails the suite rather than corrupting a codec.

const protocore_crc_params PROTOCORE_CRC8_SMBUS = {8, 0x07u, 0x00u, PROTO_FALSE, PROTO_FALSE, 0x00u};
const protocore_crc_params PROTOCORE_CRC8_MAXIM_DOW = {8, 0x31u, 0x00u, PROTO_TRUE, PROTO_TRUE, 0x00u};
const protocore_crc_params PROTOCORE_CRC8_NRSC5 = {8, 0x31u, 0xFFu, PROTO_FALSE, PROTO_FALSE, 0x00u};

const protocore_crc_params PROTOCORE_CRC16_ARC = {16, 0x8005u, 0x0000u, PROTO_TRUE, PROTO_TRUE, 0x0000u};
const protocore_crc_params PROTOCORE_CRC16_MODBUS = {16, 0x8005u, 0xFFFFu, PROTO_TRUE, PROTO_TRUE, 0x0000u};
const protocore_crc_params PROTOCORE_CRC16_IBM_3740 = {16, 0x1021u, 0xFFFFu, PROTO_FALSE, PROTO_FALSE, 0x0000u};
const protocore_crc_params PROTOCORE_CRC16_XMODEM = {16, 0x1021u, 0x0000u, PROTO_FALSE, PROTO_FALSE, 0x0000u};
const protocore_crc_params PROTOCORE_CRC16_KERMIT = {16, 0x1021u, 0x0000u, PROTO_TRUE, PROTO_TRUE, 0x0000u};
const protocore_crc_params PROTOCORE_CRC16_X25 = {16, 0x1021u, 0xFFFFu, PROTO_TRUE, PROTO_TRUE, 0xFFFFu};
const protocore_crc_params PROTOCORE_CRC16_DNP = {16, 0x3D65u, 0x0000u, PROTO_TRUE, PROTO_TRUE, 0xFFFFu};

const protocore_crc_params PROTOCORE_CRC24_OPENPGP = {24, 0x864CFBu, 0xB704CEu, PROTO_FALSE, PROTO_FALSE, 0x000000u};

const protocore_crc_params PROTOCORE_CRC32_ISO_HDLC = {32,         0x04C11DB7u, 0xFFFFFFFFu,
                                                       PROTO_TRUE, PROTO_TRUE,  0xFFFFFFFFu};
const protocore_crc_params PROTOCORE_CRC32_BZIP2 = {32,          0x04C11DB7u, 0xFFFFFFFFu,
                                                    PROTO_FALSE, PROTO_FALSE, 0xFFFFFFFFu};

/**
 * @brief The CRC calls - what CrcNs points at.
 *
 * @var CrcInternal::ns  the handle a caller sets a call's members on
 */
struct CrcInternal
{
    CrcNs *ns;
};

static struct CrcInternal s_crc = {.ns = &Crc};

// Mask of width low bits. Width 32 is handled without a 32-bit shift, which is undefined.
static uint32_t crc_mask(uint8_t width)
{
    return (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
}

// Clamp a register width to the supported 8..32 range.
static uint8_t crc_clamp_width(uint8_t width)
{
    if (width < 8)
    {
        return 8;
    }
    if (width > 32)
    {
        return 32;
    }
    return width;
}

// Reverse the 8 bits of b.
static uint8_t crc_rev8(uint8_t b)
{
    b = (uint8_t)(((b & 0xF0u) >> 4) | ((b & 0x0Fu) << 4));
    b = (uint8_t)(((b & 0xCCu) >> 2) | ((b & 0x33u) << 2));
    b = (uint8_t)(((b & 0xAAu) >> 1) | ((b & 0x55u) << 1));
    return b;
}

// Reverse the low width bits of v.
static uint32_t crc_revN(uint32_t v, uint8_t width)
{
    uint32_t r = 0;
    for (uint8_t i = 0; i < width; i++)
    {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

// The register value a start produces, so the three steps and the one-shot share one definition.
static uint32_t crc_start(const protocore_crc_params *p)
{
    return p ? (p->init & crc_mask(p->width)) : 0u;
}

// Fold len octets at data into the running register crc.
static uint32_t crc_fold(const protocore_crc_params *p, uint32_t crc, const uint8_t *data, size_t len)
{
    if (!p || (!data && len))
    {
        return crc;
    }
    const uint8_t width = crc_clamp_width(p->width);
    const uint32_t m = crc_mask(width);
    const uint32_t top = 1u << (width - 1);

    for (size_t i = 0; i < len; i++)
    {
        uint8_t b = p->refin ? crc_rev8(data[i]) : data[i];
        crc ^= ((uint32_t)b) << (width - 8);
        for (int k = 0; k < 8; k++)
        {
            crc = (crc & top) ? (((crc << 1) ^ p->poly) & m) : ((crc << 1) & m);
        }
    }
    return crc & m;
}

// Apply the output reflection and the final XOR.
static uint32_t crc_finish(const protocore_crc_params *p, uint32_t crc)
{
    if (!p)
    {
        return 0u;
    }
    const uint8_t width = crc_clamp_width(p->width);
    const uint32_t m = crc_mask(width);
    if (p->refout)
    {
        crc = crc_revN(crc & m, width);
    }
    return (crc ^ p->xorout) & m;
}

static void crc_begin(struct CrcInternal *restrict ctx)
{
    ctx->ns->value = crc_start(ctx->ns->args.params);
}

static void crc_update(struct CrcInternal *restrict ctx)
{
    ctx->ns->value = crc_fold(ctx->ns->args.params, ctx->ns->args.crc, ctx->ns->args.data, ctx->ns->args.len);
}

static void crc_final(struct CrcInternal *restrict ctx)
{
    ctx->ns->value = crc_finish(ctx->ns->args.params, ctx->ns->args.crc);
}

static void crc_compute(struct CrcInternal *restrict ctx)
{
    const protocore_crc_params *p = ctx->ns->args.params;
    ctx->ns->value = crc_finish(p, crc_fold(p, crc_start(p), ctx->ns->args.data, ctx->ns->args.len));
}

// Designated, so a member's position in the struct does not decide what it binds to.
CrcNs Crc = {.begin = crc_begin, .update = crc_update, .final = crc_final, .compute = crc_compute, .internal = &s_crc};
