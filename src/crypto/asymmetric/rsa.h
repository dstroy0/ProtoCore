// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rsa.h
 * @brief RSA-2048 PKCS#1 v1.5 signature primitive (RFC 8017) - verify + software sign.
 *
 * The shared, protocol-agnostic RSA primitive: it takes raw big-endian key material (modulus n,
 * exponent) and a message, and does the RSASSA-PKCS1-v1.5 math with a SHA-256 or SHA-512 digest. It
 * knows nothing about SSH key blobs, host-key storage, or the "ssh-rsa" / "rsa-sha2-256" wire names -
 * that layer lives in network_drivers/presentation/ssh/transport/ssh_rsa and calls into this primitive.
 *
 * Verify runs on both platforms (mbedtls on Arduino/ESP32, software on native). The software sign
 * (protocore_rsa_sign_sw, raw n/d) is the native-only reference path used by the tests; on-device signing
 * with a cached host-key context is the SSH layer's job.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_RSA_H
#define PROTOCORE_RSA_H

#include "protocore_config.h" // the entry point: PROTO_ENUM_PACKED, and protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/** @brief RSA modulus / signature size in bytes (RSA-2048). */
#define PROTOCORE_RSA_KEY_BYTES 256

/** @brief PKCS#1 v1.5 signature size for RSA-2048 in bytes. */
#define PROTOCORE_RSA_SIG_BYTES 256

/**
 * @brief Hash algorithm selecting the RSA signature scheme (RFC 8017 §9.2).
 *
 * Only the message hash and its DigestInfo OID differ between the two.
 */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_RSA_HASH_SHA256 = 0, ///< RSASSA-PKCS1-v1.5 with SHA-256
    PROTOCORE_RSA_HASH_SHA512 = 1  ///< RSASSA-PKCS1-v1.5 with SHA-512
} protocore_rsa_hash;

/** @brief Length of the DER DigestInfo wrapper for SHA-256 (RFC 8017 / RFC 5754). */
#define PROTOCORE_PKCS1_DIGESTINFO_LEN 19

/** @brief Length of the DER DigestInfo wrapper for SHA-512. */
#define PROTOCORE_PKCS1_SHA512_DIGESTINFO_LEN 19

/** @brief The DER-encoded DigestInfo wrapper for SHA-256 (prepend to the 32-byte digest). */
extern const uint8_t protocore_pkcs1_sha256_digestinfo[PROTOCORE_PKCS1_DIGESTINFO_LEN];

/** @brief The DER-encoded DigestInfo wrapper for SHA-512 (prepend to the 64-byte digest). */
extern const uint8_t protocore_pkcs1_sha512_digestinfo[PROTOCORE_PKCS1_SHA512_DIGESTINFO_LEN];

/**
 * @brief Verify an RSA-2048 PKCS#1 v1.5 signature over @p msg.
 *
 * @param n_be     Modulus n, 256 bytes big-endian.
 * @param e_be4    Public exponent e, 4 bytes big-endian (typically 65537).
 * @param work     PROTOCORE_SHA256_BORROW bytes of caller storage, for the message digest.
 * @param msg      Message that was signed (this hashes it; do not pre-hash).
 * @param msg_len  Message length.
 * @param sig      Signature, big-endian.
 * @param sig_len  Signature length (must equal PROTOCORE_RSA_KEY_BYTES).
 * @param hash     Digest algorithm (SHA-256 / SHA-512).
 * @return 0 if the signature is valid, -1 otherwise.
 */
int protocore_rsa_verify(const uint8_t n_be[PROTOCORE_RSA_KEY_BYTES], const uint8_t e_be4[4], uint8_t *work,
                         const uint8_t *msg, size_t msg_len, const uint8_t *sig, size_t sig_len,
                         protocore_rsa_hash hash);

#if !PROTOCORE_HAS_HW_BIGNUM
/**
 * @brief Software RSA-2048 PKCS#1 v1.5 sign with a raw private key (SW path).
 *
 * Full-width square-and-multiply modexp (s = pkcs1(H(msg))^d mod n). NOT constant-time - the native
 * on the HW path, signing uses the SSH layer's cached mbedtls host-key context.
 *
 * @param n_be     Modulus n, 256 bytes big-endian.
 * @param d_be     Private exponent d, 256 bytes big-endian (SENSITIVE; caller wipes).
 * @param work     PROTOCORE_SHA256_BORROW bytes of caller storage, for the message digest.
 * @param msg      Message to sign (this hashes it).
 * @param msg_len  Message length.
 * @param hash     Digest algorithm (SHA-256 / SHA-512).
 * @param sig      Output signature, PROTOCORE_RSA_SIG_BYTES big-endian.
 * @return 0 on success.
 */
int protocore_rsa_sign_sw(const uint8_t n_be[PROTOCORE_RSA_KEY_BYTES], const uint8_t d_be[PROTOCORE_RSA_KEY_BYTES],
                          uint8_t *work, const uint8_t *msg, size_t msg_len, protocore_rsa_hash hash,
                          uint8_t sig[PROTOCORE_RSA_SIG_BYTES]);
#endif

PROTOCORE_END_DECLS

#endif // PROTOCORE_RSA_H
