// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ldc1614.h
 * @brief TI LDC1614 inductance-to-digital field-sensing codec (PC_ENABLE_LDC1614).
 *
 * The LDC1614 measures the resonant frequency of an external LC tank driven by a coil; a nearby
 * conductor changes the coil's effective inductance (eddy currents), moving that frequency - so the
 * 28-bit conversion result tracks metal proximity, displacement, and EM-field perturbation without
 * contact. It shares TI's FDC/LDC register architecture: each channel's result is a DATA MSB register
 * (top 4 bits error flags, low 12 bits data MSB) plus a DATA LSB register, combining into 28 bits, with
 * `f_sensor = data / 2^28 * f_ref` and `L = 1 / (C * (2*pi*f)^2)` derived by the app from the tank C.
 *
 * This codec is pure and host-tested: ::pc_ldc1614_data combines the register pair, ::pc_ldc1614_error pulls
 * the flags, ::pc_ldc1614_sensor_freq_hz scales to frequency, and ::pc_ldc1614_build_config emits a
 * single-channel bring-up. On an ESP32 the binding replays that config and reads the channel over I2C;
 * only that touches hardware. Bridge the readings northbound like any sensor.
 */

#ifndef PROTOCORE_LDC1614_H
#define PROTOCORE_LDC1614_H

#include "protocore_config.h" // the entry point: types.h for the widths and PC_INLINE

PROTO_BEGIN_DECLS

#if PC_ENABLE_LDC1614

// Register map (channel 0).
#define LDC1614_REG_DATA_CH0_MSB 0x00
#define LDC1614_REG_DATA_CH0_LSB 0x01
#define LDC1614_REG_RCOUNT_CH0 0x08
#define LDC1614_REG_SETTLECOUNT_CH0 0x10
#define LDC1614_REG_CLOCK_DIVIDERS_CH0 0x14
#define LDC1614_REG_STATUS 0x18
#define LDC1614_REG_ERROR_CONFIG 0x19
#define LDC1614_REG_CONFIG 0x1A
#define LDC1614_REG_MUX_CONFIG 0x1B
#define LDC1614_REG_DRIVE_CURRENT_CH0 0x1E
#define LDC1614_REG_MANUFACTURER_ID 0x7E
#define LDC1614_REG_DEVICE_ID 0x7F

#define LDC1614_MANUFACTURER_ID 0x5449 ///< "TI".
#define LDC1614_DEVICE_ID 0x3055       ///< LDC1614 / LDC1612.

/** @brief Largest config sequence in bytes: 7 register writes * 3 bytes (reg, msb, lsb). */
#define LDC1614_CONFIG_MAX 21

/** @brief Combine a DATA MSB register (low 12 bits) and DATA LSB register into the 28-bit result. */
uint32_t pc_ldc1614_data(uint16_t msb_reg, uint16_t lsb_reg);

/** @brief The 4 error flags from the top of a DATA MSB register (bits 15:12). */
uint8_t pc_ldc1614_error(uint16_t msb_reg);

/** @brief Sensor frequency in Hz for a 28-bit result against a reference clock: data / 2^28 * fref. */
uint64_t pc_ldc1614_sensor_freq_hz(uint32_t data28, uint32_t fref_hz);

/**
 * @brief Emit a single-channel (CH0) continuous-conversion bring-up as `(reg, val_msb, val_lsb)` triples.
 *
 * Writes RCOUNT, SETTLECOUNT, CLOCK_DIVIDERS, DRIVE_CURRENT, ERROR_CONFIG, MUX_CONFIG, then CONFIG last
 * (CONFIG starts the conversion). Replay each triple as a 16-bit I2C register write.
 * @return bytes written (7 * 3 = 21), or 0 if @p cap < LDC1614_CONFIG_MAX.
 */
size_t pc_ldc1614_build_config(uint8_t *buf, size_t cap, uint16_t rcount, uint16_t settlecount);

// --- ESP32 binding (I2C via Wire) ------------------------------------

/** @brief Verify the device id and apply the CH0 config at @p addr. @return true if present + acked. */
proto_bool pc_ldc1614_begin(uint8_t addr, uint16_t rcount, uint16_t settlecount);

/** @brief Read channel 0's 28-bit conversion result into @p out. @return false on I2C error. */
proto_bool pc_ldc1614_read_ch0(uint32_t *out);

#endif // PC_ENABLE_LDC1614

PROTO_END_DECLS

#endif // PROTOCORE_LDC1614_H
