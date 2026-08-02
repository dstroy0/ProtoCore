// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file crc.h
 * @brief Parameterized CRC engine - one source of truth for every cyclic redundancy check.
 *
 * One implementation for every CRC in the tree: C37.118, DF1, DNP3, EnOcean, INTERBUS, Modbus,
 * Modbus Plus, NEMA TS2, raw L2, SDI-12, SHT3x, Thread and Zigbee all checksum through it. Three
 * checks stay outside it: the WAL is table-driven for bulk log throughput, RTCM3's CRC-24Q has no
 * preset here, and DShot's "CRC" is a 4-bit XOR fold rather than a CRC at all.
 *
 * It is the standard Rocksoft / Williams model, so any published CRC is expressible as six numbers
 * and needs no new code:
 *
 *   - @ref pc_crc_params::width   register width in bits (8..32)
 *   - @ref pc_crc_params::poly    generator polynomial, normal form, implicit top bit dropped
 *   - @ref pc_crc_params::init    initial register value
 *   - @ref pc_crc_params::refin   reflect each input octet
 *   - @ref pc_crc_params::refout  reflect the final register
 *   - @ref pc_crc_params::xorout  final XOR
 *
 * Every preset below carries its catalogue **check value** - the CRC of the nine ASCII octets
 * `"123456789"` - and `test_crc` asserts each one: a wrong polynomial or a flipped reflect flag
 * cannot reproduce a published check value by accident.
 *
 * Bitwise, not table-driven: a 256-entry table per polynomial would cost more flash than the frames
 * are worth on this class of device, and every caller here checksums tens to hundreds of octets, not
 * megabytes. Header-only and pure (`<stddef.h>` / `<stdint.h>` only), so it is host-testable,
 * identical on device and host, and costs nothing when unused - the same contract as `endian.h`.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CRC_H
#define PROTOCORE_CRC_H

#include "protocore_config.h" // the entry point: PC_INLINE, and types.h for the widths

/** @brief One CRC's full definition (Rocksoft model). See the file comment. */
typedef struct
{
    uint8_t width;     ///< register width in bits, 8..32.
    uint32_t poly;     ///< generator polynomial, normal form (implicit top bit dropped).
    uint32_t init;     ///< initial register value.
    proto_bool refin;  ///< reflect each input octet before feeding it in.
    proto_bool refout; ///< reflect the final register before the XOR.
    uint32_t xorout;   ///< XORed into the final register.
} pc_crc_params;

/** @brief Mask of @p width low bits (width 32 handled without a 32-bit shift, which is UB). */
PC_INLINE uint32_t crc_mask(uint8_t width)
{
    return (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
}

/** @brief Clamp a register width to the supported 8..32 range. */
PC_INLINE uint8_t crc_clamp_width(uint8_t width)
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

/** @brief Reverse the 8 bits of @p b. */
PC_INLINE uint8_t crc_rev8(uint8_t b)
{
    b = (uint8_t)(((b & 0xF0u) >> 4) | ((b & 0x0Fu) << 4));
    b = (uint8_t)(((b & 0xCCu) >> 2) | ((b & 0x33u) << 2));
    b = (uint8_t)(((b & 0xAAu) >> 1) | ((b & 0x55u) << 1));
    return b;
}

/** @brief Reverse the low @p width bits of @p v. */
PC_INLINE uint32_t crc_revN(uint32_t v, uint8_t width)
{
    uint32_t r = 0;
    for (uint8_t i = 0; i < width; i++)
    {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

/**
 * @brief Start a CRC. @return the initial register value.
 *
 * Split from @ref pc_crc_update / @ref pc_crc_final so a caller can checksum a frame that is not
 * contiguous in memory (a header struct then a payload buffer) without copying it together first.
 */
PC_INLINE uint32_t pc_crc_begin(const pc_crc_params *p)
{
    return p ? (p->init & crc_mask(p->width)) : 0u;
}

/**
 * @brief Fold @p len octets at @p data into the running register @p crc.
 *
 * Reflection of the input is applied per octet here; reflection of the *output* belongs to
 * @ref pc_crc_final, so intermediate values from this function are not meaningful CRCs on their own.
 */
PC_INLINE uint32_t pc_crc_update(const pc_crc_params *p, uint32_t crc, const uint8_t *data, size_t len)
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

/** @brief Finish a CRC: apply the output reflection and the final XOR. */
PC_INLINE uint32_t pc_crc_final(const pc_crc_params *p, uint32_t crc)
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

/** @brief One-shot CRC of @p len octets at @p data. */
PC_INLINE uint32_t pc_crc(const pc_crc_params *p, const uint8_t *data, size_t len)
{
    return pc_crc_final(p, pc_crc_update(p, pc_crc_begin(p), data, len));
}

// --- Catalogue presets ------------------------------------------------------------------------
// Each carries its published check value: the CRC of the ASCII octets "123456789". test_crc asserts
// every one of them, so an incorrect parameter here fails the suite rather than corrupting a codec.
//
// `static const` at file scope in a header: each translation unit gets its own copy, so there is
// no duplicate definition, and a preset no TU references is never emitted at all.

/** @brief CRC-8/SMBUS (a.k.a. CRC-8). check = 0xF4. */
static const pc_crc_params PC_CRC8_SMBUS = {8, 0x07u, 0x00u, PROTO_FALSE, PROTO_FALSE, 0x00u};
/** @brief CRC-8/MAXIM-DOW (1-Wire / Dallas). check = 0xA1. */
static const pc_crc_params PC_CRC8_MAXIM_DOW = {8, 0x31u, 0x00u, PROTO_TRUE, PROTO_TRUE, 0x00u};
/** @brief CRC-8/NRSC-5 - the Sensirion sensor CRC. check = 0xF7. Used by services/sht3x. */
static const pc_crc_params PC_CRC8_NRSC5 = {8, 0x31u, 0xFFu, PROTO_FALSE, PROTO_FALSE, 0x00u};

/** @brief CRC-16/ARC (a.k.a. CRC-16, IBM). check = 0xBB3D. */
static const pc_crc_params PC_CRC16_ARC = {16, 0x8005u, 0x0000u, PROTO_TRUE, PROTO_TRUE, 0x0000u};
/** @brief CRC-16/MODBUS. check = 0x4B37. */
static const pc_crc_params PC_CRC16_MODBUS = {16, 0x8005u, 0xFFFFu, PROTO_TRUE, PROTO_TRUE, 0x0000u};
/** @brief CRC-16/IBM-3740 (often called CCITT-FALSE). check = 0x29B1. */
static const pc_crc_params PC_CRC16_IBM_3740 = {16, 0x1021u, 0xFFFFu, PROTO_FALSE, PROTO_FALSE, 0x0000u};
/** @brief CRC-16/XMODEM. check = 0x31C3. */
static const pc_crc_params PC_CRC16_XMODEM = {16, 0x1021u, 0x0000u, PROTO_FALSE, PROTO_FALSE, 0x0000u};
/** @brief CRC-16/KERMIT (a.k.a. CRC-16/CCITT, reflected). check = 0x2189. */
static const pc_crc_params PC_CRC16_KERMIT = {16, 0x1021u, 0x0000u, PROTO_TRUE, PROTO_TRUE, 0x0000u};
/** @brief CRC-16/X-25 (HDLC FCS). check = 0x906E. Used by services/radio/thread, mbplus, nema_ts2. */
static const pc_crc_params PC_CRC16_X25 = {16, 0x1021u, 0xFFFFu, PROTO_TRUE, PROTO_TRUE, 0xFFFFu};
/** @brief CRC-16/DNP (DNP3 link-layer block check). check = 0xEA82. Used by services/dnp3. */
static const pc_crc_params PC_CRC16_DNP = {16, 0x3D65u, 0x0000u, PROTO_TRUE, PROTO_TRUE, 0xFFFFu};

/** @brief CRC-24/OPENPGP. check = 0x21CF02. */
static const pc_crc_params PC_CRC24_OPENPGP = {24, 0x864CFBu, 0xB704CEu, PROTO_FALSE, PROTO_FALSE, 0x000000u};

/** @brief CRC-32/ISO-HDLC (zlib / PKZIP / Ethernet). check = 0xCBF43926. */
static const pc_crc_params PC_CRC32_ISO_HDLC = {32, 0x04C11DB7u, 0xFFFFFFFFu, PROTO_TRUE, PROTO_TRUE, 0xFFFFFFFFu};
/** @brief CRC-32/BZIP2 (unreflected CRC-32). check = 0xFC891918. */
static const pc_crc_params PC_CRC32_BZIP2 = {32, 0x04C11DB7u, 0xFFFFFFFFu, PROTO_FALSE, PROTO_FALSE, 0xFFFFFFFFu};

#endif // PROTOCORE_CRC_H
