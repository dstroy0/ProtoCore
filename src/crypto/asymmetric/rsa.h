// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rsa.h
 * @brief RSA-2048 PKCS#1 v1.5 signature primitive (RFC 8017) - verify + software sign.
 *
 * The shared, protocol-agnostic RSA primitive: it takes raw big-endian key material (modulus n,
 * exponent) and a message, and does the RSASSA-PKCS1-v1.5 math with a SHA-256 or SHA-512 digest. It
 * knows nothing about SSH key blobs, host-key storage, or the "ssh-rsa" / "rsa-sha2-256" wire names -
 * that layer lives in network_drivers/tls/ssh_rsa and calls into this primitive.
 *
 * Verify runs on both platforms (mbedtls on Arduino/ESP32, software on native). The software sign
 * (pc_rsa_sign_sw, raw n/d) is the native-only reference path used by the tests; on-device signing
 * with a cached host-key context is the SSH layer's job.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_RSA_H
#define PROTOCORE_RSA_H

#include "protocore_config.h" // the entry point: PROTO_ENUM_PACKED, and types.h for the widths

/** @brief RSA modulus / signature size in bytes (RSA-2048). */
#define PC_RSA_KEY_BYTES 256

/** @brief PKCS#1 v1.5 signature size for RSA-2048 in bytes. */
#define PC_RSA_SIG_BYTES 256

/**
 * @brief Hash algorithm selecting the RSA signature scheme (RFC 8017 §9.2).
 *
 * Only the message hash and its DigestInfo OID differ between the two.
 */
typedef enum PROTO_ENUM_PACKED
{
    PC_RSA_HASH_SHA256 = 0, ///< RSASSA-PKCS1-v1.5 with SHA-256
    PC_RSA_HASH_SHA512 = 1  ///< RSASSA-PKCS1-v1.5 with SHA-512
} pc_rsa_hash;

/** @brief Length of the DER DigestInfo wrapper for SHA-256 (RFC 8017 / RFC 5754). */
#define PC_PKCS1_DIGESTINFO_LEN 19

/** @brief Length of the DER DigestInfo wrapper for SHA-512. */
#define PC_PKCS1_SHA512_DIGESTINFO_LEN 19

/** @brief The DER-encoded DigestInfo wrapper for SHA-256 (prepend to the 32-byte digest). */
extern const uint8_t pc_pkcs1_sha256_digestinfo[PC_PKCS1_DIGESTINFO_LEN];

/** @brief The DER-encoded DigestInfo wrapper for SHA-512 (prepend to the 64-byte digest). */
extern const uint8_t pc_pkcs1_sha512_digestinfo[PC_PKCS1_SHA512_DIGESTINFO_LEN];

/**
 * @brief Verify an RSA-2048 PKCS#1 v1.5 signature over @p msg.
 *
 * @param n_be     Modulus n, 256 bytes big-endian.
 * @param e_be4    Public exponent e, 4 bytes big-endian (typically 65537).
 * @param msg      Message that was signed (this hashes it; do not pre-hash).
 * @param msg_len  Message length.
 * @param sig      Signature, big-endian.
 * @param sig_len  Signature length (must equal PC_RSA_KEY_BYTES).
 * @param hash     Digest algorithm (SHA-256 / SHA-512).
 * @return 0 if the signature is valid, -1 otherwise.
 */
int pc_rsa_verify(const uint8_t n_be[PC_RSA_KEY_BYTES], const uint8_t e_be4[4], const uint8_t *msg, size_t msg_len,
                  const uint8_t *sig, size_t sig_len, pc_rsa_hash hash);

#if !PROTOCORE_HOT
/**
 * @brief Software RSA-2048 PKCS#1 v1.5 sign with a raw private key (native reference path).
 *
 * Full-width square-and-multiply modexp (s = pkcs1(H(msg))^d mod n). NOT constant-time - the native
 * path is test-only; on-device signing uses the SSH layer's cached mbedtls host-key context.
 *
 * @param n_be     Modulus n, 256 bytes big-endian.
 * @param d_be     Private exponent d, 256 bytes big-endian (SENSITIVE; caller wipes).
 * @param msg      Message to sign (this hashes it).
 * @param msg_len  Message length.
 * @param hash     Digest algorithm (SHA-256 / SHA-512).
 * @param sig      Output signature, PC_RSA_SIG_BYTES big-endian.
 * @return 0 on success.
 */
int pc_rsa_sign_sw(const uint8_t n_be[PC_RSA_KEY_BYTES], const uint8_t d_be[PC_RSA_KEY_BYTES], const uint8_t *msg,
                   size_t msg_len, pc_rsa_hash hash, uint8_t sig[PC_RSA_SIG_BYTES]);
#endif

#endif // PROTOCORE_RSA_H
