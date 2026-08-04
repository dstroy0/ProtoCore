// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
 * ARDUINO VS NATIVE
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Arduino: mbedTLS (mbedtls_ecdsa_*, mbedtls_ecp_*) - hardware-accelerated big-integer
 *   math on the ESP32 and side-channel-hardened. This is the production path. Signing
 *   uses the ESP32 hardware RNG (randomized ECDSA, RFC 6979 not required for validity).
 *
 * Native:  self-contained software P-256 - 256-bit field/scalar arithmetic (bit-serial
 *   reduction mod p and mod n), Jacobian point arithmetic, and RFC 6979 deterministic
 *   signing so the sign path is byte-exact against the RFC 6979 A.2.5 (P-256/SHA-256)
 *   known-answer vectors. Test-only, like the native RSA path; not compiled into firmware.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * WIRE FORMATS (assembled by the SSH transport/auth layers, not here)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Public-key blob (RFC 5656 §3.1):
 *   string("ecdsa-sha2-nistp256") || string("nistp256") || string(Q)
 * where Q is the uncompressed point 0x04 || X || Y (65 bytes). This module exposes Q
 * as @ref pc_ecdsa_p256_pubkey; the layers wrap it.
 *
 * Signature blob (RFC 5656 §3.1.2):
 *   string("ecdsa-sha2-nistp256") || string( mpint(r) || mpint(s) )
 * This module exposes the raw r || s (32 + 32 big-endian); the layers mpint-wrap them.
 *
 * ECDH shared secret (RFC 5656 §4):
 *   K = the X coordinate of d * Q_peer. @ref pc_ecdsa_p256_ecdh returns the raw 32-byte X;
 *   the transport encodes it as an mpint in the exchange hash and the key derivation.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ECDSA_H
#define PROTOCORE_ECDSA_H

#include "protocore_config.h" // the entry point: types.h for proto_bool and the widths

PROTO_BEGIN_DECLS

/** @brief P-256 private key (scalar d) length. */
#define PC_ECDSA_P256_PRIV_LEN 32
/** @brief P-256 coordinate length (one of X, Y). */
#define PC_ECDSA_P256_COORD_LEN 32
/** @brief P-256 uncompressed public point length: 0x04 || X || Y. */
#define PC_ECDSA_P256_PUB_LEN 65
/** @brief Raw ECDSA signature length: r || s (32 + 32, big-endian). */
#define PC_ECDSA_P256_SIG_LEN 64

/**
 * @brief Derive the uncompressed public point Q = d*G from a P-256 private scalar.
 *
 * @param[out] pub   65-byte uncompressed point 0x04 || X || Y.
 * @param[in]  priv  32-byte big-endian private scalar d (must satisfy 1 <= d < n).
 * @return true on success, false if @p priv is 0 or >= the group order n.
 */
proto_bool pc_ecdsa_p256_pubkey(uint8_t pub[PC_ECDSA_P256_PUB_LEN], const uint8_t priv[PC_ECDSA_P256_PRIV_LEN]);

/**
 * @brief Sign @p mlen bytes of @p msg with a P-256 private key (ECDSA, SHA-256).
 *
 * The message is hashed with SHA-256 internally. Native builds sign deterministically
 * (RFC 6979); Arduino builds sign with the hardware RNG. Both produce a valid signature.
 *
 * @param[out] sig   64-byte raw signature r || s (big-endian, 32 + 32).
 * @param[in]  msg   Message to sign (typically the KEX exchange hash H).
 * @param[in]  mlen  Length of @p msg.
 * @param[in]  priv  32-byte big-endian private scalar d.
 * @return true on success, false on invalid key or internal failure.
 */
proto_bool pc_ecdsa_p256_sign(uint8_t sig[PC_ECDSA_P256_SIG_LEN], const uint8_t *msg, size_t mlen,
                              const uint8_t priv[PC_ECDSA_P256_PRIV_LEN]);

/**
 * @brief Verify a P-256 ECDSA signature (SHA-256) against an uncompressed public point.
 *
 * @param[in] pub   65-byte uncompressed point 0x04 || X || Y (rejected if not on-curve).
 * @param[in] msg   Signed message.
 * @param[in] mlen  Length of @p msg.
 * @param[in] sig   64-byte raw signature r || s (big-endian, 32 + 32).
 * @return true if the signature is valid, false otherwise.
 */
proto_bool pc_ecdsa_p256_verify(const uint8_t pub[PC_ECDSA_P256_PUB_LEN], const uint8_t *msg, size_t mlen,
                                const uint8_t sig[PC_ECDSA_P256_SIG_LEN]);

/**
 * @brief P-256 ECDH: the shared-secret X coordinate of d * Q_peer (RFC 5656 §4 / RFC 5903).
 *
 * Backs the ecdh-sha2-nistp256 SSH key exchange. Validates that @p peer_pub is a valid
 * on-curve point and that the product is not the identity; the returned 32-byte big-endian
 * X coordinate is the shared field element the transport encodes as an mpint.
 *
 * @param[out] shared_x  32-byte big-endian X coordinate of d * Q_peer.
 * @param[in]  peer_pub  65-byte uncompressed peer point 0x04 || X || Y.
 * @param[in]  priv      32-byte big-endian private scalar d (1 <= d < n).
 * @return true on success, false on an invalid peer point / scalar or an identity result.
 */
proto_bool pc_ecdsa_p256_ecdh(uint8_t shared_x[PC_ECDSA_P256_COORD_LEN], const uint8_t peer_pub[PC_ECDSA_P256_PUB_LEN],
                              const uint8_t priv[PC_ECDSA_P256_PRIV_LEN]);

PROTO_END_DECLS

#endif // PROTOCORE_ECDSA_H
