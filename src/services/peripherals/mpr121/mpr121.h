// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mpr121.h
 * @brief NXP MPR121 12-channel capacitive-touch controller codec (PC_ENABLE_MPR121).
 *
 * The MPR121 reports a 16-bit touch-status word (registers 0x00/0x01): bits 0-11 are the twelve
 * electrodes, bit 12 is the proximity electrode, and bit 15 is the over-current flag. It also
 * exposes 10-bit filtered capacitance and 8-bit baseline per electrode. Bringing it up is a
 * fixed sequence of register writes (soft reset, the NXP filter/AFE defaults, per-electrode
 * touch/release thresholds, and the electrode-configuration register that starts it running).
 *
 * This codec is pure and host-tested: ::pc_mpr121_touched / ::pc_mpr121_word10 decode the reported
 * words, and ::pc_mpr121_build_init emits the whole bring-up sequence as `(register, value)` byte
 * pairs (so the exact bytes are verifiable off-target). On an ESP32 the binding replays that
 * sequence over I2C (Wire) and reads the status; only that touches hardware.
 *
 * A cheap solder-and-bench-test breakout for touch buttons / sliders: wire it up, touch a pad,
 * watch the bit set.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MPR121_H
#define PROTOCORE_MPR121_H

#include "protocore_config.h" // the entry point: types.h for the widths and PC_INLINE

PROTO_BEGIN_DECLS

#if PC_ENABLE_MPR121

/** @brief Sense electrodes on the MPR121 (ELE0..ELE11). */
#define MPR121_ELECTRODES 12

/** @brief Largest init sequence in bytes: (17 fixed + 2*12 threshold) pairs * 2 bytes. */
#define MPR121_INIT_MAX 82

/**
 * @brief Decode the 12-electrode touch bitmask from the two status registers (0x00 low, 0x01
 * high). Bit i (0..11) is set when electrode i is touched; proximity (bit 12) and the
 * over-current flag are masked out (see ::pc_mpr121_proximity / ::pc_mpr121_overcurrent).
 */
uint16_t pc_mpr121_touched(uint8_t status_lo, uint8_t status_hi);

/** @brief True if electrode @p e (0..11) is touched in a mask from ::pc_mpr121_touched. */
proto_bool pc_mpr121_is_touched(uint16_t mask, uint8_t e);

/** @brief True if the proximity electrode (status bit 12) is active. */
proto_bool pc_mpr121_proximity(uint8_t status_hi);

/** @brief True if the over-current flag (status bit 15) is set (wiring fault / short). */
proto_bool pc_mpr121_overcurrent(uint8_t status_hi);

/** @brief Combine a little-endian LSB/MSB register pair into a 10-bit value (filtered/baseline). */
uint16_t pc_mpr121_word10(uint8_t lsb, uint8_t msb);

/**
 * @brief Build the MPR121 bring-up sequence as consecutive `(register, value)` byte pairs.
 *
 * Emits: soft reset, ECR stop, the NXP rising/falling/touched filter defaults, per-electrode
 * touch/release thresholds for @p n_electrodes, debounce, CONFIG1/CONFIG2, and finally the ECR
 * that enables @p n_electrodes with baseline tracking. Replay each pair as an I2C register
 * write, in order (the ECR-start pair must be written last).
 * @return the number of bytes written (pairs * 2), or 0 if @p cap is too small / args invalid.
 */
size_t pc_mpr121_build_init(uint8_t *buf, size_t cap, uint8_t n_electrodes, uint8_t touch_thr, uint8_t release_thr);

// --- ESP32 binding (the shared I2C bus owner; no-ops on a host build) -------------------------

/** @brief Reset + configure the MPR121 at @p addr over I2C. @return true if it acknowledged. */
proto_bool pc_mpr121_begin(uint8_t addr);

/** @brief Read the current 12-electrode touch bitmask (0 if the device is absent). */
uint16_t pc_mpr121_read_touched(void);

/** @brief Read electrode @p e's 10-bit filtered capacitance value. */
uint16_t pc_mpr121_read_filtered(uint8_t e);

#endif // PC_ENABLE_MPR121

PROTO_END_DECLS

#endif // PROTOCORE_MPR121_H
