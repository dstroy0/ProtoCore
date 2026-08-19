// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ed25519.h
 * @brief Ed25519 signatures (RFC 8032) for ssh-ed25519 host keys + client auth.
 *
 * PureEdDSA over edwards25519. Deterministic signing (RFC 8032 §5.1.6) - no RNG - and
 * verification, built on the shared Curve25519 field arithmetic (protocore_curve25519) and the
 * @ref Sha512Ns entries, so which arm hashes is not visible here. Correctness is pinned to the
 * RFC 8032 §7.1 vectors and to a reference implementation (test_ed25519).
 *
 * The server signs the KEX exchange hash with its ssh-ed25519 host key, and verifies a
 * client's ed25519 public-key authentication signature.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ED25519_H
#define PROTOCORE_ED25519_H

#include "protocore_config.h" // the entry point: protocore_types.h for proto_bool and the widths

#if PROTOCORE_ENABLE_ED25519

PROTOCORE_BEGIN_DECLS

/** @brief Ed25519 seed (private key) length. */
#define PROTOCORE_ED25519_SEED_LEN 32
/** @brief Ed25519 public key length. */
#define PROTOCORE_ED25519_PUBKEY_LEN 32
/** @brief Ed25519 signature length (R || S). */
#define PROTOCORE_ED25519_SIG_LEN 64

// PROTOCORE_ED25519_BORROW - the bytes one signature operation runs out of - is stated in
// protocore_config.h, which sums it into the secure arena. A caller takes them once and passes the
// pointer to every call.

/** @brief The seed a public key is derived from. */
typedef struct
{
    const uint8_t *seed; ///< PROTOCORE_ED25519_SEED_LEN bytes
    uint8_t *pub;        ///< PROTOCORE_ED25519_PUBKEY_LEN bytes
} Ed25519PubkeyArgs;

/** @brief The message a signature covers. */
typedef struct
{
    const uint8_t *seed; ///< PROTOCORE_ED25519_SEED_LEN bytes
    const uint8_t *msg;  ///< the message
    size_t msg_len;      ///< its length
    uint8_t *sig;        ///< PROTOCORE_ED25519_SIG_LEN bytes, R || S
} Ed25519SignArgs;

/** @brief The signature a verification checks. */
typedef struct
{
    const uint8_t *pub; ///< PROTOCORE_ED25519_PUBKEY_LEN bytes
    const uint8_t *msg; ///< the message
    size_t msg_len;     ///< its length
    const uint8_t *sig; ///< PROTOCORE_ED25519_SIG_LEN bytes, R || S
} Ed25519VerifyArgs;

/**
 * @brief Ed25519 (RFC 8032).
 *
 * A caller sets the members a call takes, invokes it through ::Ed25519 with the bytes it runs out of,
 * and reads the outcome off the same handle. How those bytes are carved is this module's and is never
 * named here.
 *
 *   Ed25519.sign_args.seed = seed;
 *   Ed25519.sign_args.msg = exchange_hash;
 *   Ed25519.sign_args.msg_len = h_len;
 *   Ed25519.sign_args.sig = sig;
 *   Ed25519.sign(work);
 *
 * @var Ed25519Ns::pubkey_args  the seed a public key is derived from
 * @var Ed25519Ns::sign_args    the message a signature covers
 * @var Ed25519Ns::verify_args  the signature a verification checks
 * @var Ed25519Ns::ok           a call's true/false outcome; false on a null pointer, on a public key
 *                              that does not decode to a curve point, on a non-canonical S, and on a
 *                              signature that does not match
 * @var Ed25519Ns::pubkey       derive the 32-byte public key A from the seed
 * @var Ed25519Ns::sign         sign deterministically (RFC 8032 §5.1.6), writing R || S
 * @var Ed25519Ns::verify       check a signature (RFC 8032 §5.1.7); the answer is @ref Ed25519Ns::ok
 *
 * @c work is PROTOCORE_ED25519_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and
 * the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it. That
 * is what keeps the expanded seed, the nonce and the challenge from outliving the caller.
 *
 * No storage member and no context: a caller sets operands and reads @ref Ed25519Ns::ok, and that is
 * all the surface there is.
 */
typedef struct
{
    Ed25519PubkeyArgs pubkey_args;
    Ed25519SignArgs sign_args;
    Ed25519VerifyArgs verify_args;

    proto_bool ok;

    void (*const pubkey)(uint8_t *restrict work);
    void (*const sign)(uint8_t *restrict work);
    void (*const verify)(uint8_t *restrict work);
} Ed25519Ns;

/** @brief The one symbol this module exports. */
extern Ed25519Ns Ed25519;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ED25519

#endif // PROTOCORE_ED25519_H
