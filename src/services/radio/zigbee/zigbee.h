// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_ZIGBEE_H
#define PROTOCORE_ZIGBEE_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file zigbee.h
 * @brief Zigbee EZSP / ASH framing codec (PROTOCORE_ENABLE_ZIGBEE) - Silicon Labs NCP.
 *
 * The ASH (Asynchronous Serial Host, UG101) data-link layer that carries EZSP frames to a
 * Silicon Labs EmberZNet network co-processor over UART - a Zigbee network bridged to the
 * web. Each ASH frame is a control byte + payload + a CRC-16/CCITT, byte-stuffed so the
 * reserved control bytes never appear in the body, and terminated by a Flag byte (0x7E):
 *
 * [control | payload | CRC16(hi,lo)] --byte-stuffed--> ... | 0x7E
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
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_ZIGBEE_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @brief ASH markers / reset control bytes. */
#define ASH_FLAG 0x7E   ///< frame delimiter
#define ASH_ESCAPE 0x7D ///< byte-stuffing escape
#define ASH_RST 0xC0    ///< reset control byte
#define ASH_RSTACK 0xC1 ///< reset acknowledge
#define ASH_ERROR 0xC2  ///< error

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    uint16_t (*ash_crc16)(uint8_t *, const uint8_t *, uint16_t);
    uint16_t (*ash_frame_encode)(uint8_t *, uint8_t, const uint8_t *, uint16_t, uint8_t *, uint16_t);
    int (*ash_frame_decode)(uint8_t *, const uint8_t *, uint16_t, uint8_t *, uint8_t *, uint16_t, uint16_t *);
} ZigbeeNs;
PROTOCORE_NS_LAYOUT(ZigbeeNs, ash_crc16, ash_frame_encode, ash_frame_decode);

/**
 * @brief CRC-16/CCITT (polynomial 0x1021, MSB-first, init 0xFFFF) over buf.
 * @param work PROTOCORE_ZIGBEE_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param len Len
 * @return The uint16_t.
 */
uint16_t protocore_zigbee_ash_crc16(uint8_t *work, const uint8_t *buf, uint16_t len);
/**
 * @brief Encode an ASH frame: [control | payload] + CRC-16, byte-stuffed, .
 * @param work PROTOCORE_ZIGBEE_BORROW bytes the caller took. Not held past the call.
 * @param control Control
 * @param payload Payload
 * @param len Len
 * @param out Out
 * @param cap Cap
 * @return The uint16_t.
 */
uint16_t protocore_zigbee_ash_frame_encode(uint8_t *work, uint8_t control, const uint8_t *payload, uint16_t len,
                                           uint8_t *out, uint16_t cap);
/**
 * @brief Decode one ASH frame from the front of raw: find the flag, remove .
 * @param work PROTOCORE_ZIGBEE_BORROW bytes the caller took. Not held past the call.
 * @param raw Raw
 * @param len Len
 * @param control Control
 * @param payload Payload
 * @param pay_cap Pay cap
 * @param pay_len Pay len
 * @return The int.
 */
int protocore_zigbee_ash_frame_decode(uint8_t *work, const uint8_t *raw, uint16_t len, uint8_t *control,
                                      uint8_t *payload, uint16_t pay_cap, uint16_t *pay_len);

/** @brief Module namespace. */
PROTOCORE_NS ZigbeeNs Zigbee PROTOCORE_UNUSED = {.ash_crc16 = protocore_zigbee_ash_crc16,
                                                 .ash_frame_encode = protocore_zigbee_ash_frame_encode,
                                                 .ash_frame_decode = protocore_zigbee_ash_frame_decode};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ZIGBEE_H
