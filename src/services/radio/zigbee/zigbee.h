// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file zigbee.h
 * @brief Zigbee EZSP / ASH framing codec (PROTOCORE_ENABLE_ZIGBEE) - Silicon Labs NCP.
 *
 * The ASH (Asynchronous Serial Host, UG101) data-link layer that carries EZSP frames to a
 * Silicon Labs EmberZNet network co-processor over UART - a Zigbee network bridged to the
 * web. Each ASH frame is a control byte + payload + a CRC-16/CCITT, byte-stuffed so the
 * reserved control bytes never appear in the body, and terminated by a Flag byte (0x7E):
 *
 *   [control | payload | CRC16(hi,lo)] --byte-stuffed--> ... | 0x7E
 *
 * The reserved bytes that get stuffed (as 0x7D followed by byte XOR 0x20) are the Flag
 * 0x7E, the Escape 0x7D, XON 0x11, XOFF 0x13, Substitute 0x18, and Cancel 0x1A.
 *
 * protocore_ash_frame_encode() wraps a control byte + payload into a stuffed, CRC'd, flag-terminated
 * frame; protocore_ash_frame_decode() finds the flag, removes the stuffing, and verifies the CRC.
 * protocore_ash_crc16() is the shared CRC. The EZSP command the payload carries (version query, an
 * incoming APS message, a network-init) is the application's. Pure - you carry the bytes
 * over your UART - so it is fully host-testable.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ZIGBEE_H
#define PROTOCORE_ZIGBEE_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_ZIGBEE

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief ASH markers / reset control bytes. */
#define ASH_FLAG 0x7E   ///< frame delimiter
#define ASH_ESCAPE 0x7D ///< byte-stuffing escape
#define ASH_RST 0xC0    ///< reset control byte
#define ASH_RSTACK 0xC1 ///< reset acknowledge
#define ASH_ERROR 0xC2  ///< error

/** @brief What ash_crc16 takes: buf, len. */
typedef struct
{
    const uint8_t *buf;
    uint16_t len;
} ZigbeeAshCrc16Args;
/** @brief What ash_frame_encode takes: control, payload, len, out, cap. */
typedef struct
{
    uint8_t control;
    const uint8_t *payload;
    uint16_t len;
    uint8_t *out;
    uint16_t cap;
} ZigbeeAshFrameEncodeArgs;
/** @brief What ash_frame_decode takes: raw, len, control, payload, ... */
typedef struct
{
    const uint8_t *raw;
    uint16_t len;
    uint8_t *control;
    uint8_t *payload;
    uint16_t pay_cap;
    uint16_t *pay_len;
} ZigbeeAshFrameDecodeArgs;
/**
 * @brief Zigbee EZSP / ASH framing codec (PROTOCORE_ENABLE_ZIGBEE) - Silicon Labs NCP.
 *
 * A caller sets the members a call takes, invokes it through ::Zigbee with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Zigbee.ash_crc16_args.buf = ...;
 *   Zigbee.ash_crc16_args.len = ...;
 *   Zigbee.ash_crc16(work);
 *   // Zigbee.value is what the call reports
 *
 * @var ZigbeeNs::ash_crc16_args  what ash_crc16 takes: buf, len
 * @var ZigbeeNs::ash_frame_encode_args  what ash_frame_encode takes: control, payload, len, out, cap
 * @var ZigbeeNs::ash_frame_decode_args  what ash_frame_decode takes: raw, len, control, payload,
 * @var ZigbeeNs::ok  a call's true/false outcome
 * @var ZigbeeNs::value  the encoded frame length, or 0 if len exceeds ...
 * @var ZigbeeNs::n  the bytes consumed up to and including the flag (> 0), 0 if no flag ...
 * @var ZigbeeNs::ash_crc16  CRC-16/CCITT (polynomial 0x1021, MSB-first, init 0xFFFF) over buf
 * @var ZigbeeNs::ash_frame_encode  encode an ASH frame: [control | payload] + CRC-16, byte-stuffed, ...
 * @var ZigbeeNs::ash_frame_decode  decode one ASH frame from the front of raw: find the flag, remove ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    ZigbeeAshCrc16Args ash_crc16_args;
    ZigbeeAshFrameEncodeArgs ash_frame_encode_args;
    ZigbeeAshFrameDecodeArgs ash_frame_decode_args;
    proto_bool ok;
    uint16_t value;
    int n;
} ZigbeeVars;

/** @brief The operands and the outcome. */
extern ZigbeeVars ZigbeeV;

/** @brief The entries. */
typedef struct
{
    void (*const ash_crc16)(uint8_t *restrict work);
    void (*const ash_frame_encode)(uint8_t *restrict work);
    void (*const ash_frame_decode)(uint8_t *restrict work);
} ZigbeeNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in ZigbeeV or a region of the borrow at a fixed offset.
void protocore_zigbee_ash_crc16(uint8_t *restrict work);
void protocore_zigbee_ash_frame_encode(uint8_t *restrict work);
void protocore_zigbee_ash_frame_decode(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Zigbee.ash_crc16(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const ZigbeeNs Zigbee __attribute__((unused)) = {
    .ash_crc16 = protocore_zigbee_ash_crc16,
    .ash_frame_encode = protocore_zigbee_ash_frame_encode,
    .ash_frame_decode = protocore_zigbee_ash_frame_decode,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ZIGBEE

#endif // PROTOCORE_ZIGBEE_H
