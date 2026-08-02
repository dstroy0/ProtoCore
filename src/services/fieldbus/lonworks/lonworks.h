// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file lonworks.h
 * @brief LonWorks / LON-IP (ISO/IEC 14908) network-variable codec (PC_ENABLE_LONWORKS).
 *
 * LonWorks is the ISO/IEC 14908 building-automation network. Devices exchange **network variables**
 * (SNVTs - Standard Network Variable Types) as LonTalk application PDUs. LON/IP (14908-4) tunnels those
 * PDUs over UDP so a device speaks LON without a Neuron chip. This codec builds/parses the LonTalk
 * application-layer message a network-variable update carries:
 *
 *   [msg-code : 1][nv-selector : 2 (14-bit, big-endian)][value...]
 *
 * where the message code identifies a NetVar update (`0x80 | direction`), the selector addresses the
 * bound network variable, and the value is the SNVT-encoded data. It also provides the two most-common
 * SNVT scalar encodings: **SNVT_temp** (temperature, 0.01 K resolution, offset) and **SNVT_switch**
 * (a level 0..100.5% + a state), so an app reads/writes those without a full SNVT table. Pure, zero heap,
 * no stdlib, host-testable; the LON/IP UDP transport is the shipped UDP layer.
 */

#ifndef PROTOCORE_LONWORKS_H
#define PROTOCORE_LONWORKS_H

#include "protocore_config.h"

#if PC_ENABLE_LONWORKS

// LonTalk NV message codes + selector limit: wire values, so integer constants in a struct.
#define LON_MSG_NV_UPDATE 0x80     ///< network-variable update message code (base).
#define LON_MSG_NV_POLL 0x81       ///< network-variable poll (request).
#define LON_NV_SELECTOR_MAX 0x3FFF ///< the NV selector is 14 bits.

/**
 * @brief Build a LonTalk NV-update application PDU: [msg-code][selector:2][value...].
 * @param msg_code    LON_MSG_NV_UPDATE / LON_MSG_NV_POLL.
 * @param selector    the 14-bit NV selector (0..0x3FFF).
 * @param value       the SNVT-encoded value (may be null if value_len == 0).
 * @param value_len   value length.
 * @return the PDU length (3 + value_len), or 0 on overflow / bad args.
 */
size_t pc_lon_build_nv(uint8_t msg_code, uint16_t selector, const uint8_t *value, size_t value_len, uint8_t *out,
                       size_t cap);

/** @brief A parsed LonTalk NV PDU (value points into the input). */
typedef struct
{
    uint8_t msg_code;
    uint16_t selector;
    const uint8_t *value;
    size_t value_len;
} LonNv;

/** @brief Parse a LonTalk NV PDU. @return true if @p len >= 3. */
proto_bool pc_lon_parse_nv(const uint8_t *pdu, size_t len, LonNv *out);

/** @brief Encode a SNVT_temp value (degrees C) as the 2-byte big-endian fixed-point (0.01 K, +273.15). */
void pc_lon_snvt_temp_encode(double celsius, uint8_t out[2]);
/** @brief Decode a SNVT_temp 2-byte value to degrees C. */
double pc_lon_snvt_temp_decode(const uint8_t in[2]);

/** @brief Encode a SNVT_switch (value 0..100.5 %, state 0/1) into the 2-byte value. */
void pc_lon_snvt_switch_encode(double percent, uint8_t state, uint8_t out[2]);
/** @brief Decode a SNVT_switch 2-byte value (percent out via @p percent, state via @p state). */
void pc_lon_snvt_switch_decode(const uint8_t in[2], double *percent, uint8_t *state);

#endif // PC_ENABLE_LONWORKS
#endif // PROTOCORE_LONWORKS_H
