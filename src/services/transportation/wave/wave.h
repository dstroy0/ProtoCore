// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wave.h
 * @brief IEEE 1609 WAVE (WSMP + 1609.2 security envelope) codec (PC_ENABLE_WAVE).
 *
 * IEEE 1609 (WAVE - Wireless Access in Vehicular Environments) is the radio stack that carries the
 * J2735 V2X messages (services/transportation/j2735). This codec provides the two framing layers below J2735:
 *
 *  - **1609.3 WSMP** (WAVE Short Message Protocol): a compact header - version/subtype, a
 *    transmit-power/channel WAVE-element block, a PSID (Provider Service Identifier, a variable-length
 *    p-encoded integer identifying the application, e.g. BSM/SPaT), and a length - then the payload.
 *  - **1609.2** secured-message envelope: `protocolVersion(3) + contentType` header that wraps a signed
 *    or unsecured-data payload (the full signature/cert machinery is the crypto layer on top).
 *
 * This builds/parses the WSMP header + PSID p-encoding and the 1609.2 envelope header. Pure, zero heap,
 * no stdlib, host-testable; the DSRC / C-V2X radio is an external module.
 */

#ifndef PROTOCORE_WAVE_H
#define PROTOCORE_WAVE_H

#include "protocore_config.h"

#if PC_ENABLE_WAVE

// WSMP / 1609.2 versions + content types + PSIDs: wire values, so integer constants in a struct.
#define WSMP_VERSION 0x03         ///< WSMP version (in the low nibble of byte 0).
#define WAVE_16092_VERSION 0x03   ///< 1609.2 protocolVersion.
#define WAVE_16092_UNSECURED 0x00 ///< content type: unsecuredData.
#define WAVE_16092_SIGNED 0x01    ///< content type: signedData.
// Common PSIDs (Provider Service Identifiers).
#define WAVE_PSID_BSM 0x20    ///< vehicle safety (BSM), PSID 0x20.
#define WAVE_PSID_SPAT 0x8002 ///< signal phase and timing.
#define WAVE_PSID_MAP 0x8003  ///< map data.

/**
 * @brief Encode a PSID as a P-encoded (variable-length) integer.
 *
 * The PSID length is signalled by the top bits of the first octet: 1 octet if < 0x80, 2 if < 0x4000,
 * 3 if < 0x200000, 4 otherwise. @return octets written (1..4), or 0 if it will not fit.
 */
size_t pc_wave_encode_psid(uint32_t psid, uint8_t *out, size_t cap);

/** @brief Decode a P-encoded PSID. @return octets consumed (1..4), or 0 if malformed; sets @p psid. */
size_t pc_wave_decode_psid(const uint8_t *in, size_t len, uint32_t *psid);

/**
 * @brief Build a WSMP data frame: WSMP header (version + PSID + a 1-byte length) then the payload.
 * @param psid    the Provider Service Identifier.
 * @param payload the WSM payload (e.g. a 1609.2-wrapped J2735 message).
 * @param payload_len 0..255.
 * @return the frame length, or 0 on overflow / bad args.
 */
size_t pc_wsmp_build(uint32_t psid, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t cap);

/** @brief A parsed WSMP frame (payload points into the input). */
typedef struct
{
    uint32_t psid;
    const uint8_t *payload;
    size_t payload_len;
} WsmpFrame;

/** @brief Parse a WSMP data frame. @return true if well-formed. */
proto_bool pc_wsmp_parse(const uint8_t *frame, size_t len, WsmpFrame *out);

/**
 * @brief Build a 1609.2 secured-message envelope header + payload: [version][contentType][payload...].
 * @param content_type WAVE_16092_UNSECURED / WAVE_16092_SIGNED.
 * @return the length written (2 + payload_len), or 0 on overflow.
 */
size_t pc_wave_1609dot2_wrap(uint8_t content_type, const uint8_t *payload, size_t payload_len, uint8_t *out,
                             size_t cap);

#endif // PC_ENABLE_WAVE
#endif // PROTOCORE_WAVE_H
