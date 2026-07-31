// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls_policy.h
 * @brief TLS version negotiation + pinned cipher-suite policy (PC_ENABLE_TLS_POLICY).
 *
 * The transport TLS layer (`network_drivers/tls`, mbedTLS-backed) already runs the record and handshake
 * and floors the version at TLS 1.2 - so both TLS 1.2 (RFC 5246) and TLS 1.3 (RFC 8446) are negotiated.
 * What the roadmap's TLS items add on top is a *policy*: pin the negotiated version to an audited
 * [min,max] range and make the chosen version observable, and pin the cipher suites to an audited
 * allowlist selected by server preference (AEAD-only for a hardened profile).
 *
 * This is that pure policy core: `pc_tls_negotiate_version` picks the version the way a server does
 * (the highest it supports not above the client's), `pc_tls_version_name` names it for a status
 * endpoint, `pc_tls_select_cipher` selects a suite by server preference from the offered set, and
 * `pc_tls_is_aead` classifies a suite. Pure, host-testable; the app feeds the results to the mbedTLS
 * config. No heap, no stdlib.
 */

#ifndef PROTOCORE_TLS_POLICY_H
#define PROTOCORE_TLS_POLICY_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_TLS_POLICY

/** @brief TLS protocol version wire words. */
#define TLS_VERSION_1_2 0x0303
#define TLS_VERSION_1_3 0x0304

/**
 * @brief Negotiate the TLS version like a server: the highest supported that is not above the client's.
 * @param client_max the client's highest offered version.
 * @param server_min / @param server_max the server's supported range.
 * @return the chosen version word, or 0 if there is no overlap (client too old).
 */
uint16_t pc_tls_negotiate_version(uint16_t client_max, uint16_t server_min, uint16_t server_max);

/** @brief A human name for a version word: "TLS 1.2", "TLS 1.3", or "unknown". */
const char *pc_tls_version_name(uint16_t version);

/**
 * @brief Select a cipher suite by server preference: the first suite in @p server_pinned (ordered by
 *        preference) that also appears in @p client_offered.
 * @return the selected suite id, or 0 if none of the pinned suites was offered.
 */
uint16_t pc_tls_select_cipher(const uint16_t *client_offered, size_t n_client, const uint16_t *server_pinned,
                              size_t n_server);

/** @brief True if @p suite is one of the modern AEAD suites (GCM / ChaCha20-Poly1305). */
bool pc_tls_is_aead(uint16_t suite);

#endif // PC_ENABLE_TLS_POLICY
#endif // PROTOCORE_TLS_POLICY_H
