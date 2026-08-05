// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_rsa.h
 * @brief SSH RSA host-key layer: NVS-backed host key, host-key signing, and "ssh-rsa" blob encoding.
 *
 * This is the SSH-specific wrapper around the shared RSA primitive (crypto/rsa): it owns the device's
 * RSA-2048 host key (loaded from NVS on Arduino, a test fixture on native), signs handshake data with
 * it, and serializes the public key into the RFC 4253 / RFC 8332 "ssh-rsa" blob. The
 * protocol-agnostic RSASSA-PKCS1-v1.5 math (verify, software sign, PKCS#1 encoding, modexp) lives in
 * crypto/rsa (pc_rsa_verify / pc_rsa_sign_sw); peers' signatures are verified via that primitive.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * SECURITY MODEL - PRIVATE KEY LIFETIME
 * ═══════════════════════════════════════════════════════════════════════════
 * The RSA-2048 private key MUST NEVER live in static or global memory. On Arduino the parsed key is
 * held in a private mbedtls context for the server lifetime (as an SSH host key normally is) and every
 * sign runs under a mutex; on native the test fixture injects n/d/e and the software sign
 * (pc_rsa_sign_sw) wipes its own temporaries. The signature scheme is PKCS#1 v1.5 (RFC 8017 §8.2),
 * "rsa-sha2-256" / "rsa-sha2-512" (RFC 8332) - only the hash and its DigestInfo OID differ.
 *
 * NVS: on Arduino the private key is a DER PKCS#1 RSAPrivateKey in namespace "ssh_host_key" / key
 * "priv_der", parsed by mbedtls. On native, the test fixture injects n, e, d as raw 256-byte arrays.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SSH_RSA_H
#define PROTOCORE_SSH_RSA_H

#include "crypto/asymmetric/rsa.h" // pc_rsa_hash, PC_RSA_KEY_BYTES/SIG_BYTES, pc_rsa_verify / pc_rsa_sign_sw

PROTO_BEGIN_DECLS

/** @brief Maximum DER size for a PKCS#1 RSAPrivateKey with 2048-bit fields. */
#define SSH_RSA_KEY_DER_MAX 1700

/**
 * @brief Key-blob type string for an RSA host key.
 *
 * Per RFC 8332 §3, the RSA *public-key blob* always carries the type string "ssh-rsa" - even when the
 * negotiated *signature* algorithm is "rsa-sha2-256" / "rsa-sha2-512". Only the signature and
 * authentication algorithm-name fields use "rsa-sha2-256"; the key blob format is unchanged from
 * RFC 4253 §6.6.
 */
#define SSH_RSA_PUBKEY_ALG "ssh-rsa"

/** @brief Length of SSH_RSA_PUBKEY_ALG ("ssh-rsa" = 7 bytes). */
#define SSH_RSA_PUBKEY_ALG_LEN 7

/** @brief Signature algorithm name for SHA-256 (RFC 8332). Used in the signature blob. */
#define SSH_RSA_SIG_ALG_SHA256 "rsa-sha2-256"

/** @brief Signature algorithm name for SHA-512 (RFC 8332). Used in the signature blob. */
#define SSH_RSA_SIG_ALG_SHA512 "rsa-sha2-512"

// ---------------------------------------------------------------------------
// RSA public key (safe to keep in static/flash - no secret material)
// ---------------------------------------------------------------------------

/**
 * @brief RSA-2048 public host key parameters. Allocated in BSS; contains only n and e.
 */
typedef struct
{
    uint8_t n[PC_RSA_KEY_BYTES]; ///< Modulus n (256 bytes, big-endian).
    uint8_t e_bytes[4];          ///< Public exponent e (big-endian uint32).
    proto_bool loaded;           ///< True after pc_ssh_rsa_load_pubkey() succeeds.
} SshRsaPubKey;

/** @brief Static host public key (BSS). Set by pc_ssh_rsa_load_pubkey(). */
extern SshRsaPubKey ssh_host_pubkey;

/** @brief Upper bound on the encoded "ssh-rsa" public-key blob (len+alg + mpint e + mpint n). */
#define SSH_RSA_PUBKEY_BLOB_MAX (4 + 7 + 4 + 1 + 4 + 4 + 1 + 256)

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
 * @brief Load the public portion of the RSA host key into ssh_host_pubkey.
 *
 * Arduino: reads + parses the DER blob from NVS ("ssh_host_key"/"priv_der") and caches the signer
 * context. Native: reads n/e from the test fixture. Call once at startup (single-threaded).
 * @return 0 on success, -1 if the key is absent or malformed.
 */
int pc_ssh_rsa_load_pubkey(void);

/**
 * @brief Sign @p msg with the RSA host key (PKCS#1 v1.5, rsa-sha2-256/512).
 * @return 0 on success, -1 on failure. @p sig receives PC_RSA_SIG_BYTES big-endian.
 */
int ssh_rsa_sign(const uint8_t *msg, size_t msg_len, pc_rsa_hash hash, uint8_t sig[PC_RSA_SIG_BYTES]);

/**
 * @brief Encode ssh_host_pubkey as the RFC 4253 §6.6 "ssh-rsa" public-key blob.
 * @return 0 on success (writing @p out_len), -1 if the key is not loaded or @p out_cap is too small.
 */
int ssh_rsa_encode_pubkey(uint8_t *out, size_t *out_len, size_t out_cap);

PROTO_END_DECLS

#endif // PROTOCORE_SSH_RSA_H
