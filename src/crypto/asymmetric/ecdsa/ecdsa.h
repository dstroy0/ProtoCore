// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_ECDSA_H
#define PROTOCORE_ECDSA_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file ecdsa.h
 * @brief NIST P-256 primitives for SSH: ECDSA signatures and ECDH (RFC 5656 / FIPS 186-4).
 *
 * Backs three P-256 SSH mechanisms, all sharing the one curve:
 * - ecdsa-sha2-nistp256 host key + client publickey auth (RFC 5656 §3): the server signs
 * the KEX exchange hash with its P-256 host key and verifies a client's signature.
 * - ecdh-sha2-nistp256 key exchange (RFC 5656 §4): the P-256 ECDH shared secret.
 * ECDSA always hashes the message with SHA-256 (nistp256 pairs with SHA-256, RFC 5656 §6.2.1).
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * THE TWO ARMS
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * One self-contained software P-256 serves both: 256-bit field and scalar arithmetic, the
 * exception-free complete addition formulas, a constant-time fixed-window scalar multiply,
 * and RFC 6979 deterministic signing, so the sign path is byte-exact against the RFC 6979
 * A.2.5 (P-256/SHA-256) known-answer vectors on every target.
 *
 * Only the field multiply changes arm. A die whose MPI accelerator carries a single-shot
 * MODMULT does each 256-bit multiply on it; every other target, and a host build, runs the
 * software product. The vectors are the same either way, which is what makes the native run
 * a check on the accelerated one.
 *
 * The entries below are one surface over every arm: which one runs the curve math is this
 * module's and is never visible here.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * WIRE FORMATS (assembled by the SSH transport/auth layers, not here)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Public-key blob (RFC 5656 §3.1):
 * string("ecdsa-sha2-nistp256") || string("nistp256") || string(Q)
 * where Q is the uncompressed point 0x04 || X || Y (65 bytes). This module writes Q
 * through @ref EcdsaNs::pubkey; the layers wrap it.
 *
 * Signature blob (RFC 5656 §3.1.2):
 * string("ecdsa-sha2-nistp256") || string( mpint(r) || mpint(s) )
 * This module writes the raw r || s (32 + 32 big-endian); the layers mpint-wrap them.
 *
 * ECDH shared secret (RFC 5656 §4):
 * K = the X coordinate of d * Q_peer. @ref EcdsaNs::ecdh writes the raw 32-byte X;
 * the transport encodes it as an mpint in the exchange hash and the key derivation.
 *
 * @c work is PROTOCORE_ECDSA_BORROW secure bytes the CALLER took, at an address it knows. It is not held past the call,
 * so nothing here aliases it. The caller releases it, and the pool wipes on release; this module neither takes it,
 * holds it, releases it, nor wipes it. That is what keeps the message hash and the RFC 6979 nonce chain from outliving
 * the caller.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

/** @brief P-256 private key (scalar d) length. */
#define PROTOCORE_ECDSA_P256_PRIV_LEN 32

/** @brief P-256 coordinate length (one of X, Y). */
#define PROTOCORE_ECDSA_P256_COORD_LEN 32

/** @brief P-256 uncompressed public point length: 0x04 || X || Y. */
#define PROTOCORE_ECDSA_P256_PUB_LEN 65

/** @brief Raw ECDSA signature length: r || s (32 + 32, big-endian). */
#define PROTOCORE_ECDSA_P256_SIG_LEN 64

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*pubkey)(uint8_t *, const uint8_t *, uint8_t *);
    proto_bool (*sign)(uint8_t *, const uint8_t *, size_t, const uint8_t *, uint8_t *);
    proto_bool (*verify)(uint8_t *, const uint8_t *, const uint8_t *, size_t, const uint8_t *);
    proto_bool (*ecdh)(uint8_t *, const uint8_t *, const uint8_t *, uint8_t *);
} EcdsaNs;
PROTOCORE_NS_LAYOUT(EcdsaNs, pubkey, sign, verify, ecdh);

/**
 * @brief Derive Q = d*G and write it uncompressed.
 * @param work PROTOCORE_ECDSA_BORROW bytes the caller took. Not held past the call.
 * @param priv PROTOCORE_ECDSA_P256_PRIV_LEN big-endian scalar d, 1 <= d < n
 * @param pub PROTOCORE_ECDSA_P256_PUB_LEN bytes: 0x04 || X || Y
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_ecdsa_pubkey(uint8_t *work, const uint8_t *priv, uint8_t *pub);
/**
 * @brief Hash the message with SHA-256 and write the raw r || s.
 * @param work PROTOCORE_ECDSA_BORROW bytes the caller took. Not held past the call.
 * @param msg the message, hashed with SHA-256 here
 * @param mlen its length
 * @param priv PROTOCORE_ECDSA_P256_PRIV_LEN big-endian scalar d
 * @param sig PROTOCORE_ECDSA_P256_SIG_LEN bytes: r || s, 32 + 32 big-endian
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_ecdsa_sign(uint8_t *work, const uint8_t *msg, size_t mlen, const uint8_t *priv, uint8_t *sig);
/**
 * @brief Hash the message with SHA-256 and check r || s against the point.
 * @param work PROTOCORE_ECDSA_BORROW bytes the caller took. Not held past the call.
 * @param pub PROTOCORE_ECDSA_P256_PUB_LEN uncompressed point, rejected if not on-curve
 * @param msg the signed message
 * @param mlen its length
 * @param sig PROTOCORE_ECDSA_P256_SIG_LEN bytes: r || s, 32 + 32 big-endian
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_ecdsa_verify(uint8_t *work, const uint8_t *pub, const uint8_t *msg, size_t mlen,
                                  const uint8_t *sig);
/**
 * @brief Write the X coordinate of d * Q_peer.
 * @param work PROTOCORE_ECDSA_BORROW bytes the caller took. Not held past the call.
 * @param peer_pub PROTOCORE_ECDSA_P256_PUB_LEN uncompressed peer point 0x04 || X || Y
 * @param priv PROTOCORE_ECDSA_P256_PRIV_LEN big-endian scalar d, 1 <= d < n
 * @param shared_x PROTOCORE_ECDSA_P256_COORD_LEN big-endian X coordinate of d * Q_peer
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_ecdsa_ecdh(uint8_t *work, const uint8_t *peer_pub, const uint8_t *priv, uint8_t *shared_x);

/** @brief Module namespace. */
PROTOCORE_NS EcdsaNs Ecdsa PROTOCORE_UNUSED = {.pubkey = protocore_ecdsa_pubkey,
                                               .sign = protocore_ecdsa_sign,
                                               .verify = protocore_ecdsa_verify,
                                               .ecdh = protocore_ecdsa_ecdh};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ECDSA_H
