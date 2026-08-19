// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_rsa.h
 * @brief SSH RSA host-key layer: NVS-backed host key, host-key signing, and "ssh-rsa" blob encoding.
 *
 * This is the SSH-specific wrapper around the shared RSA primitive (crypto/rsa): it owns the device's
 * RSA-2048 host key, loaded from NVS, signs handshake data with it, and serializes the public key
 * into the RFC 4253 / RFC 8332 "ssh-rsa" blob. The protocol-agnostic RSASSA-PKCS1-v1.5 math (verify,
 * sign, PKCS#1 encoding, modexp) is ::Rsa in crypto/rsa, whose modular multiply is the arm a part
 * with an RSA accelerator takes; peers' signatures are verified through the same namespace.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * SECURITY MODEL - PRIVATE KEY LIFETIME
 * ═══════════════════════════════════════════════════════════════════════════
 * The RSA-2048 private key MUST NEVER live in static or global memory. The private exponent is a
 * secure-pool borrow (mmgr/secure.h), the DER it was walked out of is released and wiped before the
 * load returns, and ::RsaNs::sign wipes its own temporaries. The signature scheme is PKCS#1 v1.5
 * (RFC 8017 §8.2), "rsa-sha2-256" / "rsa-sha2-512" (RFC 8332) - only the hash and its DigestInfo OID
 * differ.
 *
 * NVS: the private key is a PKCS#8 DER in namespace "ssh_host_key" / key "priv_der"; n, e and d are
 * walked out of it here.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SSH_RSA_H
#define PROTOCORE_SSH_RSA_H

#include "crypto/asymmetric/rsa/rsa.h" // the complete type a public struct below holds by value
#include "protocore_config.h"          // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SSH_RSA

PROTOCORE_BEGIN_DECLS

// PROTOCORE_SSH_RSA_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

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

/** @brief Upper bound on the encoded "ssh-rsa" public-key blob (len+alg + mpint e + mpint n). */
#define SSH_RSA_PUBKEY_BLOB_MAX (4 + 7 + 4 + 1 + 4 + 4 + 1 + 256)

/**
 * @brief RSA-2048 public host key parameters. Allocated in BSS; contains only n and e.
 */
typedef struct
{
    uint8_t n[PROTOCORE_RSA_KEY_BYTES]; ///< Modulus n (256 bytes, big-endian).
    uint8_t e_bytes[4];                 ///< Public exponent e (big-endian uint32).
    proto_bool loaded;                  ///< True after protocore_ssh_rsa_load_pubkey() succeeds.
} SshRsaPubKey;

/** @brief What sign takes: crypto_work, msg, msg_len, hash, sig. */
typedef struct
{
    uint8_t *crypto_work;
    const uint8_t *msg;
    size_t msg_len;
    protocore_rsa_hash hash;
    uint8_t *sig; ///< PROTOCORE_RSA_SIG_BYTES bytes.
} SshRsaSignArgs;

/** @brief What encode_pubkey takes: out, out_len, out_cap. */
typedef struct
{
    uint8_t *out;
    size_t *out_len;
    size_t out_cap;
} SshRsaEncodePubkeyArgs;

/**
 * @brief SSH RSA host-key layer: NVS-backed host key, host-key signing, and "ssh-rsa" blob encoding.
 *
 * A caller sets the members a call takes, invokes it through ::SshRsa with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   SshRsa.load_pubkey(work);
 *   // SshRsa.n is what the call reports
 *
 * @var SshRsaNs::sign_args  what sign takes: crypto_work, msg, msg_len, hash, sig
 * @var SshRsaNs::encode_pubkey_args  what encode_pubkey takes: out, out_len, out_cap
 * @var SshRsaNs::ok  a call's true/false outcome
 * @var SshRsaNs::n  0 on success, -1 if the key is absent or malformed
 * @var SshRsaNs::load_pubkey  load the public portion of the RSA host key into ssh_host_pubkey. ...
 * @var SshRsaNs::sign  sign msg with the RSA host key (PKCS#1 v1.5, rsa-sha2-256/512)
 * @var SshRsaNs::encode_pubkey  encode ssh_host_pubkey as the RFC 4253 §6.6 "ssh-rsa" public-key ...
 *
 * @c work is PROTOCORE_SSH_RSA_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    SshRsaSignArgs sign_args;
    SshRsaEncodePubkeyArgs encode_pubkey_args;

    proto_bool ok;
    int n;

    void (*const load_pubkey)(uint8_t *restrict work);
    void (*const sign)(uint8_t *restrict work);
    void (*const encode_pubkey)(uint8_t *restrict work);
} SshRsaNs;

/** @brief The one symbol this module exports. */
extern SshRsaNs SshRsa;

/**
 * @brief The RSA host key's public half: modulus and exponent, and whether they are loaded.
 *
 * Public by definition, so it is read directly rather than through the namespace; only the private
 * exponent lives in the borrow. The transport reads @c loaded to decide whether an RSA host-key
 * algorithm can be offered at all.
 */
extern SshRsaPubKey ssh_host_pubkey;

/**
 * @brief The PROTOCORE_SSH_RSA_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_ssh_rsa_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSH_RSA

#endif // PROTOCORE_SSH_RSA_H
