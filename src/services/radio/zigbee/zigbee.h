// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file zigbee.h
 * @brief Zigbee EZSP / ASH framing codec (PC_ENABLE_ZIGBEE) - Silicon Labs NCP.
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
 * pc_ash_frame_encode() wraps a control byte + payload into a stuffed, CRC'd, flag-terminated
 * frame; pc_ash_frame_decode() finds the flag, removes the stuffing, and verifies the CRC.
 * pc_ash_crc16() is the shared CRC. The EZSP command the payload carries (version query, an
 * incoming APS message, a network-init) is the application's. Pure - you carry the bytes
 * over your UART - so it is fully host-testable.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ZIGBEE_H
#define PROTOCORE_ZIGBEE_H

#include "protocore_config.h"

#if PC_ENABLE_ZIGBEE

/** @brief ASH markers / reset control bytes. */
#define ASH_FLAG 0x7E   ///< frame delimiter
#define ASH_ESCAPE 0x7D ///< byte-stuffing escape
#define ASH_RST 0xC0    ///< reset control byte
#define ASH_RSTACK 0xC1 ///< reset acknowledge
#define ASH_ERROR 0xC2  ///< error

/** @brief CRC-16/CCITT (polynomial 0x1021, MSB-first, init 0xFFFF) over @p buf. */
uint16_t pc_ash_crc16(const uint8_t *buf, uint16_t len);

/**
 * @brief Encode an ASH frame: [control | payload] + CRC-16, byte-stuffed, flag-terminated.
 * @return the encoded frame length, or 0 if @p len exceeds PC_ZIGBEE_MAX_DATA or the
 *         stuffed frame would not fit @p cap.
 */
uint16_t pc_ash_frame_encode(uint8_t control, const uint8_t *payload, uint16_t len, uint8_t *out, uint16_t cap);

/**
 * @brief Decode one ASH frame from the front of @p raw: find the flag, remove the stuffing,
 *        verify the CRC, and copy the payload to @p payload (up to @p pay_cap).
 * @param[out] control  set to the frame's control byte.
 * @param[out] pay_len  set to the payload length.
 * @return the bytes consumed up to and including the flag (> 0), 0 if no flag is present yet
 *         (need more), or -1 if the framed content is malformed (too short, bad CRC, or the
 *         payload overflows @p pay_cap) - the caller drops one byte and retries.
 */
int pc_ash_frame_decode(const uint8_t *raw, uint16_t len, uint8_t *control, uint8_t *payload, uint16_t pay_cap,
                        uint16_t *pay_len);

#endif // PC_ENABLE_ZIGBEE

#endif // PROTOCORE_ZIGBEE_H
