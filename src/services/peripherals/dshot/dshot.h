// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dshot.h
 * @brief DShot ESC digital throttle protocol codec (PC_ENABLE_DSHOT).
 *
 * DShot is the digital replacement for analog PWM on brushless-motor ESCs (drones, robotics). Each
 * command is a 16-bit frame - 11 bits of value, 1 telemetry-request bit, and a 4-bit CRC:
 *
 *     bits 15..5  value    (0 = disarm / command context, 1..47 = special commands, 48..2047 = throttle)
 *     bit  4      telemetry-request
 *     bits 3..0   CRC = xor of the three nibbles of (value<<1 | telemetry)
 *
 * For **bidirectional / "extended" DShot** (the ESC sends RPM/telemetry back on the same wire) the CRC
 * is inverted. This is the wire codec: `pc_dshot_encode` builds the 16-bit frame and
 * `pc_dshot_decode` validates the CRC and unpacks it. The physical layer (the bit-timed pulse train
 * at 150/300/600/1200 kbit via the ESP32 RMT peripheral) is the app's transport - `pc_dshot_bit_ns`
 * gives the high-time for a 0/1 bit at a given rate so a driver can program the RMT symbols.
 *
 * Pure, zero heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_DSHOT_H
#define PROTOCORE_DSHOT_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_DSHOT

/** @brief The standard DShot special commands (value field 0..47; throttle starts at 48). */
struct DshotCmd
{
    static constexpr uint8_t DSHOT_CMD_MOTOR_STOP = 0; ///< disarm / zero throttle.
    static constexpr uint8_t DSHOT_CMD_BEACON1 = 1;    ///< beep (1..5 = rising tones).
    static constexpr uint8_t DSHOT_CMD_BEACON5 = 5;
    static constexpr uint8_t DSHOT_CMD_ESC_INFO = 6;         ///< request ESC info (telemetry bit must be set).
    static constexpr uint8_t DSHOT_CMD_SPIN_DIRECTION_1 = 7; ///< set spin direction normal (send 6x).
    static constexpr uint8_t DSHOT_CMD_SPIN_DIRECTION_2 = 8; ///< set spin direction reversed (send 6x).
    static constexpr uint8_t DSHOT_CMD_3D_MODE_OFF = 9;      ///< disable bidirectional 3D mode (send 6x).
    static constexpr uint8_t DSHOT_CMD_3D_MODE_ON = 10;      ///< enable bidirectional 3D mode (send 6x).
    static constexpr uint8_t DSHOT_CMD_SETTINGS_REQUEST = 11;
    static constexpr uint8_t DSHOT_CMD_SAVE_SETTINGS = 12; ///< persist settings (send 6x).
    // The throttle / value domain is the 11-bit value field (0..2047), so these need a 16-bit type -
    // uint8_t would silently truncate 2047 to 255.
    static constexpr uint16_t DSHOT_THROTTLE_MIN = 48;   ///< first real throttle step.
    static constexpr uint16_t DSHOT_THROTTLE_MAX = 2047; ///< last throttle step (2000 steps of resolution).
    static constexpr uint16_t DSHOT_VALUE_MAX = 2047;    ///< widest value the 11-bit field holds.
};

/**
 * @brief Build a 16-bit DShot frame.
 * @param value11       the 11-bit value (0..2047): a throttle (48..2047) or a special command (0..47).
 * @param telemetry     request telemetry on this frame.
 * @param bidirectional bidirectional/extended DShot (the CRC is inverted).
 * @return the 16-bit frame `(value<<5) | (telemetry<<4) | crc`, ready to clock out MSB-first. @p value11
 *         above 2047 is masked to 11 bits.
 */
uint16_t pc_dshot_encode(uint16_t value11, bool telemetry, bool bidirectional);

/**
 * @brief Validate + unpack a 16-bit DShot frame.
 * @param frame         the received 16-bit frame.
 * @param value11       out: the 11-bit value (may be null).
 * @param telemetry     out: the telemetry-request bit (may be null).
 * @param bidirectional interpret the CRC as the inverted (bidirectional) form.
 * @return true if the CRC is valid.
 */
bool pc_dshot_decode(uint16_t frame, uint16_t *value11, bool *telemetry, bool bidirectional);

/**
 * @brief High-time (ns) of a bit at a DShot rate. A DShot bit is one pulse per bit-period; a "1" is
 *        high for ~3/4 of the period, a "0" for ~3/8 (the ESC samples the width).
 * @param rate_kbit one of 150, 300, 600, 1200. Others return 0.
 * @param bit       the bit value (false = 0, true = 1).
 * @return the high time in nanoseconds, or 0 for an unknown rate.
 */
uint32_t pc_dshot_bit_ns(uint16_t rate_kbit, bool bit);

/** @brief The legacy analog-PWM ESC protocols (pulse width carries the throttle), for pc_esc_pwm_ns. */
enum class pc_esc_pwm : uint8_t
{
    PC_ESC_PWM,        ///< standard servo PWM: 1000-2000 us.
    PC_ESC_ONESHOT125, ///< OneShot125: 125-250 us.
    PC_ESC_ONESHOT42,  ///< OneShot42: 42-84 us.
    PC_ESC_MULTISHOT,  ///< Multishot: 5-25 us.
};

/**
 * @brief Pulse width (ns) for an analog-PWM ESC protocol at a given throttle.
 * @param throttle_1000 throttle 0..1000 (clamped); 0 = min pulse (idle/arm), 1000 = max.
 * @param mode          one of pc_esc_pwm.
 * @return the pulse high-time in nanoseconds, linearly mapped across the mode's [min, max] range. These
 *         are driven by the MCPWM peripheral; this is the pure throttle->width mapping the driver needs.
 */
uint32_t pc_esc_pwm_ns(uint16_t throttle_1000, pc_esc_pwm mode);

#endif // PC_ENABLE_DSHOT
#endif // PROTOCORE_DSHOT_H
