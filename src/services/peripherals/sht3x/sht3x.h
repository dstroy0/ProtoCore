// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sht3x.h
 * @brief Sensirion SHT3x temperature / humidity sensor codec (PC_ENABLE_SHT3X).
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

#include "protocore_config.h" // the entry point: types.h for the widths and PC_INLINE

PROTO_BEGIN_DECLS

#if PC_ENABLE_SHT3X

// Single-shot measurement commands (16-bit, sent most-significant byte first).
#define SHT3X_CMD_SINGLE_HIGH 0x2400 ///< high repeatability, no clock stretching
#define SHT3X_CMD_SINGLE_MED 0x240B  ///< medium repeatability
#define SHT3X_CMD_SINGLE_LOW 0x2416  ///< low repeatability
#define SHT3X_CMD_SOFT_RESET 0x30A2  ///< soft reset
#define SHT3X_CMD_READ_STATUS 0xF32D ///< read the status register
#define SHT3X_CMD_HEATER_ON 0x306D   ///< enable the on-chip heater
#define SHT3X_CMD_HEATER_OFF 0x3066  ///< disable the on-chip heater

/** @brief Sensirion CRC-8 (poly 0x31, init 0xFF) over @p len bytes. */
uint8_t pc_sht3x_crc8(const uint8_t *data, size_t len);

/** @brief Convert a raw 16-bit temperature tick to milli-degrees Celsius. */
int32_t pc_sht3x_temp_mc(uint16_t raw);

/** @brief Convert a raw 16-bit humidity tick to milli-percent relative humidity (0..100000). */
int32_t pc_sht3x_rh_mpct(uint16_t raw);

/**
 * @brief Decode a six-byte single-shot response (T msb/lsb/crc, RH msb/lsb/crc). Verifies both
 * CRC-8 words, then fills @p temp_mc and @p rh_mpct (either may be null).
 * @return false if a CRC does not match (a corrupt read).
 */
proto_bool pc_sht3x_parse(const uint8_t resp[6], int32_t *temp_mc, int32_t *rh_mpct);

// --- ESP32 binding (I2C via Wire) ------------------------------------

/** @brief Soft-reset the SHT3x at @p addr over I2C. @return true if it acknowledged. */
proto_bool pc_sht3x_begin(uint8_t addr);

/**
 * @brief Trigger a single-shot high-repeatability measurement, read + verify the six bytes, and
 * return the temperature (milli-C) and humidity (milli-%RH). @return false on I2C or CRC error.
 */
proto_bool pc_sht3x_read(int32_t *temp_mc, int32_t *rh_mpct);

#endif // PC_ENABLE_SHT3X

PROTO_END_DECLS

#endif // PROTOCORE_SHT3X_H
