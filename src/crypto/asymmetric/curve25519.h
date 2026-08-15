// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file curve25519.h
 * @brief Curve25519 field arithmetic + X25519 (RFC 7748) for the curve25519-sha256 KEX.
 *
 * Field elements are GF(2^255 - 19) in a portable radix-2^16 representation (sixteen
 * int64 limbs), so no 128-bit integer type is needed - important because 32-bit xtensa
 * (ESP32) gcc has no __int128. The same field arithmetic backs Ed25519 (protocore_ed25519),
 * so the field ops are exported here. Correctness is pinned to the RFC 7748 §5.2 test
 * vectors (test_ed25519). No heap; all state is on the stack.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CURVE25519_H
#define PROTOCORE_CURVE25519_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths and PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_CURVE25519

PROTOCORE_BEGIN_DECLS

/** @brief A field element of GF(2^255 - 19): 16 limbs, radix 2^16 (limb i weighs 2^(16i)). */
typedef int64_t protocore_gf[16];

// --- Field arithmetic (shared with protocore_ed25519) ----------------------------

void protocore_gf_copy(protocore_gf out, const protocore_gf in);                     ///< out = in
void protocore_gf_add(protocore_gf out, const protocore_gf a, const protocore_gf b); ///< out = a + b (unreduced)
void protocore_gf_sub(protocore_gf out, const protocore_gf a, const protocore_gf b); ///< out = a - b (unreduced)
void protocore_gf_mul(protocore_gf out, const protocore_gf a, const protocore_gf b); ///< out = a * b mod p
void protocore_gf_sq(protocore_gf out, const protocore_gf a);                        ///< out = a^2 mod p
void protocore_gf_inv(protocore_gf out, const protocore_gf a);                       ///< out = a^-1 mod p (= a^(p-2))
void protocore_gf_pack(uint8_t out[32], const protocore_gf a);    ///< canonical little-endian 32-byte encoding
void protocore_gf_unpack(protocore_gf out, const uint8_t in[32]); ///< decode 32 bytes (high bit ignored)
void protocore_gf_cswap(protocore_gf p, protocore_gf q, int b);   ///< constant-time conditional swap of p,q when b==1

// --- X25519 (RFC 7748) -----------------------------------------------------

/**
 * @brief X25519 scalar multiplication: @p out = @p scalar * @p point (RFC 7748 §5).
 *
 * @p scalar and @p point are 32-byte little-endian; the scalar is clamped internally.
 * @p out may alias neither input. Constant-time in the scalar (Montgomery ladder with
 * conditional swaps).
 */
void protocore_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);

/** @brief X25519 with the standard base point u=9: @p out = @p scalar * G. */
void protocore_x25519_base(uint8_t out[32], const uint8_t scalar[32]);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CURVE25519

#endif // PROTOCORE_CURVE25519_H
