// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file vl53l0x.h
 * @brief ST VL53L0X / VL53L1X optical time-of-flight ranging codec (PROTOCORE_ENABLE_VL53L0X).
 *
 * The VL53L0X emits an infrared pulse and times the round-trip to a target, reporting distance in
 * millimeters - contactless ranging and gesture, bridged to the same telemetry sink as the other field
 * sensors. Its documented register interface is small: check IDENTIFICATION_MODEL_ID (0xC0 == 0xEE),
 * start ranging via SYSRANGE_START, poll RESULT_INTERRUPT_STATUS for data-ready, read the 16-bit range
 * from RESULT_RANGE_STATUS + 10, then clear the interrupt.
 *
 * This codec is pure and host-tested: ::protocore_vl53l0x_range_mm combines the range register pair,
 * ::protocore_vl53l0x_data_ready decodes the interrupt-status byte, and ::protocore_vl53l0x_range_valid checks the
 * device range-status field. On an ESP32 the binding runs the ranging loop over I2C (Wire); only that touches hardware.
 * Note: ST's optional tuning blob (for best accuracy) is not applied - default-settings ranging via the documented
 * registers.
 */

#ifndef PROTOCORE_VL53L0X_H
#define PROTOCORE_VL53L0X_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_VL53L0X

PROTOCORE_BEGIN_DECLS

// PROTOCORE_I2C_DEVICE_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

#define VL53L0X_REG_SYSRANGE_START 0x00

#define VL53L0X_REG_SYSTEM_INTERRUPT_CLEAR 0x0B

#define VL53L0X_REG_RESULT_INTERRUPT_STATUS 0x13

#define VL53L0X_REG_RESULT_RANGE_STATUS 0x14

#define VL53L0X_REG_IDENTIFICATION_MODEL_ID 0xC0

#define VL53L0X_MODEL_ID 0xEE ///< IDENTIFICATION_MODEL_ID for the VL53L0X.

#define VL53L0X_RANGE_VALID 11 ///< DeviceRangeStatus value that means a valid measurement.

/** @brief What range_mm takes: hi, lo. */
typedef struct
{
    uint8_t hi;
    uint8_t lo;
} Vl53l0xRangeMmArgs;

/** @brief What data_ready takes: interrupt_status. */
typedef struct
{
    uint8_t interrupt_status;
} Vl53l0xDataReadyArgs;

/** @brief What range_status takes: range_status_reg. */
typedef struct
{
    uint8_t range_status_reg;
} Vl53l0xRangeStatusArgs;

/** @brief What range_valid takes: range_status_reg. */
typedef struct
{
    uint8_t range_status_reg;
} Vl53l0xRangeValidArgs;

/** @brief What begin takes: addr. */
typedef struct
{
    uint8_t addr;
} Vl53l0xBeginArgs;

/** @brief What read_mm takes: mm. */
typedef struct
{
    uint16_t *mm;
} Vl53l0xReadMmArgs;

/**
 * @brief ST VL53L0X / VL53L1X optical time-of-flight ranging codec (PROTOCORE_ENABLE_VL53L0X).
 *
 * A caller sets the members a call takes, invokes it through ::Vl53l0x with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Vl53l0x.range_mm_args.hi = ...;
 *   Vl53l0x.range_mm_args.lo = ...;
 *   Vl53l0x.range_mm(work);
 *   // Vl53l0x.mm is what the call reports
 *
 * @var Vl53l0xNs::range_mm_args  what range_mm takes: hi, lo
 * @var Vl53l0xNs::data_ready_args  what data_ready takes: interrupt_status
 * @var Vl53l0xNs::range_status_args  what range_status takes: range_status_reg
 * @var Vl53l0xNs::range_valid_args  what range_valid takes: range_status_reg
 * @var Vl53l0xNs::begin_args  what begin takes: addr
 * @var Vl53l0xNs::read_mm_args  what read_mm takes: mm
 * @var Vl53l0xNs::ok  true on a fresh, valid reading; false if not ready / invalid / I2C ...
 * @var Vl53l0xNs::mm  what a call reports
 * @var Vl53l0xNs::status  what a call reports
 * @var Vl53l0xNs::range_mm  combine the range high/low bytes (RESULT_RANGE_STATUS+10 / +11) ...
 * @var Vl53l0xNs::data_ready  true if a new measurement is ready (any of the low 3 ...
 * @var Vl53l0xNs::range_status  the DeviceRangeStatus field (bits 6:3) of the RESULT_RANGE_STATUS ...
 * @var Vl53l0xNs::range_valid  true if the range-status field reports a valid measurement (== ...
 * @var Vl53l0xNs::begin  verify the model id and start continuous back-to-back ranging at ...
 * @var Vl53l0xNs::read_mm  if a measurement is ready, read the distance into mm and clear the ...
 *
 * @c work is PROTOCORE_I2C_DEVICE_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    Vl53l0xRangeMmArgs range_mm_args;
    Vl53l0xDataReadyArgs data_ready_args;
    Vl53l0xRangeStatusArgs range_status_args;
    Vl53l0xRangeValidArgs range_valid_args;
    Vl53l0xBeginArgs begin_args;
    Vl53l0xReadMmArgs read_mm_args;

    proto_bool ok;
    uint16_t mm;
    uint8_t status;

    void (*const range_mm)(uint8_t *restrict work);
    void (*const data_ready)(uint8_t *restrict work);
    void (*const range_status)(uint8_t *restrict work);
    void (*const range_valid)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const read_mm)(uint8_t *restrict work);
} Vl53l0xNs;

/** @brief The one symbol this module exports. */
extern Vl53l0xNs Vl53l0x;

/**
 * @brief The PROTOCORE_I2C_DEVICE_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_vl53l0x_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_VL53L0X

#endif // PROTOCORE_VL53L0X_H
