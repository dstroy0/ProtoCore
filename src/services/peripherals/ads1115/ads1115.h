// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ads1115.h
 * @brief TI ADS1115 16-bit ADC codec (PC_ENABLE_ADS1115).
 *
 * The ADS1115 is a 4-channel 16-bit analog-to-digital converter on the I2C bus with a
 * programmable-gain amplifier - far more resolution and range control than the ESP32's own ADC.
 * A reading is a 16-bit config-register write (start, channel, gain, mode, data rate) followed
 * by a 16-bit read of the conversion register; the signed result scales to a voltage by the
 * selected gain's full-scale range.
 *
 * This codec is pure and host-tested: ::pc_ads1115_config_single builds the config word for a
 * single-shot single-ended reading, and ::pc_ads1115_raw_to_uv converts the signed sample to
 * microvolts. On an ESP32 the binding writes the config, waits for the conversion, and reads it
 * back over I2C (Wire); only that touches hardware.
 *
 * A cheap solder-and-bench-test breakout: measure a battery, a potentiometer, or an analog
 * sensor and bridge the reading onto the network.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ADS1115_H
#define PROTOCORE_ADS1115_H

#include "protocore_config.h" // the entry point: types.h for the widths and PC_INLINE

#define ADS1115_REG_CONVERSION 0x00 ///< conversion result register
#define ADS1115_REG_CONFIG 0x01     ///< configuration register

/** @brief Programmable-gain settings (PGA register codes; full-scale +/- range). Config field values
 *  shifted into the config word, so integer constants in a namespacing struct - cast-free. */
#define S 0   ///< +/- 6.144 V
#define _1 1  ///< +/- 4.096 V
#define _2 2  ///< +/- 2.048 V (default)
#define _4 3  ///< +/- 1.024 V
#define _8 4  ///< +/- 0.512 V
#define _16 5 ///< +/- 0.256 V

/** @brief Data-rate settings (DR register codes; samples per second). */
#define _8 0   ///< 8 SPS
#define _16 1  ///< 16 SPS
#define _32 2  ///< 32 SPS
#define _64 3  ///< 64 SPS
#define _128 4 ///< 128 SPS (default)
#define _250 5 ///< 250 SPS
#define _475 6 ///< 475 SPS
#define _860 7 ///< 860 SPS

/**
 * @brief Build the 16-bit config word for a single-shot, single-ended reading of @p channel
 * (0..3) at gain @p gain and data rate @p dr (comparator disabled). Out-of-range fields fall
 * back to channel 0 / gain +/-2.048 V / 128 SPS.
 */
uint16_t pc_ads1115_config_single(uint8_t channel, uint8_t gain, uint8_t dr);

/** @brief Convert a signed 16-bit sample to microvolts for @p gain's full-scale range. */
int32_t pc_ads1115_raw_to_uv(int16_t raw, uint8_t gain);

// --- ESP32 binding (I2C via Wire; no-ops on a host build) ------------------------------------

/** @brief Initialize the I2C bus for the ADS1115 at @p addr. @return true on ESP32. */
proto_bool pc_ads1115_begin(uint8_t addr);

/** @brief Single-shot read of @p channel (0..3) at @p gain into @p raw. @return false on error. */
proto_bool pc_ads1115_read_raw(uint8_t channel, uint8_t gain, int16_t *raw);

/** @brief Single-shot read of @p channel at @p gain, converted to microvolts in @p microvolts. */
proto_bool pc_ads1115_read_uv(uint8_t channel, uint8_t gain, int32_t *microvolts);

#endif // PROTOCORE_ADS1115_H
