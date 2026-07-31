// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file nts.h
 * @brief Network Time Security (NTS, RFC 8915) wire codec (PC_ENABLE_NTS).
 *
 * NTS secures NTP against spoofing. It has two wire formats, both codified here:
 *
 *  - **NTS-KE** (Key Establishment, RFC 8915 sec 4), a short record exchange run over TLS 1.3 on port
 *    4460: TLV records `[critical|type : u16][body-length : u16][body]`. The client offers a next
 *    protocol (NTPv4) + an AEAD algorithm (AES-SIV-CMAC-256); the server returns cookies + the
 *    negotiated AEAD (+ optional server/port). `pc_nts_ke_record` / `_request` build the request and
 *    `pc_nts_ke_parse` walks a response, surfacing each record via a callback.
 *
 *  - **NTS-protected NTP** (RFC 8915 sec 5), NTPv4 with RFC 7822 extension fields: the Unique
 *    Identifier, the NTS Cookie, and the NTS Authenticator-and-Encrypted-Extension-Fields (AEAD nonce +
 *    ciphertext). `pc_nts_ef` builds a padded extension field; `pc_nts_ef_unique_id` /
 *    `_cookie` are the common ones.
 *
 * Pure framing, zero heap, no stdlib, host-testable. The AES-SIV-CMAC-256 AEAD (RFC 5297) that protects
 * the authenticator, and the TLS-exporter key derivation (sec 5.1), are the crypto integration on top -
 * the label constants for that derivation are exposed here.
 */

#ifndef PROTOCORE_NTS_H
#define PROTOCORE_NTS_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_NTS

/** @brief NTS-KE record types (RFC 8915 sec 4). The critical bit is 0x8000. */
struct Nts
{
    static constexpr uint16_t NTS_KE_CRITICAL = 0x8000;
    static constexpr uint16_t NTS_KE_END_OF_MESSAGE = 0;
    static constexpr uint16_t NTS_KE_NEXT_PROTOCOL = 1;
    static constexpr uint16_t NTS_KE_ERROR = 2;
    static constexpr uint16_t NTS_KE_WARNING = 3;
    static constexpr uint16_t NTS_KE_AEAD_ALGORITHM = 4;
    static constexpr uint16_t NTS_KE_COOKIE = 5;
    static constexpr uint16_t NTS_KE_NTPV4_SERVER = 6;
    static constexpr uint16_t NTS_KE_NTPV4_PORT = 7;
    static constexpr uint16_t NTS_NEXT_PROTO_NTPV4 = 0; ///< the only next-protocol defined.
    static constexpr uint16_t NTS_AEAD_AES_SIV_CMAC_256 =
        15; ///< the mandatory-to-implement AEAD (RFC 5297 / IANA id 15).
};

/** @brief NTS NTP extension-field types (RFC 8915 sec 5.3; RFC 7822 EF format). */
struct NtsEf
{
    static constexpr uint16_t NTS_EF_UNIQUE_IDENTIFIER = 0x0104;
    static constexpr uint16_t NTS_EF_COOKIE = 0x0204;
    static constexpr uint16_t NTS_EF_COOKIE_PLACEHOLDER = 0x0304;
    static constexpr uint16_t NTS_EF_AUTH_AND_ENCRYPTED = 0x0404;
};

/** @brief RFC 8915 sec 5.1 TLS exporter label + per-direction context (C2S = 0x0000_0001_00, S2C = ..01). */
extern const char NTS_EXPORTER_LABEL[]; ///< "EXPORTER-network-time-security".

/** @brief Build one NTS-KE record `[critical|type][len][body]`. @return bytes written, or 0 if it won't fit. */
size_t pc_nts_ke_record(bool critical, uint16_t type, const uint8_t *body, size_t body_len, uint8_t *out, size_t cap);

/**
 * @brief Build the standard NTS-KE client request: Next Protocol (NTPv4), AEAD (AES-SIV-CMAC-256), End
 *        of Message - all critical. @return bytes written, or 0 if @p cap is too small.
 */
size_t pc_nts_ke_request(uint8_t *out, size_t cap);

/** @brief One record surfaced by pc_nts_ke_parse. */
typedef void (*pc_nts_ke_cb)(bool critical, uint16_t type, const uint8_t *body, size_t body_len, void *arg);

/**
 * @brief Walk an NTS-KE record stream, invoking @p cb for each record.
 * @return true if the stream is well-formed and ends with an End-of-Message record.
 */
bool pc_nts_ke_parse(const uint8_t *buf, size_t len, pc_nts_ke_cb cb, void *arg);

/**
 * @brief Build an RFC 7822 extension field `[type][length][value][padding-to-4]`.
 * @param field_type the NTS_EF_* type.
 * @param value      the field value (may be null when value_len == 0).
 * @param value_len  value length.
 * @return the total field length written (a multiple of 4), or 0 if it won't fit. The Length field
 *         counts the type + length + value + padding, per RFC 7822.
 */
size_t pc_nts_ef(uint16_t field_type, const uint8_t *value, size_t value_len, uint8_t *out, size_t cap);

/** @brief Build a Unique Identifier EF (>= 32 bytes of the caller's random, RFC 8915 sec 5.3). */
size_t pc_nts_ef_unique_id(const uint8_t *nonce, size_t nonce_len, uint8_t *out, size_t cap);

/** @brief Build an NTS Cookie EF carrying @p cookie. */
size_t pc_nts_ef_cookie(const uint8_t *cookie, size_t cookie_len, uint8_t *out, size_t cap);

#endif // PC_ENABLE_NTS
#endif // PROTOCORE_NTS_H
