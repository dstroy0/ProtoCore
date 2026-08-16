// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
 * Both entries take raw big-endian key material: verify takes n and the four-byte public exponent,
 * sign takes n and the full-width private exponent d. One modular multiply underneath them has an
 * accelerated arm and a software arm; everything else is the same code on every part.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_RSA_H
#define PROTOCORE_RSA_H

#include "protocore_config.h" // the entry point: PROTO_ENUM_PACKED, and protocore_types.h for the widths

#if PROTOCORE_ENABLE_RSA

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

// PROTOCORE_RSA_BORROW - the bytes one signature operation runs out of - is stated in
// protocore_config.h, which sums it into the secure arena. A caller takes them once and passes the
// pointer to every call.

/** @brief The key, message and signature a verify checks. */
typedef struct
{
    const uint8_t *n;        ///< modulus n, PROTOCORE_RSA_KEY_BYTES big-endian
    const uint8_t *e;        ///< public exponent e, 4 bytes big-endian (typically 65537)
    const uint8_t *msg;      ///< the message that was signed; this hashes it, do not pre-hash
    size_t msg_len;          ///< its length
    const uint8_t *sig;      ///< the signature, big-endian
    size_t sig_len;          ///< its length; must equal PROTOCORE_RSA_KEY_BYTES
    protocore_rsa_hash hash; ///< digest algorithm (SHA-256 / SHA-512)
} RsaVerifyArgs;

/** @brief The key and message a sign covers. */
typedef struct
{
    const uint8_t *n;        ///< modulus n, PROTOCORE_RSA_KEY_BYTES big-endian
    const uint8_t *d;        ///< private exponent d, PROTOCORE_RSA_KEY_BYTES big-endian
    const uint8_t *msg;      ///< the message to sign; this hashes it
    size_t msg_len;          ///< its length
    protocore_rsa_hash hash; ///< digest algorithm (SHA-256 / SHA-512)
    uint8_t *sig;            ///< PROTOCORE_RSA_SIG_BYTES big-endian signature bytes
} RsaSignArgs;

/**
 * @brief RSASSA-PKCS1-v1.5 over RSA-2048 (RFC 8017 §8.2).
 *
 * A caller sets the members a call takes, invokes it through ::Rsa with the bytes it runs out of, and
 * reads the outcome off the same handle. How those bytes are carved is this module's and is never
 * named here.
 *
 *   Rsa.verify_args.n = n_be;
 *   Rsa.verify_args.e = e_be4;
 *   Rsa.verify_args.msg = msg;
 *   Rsa.verify_args.msg_len = msg_len;
 *   Rsa.verify_args.sig = sig;
 *   Rsa.verify_args.sig_len = sig_len;
 *   Rsa.verify_args.hash = PROTOCORE_RSA_HASH_SHA256;
 *   Rsa.verify(work);
 *   // Rsa.ok is true only for a signature that verified
 *
 * @var RsaNs::verify_args  the key, message and signature a verify checks
 * @var RsaNs::sign_args    the key and message a sign covers
 * @var RsaNs::ok           a call's true/false outcome; false on a null pointer, a signature that is
 *                          not PROTOCORE_RSA_KEY_BYTES long, a representative that is not below n, and
 *                          a block that does not match
 * @var RsaNs::verify       hash the message, recover the signature block, compare the two in constant
 *                          time
 * @var RsaNs::sign         hash the message, PKCS#1 v1.5 encode it, raise it to d mod n; NOT
 *                          constant-time - see SECURITY.md, timing
 *
 * @c work is PROTOCORE_RSA_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and
 * the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it. That is
 * what keeps the private exponent and the encoded block a sign works in from outliving the caller. The
 * digest each entry takes runs out of those bytes too, so a verify costs one borrow and no wipe.
 *
 * No storage member and no context: a caller sets operands and reads @ref RsaNs::ok, and that is all
 * the surface there is.
 */
typedef struct
{
    RsaVerifyArgs verify_args;
    RsaSignArgs sign_args;

    proto_bool ok;

    void (*const verify)(uint8_t *restrict work);
    void (*const sign)(uint8_t *restrict work);
} RsaNs;

/** @brief The one symbol this module exports. */
extern RsaNs Rsa;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_RSA

#endif // PROTOCORE_RSA_H
