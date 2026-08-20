// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
 *   - @ref protocore_crc_params::width   register width in bits (8..32)
 *   - @ref protocore_crc_params::poly    generator polynomial, normal form, implicit top bit dropped
 *   - @ref protocore_crc_params::init    initial register value
 *   - @ref protocore_crc_params::refin   reflect each input octet
 *   - @ref protocore_crc_params::refout  reflect the final register
 *   - @ref protocore_crc_params::xorout  final XOR
 *
 * Every preset below carries its catalogue **check value** - the CRC of the nine ASCII octets
 * `"123456789"` - and `test_crc` asserts each one: a wrong polynomial or a flipped reflect flag
 * cannot reproduce a published check value by accident.
 *
 * Bitwise, not table-driven: a 256-entry table per polynomial would cost more flash than the frames
 * are worth on this class of device, and every caller here checksums tens to hundreds of octets, not
 * megabytes. Pure, so it is host-testable and identical on device and host.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CRC_H
#define PROTOCORE_CRC_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

/** @brief One CRC's full definition (Rocksoft model). See the file comment. */
typedef struct
{
    uint8_t width;     ///< register width in bits, 8..32.
    uint32_t poly;     ///< generator polynomial, normal form (implicit top bit dropped).
    uint32_t init;     ///< initial register value.
    proto_bool refin;  ///< reflect each input octet before feeding it in.
    proto_bool refout; ///< reflect the final register before the XOR.
    uint32_t xorout;   ///< XORed into the final register.
} protocore_crc_params;
/** @brief What one CRC step runs over: the definition, the running register, and the octets. */
typedef struct
{
    const protocore_crc_params *params; ///< the CRC's full definition
    uint32_t crc;                       ///< the running register a fold or a finish carries in
    const uint8_t *data;                ///< the octets a fold takes
    size_t len;                         ///< how many
} CrcArgs;
/**
 * @brief The Rocksoft CRC model.
 *
 * A caller sets the members a call takes, invokes it through ::Crc, and reads the register off the
 * same handle. Nothing is held between calls: the running value is the caller's, carried in
 * @ref CrcArgs::crc and reported in @ref CrcNs::value.
 *
 * @var CrcNs::args      the definition, the running register, and the octets
 * @var CrcNs::value     the register a step produced
 * @var CrcNs::begin     the initial register value
 * @var CrcNs::update    fold args.len octets at args.data into args.crc
 * @var CrcNs::final     apply the output reflection and the final XOR to args.crc
 * @var CrcNs::compute   one-shot: begin, update and final over args.data
 *
 * The three steps are split so a caller can checksum a frame that is not contiguous in memory (a
 * header struct then a payload buffer) without copying it together first. Input reflection is
 * applied per octet by update; output reflection belongs to final, so an intermediate register is
 * not a meaningful CRC on its own.
 *
 * No storage member: the register is the caller's and the presets below are constants.
 */
typedef struct
{
    CrcArgs args;
    uint32_t value;
} CrcVars;

/** @brief The operands and the outcome. */
extern CrcVars CrcV;

/** @brief The entries. */
typedef struct
{
    void (*const begin)(uint8_t *restrict work);
    void (*const update)(uint8_t *restrict work);
    void (*const final)(uint8_t *restrict work);
    void (*const compute)(uint8_t *restrict work);
} CrcNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in CrcV or a region of the borrow at a fixed offset.
void protocore_crc_begin(uint8_t *restrict work);
void protocore_crc_update(uint8_t *restrict work);
void protocore_crc_final(uint8_t *restrict work);
void protocore_crc_compute(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Crc.begin(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const CrcNs Crc __attribute__((unused)) = {
    .begin = protocore_crc_begin,
    .update = protocore_crc_update,
    .final = protocore_crc_final,
    .compute = protocore_crc_compute,
};

// --- Catalogue presets ------------------------------------------------------------------------
// Each carries its published check value: the CRC of the ASCII octets "123456789". test_crc asserts
// every one of them, so an incorrect parameter here fails the suite rather than corrupting a codec.
// Defined once in crc.c rather than per translation unit.

/** @brief CRC-8/SMBUS (a.k.a. CRC-8). check = 0xF4. */
extern const protocore_crc_params PROTOCORE_CRC8_SMBUS;
/** @brief CRC-8/MAXIM-DOW (1-Wire / Dallas). check = 0xA1. */
extern const protocore_crc_params PROTOCORE_CRC8_MAXIM_DOW;
/** @brief CRC-8/NRSC-5 - the sensor CRC. check = 0xF7. Used by services/sht3x. */
extern const protocore_crc_params PROTOCORE_CRC8_NRSC5;

/** @brief CRC-16/ARC (a.k.a. CRC-16, IBM). check = 0xBB3D. */
extern const protocore_crc_params PROTOCORE_CRC16_ARC;
/** @brief CRC-16/MODBUS. check = 0x4B37. */
extern const protocore_crc_params PROTOCORE_CRC16_MODBUS;
/** @brief CRC-16/IBM-3740 (often called CCITT-FALSE). check = 0x29B1. */
extern const protocore_crc_params PROTOCORE_CRC16_IBM_3740;
/** @brief CRC-16/XMODEM. check = 0x31C3. */
extern const protocore_crc_params PROTOCORE_CRC16_XMODEM;
/** @brief CRC-16/KERMIT (a.k.a. CRC-16/CCITT, reflected). check = 0x2189. */
extern const protocore_crc_params PROTOCORE_CRC16_KERMIT;
/** @brief CRC-16/X-25 (HDLC FCS). check = 0x906E. Used by services/radio/thread, mbplus, nema_ts2. */
extern const protocore_crc_params PROTOCORE_CRC16_X25;
/** @brief CRC-16/DNP (DNP3 link-layer block check). check = 0xEA82. Used by services/dnp3. */
extern const protocore_crc_params PROTOCORE_CRC16_DNP;

/** @brief CRC-24/OPENPGP. check = 0x21CF02. */
extern const protocore_crc_params PROTOCORE_CRC24_OPENPGP;

/** @brief CRC-32/ISO-HDLC (zlib / PKZIP / Ethernet). check = 0xCBF43926. */
extern const protocore_crc_params PROTOCORE_CRC32_ISO_HDLC;
/** @brief CRC-32/BZIP2 (unreflected CRC-32). check = 0xFC891918. */
extern const protocore_crc_params PROTOCORE_CRC32_BZIP2;

#endif // PROTOCORE_CRC_H
