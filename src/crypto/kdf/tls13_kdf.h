// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls13_kdf.h
 * @brief TLS 1.3 key schedule (RFC 8446 sec 7.1) for the QUIC handshake.
 *
 * QUIC runs TLS 1.3 as its handshake protocol (RFC 9001), and mbedTLS exposes no QUIC-TLS callback
 * API, so the handshake is hand-rolled here. This module is the key schedule: the chain of
 * HKDF-Extract and Derive-Secret steps (RFC 8446 sec 7.1) that turns the (EC)DHE shared secret and
 * the running handshake transcript hash into the traffic secrets for each encryption level, plus the
 * per-message Finished MAC (sec 4.4.4). It is cipher-suite TLS_AES_128_GCM_SHA256 only, so the hash
 * is SHA-256 throughout and every secret is 32 bytes.
 *
 * The schedule is transcript-hash-driven: each step takes a Transcript-Hash the caller computed over
 * the handshake messages so far, so this module has no dependency on the message wire formats and is
 * host-testable in isolation against the RFC 8448 sec 3 worked trace (which lists every intermediate
 * secret and the (EC)DHE input directly). The QUIC packet-protection keys ({key, iv, hp}) are then
 * derived from these traffic secrets by pc_quic_keys_from_secret() (RFC 9001 sec 5.1).
 *
 * Pure, zero heap, host-tested against RFC 8448 sec 3.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TLS13_KDF_H
#define PROTOCORE_TLS13_KDF_H

#include "protocore_config.h"

// Shared by the HTTP/3 (QUIC) handshake and the DTLS 1.3 handshake - both run the same TLS 1.3 key
// schedule (see pc_tls13_msg.h for the matching guard on the message layer).
#if (PC_ENABLE_HTTP3 || PC_ENABLE_DTLS)

/** @brief SHA-256 secret length; every TLS 1.3 secret here is 32 bytes. */
#define TLS13_SECRET_LEN 32

/**
 * @brief The one thing that differs between the TLS 1.3 and DTLS 1.3 key schedules: the
 * HKDF-Expand-Label prefix ("tls13 " for TLS/QUIC per RFC 8446 sec 7.1, "dtls13" for DTLS 1.3 per
 * RFC 9147 sec 5.9). A caller picks the variant once (@ref TLS13_KDF or @ref DTLS13_KDF) and the
 * key schedule carries it, so no per-call flag is threaded through the derivation steps.
 */
typedef struct
{
    const char *label_prefix;
} Tls13Kdf;

/** @brief TLS 1.3 / QUIC variant ("tls13 " prefix, RFC 8446). */
extern const Tls13Kdf TLS13_KDF;
/** @brief DTLS 1.3 variant ("dtls13" prefix, RFC 9147 sec 5.9). */
extern const Tls13Kdf DTLS13_KDF;

/**
 * @brief The running key-schedule state for one handshake (server side).
 *
 * Filled in three steps as the handshake progresses: pc_tls13_ks_early() (which also binds the @ref
 * Tls13Kdf variant) before any (EC)DHE, pc_tls13_ks_handshake() once ClientHello..ServerHello is hashed
 * and the shared secret is known, and pc_tls13_ks_master() once ClientHello..server Finished is hashed.
 * Each step also derives that level's client and server traffic secrets, from which the record/packet
 * keys are made.
 */
typedef struct
{
    const Tls13Kdf *kdf;                         ///< variant (label prefix) bound by pc_tls13_ks_early()
    uint8_t early_secret[TLS13_SECRET_LEN];      ///< HKDF-Extract(0, PSK|0) - no-PSK: Extract(0, 0^32)
    uint8_t handshake_secret[TLS13_SECRET_LEN];  ///< HKDF-Extract(Derive(early,"derived"), (EC)DHE)
    uint8_t master_secret[TLS13_SECRET_LEN];     ///< HKDF-Extract(Derive(handshake,"derived"), 0^32)
    uint8_t client_hs_traffic[TLS13_SECRET_LEN]; ///< Derive-Secret(handshake, "c hs traffic", CH..SH)
    uint8_t server_hs_traffic[TLS13_SECRET_LEN]; ///< Derive-Secret(handshake, "s hs traffic", CH..SH)
    uint8_t client_ap_traffic[TLS13_SECRET_LEN]; ///< Derive-Secret(master, "c ap traffic", CH..SFIN)
    uint8_t server_ap_traffic[TLS13_SECRET_LEN]; ///< Derive-Secret(master, "s ap traffic", CH..SFIN)
} Tls13KeySchedule;

/**
 * @brief HKDF-Expand-Label under a KDF variant (RFC 8446 sec 7.1 with the @p kdf label prefix).
 *
 * The record-key derivations (key/iv/sn) call this so the label prefix follows the negotiated
 * protocol without the record layer knowing the prefix string.
 */
void pc_tls13_kdf_expand_label(const Tls13Kdf *kdf, const uint8_t secret[TLS13_SECRET_LEN], const char *label,
                               uint8_t *out, size_t out_len);

/**
 * @brief Derive-Secret (RFC 8446 sec 7.1): HKDF-Expand-Label(secret, label, transcript_hash, 32).
 *
 * @param kdf              KDF variant (label prefix).
 * @param secret           A 32-byte PRK / traffic secret.
 * @param label            Short label without the prefix, e.g. "c hs traffic", "derived".
 * @param transcript_hash  Transcript-Hash of the relevant messages (32 bytes; H("") for "derived").
 * @param out              32-byte derived secret.
 */
void pc_tls13_derive_secret(const Tls13Kdf *kdf, const uint8_t secret[TLS13_SECRET_LEN], const char *label,
                            const uint8_t transcript_hash[TLS13_SECRET_LEN], uint8_t out[TLS13_SECRET_LEN]);

/** @brief Step 1: bind the @p kdf variant and compute early_secret = HKDF-Extract(0, 0^32) (no-PSK). */
void pc_tls13_ks_early(const Tls13Kdf *kdf, Tls13KeySchedule *ks);

/**
 * @brief Step 2: handshake_secret and the client/server handshake traffic secrets.
 *
 * handshake_secret = HKDF-Extract(Derive-Secret(early, "derived", H("")), @p ecdhe); the traffic
 * secrets are Derive-Secret(handshake_secret, "c hs traffic"/"s hs traffic", @p ch_sh_hash). The
 * variant bound by pc_tls13_ks_early() is used throughout.
 *
 * @param ecdhe       The (EC)DHE shared secret: 32 bytes for X25519, or the 64-byte concatenation
 *                    ML-KEM_secret || X25519_secret for the X25519MLKEM768 hybrid group.
 * @param ch_sh_hash  Transcript-Hash of ClientHello..ServerHello.
 * @param ecdhe_len   Length of @p ecdhe (32 for X25519, 64 for the hybrid).
 */
void pc_tls13_ks_handshake(Tls13KeySchedule *ks, const uint8_t *ecdhe, const uint8_t ch_sh_hash[TLS13_SECRET_LEN],
                           size_t ecdhe_len = TLS13_SECRET_LEN);

/**
 * @brief Step 3: master_secret and the client/server application traffic secrets.
 *
 * master_secret = HKDF-Extract(Derive-Secret(handshake, "derived", H("")), 0^32); the traffic
 * secrets are Derive-Secret(master_secret, "c ap traffic"/"s ap traffic", @p ch_sfin_hash).
 *
 * @param ch_sfin_hash  Transcript-Hash of ClientHello..server Finished.
 */
void pc_tls13_ks_master(Tls13KeySchedule *ks, const uint8_t ch_sfin_hash[TLS13_SECRET_LEN]);

/**
 * @brief The Finished verify_data (RFC 8446 sec 4.4.4).
 *
 * finished_key = HKDF-Expand-Label(@p base_secret, "finished", "", 32); the output is
 * HMAC-SHA256(finished_key, @p transcript_hash). @p base_secret is the sender's handshake traffic
 * secret (server_hs_traffic for the server's Finished, client_hs_traffic to verify the client's).
 *
 * @param kdf              KDF variant (label prefix).
 * @param base_secret      The Finished sender's handshake traffic secret.
 * @param transcript_hash  Transcript-Hash of the handshake up to but excluding this Finished.
 * @param out              32-byte verify_data.
 */
void pc_tls13_finished_mac(const Tls13Kdf *kdf, const uint8_t base_secret[TLS13_SECRET_LEN],
                           const uint8_t transcript_hash[TLS13_SECRET_LEN], uint8_t out[TLS13_SECRET_LEN]);

#endif // PC_ENABLE_HTTP3
#endif // PROTOCORE_TLS13_KDF_H
