// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sht3x.h
 * @brief Sensirion SHT3x temperature / humidity sensor codec (PROTOCORE_ENABLE_SHT3X).
 *
 * The SHT3x (SHT30 / SHT31 / SHT35) answers a single-shot measurement command with six bytes:
 * a 16-bit temperature word + its CRC-8, then a 16-bit humidity word + its CRC-8. The CRC is
 * the Sensirion CRC-8 (polynomial 0x31, init 0xFF, no reflection, no final XOR; the datasheet
 * check value is 0xBEEF -> 0x92). Raw ticks convert linearly:
 *   T[C]   = -45 + 175 * raw / 65535
 *   RH[%]  =       100 * raw / 65535
 *
 * To stay heap- and float-printf-free, the results are returned as signed integer milli-units
 * (milli-degrees C, milli-percent RH). The CRC check and the conversion are pure and
 * host-tested; only the command write / data read touches I2C.
 *
 * A cheap solder-and-bench-test breakout (GY-SHT31 etc.): read it, bridge the reading onto the
 * network as telemetry.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SHT3X_H
#define PROTOCORE_SHT3X_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SHT3X

PROTOCORE_BEGIN_DECLS

// PROTOCORE_I2C_DEVICE_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

#define SHT3X_CMD_SINGLE_HIGH 0x2400 ///< high repeatability, no clock stretching

#define SHT3X_CMD_SINGLE_MED 0x240B ///< medium repeatability

#define SHT3X_CMD_SINGLE_LOW 0x2416 ///< low repeatability

#define SHT3X_CMD_SOFT_RESET 0x30A2 ///< soft reset

#define SHT3X_CMD_READ_STATUS 0xF32D ///< read the status register

#define SHT3X_CMD_HEATER_ON 0x306D ///< enable the on-chip heater

#define SHT3X_CMD_HEATER_OFF 0x3066 ///< disable the on-chip heater

/** @brief What crc8 takes: data, len. */
typedef struct
{
    const uint8_t *data;
    size_t len;
} Sht3xCrc8Args;

/** @brief What temp_mc takes: raw. */
typedef struct
{
    uint16_t raw;
} Sht3xTempMcArgs;

/** @brief What rh_mpct takes: raw. */
typedef struct
{
    uint16_t raw;
} Sht3xRhMpctArgs;

/** @brief What parse takes: resp, temp_mc, rh_mpct. */
typedef struct
{
    const uint8_t *resp; ///< 6 bytes.
    int32_t *temp_mc;
    int32_t *rh_mpct;
} Sht3xParseArgs;

/** @brief What begin takes: addr. */
typedef struct
{
    uint8_t addr;
} Sht3xBeginArgs;

/** @brief What read takes: temp_mc, rh_mpct. */
typedef struct
{
    int32_t *temp_mc;
    int32_t *rh_mpct;
} Sht3xReadArgs;

/**
 * @brief Sensirion SHT3x temperature / humidity sensor codec (PROTOCORE_ENABLE_SHT3X). The SHT3x (SHT30 / SHT31 / ...
 *
 * A caller sets the members a call takes, invokes it through ::Sht3x with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Sht3x.crc8_args.data = ...;
 *   Sht3x.crc8_args.len = ...;
 *   Sht3x.crc8(work);
 *   // Sht3x.crc is what the call reports
 *
 * @var Sht3xNs::crc8_args  what crc8 takes: data, len
 * @var Sht3xNs::temp_mc_args  what temp_mc takes: raw
 * @var Sht3xNs::rh_mpct_args  what rh_mpct takes: raw
 * @var Sht3xNs::parse_args  what parse takes: resp, temp_mc, rh_mpct
 * @var Sht3xNs::begin_args  what begin takes: addr
 * @var Sht3xNs::read_args  what read takes: temp_mc, rh_mpct
 * @var Sht3xNs::ok  false if a CRC does not match (a corrupt read)
 * @var Sht3xNs::crc  what a call reports
 * @var Sht3xNs::milli  what a call reports
 * @var Sht3xNs::crc8  sensirion CRC-8 (poly 0x31, init 0xFF) over len bytes
 * @var Sht3xNs::temp_mc  convert a raw 16-bit temperature tick to milli-degrees Celsius
 * @var Sht3xNs::rh_mpct  convert a raw 16-bit humidity tick to milli-percent relative ...
 * @var Sht3xNs::parse  decode a six-byte single-shot response (T msb/lsb/crc, RH ...
 * @var Sht3xNs::begin  soft-reset the SHT3x at addr over I2C. true if it acknowledged
 * @var Sht3xNs::read  trigger a single-shot high-repeatability measurement, read + verify ...
 *
 * @c work is PROTOCORE_I2C_DEVICE_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    Sht3xCrc8Args crc8_args;
    Sht3xTempMcArgs temp_mc_args;
    Sht3xRhMpctArgs rh_mpct_args;
    Sht3xParseArgs parse_args;
    Sht3xBeginArgs begin_args;
    Sht3xReadArgs read_args;

    proto_bool ok;
    uint8_t crc;
    int32_t milli;

    void (*const crc8)(uint8_t *restrict work);
    void (*const temp_mc)(uint8_t *restrict work);
    void (*const rh_mpct)(uint8_t *restrict work);
    void (*const parse)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const read)(uint8_t *restrict work);
} Sht3xNs;

/** @brief The one symbol this module exports. */
extern Sht3xNs Sht3x;

/**
 * @brief The PROTOCORE_I2C_DEVICE_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_sht3x_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SHT3X

#endif // PROTOCORE_SHT3X_H
