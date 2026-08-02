// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pca9685.h
 * @brief NXP PCA9685 16-channel 12-bit PWM / servo driver codec (PC_ENABLE_PCA9685).
 *
 * The PCA9685 generates sixteen independent 12-bit PWM outputs from a 25 MHz oscillator. The
 * output frequency is set by a PRESCALE register value; each channel is four registers (a 12-bit
 * ON count and a 12-bit OFF count) at `0x06 + 4 * channel`. Driving a hobby servo is a matter of
 * turning a pulse width (in microseconds) into an OFF count at the configured frequency.
 *
 * This codec is pure and host-tested: ::pc_pca9685_prescale computes the prescale for a frequency,
 * ::pc_pca9685_us_to_count converts a servo pulse width to a 12-bit count, ::pc_pca9685_channel_reg
 * gives a channel's register base, and ::pc_pca9685_set_pwm_bytes emits the 5-byte channel write.
 * On an ESP32 the binding replays those writes over I2C (Wire); only that touches hardware.
 *
 * A cheap solder-and-bench-test breakout for driving up to 16 servos or LEDs: wire it up, sweep
 * a servo.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PCA9685_H
#define PROTOCORE_PCA9685_H

#include "protocore_config.h" // the entry point: types.h for the widths and PC_INLINE

#define PCA9685_CHANNELS 16     ///< PWM output channels
#define PCA9685_COUNT_MAX 4095  ///< a PWM count is 12-bit (0..4095)
#define PCA9685_FULL_ON 0x1000  ///< pass as `on` for a channel fully on (bit 12 flag)
#define PCA9685_FULL_OFF 0x1000 ///< pass as `off` for a channel fully off (bit 12 flag)

// Registers.
#define PCA9685_REG_MODE1 0x00
#define PCA9685_REG_MODE2 0x01
#define PCA9685_REG_LED0_ON_L 0x06 ///< channel 0 base; channel n is this + 4*n
#define PCA9685_REG_PRESCALE 0xFE

/**
 * @brief Compute the PRESCALE register value for a PWM output frequency (25 MHz oscillator):
 * `round(25e6 / (4096 * freq)) - 1`, clamped to the chip's valid 3..255 range.
 */
uint8_t pc_pca9685_prescale(uint32_t freq_hz);

/** @brief The register base (LED_ON_L) for @p channel (0..15); 0 for an out-of-range channel. */
uint8_t pc_pca9685_channel_reg(uint8_t channel);

/**
 * @brief Convert a servo pulse width (microseconds) at @p freq_hz to a 12-bit OFF count
 * (rounded), clamped to 0..4095. ON is taken as 0, so the pulse starts at the period's edge.
 */
uint16_t pc_pca9685_us_to_count(uint32_t microseconds, uint32_t freq_hz);

/**
 * @brief Emit the 5-byte channel PWM write: `[LED_ON_L(channel), ON_L, ON_H, OFF_L, OFF_H]`
 * (each count 12-bit little-endian; bit 12 / ::PCA9685_FULL_ON is the full-on/off flag).
 * @return 5, or 0 if @p cap < 5 or @p channel is out of range.
 */
size_t pc_pca9685_set_pwm_bytes(uint8_t *buf, size_t cap, uint8_t channel, uint16_t on, uint16_t off);

// --- ESP32 binding (I2C via Wire; no-ops on a host build) ------------------------------------

/** @brief Reset the PCA9685 at @p addr and set the PWM frequency @p freq_hz. @return true on ack. */
proto_bool pc_pca9685_begin(uint8_t addr, uint32_t freq_hz);

/** @brief Set @p channel's raw 12-bit ON / OFF counts. @return false on I2C error / bad channel. */
proto_bool pc_pca9685_set_pwm(uint8_t channel, uint16_t on, uint16_t off);

/** @brief Drive a servo on @p channel to a @p microseconds pulse (uses the configured frequency). */
proto_bool pc_pca9685_set_servo_us(uint8_t channel, uint32_t microseconds);

#endif // PROTOCORE_PCA9685_H
