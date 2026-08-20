// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ecdsa.h
 * @brief NIST P-256 primitives for SSH: ECDSA signatures and ECDH (RFC 5656 / FIPS 186-4).
 *
 * Backs three P-256 SSH mechanisms, all sharing the one curve:
 *   - ecdsa-sha2-nistp256 host key + client publickey auth (RFC 5656 §3): the server signs
 *     the KEX exchange hash with its P-256 host key and verifies a client's signature.
 *   - ecdh-sha2-nistp256 key exchange (RFC 5656 §4): the P-256 ECDH shared secret.
 * ECDSA always hashes the message with SHA-256 (nistp256 pairs with SHA-256, RFC 5656 §6.2.1).
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * THE TWO ARMS
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * One self-contained software P-256 serves both: 256-bit field and scalar arithmetic, the
 *   exception-free complete addition formulas, a constant-time fixed-window scalar multiply,
 *   and RFC 6979 deterministic signing, so the sign path is byte-exact against the RFC 6979
 *   A.2.5 (P-256/SHA-256) known-answer vectors on every target.
 *
 * Only the field multiply changes arm. A die whose MPI accelerator carries a single-shot
 *   MODMULT does each 256-bit multiply on it; every other target, and a host build, runs the
 *   software product. The vectors are the same either way, which is what makes the native run
 *   a check on the accelerated one.
 *
 * The entries below are one surface over every arm: which one runs the curve math is this
 * module's and is never visible here.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * WIRE FORMATS (assembled by the SSH transport/auth layers, not here)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Public-key blob (RFC 5656 §3.1):
 *   string("ecdsa-sha2-nistp256") || string("nistp256") || string(Q)
 * where Q is the uncompressed point 0x04 || X || Y (65 bytes). This module writes Q
 * through @ref EcdsaNs::pubkey; the layers wrap it.
 *
 * Signature blob (RFC 5656 §3.1.2):
 *   string("ecdsa-sha2-nistp256") || string( mpint(r) || mpint(s) )
 * This module writes the raw r || s (32 + 32 big-endian); the layers mpint-wrap them.
 *
 * ECDH shared secret (RFC 5656 §4):
 *   K = the X coordinate of d * Q_peer. @ref EcdsaNs::ecdh writes the raw 32-byte X;
 *   the transport encodes it as an mpint in the exchange hash and the key derivation.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ECDSA_H
#define PROTOCORE_ECDSA_H

#include "protocore_config.h" // the entry point: protocore_types.h for proto_bool and the widths

#if PROTOCORE_ENABLE_ECDSA

PROTOCORE_BEGIN_DECLS

/** @brief P-256 private key (scalar d) length. */
#define PROTOCORE_ECDSA_P256_PRIV_LEN 32
/** @brief P-256 coordinate length (one of X, Y). */
#define PROTOCORE_ECDSA_P256_COORD_LEN 32
/** @brief P-256 uncompressed public point length: 0x04 || X || Y. */
#define PROTOCORE_ECDSA_P256_PUB_LEN 65
/** @brief Raw ECDSA signature length: r || s (32 + 32, big-endian). */
#define PROTOCORE_ECDSA_P256_SIG_LEN 64

// PROTOCORE_ECDSA_BORROW - the bytes one P-256 operation runs out of - is stated in protocore_config.h,
// which sums it into the secure arena. A caller takes them once and passes the pointer to every call.

/** @brief The private scalar a public point is derived from. */
typedef struct
{
    const uint8_t *priv; ///< PROTOCORE_ECDSA_P256_PRIV_LEN big-endian scalar d, 1 <= d < n
    uint8_t *pub;        ///< PROTOCORE_ECDSA_P256_PUB_LEN bytes: 0x04 || X || Y
} EcdsaPubkeyArgs;
/** @brief The message and key a signature is taken over. */
typedef struct
{
    const uint8_t *msg;  ///< the message, hashed with SHA-256 here
    size_t mlen;         ///< its length
    const uint8_t *priv; ///< PROTOCORE_ECDSA_P256_PRIV_LEN big-endian scalar d
    uint8_t *sig;        ///< PROTOCORE_ECDSA_P256_SIG_LEN bytes: r || s, 32 + 32 big-endian
} EcdsaSignArgs;
/** @brief The message, key and signature a verification checks. */
typedef struct
{
    const uint8_t *pub; ///< PROTOCORE_ECDSA_P256_PUB_LEN uncompressed point, rejected if not on-curve
    const uint8_t *msg; ///< the signed message
    size_t mlen;        ///< its length
    const uint8_t *sig; ///< PROTOCORE_ECDSA_P256_SIG_LEN bytes: r || s, 32 + 32 big-endian
} EcdsaVerifyArgs;
/** @brief The peer point and key an ECDH shared secret is taken from. */
typedef struct
{
    const uint8_t *peer_pub; ///< PROTOCORE_ECDSA_P256_PUB_LEN uncompressed peer point 0x04 || X || Y
    const uint8_t *priv;     ///< PROTOCORE_ECDSA_P256_PRIV_LEN big-endian scalar d, 1 <= d < n
    uint8_t *shared_x;       ///< PROTOCORE_ECDSA_P256_COORD_LEN big-endian X coordinate of d * Q_peer
} EcdsaEcdhArgs;
/**
 * @brief NIST P-256 ECDSA and ECDH (RFC 5656 / FIPS 186-4).
 *
 * A caller sets the members a call takes, invokes it through ::Ecdsa with the bytes it runs out of, and
 * reads the outcome off the same handle. How those bytes are carved is this module's and is never named
 * here.
 *
 *   Ecdsa.sign_args.msg = exchange_hash;
 *   Ecdsa.sign_args.mlen = 32;
 *   Ecdsa.sign_args.priv = host_key;
 *   Ecdsa.sign_args.sig = sig;
 *   Ecdsa.sign(work);
 *
 * @var EcdsaNs::pubkey_args  the private scalar a public point is derived from
 * @var EcdsaNs::sign_args    the message and key a signature is taken over
 * @var EcdsaNs::verify_args  the message, key and signature a verification checks
 * @var EcdsaNs::ecdh_args    the peer point and key an ECDH shared secret is taken from
 * @var EcdsaNs::ok           a call's true/false outcome; false on a null pointer, an out-of-range
 *                            scalar, an off-curve point, an identity result, and on a bad signature
 * @var EcdsaNs::pubkey       derive Q = d*G and write it uncompressed
 * @var EcdsaNs::sign         hash the message with SHA-256 and write the raw r || s
 * @var EcdsaNs::verify       hash the message with SHA-256 and check r || s against the point
 * @var EcdsaNs::ecdh         write the X coordinate of d * Q_peer
 *
 * @c work is PROTOCORE_ECDSA_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and
 * the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it. That is
 * what keeps the message hash and the RFC 6979 nonce chain from outliving the caller.
 *
 * No storage member and no context: a caller sets operands and reads @ref EcdsaNs::ok, and that is all
 * the surface there is.
 */
typedef struct
{
    EcdsaPubkeyArgs pubkey_args;
    EcdsaSignArgs sign_args;
    EcdsaVerifyArgs verify_args;
    EcdsaEcdhArgs ecdh_args;
    proto_bool ok;
} EcdsaVars;

/** @brief The operands and the outcome. */
extern EcdsaVars EcdsaV;

/** @brief The entries. */
typedef struct
{
    void (*const pubkey)(uint8_t *restrict work);
    void (*const sign)(uint8_t *restrict work);
    void (*const verify)(uint8_t *restrict work);
    void (*const ecdh)(uint8_t *restrict work);
} EcdsaNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in EcdsaV or a region of the borrow at a fixed offset.
void protocore_ecdsa_pubkey(uint8_t *restrict work);
void protocore_ecdsa_sign(uint8_t *restrict work);
void protocore_ecdsa_verify(uint8_t *restrict work);
void protocore_ecdsa_ecdh(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Ecdsa.pubkey(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const EcdsaNs Ecdsa __attribute__((unused)) = {
    .pubkey = protocore_ecdsa_pubkey,
    .sign = protocore_ecdsa_sign,
    .verify = protocore_ecdsa_verify,
    .ecdh = protocore_ecdsa_ecdh,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ECDSA

#endif // PROTOCORE_ECDSA_H
