// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file fdc2214.h
 * @brief TI FDC2114/2214 capacitance-to-digital field-sensing codec (PC_ENABLE_FDC2214).
 *
 * The FDC2x14 measures the resonant frequency of an external LC tank; a capacitance shift (a finger
 * near the electrode, a liquid rising past it, a material change) moves that frequency, so watching the
 * 28-bit conversion result gives proximity / level / material sensing without contact. Each channel's
 * result is two 16-bit registers - a DATA MSB register whose top 4 bits are error flags and low 12 bits
 * are the data MSB, and a DATA LSB register - which combine into the 28-bit reading. `f_sensor =
 * data / 2^28 * f_ref`.
 *
 * This codec is pure and host-tested: ::pc_fdc2214_data combines the register pair, ::pc_fdc2214_error pulls
 * the flags, ::pc_fdc2214_sensor_freq_hz scales to frequency, and ::pc_fdc2214_build_config emits a
 * single-channel continuous-conversion bring-up as `(reg, msb, lsb)` triples. On an ESP32 the binding
 * replays that config and reads the channel over I2C (Wire); only that touches hardware. Bridge the
 * readings northbound like any sensor.
 */

#ifndef PROTOCORE_FDC2214_H
#define PROTOCORE_FDC2214_H

#include "protocore_config.h" // the entry point: types.h for the widths and PC_INLINE

PROTO_BEGIN_DECLS

#if PC_ENABLE_FDC2214

// Register map (channel 0; CH1..3 follow at +2 / +1 offsets).
#define FDC2214_REG_DATA_CH0_MSB 0x00
#define FDC2214_REG_DATA_CH0_LSB 0x01
#define FDC2214_REG_RCOUNT_CH0 0x08
#define FDC2214_REG_SETTLECOUNT_CH0 0x10
#define FDC2214_REG_CLOCK_DIVIDERS_CH0 0x14
#define FDC2214_REG_STATUS 0x18
#define FDC2214_REG_ERROR_CONFIG 0x19
#define FDC2214_REG_CONFIG 0x1A
#define FDC2214_REG_MUX_CONFIG 0x1B
#define FDC2214_REG_DRIVE_CURRENT_CH0 0x1E
#define FDC2214_REG_MANUFACTURER_ID 0x7E
#define FDC2214_REG_DEVICE_ID 0x7F

#define FDC2214_MANUFACTURER_ID 0x5449 ///< "TI".
#define FDC2214_DEVICE_ID 0x3055       ///< FDC2214 (the 12-bit FDC2114 reads 0x3054).

/** @brief Largest config sequence in bytes: 7 register writes * 3 bytes (reg, msb, lsb). */
#define FDC2214_CONFIG_MAX 21

/** @brief Combine a DATA MSB register (low 12 bits) and DATA LSB register into the 28-bit result. */
uint32_t pc_fdc2214_data(uint16_t msb_reg, uint16_t lsb_reg);

/** @brief The 4 error flags from the top of a DATA MSB register (bits 15:12). */
uint8_t pc_fdc2214_error(uint16_t msb_reg);

/** @brief Sensor frequency in Hz for a 28-bit result against a reference clock: data / 2^28 * fref. */
uint64_t pc_fdc2214_sensor_freq_hz(uint32_t data28, uint32_t fref_hz);

/**
 * @brief Emit a single-channel (CH0) continuous-conversion bring-up as `(reg, val_msb, val_lsb)` triples.
 *
 * Writes RCOUNT, SETTLECOUNT, CLOCK_DIVIDERS, DRIVE_CURRENT, ERROR_CONFIG, MUX_CONFIG, then CONFIG last
 * (CONFIG starts the conversion, so it must be written after the rest). Replay each triple as a 16-bit
 * I2C register write.
 * @return bytes written (7 * 3 = 21), or 0 if @p cap < FDC2214_CONFIG_MAX.
 */
size_t pc_fdc2214_build_config(uint8_t *buf, size_t cap, uint16_t rcount, uint16_t settlecount);

// --- ESP32 binding (I2C via Wire; no-ops on a host build) ------------------------------------

/** @brief Verify the device id and apply the CH0 config at @p addr. @return true if present + acked. */
proto_bool pc_fdc2214_begin(uint8_t addr, uint16_t rcount, uint16_t settlecount);

/** @brief Read channel 0's 28-bit conversion result into @p out. @return false on I2C error. */
proto_bool pc_fdc2214_read_ch0(uint32_t *out);

#endif // PC_ENABLE_FDC2214

PROTO_END_DECLS

#endif // PROTOCORE_FDC2214_H
