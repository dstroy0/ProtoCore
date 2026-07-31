// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ed25519.h
 * @brief Ed25519 signatures (RFC 8032) for ssh-ed25519 host keys + client auth.
 *
 * PureEdDSA over edwards25519. Deterministic signing (RFC 8032 §5.1.6) - no RNG - and
 * verification, built on the shared Curve25519 field arithmetic (pc_curve25519) and
 * SHA-512 (pc_sha512). No heap; state is on the stack. Correctness is pinned to the
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

#include <stddef.h>
#include <stdint.h>

/** @brief Ed25519 seed (private key) length. */
#define PC_ED25519_SEED_LEN 32
/** @brief Ed25519 public key length. */
#define PC_ED25519_PUBKEY_LEN 32
/** @brief Ed25519 signature length (R || S). */
#define PC_ED25519_SIG_LEN 64

/** @brief Derive the 32-byte public key A from a 32-byte @p seed. */
void pc_ed25519_pubkey(uint8_t pub[PC_ED25519_PUBKEY_LEN], const uint8_t seed[PC_ED25519_SEED_LEN]);

/**
 * @brief Deterministically sign @p mlen bytes of @p msg with @p seed (RFC 8032 §5.1.6).
 * @param sig  Output R || S, PC_ED25519_SIG_LEN bytes.
 */
void pc_ed25519_sign(uint8_t sig[PC_ED25519_SIG_LEN], const uint8_t *msg, size_t mlen,
                     const uint8_t seed[PC_ED25519_SEED_LEN]);

/**
 * @brief Verify an Ed25519 signature (RFC 8032 §5.1.7).
 * @return true if @p sig is a valid signature of @p msg under public key @p pub.
 */
bool pc_ed25519_verify(const uint8_t pub[PC_ED25519_PUBKEY_LEN], const uint8_t *msg, size_t mlen,
                       const uint8_t sig[PC_ED25519_SIG_LEN]);

#endif // PROTOCORE_ED25519_H
