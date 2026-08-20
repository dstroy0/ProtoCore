// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dshot.h
 * @brief DShot ESC digital throttle protocol codec (PROTOCORE_ENABLE_DSHOT).
 *
 * DShot is the digital replacement for analog PWM on brushless-motor ESCs (drones, robotics). Each
 * command is a 16-bit frame - 11 bits of value, 1 telemetry-request bit, and a 4-bit CRC:
 *
 *     bits 15..5  value    (0 = disarm / command context, 1..47 = special commands, 48..2047 = throttle)
 *     bit  4      telemetry-request
 *     bits 3..0   CRC = xor of the three nibbles of (value<<1 | telemetry)
 *
 * For **bidirectional / "extended" DShot** (the ESC sends RPM/telemetry back on the same wire) the CRC
 * is inverted. This is the wire codec: `protocore_dshot_encode` builds the 16-bit frame and
 * `protocore_dshot_decode` validates the CRC and unpacks it. The physical layer (the bit-timed pulse train
 * at 150/300/600/1200 kbit via the ESP32 RMT peripheral) is the app's transport - `protocore_dshot_bit_ns`
 * gives the high-time for a 0/1 bit at a given rate so a driver can program the RMT symbols.
 *
 * Pure, zero heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_DSHOT_H
#define PROTOCORE_DSHOT_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_DSHOT

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

#define DSHOT_CMD_MOTOR_STOP 0 ///< disarm / zero throttle.

#define DSHOT_CMD_BEACON1 1 ///< beep (1..5 = rising tones).

#define DSHOT_CMD_BEACON5 5

#define DSHOT_CMD_ESC_INFO 6 ///< request ESC info (telemetry bit must be set).

#define DSHOT_CMD_SPIN_DIRECTION_1 7 ///< set spin direction normal (send 6x).

#define DSHOT_CMD_SPIN_DIRECTION_2 8 ///< set spin direction reversed (send 6x).

#define DSHOT_CMD_3D_MODE_OFF 9 ///< disable bidirectional 3D mode (send 6x).

#define DSHOT_CMD_3D_MODE_ON 10 ///< enable bidirectional 3D mode (send 6x).

#define DSHOT_CMD_SETTINGS_REQUEST 11

#define DSHOT_CMD_SAVE_SETTINGS 12 ///< persist settings (send 6x).

#define DSHOT_THROTTLE_MIN 48 ///< first real throttle step.

#define DSHOT_THROTTLE_MAX 2047 ///< last throttle step (2000 steps of resolution).

#define DSHOT_VALUE_MAX 2047 ///< widest value the 11-bit field holds.

/** @brief The legacy analog-PWM ESC protocols (pulse width carries the throttle), for protocore_esc_pwm_ns. */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_ESC_PWM,        ///< standard servo PWM: 1000-2000 us.
    PROTOCORE_ESC_ONESHOT125, ///< OneShot125: 125-250 us.
    PROTOCORE_ESC_ONESHOT42,  ///< OneShot42: 42-84 us.
    PROTOCORE_ESC_MULTISHOT,  ///< Multishot: 5-25 us.
} protocore_esc_pwm;

/** @brief What encode takes: value11, telemetry, bidirectional. */
typedef struct
{
    uint16_t value11;         ///< the 11-bit value (0..2047): a throttle (48..2047) or a special command (0..47)
    proto_bool telemetry;     ///< request telemetry on this frame
    proto_bool bidirectional; ///< bidirectional/extended DShot (the CRC is inverted)
} DshotEncodeArgs;

/** @brief What decode takes: frame, value11, telemetry, bidirectional. */
typedef struct
{
    uint16_t frame;           ///< the received 16-bit frame
    uint16_t *value11;        ///< out: the 11-bit value (may be null)
    proto_bool *telemetry;    ///< out: the telemetry-request bit (may be null)
    proto_bool bidirectional; ///< interpret the CRC as the inverted (bidirectional) form
} DshotDecodeArgs;

/** @brief What bit_ns takes: rate_kbit, bit. */
typedef struct
{
    uint16_t rate_kbit; ///< one of 150, 300, 600, 1200. Others return 0
    proto_bool bit;     ///< the bit value (false = 0, true = 1)
} DshotBitNsArgs;

/** @brief What esc_pwm_ns takes: throttle_1000, mode. */
typedef struct
{
    uint16_t throttle_1000; ///< throttle 0..1000 (clamped); 0 = min pulse (idle/arm), 1000 = max
    protocore_esc_pwm mode; ///< one of protocore_esc_pwm
} DshotEscPwmNsArgs;

/**
 * @brief DShot ESC digital throttle protocol codec (PROTOCORE_ENABLE_DSHOT). DShot is the digital replacement for ...
 *
 * A caller sets the members a call takes, invokes it through ::Dshot with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Dshot.encode_args.value11 = ...;
 *   Dshot.encode_args.telemetry = ...;
 *   Dshot.encode_args.bidirectional = ...;
 *   Dshot.encode(work);
 *   // Dshot.frame is what the call reports
 *
 * @var DshotNs::encode_args  what encode takes: value11, telemetry, bidirectional
 * @var DshotNs::decode_args  what decode takes: frame, value11, telemetry, bidirectional
 * @var DshotNs::bit_ns_args  what bit_ns takes: rate_kbit, bit
 * @var DshotNs::esc_pwm_ns_args  what esc_pwm_ns takes: throttle_1000, mode
 * @var DshotNs::ok  true if the CRC is valid
 * @var DshotNs::frame  the 16-bit frame `(value<<5) | (telemetry<<4) | crc`, ready to ...
 * @var DshotNs::ns  the high time in nanoseconds, or 0 for an unknown rate
 * @var DshotNs::encode  build a 16-bit DShot frame
 * @var DshotNs::decode  validate + unpack a 16-bit DShot frame
 * @var DshotNs::bit_ns  high-time (ns) of a bit at a DShot rate. A DShot bit is one pulse ...
 * @var DshotNs::esc_pwm_ns  pulse width (ns) for an analog-PWM ESC protocol at a given throttle
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    DshotEncodeArgs encode_args;
    DshotDecodeArgs decode_args;
    DshotBitNsArgs bit_ns_args;
    DshotEscPwmNsArgs esc_pwm_ns_args;

    proto_bool ok;
    uint16_t frame;
    uint32_t ns;

    void (*const encode)(uint8_t *restrict work);
    void (*const decode)(uint8_t *restrict work);
    void (*const bit_ns)(uint8_t *restrict work);
    void (*const esc_pwm_ns)(uint8_t *restrict work);
} DshotNs;

/** @brief The one symbol this module exports. */
extern DshotNs Dshot;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DSHOT

#endif // PROTOCORE_DSHOT_H
