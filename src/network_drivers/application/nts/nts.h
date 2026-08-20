// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file nts.h
 * @brief Network Time Security (NTS, RFC 8915) wire codec (PROTOCORE_ENABLE_NTS).
 *
 * NTS secures NTP against spoofing. It has two wire formats, both codified here:
 *
 *  - **NTS-KE** (Key Establishment, RFC 8915 sec 4), a short record exchange run over TLS 1.3 on port
 *    4460: TLV records `[critical|type : u16][body-length : u16][body]`. The client offers a next
 *    protocol (NTPv4) + an AEAD algorithm (AES-SIV-CMAC-256); the server returns cookies + the
 *    negotiated AEAD (+ optional server/port). `protocore_nts_ke_record` / `_request` build the request and
 *    `protocore_nts_ke_parse` walks a response, surfacing each record via a callback.
 *
 *  - **NTS-protected NTP** (RFC 8915 sec 5), NTPv4 with RFC 7822 extension fields: the Unique
 *    Identifier, the NTS Cookie, and the NTS Authenticator-and-Encrypted-Extension-Fields (AEAD nonce +
 *    ciphertext). `protocore_nts_ef` builds a padded extension field; `protocore_nts_ef_unique_id` /
 *    `_cookie` are the common ones.
 *
 * Pure framing, zero heap, no stdlib, host-testable. The AES-SIV-CMAC-256 AEAD (RFC 5297) that protects
 * the authenticator, and the TLS-exporter key derivation (sec 5.1), are the crypto integration on top -
 * the label constants for that derivation are exposed here.
 */

#ifndef PROTOCORE_NTS_H
#define PROTOCORE_NTS_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_NTS

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief NTS-KE record types (RFC 8915 sec 4). The critical bit is 0x8000. */
#define NTS_KE_CRITICAL 0x8000
#define NTS_KE_END_OF_MESSAGE 0
#define NTS_KE_NEXT_PROTOCOL 1
#define NTS_KE_ERROR 2
#define NTS_KE_WARNING 3
#define NTS_KE_AEAD_ALGORITHM 4
#define NTS_KE_COOKIE 5
#define NTS_KE_NTPV4_SERVER 6
#define NTS_KE_NTPV4_PORT 7
#define NTS_NEXT_PROTO_NTPV4 0       ///< the only next-protocol defined.
#define NTS_AEAD_AES_SIV_CMAC_256 15 ///< the mandatory-to-implement AEAD (RFC 5297 / IANA id 15).

/** @brief NTS NTP extension-field types (RFC 8915 sec 5.3; RFC 7822 EF format). */
#define NTS_EF_UNIQUE_IDENTIFIER 0x0104
#define NTS_EF_COOKIE 0x0204
#define NTS_EF_COOKIE_PLACEHOLDER 0x0304
#define NTS_EF_AUTH_AND_ENCRYPTED 0x0404

/** @brief One record surfaced by protocore_nts_ke_parse. */
typedef void (*protocore_nts_ke_cb)(proto_bool critical, uint16_t type, const uint8_t *body, size_t body_len,
                                    void *arg);

/** @brief What ke_record takes: critical, type, body, body_len, out, ... */
typedef struct
{
    proto_bool critical;
    uint16_t type;
    const uint8_t *body;
    size_t body_len;
    uint8_t *out;
    size_t cap;
} NtsKeRecordArgs;

/** @brief What ke_request takes: out, cap. */
typedef struct
{
    uint8_t *out;
    size_t cap;
} NtsKeRequestArgs;

/** @brief What ke_parse takes: buf, len, cb, arg. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    protocore_nts_ke_cb cb;
    void *arg;
} NtsKeParseArgs;

/** @brief What ef takes: field_type, value, value_len, out, cap. */
typedef struct
{
    uint16_t field_type;  ///< the NTS_EF_* type
    const uint8_t *value; ///< the field value (may be null when value_len == 0)
    size_t value_len;     ///< value length
    uint8_t *out;
    size_t cap;
} NtsEfArgs;

/** @brief What ef_unique_id takes: nonce, nonce_len, out, cap. */
typedef struct
{
    const uint8_t *nonce;
    size_t nonce_len;
    uint8_t *out;
    size_t cap;
} NtsEfUniqueIdArgs;

/** @brief What ef_cookie takes: cookie, cookie_len, out, cap. */
typedef struct
{
    const uint8_t *cookie;
    size_t cookie_len;
    uint8_t *out;
    size_t cap;
} NtsEfCookieArgs;

/** @brief RFC 8915 sec 5.1 TLS exporter label + per-direction context (C2S = 0x0000_0001_00, S2C = ..01). */
extern const char NTS_EXPORTER_LABEL[]; ///< "EXPORTER-network-time-security".

/**
 * @brief Network Time Security (NTS, RFC 8915) wire codec (PROTOCORE_ENABLE_NTS).
 *
 * A caller sets the members a call takes, invokes it through ::Nts with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Nts.ke_record_args.critical = ...;
 *   Nts.ke_record_args.type = ...;
 *   Nts.ke_record_args.body = ...;
 *   Nts.ke_record_args.body_len = ...;
 *   Nts.ke_record_args.out = ...;
 *   Nts.ke_record_args.cap = ...;
 *   Nts.ke_record(work);
 *   // Nts.n is what the call reports
 *
 * @var NtsNs::ke_record_args  what ke_record takes: critical, type, body, body_len, out,
 * @var NtsNs::ke_request_args  what ke_request takes: out, cap
 * @var NtsNs::ke_parse_args  what ke_parse takes: buf, len, cb, arg
 * @var NtsNs::ef_args  what ef takes: field_type, value, value_len, out, cap
 * @var NtsNs::ef_unique_id_args  what ef_unique_id takes: nonce, nonce_len, out, cap
 * @var NtsNs::ef_cookie_args  what ef_cookie takes: cookie, cookie_len, out, cap
 * @var NtsNs::ok  true if the stream is well-formed and ends with an End-of-Message ...
 * @var NtsNs::n  the total field length written (a multiple of 4), or 0 if it won't ...
 * @var NtsNs::ke_record  build one NTS-KE record `[critical|type][len][body]`. bytes ...
 * @var NtsNs::ke_request  build the standard NTS-KE client request: Next Protocol (NTPv4), ...
 * @var NtsNs::ke_parse  walk an NTS-KE record stream, invoking cb for each record
 * @var NtsNs::ef  build an RFC 7822 extension field ...
 * @var NtsNs::ef_unique_id  build a Unique Identifier EF (>= 32 bytes of the caller's random, ...
 * @var NtsNs::ef_cookie  build an NTS Cookie EF carrying cookie
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    NtsKeRecordArgs ke_record_args;
    NtsKeRequestArgs ke_request_args;
    NtsKeParseArgs ke_parse_args;
    NtsEfArgs ef_args;
    NtsEfUniqueIdArgs ef_unique_id_args;
    NtsEfCookieArgs ef_cookie_args;

    proto_bool ok;
    size_t n;

    void (*const ke_record)(uint8_t *restrict work);
    void (*const ke_request)(uint8_t *restrict work);
    void (*const ke_parse)(uint8_t *restrict work);
    void (*const ef)(uint8_t *restrict work);
    void (*const ef_unique_id)(uint8_t *restrict work);
    void (*const ef_cookie)(uint8_t *restrict work);
} NtsNs;

/** @brief The one symbol this module exports. */
extern NtsNs Nts;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_NTS

#endif // PROTOCORE_NTS_H
