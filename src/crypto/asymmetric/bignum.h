// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bignum.h
 * @brief 2048-bit big-integer arithmetic for DH-group14 and RSA-2048.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * DESIGN RATIONALE
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * pc_bignum is a fixed-width 2048-bit integer stored as 64 little-endian
 * 32-bit limbs (d[0] = least significant).  Fixed width means:
 *   - Struct size is a compile-time constant: 256 bytes.
 *   - No dynamic allocation - both DH scalars and RSA key fragments fit in
 *     the same type and can live in BSS or on the stack.
 *   - Array indexing is bounds-safe; no VLA or pointer arithmetic hazards.
 *
 * On Arduino (ESP32), DH uses mbedtls_mpi from ESP-IDF (heap-allocated,
 * variable-length bignum), which has hardware-accelerated multiplication.
 * On native builds, the software Montgomery path is used - correct but
 * slower (~200 ms for a 2048-bit exponentiation on x86 at test time).
 * Since DH happens once per connection and ESP32 uses the HW path in
 * production, the native speed is acceptable for testing.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * MONTGOMERY MULTIPLICATION
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * For DH-group14 the modulus p ends in ...FFFFFFFF FFFFFFFF (little-endian
 * d[0]=0xFFFFFFFF).  This gives Montgomery parameter:
 *
 *   p_inv = (-(p mod 2^32))^(-1) mod 2^32
 *         = (-(0xFFFFFFFF))^(-1) mod 2^32
 *         = (0x00000001)^(-1) mod 2^32
 *         = 1
 *
 * p_inv = 1 simplifies the inner reduction loop: m_i = t[i] * 1 = t[i].
 *
 * Montgomery product:  MonPro(a,b) = a·b·R^-1 mod p
 *   where R = 2^2048.
 *
 * To compute a·b mod p normally:
 *   1. Convert a, b to Montgomery form: aR = a·R mod p (= MonPro(a, R²mod p))
 *   2. Compute MonPro(aR, bR) = a·b·R mod p
 *   3. Convert back: MonPro(a·b·R, 1) = a·b mod p
 *
 * R² mod p is a precomputed 2048-bit constant (see pc_bignum.cpp).
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * SCRATCH BUFFER
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * The Montgomery SOS multiplication needs a 129-word (516-byte) temporary and the
 * expmod three pc_bignum temporaries (768 bytes). bn_expmod_group14() borrows all of
 * them as ONE working set from the secure pool (mmgr/secure.h), so the layout
 * is the struct's own field order rather than byte offsets kept in step by hand here.
 *
 * These temporaries hold DH private-exponent and shared-secret fragments. They are
 * wiped when the borrow is released - by the pool, on every exit path. This module
 * does not perform the wipe and does not need to know that one happens.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_BIGNUM_H
#define PROTOCORE_BIGNUM_H

#include "mmgr/secure.h"
#include "protocore_config.h"

PROTO_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Fixed-width 2048-bit integer
// ---------------------------------------------------------------------------

/** @brief Number of 32-bit limbs in a 2048-bit integer. */
#define PC_BN_LIMBS 64

/**
 * @brief A 2048-bit unsigned integer stored as 64 little-endian 32-bit limbs.
 *
 * d[0] = least significant 32 bits.
 * d[63] = most significant 32 bits.
 */
typedef struct
{
    uint32_t d[PC_BN_LIMBS]; ///< 256 bytes of magnitude, little-endian limbs.
} pc_bignum;

// ---------------------------------------------------------------------------
// Scratch buffer (defined in pc_bignum.cpp)
// ---------------------------------------------------------------------------

// bn_expmod_group14() borrows its Montgomery temporaries from the secure pool as one working set; the pool
// wipes them when the borrow is released. This module names no address and performs no wipe.

// ---------------------------------------------------------------------------
// Conversion helpers
// ---------------------------------------------------------------------------

/**
 * @brief Read a big-endian byte array of @p len bytes into a pc_bignum.
 *
 * If @p len < 256 the most-significant limbs are zeroed.
 * If @p len > 256 only the least-significant 256 bytes are read.
 *
 * @param out   Destination bignum.
 * @param bytes Big-endian source bytes.
 * @param len   Number of source bytes (typically 256 for 2048-bit).
 */
void bn_from_bytes(pc_bignum *out, const uint8_t *bytes, size_t len);

/**
 * @brief Write a pc_bignum as a 256-byte big-endian array.
 *
 * @param bytes Destination buffer (exactly 256 bytes).
 * @param in    Source bignum.
 */
void bn_to_bytes(uint8_t bytes[256], const pc_bignum *in);

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

/**
 * @brief Compare two pc_bignum values.
 * @return -1 if a < b, 0 if a == b, 1 if a > b.
 */
int bn_cmp(const pc_bignum *a, const pc_bignum *b);

/**
 * @brief Return non-zero if @p a is zero (all limbs zero).
 */
int bn_is_zero(const pc_bignum *a);

// ---------------------------------------------------------------------------
// DH-group14 modular exponentiation
// ---------------------------------------------------------------------------

/**
 * @brief Compute out = base^exp mod group14_p.
 *
 * Uses Montgomery modular exponentiation with left-to-right binary scan.
 * Borrows one working set for all temporaries; it is wiped when released.
 *
 * On Arduino the computation is delegated to mbedtls_mpi_exp_mod() which
 * uses hardware multiplication and blinding.
 *
 * @param out   Result (base^exp mod p, 2048-bit).
 * @param base  Base value; must satisfy 1 < base < p-1.
 * @param exp   Exponent (e.g. the 2048-bit private DH scalar y).
 */
// ---------------------------------------------------------------------------
// Backend-facing
// ---------------------------------------------------------------------------
//
// bn_expmod_group14() is DECLARED here and DEFINED by exactly one backend under board_drivers/,
// chosen by the vendor's PC_HAS_HW_BIGNUM. There is no weak default: link no backend and this is an
// undefined reference; link two and it is a duplicate definition. Software crypto is a legitimate
// choice - on some parts the only one - but it is always a stated one, never a fallback.

/** @brief Compare two @p n-limb magnitudes: -1, 0 or 1. Shared with the backends. */
int bn_cmp_raw(const uint32_t *a, const uint32_t *b, int n);

void bn_expmod_group14(pc_bignum *out, const pc_bignum *base, const pc_bignum *exp);

/**
 * @brief Validate a received DH public value.
 *
 * RFC 4253 §8: the received value e (or f) must satisfy 1 < e < p-1.
 * Returns 0 if the value is valid, -1 otherwise.
 *
 * @param v  Received public DH value.
 */
int bn_dh_validate(const pc_bignum *v);

// ---------------------------------------------------------------------------
// Group-14 prime constant (exposed for key-derivation and validation)
// ---------------------------------------------------------------------------

/** @brief The RFC 3526 MODP group-14 prime (2048-bit). */
extern const pc_bignum group14_p;

/** @brief Generator for group-14: g = 2. */
extern const pc_bignum group14_g;

PROTO_END_DECLS

#endif // PROTOCORE_BIGNUM_H
