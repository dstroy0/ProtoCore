// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_snmp_crypto.h
 * @brief USM cryptographic primitives for SNMPv3 (PC_ENABLE_SNMP_V3).
 *
 * Provides exactly what the User-based Security Model needs and nothing more:
 *
 *  - **Key localization** (RFC 3414 §2.6, SHA-256 variant per RFC 7860): turn a
 *    human password into the engine-localized authentication/privacy key.
 *  - **AES-128 in CFB-128 mode** (RFC 3826): the v3 privacy transform. A compact
 *    portable software AES is used on both host and ESP32 - SNMP payloads are a
 *    few hundred bytes, so this is not a throughput path; it is therefore
 *    identical and unit-testable off-target. (Like the SSH software crypto, it is
 *    not constant-time; for SNMP this is acceptable - see SECURITY.md.)
 *
 * SHA-256 and HMAC-SHA-256 are reused from the existing SSH crypto layer.
 */

#ifndef PROTOCORE_SNMP_CRYPTO_H
#define PROTOCORE_SNMP_CRYPTO_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_SNMP_V3

/** @brief Localized-key length (SHA-256 digest size). */
#define SNMP_USM_KEY_LEN 32

/**
 * @brief Derive the engine-localized USM key from a password (RFC 3414 §2.6).
 *
 * Expands @p password to 2^20 bytes and hashes it with SHA-256 to form the
 * user key Ku, then computes the localized key Kul = SHA-256(Ku || engineID ||
 * Ku). The same procedure produces both the authentication key and the privacy
 * key (each from its own password). The privacy transform uses the first 16
 * bytes of the result as the AES-128 key.
 *
 * @param password       NUL-terminated auth/priv password (>= 8 chars per RFC 3414).
 * @param engine_id      authoritative engine ID bytes.
 * @param engine_id_len  length of @p engine_id.
 * @param key_out        receives SNMP_USM_KEY_LEN localized key bytes.
 */
void pc_snmp_usm_localize_key(const char *password, const uint8_t *engine_id, size_t engine_id_len,
                              uint8_t key_out[SNMP_USM_KEY_LEN]);

/**
 * @brief AES-128 CFB-128 transform (RFC 3826), used for SNMPv3 privacy.
 *
 * CFB is a stream mode: the output length equals the input length (no padding),
 * and decryption is the same construction with the feedback taken from the
 * ciphertext. Safe for in-place use (@p out may equal @p in).
 *
 * @param key      16-byte AES-128 key (first 16 bytes of the localized priv key).
 * @param iv       16-byte IV = engineBoots(4, big-endian) || engineTime(4) || privParams(8).
 * @param in       input bytes.
 * @param out      output bytes (length @p len).
 * @param len      number of bytes.
 * @param encrypt  true to encrypt, false to decrypt.
 */
void pc_snmp_aes128_cfb(const uint8_t key[16], const uint8_t iv[16], const uint8_t *in, uint8_t *out, size_t len,
                        bool encrypt);

#endif // PC_ENABLE_SNMP_V3

#endif // PROTOCORE_SNMP_CRYPTO_H
