// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file vl53l0x.h
 * @brief ST VL53L0X / VL53L1X optical time-of-flight ranging codec (PC_ENABLE_VL53L0X).
 *
 * The VL53L0X emits an infrared pulse and times the round-trip to a target, reporting distance in
 * millimeters - contactless ranging and gesture, bridged to the same telemetry sink as the other field
 * sensors. Its documented register interface is small: check IDENTIFICATION_MODEL_ID (0xC0 == 0xEE),
 * start ranging via SYSRANGE_START, poll RESULT_INTERRUPT_STATUS for data-ready, read the 16-bit range
 * from RESULT_RANGE_STATUS + 10, then clear the interrupt.
 *
 * This codec is pure and host-tested: ::pc_vl53l0x_range_mm combines the range register pair,
 * ::pc_vl53l0x_data_ready decodes the interrupt-status byte, and ::pc_vl53l0x_range_valid checks the device
 * range-status field. On an ESP32 the binding runs the ranging loop over I2C (Wire); only that touches
 * hardware. Note: ST's optional tuning blob (for best accuracy) is not applied - default-settings
 * ranging via the documented registers.
 */

#ifndef PROTOCORE_VL53L0X_H
#define PROTOCORE_VL53L0X_H

#include "protocore_config.h" // the entry point: types.h for the widths and PC_INLINE

PROTO_BEGIN_DECLS

#if PC_ENABLE_VL53L0X

#define VL53L0X_REG_SYSRANGE_START 0x00
#define VL53L0X_REG_SYSTEM_INTERRUPT_CLEAR 0x0B
#define VL53L0X_REG_RESULT_INTERRUPT_STATUS 0x13
#define VL53L0X_REG_RESULT_RANGE_STATUS 0x14
#define VL53L0X_REG_IDENTIFICATION_MODEL_ID 0xC0

#define VL53L0X_MODEL_ID 0xEE  ///< IDENTIFICATION_MODEL_ID for the VL53L0X.
#define VL53L0X_RANGE_VALID 11 ///< DeviceRangeStatus value that means a valid measurement.

/** @brief Combine the range high/low bytes (RESULT_RANGE_STATUS+10 / +11) into millimeters. */
uint16_t pc_vl53l0x_range_mm(uint8_t hi, uint8_t lo);

/** @brief True if a new measurement is ready (any of the low 3 interrupt-status bits set). */
proto_bool pc_vl53l0x_data_ready(uint8_t interrupt_status);

/** @brief The DeviceRangeStatus field (bits 6:3) of the RESULT_RANGE_STATUS register. */
uint8_t pc_vl53l0x_range_status(uint8_t range_status_reg);

/** @brief True if the range-status field reports a valid measurement (== VL53L0X_RANGE_VALID). */
proto_bool pc_vl53l0x_range_valid(uint8_t range_status_reg);

// --- ESP32 binding (I2C via Wire; no-ops on a host build) ------------------------------------

/** @brief Verify the model id and start continuous back-to-back ranging at @p addr. @return present + ack. */
proto_bool pc_vl53l0x_begin(uint8_t addr);

/**
 * @brief If a measurement is ready, read the distance into @p mm and clear the interrupt.
 * @return true on a fresh, valid reading; false if not ready / invalid / I2C error.
 */
proto_bool pc_vl53l0x_read_mm(uint16_t *mm);

#endif // PC_ENABLE_VL53L0X

PROTO_END_DECLS

#endif // PROTOCORE_VL53L0X_H
