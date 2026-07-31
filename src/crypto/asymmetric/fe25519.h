// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file fe25519.h
 * @brief Per-variant GF(2^255-19) field layer on the RSA/MPI hardware accelerator (X25519 + Ed25519).
 *
 * Field elements are canonical `uint32[8]` (< p = 2^255-19) so every field multiply is a single
 * 256-bit modular multiply on the RSA accelerator (S3: ~1,386 cycles vs 7,955 for the software SIMD
 * `pc_gf_mul`; P4: ~2,118 cycles / 5.9 us). add/sub are native 32-bit (carry + one conditional subtract
 * of p); bytes<->fe is a per-scalar-mult conversion, not per multiply. This is the shared engine behind
 * both the X25519 KEX (`pc_curve25519.cpp`) and the Ed25519 host-key signature (`pc_ed25519.cpp`) on
 * every die with a single-shot hardware MODMULT (S3 hw_ver1, P4 and newer hw_ver3 - see the gate below);
 * the radix-2^16 `pc_gf` path is the native / classic-ESP32 fallback in both.
 *
 * The accelerator (and its lock) are shared with mbedTLS RSA/DH, so a scalar-mult brackets itself with
 * `pc_fe_hw_enable()` / `pc_fe_hw_disable()` (mbedTLS's own `esp_mpi_{enable,disable}_hardware_hw_op`,
 * i.e. acquire the MPI lock + clock/power the peripheral) and holds the lock for its whole run.
 *
 * Header-only `static inline` on purpose: the cheap ops (add/sub/cswap) inline into the ladder in each
 * translation unit with no cross-TU call overhead, and the whole layer stays one source of truth.
 */

#ifndef PROTOCORE_FE25519_H
#define PROTOCORE_FE25519_H

#include <stdint.h>

#include "board_drivers/hal/esp/esp_crypto_hal.h" // pc_rsa_modmul + pc_rsa_hw_acquire/release (the RSA-accelerator HAL)
#include "crypto/crypto_scratch.h"                // pc_ct_eq (the canonical constant-time compare)

// 25519 has no dedicated ECC accelerator on any ESP32 die, so the RSA MODMULT is the field-layer win wherever
// it exists - track the HAL's PC_RSA_MODMUL_HW (S3, P4, ...). Classic ESP32 / native keep the software ladder.
#ifdef PC_RSA_MODMUL_HW
#define PC_FE25519_MPI_HW 1
#endif

#ifdef PC_FE25519_MPI_HW

/** @brief A field element of GF(2^255-19): canonical, eight little-endian 32-bit limbs (< p). */
typedef uint32_t fe[8];

// Constants for the 256-bit modular multiply mod p = 2^255-19 (scratchpad/montconst.py): Montgomery m'
// and R^2 mod p (= 38^2 = 1444 = 0x5a4). Preloading R^2 into the result block makes the accelerator
// return a plain residue X*Y mod p rather than a Montgomery form (the esp_mpi_mul_mpi_mod convention).
static const uint32_t FE_MOD_MPRIME = 0x286bca1bu;
static const uint32_t FE_MOD_P[8] = {0xffffffedu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
                                     0xffffffffu, 0xffffffffu, 0xffffffffu, 0x7fffffffu};
static const uint32_t FE_MOD_R2[8] = {0x000005a4u, 0, 0, 0, 0, 0, 0, 0};

// Acquire the accelerator (lock + power) for a scalar-mult, and drop it after. Bracket every run. Thin names
// over the HAL so the X25519 ladder / Ed25519 point arithmetic read as before.
static inline void pc_fe_hw_enable(void)
{
    pc_rsa_hw_acquire();
}
static inline void pc_fe_hw_disable(void)
{
    pc_rsa_hw_release();
}

// z = x*y mod p (8 words / 256-bit) on the RSA MODMULT. Requires pc_fe_hw_enable() first. Canonical (< p),
// safe if z aliases x/y. Delegates to the HAL modmul with this domain's constants; the crypto TUs that pull
// this in build at -O2 (PC_CRYPTO_HOT), where the always_inline HAL folds FE_MOD_P / the mostly-zero
// FE_MOD_R2 into immediate stores - the hand-tuned ~1,380-cyc path.
static inline void fe_mul(fe z, const fe x, const fe y)
{
    pc_rsa_modmul(z, x, y, FE_MOD_P, FE_MOD_MPRIME, FE_MOD_R2, 8);
}
static inline void fe_sq(fe o, const fe x)
{
    fe_mul(o, x, x);
}

static inline void fe_copy(fe o, const fe a)
{
    for (int i = 0; i < 8; i++)
    {
        o[i] = a[i];
    }
}
static inline void fe_0(fe o)
{
    for (int i = 0; i < 8; i++)
    {
        o[i] = 0;
    }
}
static inline void fe_1(fe o)
{
    o[0] = 1;
    for (int i = 1; i < 8; i++)
    {
        o[i] = 0;
    }
}
// If o >= p (o is in [p, 2p)), subtract p. Constant-time: the borrow out of o-p selects o or o-p.
static inline void fe_reduce_once(fe o)
{
    uint32_t t[8];
    int64_t b = 0;
    for (int i = 0; i < 8; i++)
    {
        b += (int64_t)o[i] - (int64_t)FE_MOD_P[i];
        t[i] = (uint32_t)b;
        b >>= 32;
    }
    uint32_t keep = (uint32_t)b; // 0 if o>=p (take t=o-p), 0xffffffff if o<p (keep o)
    for (int i = 0; i < 8; i++)
    {
        o[i] = (o[i] & keep) | (t[i] & ~keep);
    }
}
static inline void fe_add(fe o, const fe x, const fe y) // x,y < p -> o = x+y mod p
{
    uint64_t c = 0;
    for (int i = 0; i < 8; i++)
    {
        c += (uint64_t)x[i] + y[i];
        o[i] = (uint32_t)c;
        c >>= 32;
    }
    fe_reduce_once(o); // x+y < 2p, one conditional subtract
}
static inline void fe_sub(fe o, const fe x, const fe y) // x,y < p -> o = x-y mod p
{
    int64_t b = 0;
    uint32_t t[8];
    for (int i = 0; i < 8; i++)
    {
        b += (int64_t)x[i] - (int64_t)y[i];
        t[i] = (uint32_t)b;
        b >>= 32;
    }
    uint32_t borrow = (uint32_t)b; // 0xffffffff if x<y -> add p back
    uint64_t c = 0;
    for (int i = 0; i < 8; i++)
    {
        c += (uint64_t)t[i] + (FE_MOD_P[i] & borrow);
        o[i] = (uint32_t)c;
        c >>= 32;
    }
}
static inline void fe_cswap(fe x, fe y, uint32_t swap) // constant-time swap of x,y when swap==1
{
    uint32_t mask = (uint32_t)(-(int32_t)swap);
    for (int i = 0; i < 8; i++)
    {
        uint32_t t = mask & (x[i] ^ y[i]);
        x[i] ^= t;
        y[i] ^= t;
    }
}
static inline void fe_frombytes(fe o, const uint8_t b[32])
{
    for (int i = 0; i < 8; i++)
    {
        o[i] = (uint32_t)b[4 * i] | ((uint32_t)b[4 * i + 1] << 8) | ((uint32_t)b[4 * i + 2] << 16) |
               ((uint32_t)b[4 * i + 3] << 24);
    }
    o[7] &= 0x7fffffffu; // Ed25519/X25519 both ignore bit 255 of the y/u coordinate
    fe_reduce_once(o);   // the masked value can still be in [p, 2^255) -> canonicalize
}
static inline void fe_tobytes(uint8_t b[32], const fe a)
{
    fe t;
    fe_copy(t, a);
    fe_reduce_once(t); // freeze to the canonical residue
    for (int i = 0; i < 8; i++)
    {
        b[4 * i] = (uint8_t)t[i];
        b[4 * i + 1] = (uint8_t)(t[i] >> 8);
        b[4 * i + 2] = (uint8_t)(t[i] >> 16);
        b[4 * i + 3] = (uint8_t)(t[i] >> 24);
    }
}
// o = a^(p-2) = a^-1 mod p (tweetnacl square-and-multiply chain for the exponent 2^255-21).
static inline void fe_invert(fe o, const fe a)
{
    fe c;
    fe_copy(c, a);
    for (int i = 253; i >= 0; i--)
    {
        fe_sq(c, c);
        if (i != 2 && i != 4)
        {
            fe_mul(c, c, a);
        }
    }
    fe_copy(o, c);
}
// o = a^((p-5)/8) = a^(2^252-3) - the square-root exponent for Ed25519 point decompression.
static inline void fe_pow2523(fe o, const fe a)
{
    fe c;
    fe_copy(c, a);
    for (int i = 250; i >= 0; i--)
    {
        fe_sq(c, c);
        if (i != 1)
        {
            fe_mul(c, c, a);
        }
    }
    fe_copy(o, c);
}
// Low bit of the canonical encoding (Ed25519 x-coordinate sign).
static inline int fe_parity(const fe a)
{
    uint8_t d[32];
    fe_tobytes(d, a);
    return d[0] & 1;
}
// 0 if a and b encode the same field element, -1 otherwise (constant-time over the 32 bytes).
static inline int fe_neq(const fe a, const fe b)
{
    uint8_t c[32];
    uint8_t d[32];
    fe_tobytes(c, a);
    fe_tobytes(d, b);
    return pc_ct_eq(c, d, 32) ? 0 : -1;
}

#endif // PC_FE25519_MPI_HW
#endif // PROTOCORE_FE25519_H
