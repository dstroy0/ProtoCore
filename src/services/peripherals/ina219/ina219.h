// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ina219.h
 * @brief TI INA219 high-side current / power monitor codec (PC_ENABLE_INA219).
 *
 * The INA219 measures the voltage across a shunt resistor (LSB 10 uV) and the bus voltage (LSB
 * 4 mV, in the upper 13 bits of its register), and - once a calibration value derived from the
 * shunt resistance and a chosen current LSB is programmed - reports current and power directly.
 * From those you get how much current and power a circuit draws.
 *
 * This codec is pure and host-tested: ::pc_ina219_bus_mv / ::pc_ina219_shunt_uv decode the voltage
 * registers, ::pc_ina219_calibration computes the calibration register, and ::pc_ina219_current_ua /
 * ::pc_ina219_power_uw scale the raw current / power registers by the current LSB. On an ESP32 the
 * binding programs the calibration + config and reads the registers over I2C (Wire); only that
 * touches hardware.
 *
 * A cheap solder-and-bench-test breakout: put it in series with a load and watch the current.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_INA219_H
#define PROTOCORE_INA219_H

#include "protocore_config.h" // the entry point: types.h for the widths and PC_INLINE

#if PC_ENABLE_INA219

#define INA219_REG_CONFIG 0x00      ///< configuration
#define INA219_REG_SHUNT 0x01       ///< shunt voltage
#define INA219_REG_BUS 0x02         ///< bus voltage
#define INA219_REG_POWER 0x03       ///< power
#define INA219_REG_CURRENT 0x04     ///< current
#define INA219_REG_CALIBRATION 0x05 ///< calibration

/** @brief Decode the bus-voltage register to millivolts (value is bits [15:3], LSB 4 mV). */
int32_t pc_ina219_bus_mv(uint16_t raw);

/** @brief Decode the shunt-voltage register to microvolts (signed, LSB 10 uV). */
int32_t pc_ina219_shunt_uv(int16_t raw);

/**
 * @brief Compute the calibration register from the current LSB (microamps per bit) and the shunt
 * resistance (milliohms): `trunc(0.04096 / (current_LSB[A] * R[ohm]))` = `40960000 / (lsb_ua *
 * shunt_mohm)`, clamped to 16 bits. (100 uA + 100 mohm -> 4096.) 0 on a zero denominator.
 */
uint16_t pc_ina219_calibration(uint32_t current_lsb_ua, uint32_t shunt_mohm);

/** @brief Scale the raw current register to microamps (raw * current_lsb_ua). */
int32_t pc_ina219_current_ua(int16_t raw, uint32_t current_lsb_ua);

/** @brief Scale the raw power register to microwatts (power LSB is 20 * current LSB). */
int32_t pc_ina219_power_uw(int16_t raw, uint32_t current_lsb_ua);

// --- ESP32 binding (I2C via Wire; no-ops on a host build) ------------------------------------

/**
 * @brief Program the INA219 at @p addr: write the calibration for @p current_lsb_ua (uA/bit) and
 * @p shunt_mohm (milliohms), then the default 32 V / 320 mV continuous config. @return true on ack.
 */
proto_bool pc_ina219_begin(uint8_t addr, uint32_t current_lsb_ua, uint32_t shunt_mohm);

/** @brief Read the bus voltage into @p millivolts. @return false on I2C error. */
proto_bool pc_ina219_read_bus_mv(int32_t *millivolts);

/** @brief Read the shunt voltage into @p microvolts. @return false on I2C error. */
proto_bool pc_ina219_read_shunt_uv(int32_t *microvolts);

/** @brief Read the current into @p microamps (needs the calibration set by pc_ina219_begin). */
proto_bool pc_ina219_read_current_ua(int32_t *microamps);

/** @brief Read the power into @p microwatts (needs the calibration set by pc_ina219_begin). */
proto_bool pc_ina219_read_power_uw(int32_t *microwatts);

#endif // PC_ENABLE_INA219

#endif // PROTOCORE_INA219_H
