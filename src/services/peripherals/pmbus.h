// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pmbus.h
 * @brief PMBus 1.3 power-management command set over SMBus.
 *
 * PMBus is SMBus with the command codes fixed and the numbers given an encoding, so a digital
 * point-of-load converter, a hot-swap controller or a power supply reports its input voltage,
 * output current, temperature and faults through one set of commands instead of a register map
 * per part.
 *
 * Three encodings carry the numbers, and all three are decoded here rather than in a driver:
 *
 *   - **LINEAR11** for most telemetry: one word holding a 5-bit signed exponent in bits 15:11 and
 *     an 11-bit signed mantissa in bits 10:0. The value is mantissa * 2^exponent.
 *   - **LINEAR16** for output voltage: the word is a 16-bit unsigned mantissa and the exponent
 *     comes from the part's VOUT_MODE register, so it is read once and applied to every reading.
 *   - **DIRECT** where a part uses it: the value is (word / 10^R - b) / m, with m, b and R coming
 *     from the part's datasheet.
 *
 * Values are returned in micro-units (microvolts, microamps, microwatts) and temperatures in
 * milli-degrees Celsius, which is what the other sensor drivers here report in. No float is
 * involved: the exponent is a shift.
 *
 * The encodings are pure and host-tested. The commands ride the SMBus shapes, so a build with no
 * bus seam refuses them.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PMBUS_H
#define PROTOCORE_PMBUS_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_PMBUS

// Command codes, from the PMBus 1.3 Part II specification.
#define PC_PMBUS_PAGE 0x00u
#define PC_PMBUS_OPERATION 0x01u
#define PC_PMBUS_ON_OFF_CONFIG 0x02u
#define PC_PMBUS_CLEAR_FAULTS 0x03u
#define PC_PMBUS_CAPABILITY 0x19u
#define PC_PMBUS_VOUT_MODE 0x20u
#define PC_PMBUS_VOUT_COMMAND 0x21u
#define PC_PMBUS_VOUT_MAX 0x24u
#define PC_PMBUS_VOUT_OV_FAULT_LIM 0x40u
#define PC_PMBUS_IOUT_OC_FAULT_LIM 0x46u
#define PC_PMBUS_OT_FAULT_LIMIT 0x4Fu
#define PC_PMBUS_VIN_OV_FAULT_LIM 0x55u
#define PC_PMBUS_STATUS_BYTE 0x78u
#define PC_PMBUS_STATUS_WORD 0x79u
#define PC_PMBUS_STATUS_VOUT 0x7Au
#define PC_PMBUS_STATUS_IOUT 0x7Bu
#define PC_PMBUS_STATUS_INPUT 0x7Cu
#define PC_PMBUS_STATUS_TEMP 0x7Du
#define PC_PMBUS_STATUS_CML 0x7Eu
#define PC_PMBUS_READ_VIN 0x88u
#define PC_PMBUS_READ_IIN 0x89u
#define PC_PMBUS_READ_VOUT 0x8Bu
#define PC_PMBUS_READ_IOUT 0x8Cu
#define PC_PMBUS_READ_TEMP_1 0x8Du
#define PC_PMBUS_READ_TEMP_2 0x8Eu
#define PC_PMBUS_READ_FAN_SPEED_1 0x90u
#define PC_PMBUS_READ_POUT 0x96u
#define PC_PMBUS_READ_PIN 0x97u
#define PC_PMBUS_MFR_ID 0x99u
#define PC_PMBUS_MFR_MODEL 0x9Au
#define PC_PMBUS_MFR_REVISION 0x9Bu

// STATUS_BYTE bits, which STATUS_WORD carries in its low half.
#define PC_PMBUS_ST_NONE_ABOVE 0x01u
#define PC_PMBUS_ST_CML 0x02u
#define PC_PMBUS_ST_TEMPERATURE 0x04u
#define PC_PMBUS_ST_VIN_UV 0x08u
#define PC_PMBUS_ST_IOUT_OC 0x10u
#define PC_PMBUS_ST_VOUT_OV 0x20u
#define PC_PMBUS_ST_OFF 0x40u
#define PC_PMBUS_ST_BUSY 0x80u

// VOUT_MODE bits 7:5 name which encoding the output-voltage commands use.
#define PC_PMBUS_MODE_LINEAR 0u
#define PC_PMBUS_MODE_VID 1u
#define PC_PMBUS_MODE_DIRECT 2u
#define PC_PMBUS_MODE_IEEE 3u

/** @brief Value a decode returns when the word cannot be expressed in micro-units. */
#define PC_PMBUS_INVALID INT32_MIN

/** @brief The encoding VOUT_MODE names, from its bits 7:5. */
uint8_t pc_pmbus_vout_mode_kind(uint8_t vout_mode);

/** @brief The exponent VOUT_MODE carries in its bits 4:0, sign-extended from 5 bits. */
int8_t pc_pmbus_vout_exponent(uint8_t vout_mode);

/** @brief The 11-bit signed mantissa of a LINEAR11 word, sign-extended. */
int16_t pc_pmbus_l11_mantissa(uint16_t word);

/** @brief The 5-bit signed exponent of a LINEAR11 word, sign-extended. */
int8_t pc_pmbus_l11_exponent(uint16_t word);

/**
 * @brief Decode a LINEAR11 word to micro-units: mantissa * 2^exponent, scaled by a million.
 * @return the value, or ::PC_PMBUS_INVALID if it does not fit in an int32.
 */
int32_t pc_pmbus_linear11_micro(uint16_t word);

/**
 * @brief Encode @p micro micro-units as a LINEAR11 word, picking the exponent that keeps the most
 *        significant bits of the mantissa.
 */
uint16_t pc_pmbus_linear11_encode(int32_t micro);

/**
 * @brief Decode a LINEAR16 word to micro-units: an unsigned mantissa scaled by 2^@p exponent,
 *        the exponent coming from ::pc_pmbus_vout_exponent.
 */
int32_t pc_pmbus_linear16_micro(uint16_t word, int8_t exponent);

/** @brief Encode @p micro micro-units as a LINEAR16 word at @p exponent. */
uint16_t pc_pmbus_linear16_encode(int32_t micro, int8_t exponent);

/**
 * @brief Decode a DIRECT-format word to micro-units: (word / 10^@p r - @p b) / @p m, with the
 *        coefficients coming from the part's datasheet.
 */
int32_t pc_pmbus_direct_micro(uint16_t word, int16_t m, int16_t b, int8_t r);

/** @brief Bring up the bus for PMBus traffic. */
proto_bool pc_pmbus_begin(void);

/** @brief Select page @p page on @p addr; a multi-rail part answers per page. */
proto_bool pc_pmbus_set_page(uint8_t addr, uint8_t page);

/** @brief Read VOUT_MODE, whose exponent every LINEAR16 reading on this part is scaled by. */
proto_bool pc_pmbus_read_vout_mode(uint8_t addr, uint8_t *out);

/** @brief Read a LINEAR11 telemetry command (READ_VIN, READ_IOUT, READ_PIN, ...) in micro-units. */
proto_bool pc_pmbus_read_linear11(uint8_t addr, uint8_t cmd, int32_t *micro);

/** @brief Read a LINEAR16 command (READ_VOUT, VOUT_COMMAND) in microvolts at @p exponent. */
proto_bool pc_pmbus_read_linear16(uint8_t addr, uint8_t cmd, int8_t exponent, int32_t *micro);

/** @brief Write @p micro microvolts to a LINEAR16 command at @p exponent. */
proto_bool pc_pmbus_write_linear16(uint8_t addr, uint8_t cmd, int8_t exponent, int32_t micro);

/** @brief Read STATUS_BYTE. */
proto_bool pc_pmbus_status_byte(uint8_t addr, uint8_t *out);

/** @brief Read STATUS_WORD, whose low half is STATUS_BYTE. */
proto_bool pc_pmbus_status_word(uint8_t addr, uint16_t *out);

/** @brief Clear every latched fault on @p addr. */
proto_bool pc_pmbus_clear_faults(uint8_t addr);

/**
 * @brief Read one of the block-encoded manufacturer strings (MFR_ID, MFR_MODEL, MFR_REVISION).
 * @param out  caller-owned, @p cap bytes; not terminated, the length comes back in @p len.
 */
proto_bool pc_pmbus_read_mfr_string(uint8_t addr, uint8_t cmd, uint8_t *out, size_t cap, size_t *len);

#endif // PC_ENABLE_PMBUS

PROTO_END_DECLS

#endif // PROTOCORE_PMBUS_H
