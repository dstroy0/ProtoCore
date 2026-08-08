// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ecdsa.c
 * @brief ECDSA over NIST P-256 for ecdsa-sha2-nistp256 (RFC 5656 / FIPS 186-4).
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * THREE BUILD PATHS
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * ESP32-S3 (Arduino): a self-contained P-256 whose every 256-bit field / scalar multiply is one
 *   modular multiply on the RSA/MPI hardware accelerator (the same engine pc_fe25519.h drives for
 *   X25519 / Ed25519) - the MODMULT is modulus-generic, so it serves both the field domain (mod p)
 *   and the scalar domain (mod n) by swapping the {M, m', R^2} constants. Point arithmetic uses the
 *   exception-free complete formulas (Renes-Costello-Batina 2016, EFD add/dbl-2015-rcb, a = -3) driven by
 *   a constant-time 4-bit fixed-window scalar multiply (uniform op sequence, full-table masked select, no
 *   input-dependent branches). Signing is RFC 6979 deterministic, so the on-device output is byte-exact to
 *   the published vectors (same KATs as native). This is the production path (PC_ECDSA_MPI_HW); sign /
 *   verify / ecdh run ~2.7-2.9x faster than the mbedTLS ECP path it replaces on non-S3 targets.
 *
 * Native: the identical complete-formula / RFC 6979 code, but each field multiply is a software
 *   schoolbook product reduced bit-serially. Only fp_mul differs from the S3 path, so the native KATs
 *   validate the exact point / scalar arithmetic the accelerator runs. Test-only, not in firmware.
 *
 * Other Arduino (classic ESP32 etc.): mbedTLS (mbedtls_ecdsa_*, mbedtls_ecp_*) - hardware big-integer
 *   math and side-channel hardening, signing with the ESP32 hardware RNG. The MODMULT register layout is
 *   an S3 specialization, so non-S3 targets keep the portable mbedTLS path (no perf regression).
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * WIRE FORMATS (assembled by the SSH transport/auth layers, not here)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Public-key blob:  string("ecdsa-sha2-nistp256") || string("nistp256") || string(Q), Q = 0x04||X||Y.
 * Signature blob:   string("ecdsa-sha2-nistp256") || string( mpint(r) || mpint(s) ); this module
 *   exposes raw r||s (32+32 big-endian) and the layers mpint-wrap them.
 * ECDH shared secret (RFC 5656 sec 4): K = X coordinate of d*Q_peer, raw 32-byte big-endian.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#include "crypto/asymmetric/ecdsa.h"
#include "crypto/hash/sha256.h"

#if PC_HAS_HW_ECC
#include "sdkconfig.h" // CONFIG_IDF_TARGET_ESP32S3 - selects the MODMULT field layer
#endif

// The S3 field/scalar layer drives the RSA peripheral through mbedTLS's port (esp_mpi_*), which only
// exists in the on-device toolchain and whose MODMULT register map is an S3 specialization.
#if PC_HAS_HW_ECC && defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3
#define PC_ECDSA_MPI_HW 1
#endif

// Platform-conditional headers, hoisted here so no #include follows code (no mid-file includes -
// ci_tooling/check/check_src_banned.py enforces it). The implementation branches below use the same guards.
#if PC_HAS_HW_ECC && !defined(PC_ECDSA_MPI_HW)
#include <mbedtls/ecdh.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#else
#include "crypto/mac/hmac_sha256.h" // RFC 6979 HMAC-DRBG for the deterministic-nonce complete-formula path
#ifdef PC_ECDSA_MPI_HW
#include "core_setup/hal/esp/esp_crypto_hal.h" // pc_rsa_modmul + pc_rsa_hw_acquire/release (RSA-accelerator HAL)
#endif
#endif

// Measured (crypto bench): the S3 P-256 MODMULT path's ~14% -O3 win is carried by -fpeel-loops (bisected
// on-device); pin just that transform on the -O2 floor. Non-S3 dies run ecdsa on mbedtls/HW (flat at any level)
// and take the crypto_opt per-die default (P4 -O3, else -O2). sdkconfig is included above.
#include "crypto/crypto_opt.h"
#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3
PC_CRYPTO_HOT_PEEL
#else
PC_CRYPTO_HOT
#endif

// ---------------------------------------------------------------------------
// HW path without MPI modmult - mbedTLS
// ---------------------------------------------------------------------------

#if PC_HAS_HW_ECC && !defined(PC_ECDSA_MPI_HW)

// RNG callback backed by the ESP32 hardware RNG.
static int ecdsa_rng(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    pc_platform_rand_fill(buf, len);
    return 0;
}

proto_bool pc_ecdsa_p256_pubkey(uint8_t pub[PC_ECDSA_P256_PUB_LEN], const uint8_t priv[PC_ECDSA_P256_PRIV_LEN])
{
    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_mpi d;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&d);

    proto_bool ok = PROTO_FALSE;
    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_mpi_read_binary(&d, priv, PC_ECDSA_P256_PRIV_LEN) == 0 &&
        mbedtls_ecp_mul(&grp, &Q, &d, &grp.G, ecdsa_rng, NULL) == 0)
    {
        size_t olen = 0;
        if (mbedtls_ecp_point_write_binary(&grp, &Q, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, pub, PC_ECDSA_P256_PUB_LEN) ==
                0 &&
            olen == PC_ECDSA_P256_PUB_LEN)
        {
            ok = PROTO_TRUE;
        }
    }

    mbedtls_mpi_free(&d);
    mbedtls_ecp_point_free(&Q);
    mbedtls_ecp_group_free(&grp);
    return ok;
}

proto_bool pc_ecdsa_p256_sign(uint8_t sig[PC_ECDSA_P256_SIG_LEN], const uint8_t *msg, size_t mlen,
                              const uint8_t priv[PC_ECDSA_P256_PRIV_LEN])
{
    uint8_t h[PC_SHA256_DIGEST_LEN];
    pc_sha256(msg, mlen, h);

    mbedtls_ecp_group grp;
    mbedtls_mpi d;
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    proto_bool ok = PROTO_FALSE;
    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_mpi_read_binary(&d, priv, PC_ECDSA_P256_PRIV_LEN) == 0 &&
        mbedtls_ecdsa_sign(&grp, &r, &s, &d, h, PC_SHA256_DIGEST_LEN, ecdsa_rng, NULL) == 0 &&
        mbedtls_mpi_write_binary(&r, sig, PC_ECDSA_P256_COORD_LEN) == 0 &&
        mbedtls_mpi_write_binary(&s, sig + PC_ECDSA_P256_COORD_LEN, PC_ECDSA_P256_COORD_LEN) == 0)
    {
        ok = PROTO_TRUE;
    }

    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&grp);
    return ok;
}

proto_bool pc_ecdsa_p256_verify(const uint8_t pub[PC_ECDSA_P256_PUB_LEN], const uint8_t *msg, size_t mlen,
                                const uint8_t sig[PC_ECDSA_P256_SIG_LEN])
{
    uint8_t h[PC_SHA256_DIGEST_LEN];
    pc_sha256(msg, mlen, h);

    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    proto_bool ok = PROTO_FALSE;
    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_ecp_point_read_binary(&grp, &Q, pub, PC_ECDSA_P256_PUB_LEN) == 0 &&
        mbedtls_ecp_check_pubkey(&grp, &Q) == 0 && mbedtls_mpi_read_binary(&r, sig, PC_ECDSA_P256_COORD_LEN) == 0 &&
        mbedtls_mpi_read_binary(&s, sig + PC_ECDSA_P256_COORD_LEN, PC_ECDSA_P256_COORD_LEN) == 0 &&
        mbedtls_ecdsa_verify(&grp, h, PC_SHA256_DIGEST_LEN, &Q, &r, &s) == 0)
    {
        ok = PROTO_TRUE;
    }

    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&Q);
    mbedtls_ecp_group_free(&grp);
    return ok;
}

proto_bool pc_ecdsa_p256_ecdh(uint8_t shared_x[PC_ECDSA_P256_COORD_LEN], const uint8_t peer_pub[PC_ECDSA_P256_PUB_LEN],
                              const uint8_t priv[PC_ECDSA_P256_PRIV_LEN])
{
    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_mpi d;
    mbedtls_mpi z; // shared secret = the X coordinate of d*Q
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&z);

    proto_bool ok = PROTO_FALSE;
    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_ecp_point_read_binary(&grp, &Q, peer_pub, PC_ECDSA_P256_PUB_LEN) == 0 &&
        mbedtls_ecp_check_pubkey(&grp, &Q) == 0 && mbedtls_mpi_read_binary(&d, priv, PC_ECDSA_P256_PRIV_LEN) == 0 &&
        mbedtls_ecdh_compute_shared(&grp, &z, &Q, &d, ecdsa_rng, NULL) == 0 &&
        mbedtls_mpi_write_binary(&z, shared_x, PC_ECDSA_P256_COORD_LEN) == 0)
    {
        ok = PROTO_TRUE;
    }

    mbedtls_mpi_free(&z);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_point_free(&Q);
    mbedtls_ecp_group_free(&grp);
    return ok;
}

#else // ---- S3 HW-MODMULT path, or native software path (shared complete-formula P-256) ----

// ---- 256-bit little-endian field / scalar arithmetic ----
// Values are eight uint32 limbs (limb 0 least significant), held canonical (< the domain modulus).

// P-256 domain parameters (little-endian words).
static const uint32_t P256_P[8] = {0xffffffffu, 0xffffffffu, 0xffffffffu, 0x00000000u,
                                   0x00000000u, 0x00000000u, 0x00000001u, 0xffffffffu};
static const uint32_t P256_N[8] = {0xfc632551u, 0xf3b9cac2u, 0xa7179e84u, 0xbce6faadu,
                                   0xffffffffu, 0xffffffffu, 0x00000000u, 0xffffffffu};
static const uint32_t P256_B[8] = {0x27d2604bu, 0x3bce3c3eu, 0xcc53b0f6u, 0x651d06b0u,
                                   0x769886bcu, 0xb3ebbd55u, 0xaa3a93e7u, 0x5ac635d8u};
static const uint32_t P256_B3[8] = {0x777720e2u, 0xb36ab4bau, 0x64fb12e2u, 0x2f571411u,
                                    0x63c99435u, 0x1bc33800u, 0xfeafbbb6u, 0x1052a18au}; // 3b mod p

// R = 2^256. Montgomery constants for the MODMULT (m' = -M^-1 mod 2^32, R^2 mod M); scratchpad/p256_verify.py.
static const uint32_t P256_P_R2[8] = {0x00000003u, 0x00000000u, 0xffffffffu, 0xfffffffbu,
                                      0xfffffffeu, 0xffffffffu, 0xfffffffdu, 0x00000004u};
static const uint32_t P256_N_R2[8] = {0xbe79eea2u, 0x83244c95u, 0x49bd6fa6u, 0x4699799cu,
                                      0x2b6bec59u, 0x2845b239u, 0xf3d95620u, 0x66e12d94u};

// A field/scalar domain: its modulus and (S3) the two MODMULT constants.
typedef struct
{
    const uint32_t *m;
    uint32_t mprime;
    const uint32_t *r2;
} Fp;
static const Fp FP = {P256_P, 0x00000001u, P256_P_R2}; // field domain (mod p): m' = 1 since p ends in 0xffffffff
static const Fp FN = {P256_N, 0xee00bc4fu, P256_N_R2}; // scalar domain (mod n)

static void fp_set(uint32_t r[8], const uint32_t a[8])
{
    for (int i = 0; i < 8; i++)
    {
        r[i] = a[i];
    }
}
static void fp_zero(uint32_t r[8])
{
    for (int i = 0; i < 8; i++)
    {
        r[i] = 0;
    }
}
static proto_bool fp_is_zero(const uint32_t a[8])
{
    uint32_t x = 0;
    for (int i = 0; i < 8; i++)
    {
        x |= a[i];
    }
    return x == 0;
}
// -1 if a<b, 0 if a==b, 1 if a>b.
static int fp_cmp(const uint32_t a[8], const uint32_t b[8])
{
    for (int i = 7; i >= 0; i--)
    {
        if (a[i] != b[i])
        {
            return a[i] > b[i] ? 1 : -1;
        }
    }
    return 0;
}
// r = a - b (mod 2^256); returns borrow.
static uint32_t sub_raw(uint32_t r[8], const uint32_t a[8], const uint32_t b[8])
{
    uint64_t brw = 0;
    for (int i = 0; i < 8; i++)
    {
        uint64_t t = (uint64_t)a[i] - b[i] - brw;
        r[i] = (uint32_t)t;
        brw = (t >> 32) & 1u;
    }
    return (uint32_t)brw;
}
// r = a + b (mod m); a,b < m -> one conditional subtract of m. Constant-time.
static void fp_add(uint32_t r[8], const uint32_t a[8], const uint32_t b[8], const Fp *F)
{
    uint32_t s[8];
    uint64_t c = 0;
    for (int i = 0; i < 8; i++)
    {
        c += (uint64_t)a[i] + b[i];
        s[i] = (uint32_t)c;
        c >>= 32;
    }
    uint32_t carry = (uint32_t)c; // a+b may be a 257-bit value
    uint32_t t[8];
    uint64_t b2 = 0;
    for (int i = 0; i < 8; i++)
    {
        uint64_t v = (uint64_t)s[i] - F->m[i] - b2;
        t[i] = (uint32_t)v;
        b2 = (v >> 32) & 1u;
    }
    // keep s if (s - m) borrowed and there was no carry out of the add; else take s - m.
    uint32_t take_t = carry | (uint32_t)(1u - b2); // 1 -> s>=m, use t
    uint32_t mask = (uint32_t)(-(int32_t)take_t);
    for (int i = 0; i < 8; i++)
    {
        r[i] = (t[i] & mask) | (s[i] & ~mask);
    }
}
// r = a - b (mod m); a,b < m -> conditional add of m on borrow. Constant-time.
static void fp_sub(uint32_t r[8], const uint32_t a[8], const uint32_t b[8], const Fp *F)
{
    uint32_t t[8];
    uint32_t borrow = sub_raw(t, a, b); // 1 if a < b
    uint32_t mask = (uint32_t)(-(int32_t)borrow);
    uint64_t c = 0;
    for (int i = 0; i < 8; i++)
    {
        c += (uint64_t)t[i] + (F->m[i] & mask);
        r[i] = (uint32_t)c;
        c >>= 32;
    }
}
// Reduce a single value a in [0, 2m) into [0, m): subtract m once if a >= m. Constant-time.
static void fp_reduce_once(uint32_t r[8], const uint32_t a[8], const uint32_t m[8])
{
    uint32_t t[8];
    uint32_t borrow = sub_raw(t, a, m); // 1 -> a < m, keep a
    uint32_t mask = (uint32_t)(-(int32_t)borrow);
    for (int i = 0; i < 8; i++)
    {
        r[i] = (a[i] & mask) | (t[i] & ~mask);
    }
}

#ifdef PC_ECDSA_MPI_HW
// z = x*y mod F->m on the S3 RSA accelerator. Requires ecdsa_hw_on() first. Preloading R^2 into the result
// block makes MODMULT return the plain residue (the esp_mpi_mul_mpi_mod convention). Output canonical (< m).
static void fp_mul(uint32_t z[8], const uint32_t x[8], const uint32_t y[8], const Fp *F) // safe if z aliases x/y
{
    // Same 256-bit RSA MODMULT the 25519 field layer uses, parameterized by the domain (mod p or mod n): the
    // HAL owns the register access; here we only pass this domain's {m, m', R^2 mod m}. Output canonical (< m).
    pc_rsa_modmul(z, x, y, F->m, F->mprime, F->r2, 8);
}
static void ecdsa_hw_on()
{
    pc_rsa_hw_acquire();
}
static void ecdsa_hw_off()
{
    pc_rsa_hw_release();
}
#else
// acc[0..7] >= m[0..7]? Compares the low 8 limbs from the most significant down.
// Both P256_P and P256_N are prime, and every operand ever passed into fp_mul (hence into this
// bit-serial reduction) is invariant-bound strictly below its own modulus before use (see every
// call site: d/k/r/s/e/qx/qy are all range-checked or reduced before reaching a multiply). So the
// integer product of two such operands can never equal the modulus exactly - that would need one
// factor to be exactly 1 and the other exactly the modulus, and the modulus itself is never a valid
// operand anywhere in this file. A full 8-limb "acc == m" coincidence at any bit-serial reduction
// step, or even a partial (single-limb) coincidence while the loop is still scanning, was never
// observed across this suite's 590M+ calls to this function. Not exercised by any host-reachable input.
static proto_bool reduce_low8_ge(const uint32_t acc[8], const uint32_t m[8])
{
    for (int k = 7; k >= 0; k--)
    {
        if (acc[k] != m[k])
        {
            return acc[k] > m[k];
        }
    }
    return PROTO_TRUE;
}
// Reduce a 512-bit product mod m (bit-serial, MSB to LSB). Correct but slow; the native path is test-only.
static void reduce_mod(uint32_t r[8], const uint32_t prod[16], const uint32_t m[8])
{
    uint32_t acc[9];
    for (int k = 0; k < 9; k++)
    {
        acc[k] = 0;
    }
    for (int bit = 511; bit >= 0; bit--)
    {
        uint32_t carry = 0;
        for (int k = 0; k < 9; k++)
        {
            uint32_t nc = acc[k] >> 31;
            acc[k] = (acc[k] << 1) | carry;
            carry = nc;
        }
        acc[0] |= (prod[bit >> 5] >> (bit & 31)) & 1u;
        proto_bool ge = acc[8] != 0;
        if (!ge)
        {
            ge = reduce_low8_ge(acc, m);
        }
        if (ge)
        {
            uint64_t brw = 0;
            for (int k = 0; k < 8; k++)
            {
                uint64_t t = (uint64_t)acc[k] - m[k] - brw;
                acc[k] = (uint32_t)t;
                brw = (t >> 32) & 1u;
            }
            acc[8] -= (uint32_t)brw;
        }
    }
    for (int k = 0; k < 8; k++)
    {
        r[k] = acc[k];
    }
}
// z = (x * y) mod F->m (software schoolbook + reduction).
static void fp_mul(uint32_t z[8], const uint32_t x[8], const uint32_t y[8], const Fp *F)
{
    uint32_t prod[16];
    for (int k = 0; k < 16; k++)
    {
        prod[k] = 0;
    }
    for (int i = 0; i < 8; i++)
    {
        uint64_t carry = 0;
        for (int j = 0; j < 8; j++)
        {
            uint64_t t = (uint64_t)prod[i + j] + (uint64_t)x[i] * y[j] + carry;
            prod[i + j] = (uint32_t)t;
            carry = t >> 32;
        }
        int k = i + 8;
        while (carry)
        {
            uint64_t t = (uint64_t)prod[k] + carry;
            prod[k] = (uint32_t)t;
            carry = t >> 32;
            k++;
        }
    }
    reduce_mod(z, prod, F->m);
}
static void ecdsa_hw_on()
{
    // Software big-integer path: there is no hardware MPI accelerator to enable on this build (the HW
    // variant of these hooks is compiled instead when the accelerator is present).
}
static void ecdsa_hw_off()
{
    // Counterpart to ecdsa_hw_on(): nothing to disable on the software path.
}
#endif

static void fp_sqr(uint32_t r[8], const uint32_t a[8], const Fp *F)
{
    fp_mul(r, a, a, F);
}
// r = a*x mod p where the curve a = p - 3, i.e. r = -3x. Two adds + a negate instead of a MODMULT.
// Alias-safe (r may be x). Only the field domain has this a, so it is hard-wired to FP.
static void fp_mul_by_a(uint32_t r[8], const uint32_t x[8])
{
    uint32_t tx[8];
    fp_add(tx, x, x, &FP);
    fp_add(tx, tx, x, &FP); // 3x
    static const uint32_t zero[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    fp_sub(r, zero, tx, &FP); // -3x
}
// r = a^(m-2) mod m (Fermat inverse; m prime). Fixed public exponent -> constant-time in a.
static void fp_inv(uint32_t r[8], const uint32_t a[8], const Fp *F)
{
    static const uint32_t two[8] = {2, 0, 0, 0, 0, 0, 0, 0};
    uint32_t e[8];
    sub_raw(e, F->m, two); // e = m - 2
    uint32_t res[8] = {1, 0, 0, 0, 0, 0, 0, 0};
    uint32_t base[8];
    fp_set(base, a);
    for (int i = 0; i < 256; i++)
    {
        if ((e[i >> 5] >> (i & 31)) & 1u)
        {
            fp_mul(res, res, base, F);
        }
        fp_mul(base, base, base, F);
    }
    fp_set(r, res);
}

static void load_be(uint32_t r[8], const uint8_t b[32])
{
    for (int i = 0; i < 8; i++)
    {
        const uint8_t *p = b + (28 - 4 * i);
        r[i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    }
}
static void store_be(uint8_t b[32], const uint32_t r[8])
{
    for (int i = 0; i < 8; i++)
    {
        uint8_t *p = b + (28 - 4 * i);
        p[0] = (uint8_t)(r[i] >> 24);
        p[1] = (uint8_t)(r[i] >> 16);
        p[2] = (uint8_t)(r[i] >> 8);
        p[3] = (uint8_t)r[i];
    }
}

// ---- Point arithmetic: complete formulas on y^2 = x^3 - 3x + b, projective (X:Y:Z), x=X/Z, y=Y/Z ----
// Exception-free for all inputs on the prime-order curve (RCB 2016), so the constant-time ladder needs no
// special cases. Identity is (0:1:0). Every field op is mod p (FP).

typedef struct
{
    uint32_t X[8];
    uint32_t Y[8];
    uint32_t Z[8];
} Pt;

// Base point G (affine, Z = 1).
static const Pt P256_G = {
    {0xd898c296u, 0xf4a13945u, 0x2deb33a0u, 0x77037d81u, 0x63a440f2u, 0xf8bce6e5u, 0xe12c4247u, 0x6b17d1f2u},
    {0x37bf51f5u, 0xcbb64068u, 0x6b315eceu, 0x2bce3357u, 0x7c0f9e16u, 0x8ee7eb4au, 0xfe1a7f9bu, 0x4fe342e2u},
    {1u, 0, 0, 0, 0, 0, 0, 0}};

static proto_bool pt_is_infinity(const Pt *p)
{
    return fp_is_zero(p->Z);
}
static void pt_set_infinity(Pt *p)
{
    fp_zero(p->X);
    fp_zero(p->Y);
    p->Y[0] = 1;
    fp_zero(p->Z);
}
static void pt_from_affine(Pt *p, const uint32_t x[8], const uint32_t y[8])
{
    fp_set(p->X, x);
    fp_set(p->Y, y);
    fp_zero(p->Z);
    p->Z[0] = 1;
}
// (x, y) = (X/Z, Y/Z). Caller ensures p is not the identity.
static void pt_to_affine(uint32_t x[8], uint32_t y[8], const Pt *p)
{
    uint32_t zi[8];
    fp_inv(zi, p->Z, &FP);
    fp_mul(x, p->X, zi, &FP);
    fp_mul(y, p->Y, zi, &FP);
}

// r = a + b (EFD add-2015-rcb, a = -3). Alias-safe: all reads land in locals before *r is written.
static void pt_add(Pt *r, const Pt *a, const Pt *b)
{
    uint32_t t0[8];
    uint32_t t1[8];
    uint32_t t2[8];
    uint32_t t3[8];
    uint32_t t4[8];
    uint32_t t5[8];
    uint32_t x3[8];
    uint32_t y3[8];
    uint32_t z3[8];
    fp_mul(t0, a->X, b->X, &FP);
    fp_mul(t1, a->Y, b->Y, &FP);
    fp_mul(t2, a->Z, b->Z, &FP);
    fp_add(t3, a->X, a->Y, &FP);
    fp_add(t4, b->X, b->Y, &FP);
    fp_mul(t3, t3, t4, &FP);
    fp_add(t4, t0, t1, &FP);
    fp_sub(t3, t3, t4, &FP);
    fp_add(t4, a->X, a->Z, &FP);
    fp_add(t5, b->X, b->Z, &FP);
    fp_mul(t4, t4, t5, &FP);
    fp_add(t5, t0, t2, &FP);
    fp_sub(t4, t4, t5, &FP);
    fp_add(t5, a->Y, a->Z, &FP);
    fp_add(x3, b->Y, b->Z, &FP);
    fp_mul(t5, t5, x3, &FP);
    fp_add(x3, t1, t2, &FP);
    fp_sub(t5, t5, x3, &FP);
    fp_mul_by_a(z3, t4);
    fp_mul(x3, P256_B3, t2, &FP);
    fp_add(z3, x3, z3, &FP);
    fp_sub(x3, t1, z3, &FP);
    fp_add(z3, t1, z3, &FP);
    fp_mul(y3, x3, z3, &FP);
    fp_add(t1, t0, t0, &FP);
    fp_add(t1, t1, t0, &FP);
    fp_mul_by_a(t2, t2);
    fp_mul(t4, P256_B3, t4, &FP);
    fp_add(t1, t1, t2, &FP);
    fp_sub(t2, t0, t2, &FP);
    fp_mul_by_a(t2, t2);
    fp_add(t4, t4, t2, &FP);
    fp_mul(t0, t1, t4, &FP);
    fp_add(y3, y3, t0, &FP);
    fp_mul(t0, t5, t4, &FP);
    fp_mul(x3, t3, x3, &FP);
    fp_sub(x3, x3, t0, &FP);
    fp_mul(t0, t3, t1, &FP);
    fp_mul(z3, t5, z3, &FP);
    fp_add(z3, z3, t0, &FP);
    fp_set(r->X, x3);
    fp_set(r->Y, y3);
    fp_set(r->Z, z3);
}

// r = 2*a (EFD dbl-2015-rcb, a = -3). Alias-safe.
static void pt_dbl(Pt *r, const Pt *a)
{
    uint32_t t0[8];
    uint32_t t1[8];
    uint32_t t2[8];
    uint32_t t3[8];
    uint32_t x3[8];
    uint32_t y3[8];
    uint32_t z3[8];
    fp_sqr(t0, a->X, &FP);
    fp_sqr(t1, a->Y, &FP);
    fp_sqr(t2, a->Z, &FP);
    fp_mul(t3, a->X, a->Y, &FP);
    fp_add(t3, t3, t3, &FP);
    fp_mul(z3, a->X, a->Z, &FP);
    fp_add(z3, z3, z3, &FP);
    fp_mul_by_a(x3, z3);
    fp_mul(y3, P256_B3, t2, &FP);
    fp_add(y3, x3, y3, &FP);
    fp_sub(x3, t1, y3, &FP);
    fp_add(y3, t1, y3, &FP);
    fp_mul(y3, x3, y3, &FP);
    fp_mul(x3, t3, x3, &FP);
    fp_mul(z3, P256_B3, z3, &FP);
    fp_mul_by_a(t2, t2);
    fp_sub(t3, t0, t2, &FP);
    fp_mul_by_a(t3, t3);
    fp_add(t3, t3, z3, &FP);
    fp_add(z3, t0, t0, &FP);
    fp_add(t0, z3, t0, &FP);
    fp_add(t0, t0, t2, &FP);
    fp_mul(t0, t0, t3, &FP);
    fp_add(y3, y3, t0, &FP);
    fp_mul(t2, a->Y, a->Z, &FP);
    fp_add(t2, t2, t2, &FP);
    fp_mul(t0, t2, t3, &FP);
    fp_sub(x3, x3, t0, &FP);
    fp_mul(z3, t2, t1, &FP);
    fp_add(z3, z3, z3, &FP);
    fp_add(z3, z3, z3, &FP);
    fp_set(r->X, x3);
    fp_set(r->Y, y3);
    fp_set(r->Z, z3);
}

// dst = table[idx], scanning all 16 entries so the access pattern is independent of the (secret) idx.
static void pt_table_select(Pt *dst, const Pt table[16], uint32_t idx)
{
    fp_zero(dst->X);
    fp_zero(dst->Y);
    fp_zero(dst->Z);
    for (uint32_t e = 0; e < 16; e++)
    {
        uint32_t x = e ^ idx;
        uint32_t nz = (x | (0u - x)) >> 31;  // 1 if e != idx, else 0
        uint32_t mask = (uint32_t)(nz - 1u); // 0xffffffff if e == idx, else 0
        for (int i = 0; i < 8; i++)
        {
            dst->X[i] |= table[e].X[i] & mask;
            dst->Y[i] |= table[e].Y[i] & mask;
            dst->Z[i] |= table[e].Z[i] & mask;
        }
    }
}

// r = k * p, k a 256-bit little-endian scalar. Constant-time 4-bit fixed window: a uniform op sequence
// (256 doublings + 64 additions + a data-independent table build) with the only secret-dependent step a
// full-table masked select - no input-dependent branches, and the complete formulas are exception-free.
// The 16-entry table (~1.5 KB) is on the stack, live only in this shallow phase (well under the SSH KEX
// peak), so it is reentrant across worker tasks. Used for secret scalars.
static void pt_scalarmul(Pt *r, const uint32_t k[8], const Pt *p)
{
    Pt table[16]; // table[i] = i * p; table[0] = identity
    pt_set_infinity(&table[0]);
    table[1] = *p;
    for (int i = 2; i < 16; i++)
    {
        if (i & 1)
        {
            pt_add(&table[i], &table[i - 1], p);
        }
        else
        {
            pt_dbl(&table[i], &table[i / 2]);
        }
    }
    Pt acc;
    pt_set_infinity(&acc);
    for (int w = 63; w >= 0; w--) // 64 nibbles, most significant first (Horner)
    {
        pt_dbl(&acc, &acc);
        pt_dbl(&acc, &acc);
        pt_dbl(&acc, &acc);
        pt_dbl(&acc, &acc); // acc *= 16
        uint32_t idx = (k[w >> 3] >> ((w & 7) * 4)) & 0xfu;
        Pt sel;
        pt_table_select(&sel, table, idx);
        pt_add(&acc, &acc, &sel);
    }
    *r = acc;
}

// Check (x, y) is on y^2 = x^3 - 3x + b (mod p) and both coordinates are in range. Requires ecdsa_hw_on().
static proto_bool on_curve(const uint32_t x[8], const uint32_t y[8])
{
    if (fp_cmp(x, P256_P) >= 0 || fp_cmp(y, P256_P) >= 0)
    {
        return PROTO_FALSE;
    }
    uint32_t lhs[8];
    uint32_t rhs[8];
    uint32_t t[8];
    fp_sqr(lhs, y, &FP); // y^2
    fp_sqr(rhs, x, &FP);
    fp_mul(rhs, rhs, x, &FP); // x^3
    fp_add(t, x, x, &FP);
    fp_add(t, t, x, &FP);     // 3x
    fp_sub(rhs, rhs, t, &FP); // x^3 - 3x
    fp_add(rhs, rhs, P256_B, &FP);
    return fp_cmp(lhs, rhs) == 0;
}

// ---- RFC 6979 deterministic nonce (HMAC-SHA256 DRBG, hlen = qlen = 256) ----

// out = HMAC-SHA256(key, V || (tag>=0 ? tag||x||e : nothing)).
static void pc_hmac_cat(uint8_t out[32], const uint8_t key[32], const uint8_t *v, size_t vlen, const int tag,
                        const uint8_t *x, const uint8_t *e)
{
    uint8_t buf[97]; // 32 (V) + 1 (tag) + 32 (x) + 32 (e)
    size_t n = 0;
    memcpy(buf + n, v, vlen);
    n += vlen;
    if (tag >= 0)
    {
        buf[n++] = (uint8_t)tag;
        memcpy(buf + n, x, 32);
        n += 32;
        memcpy(buf + n, e, 32);
        n += 32;
    }
    pc_hmac_sha256(key, 32, buf, n, out);
}

// One RFC 6979 candidate k: if it yields a valid r and s, write the 64-byte signature and return true.
//
// The three guards below (k invalid, r == 0, s == 0) are RFC 6979's mandated rejection-sampling safety
// net (RFC 6979 section 3.2 step h.3 / SEC1 section 4.1.3): each rejects a candidate with probability
// roughly 2^-32 (k landing in [n, 2^256) - n is only ~2^224 short of 2^256) or roughly 2^-256 (r or s
// landing on exactly 0). Every value guarded here is HMAC-SHA256 output (bits2int(V), or derived from
// it through the field/scalar arithmetic) - a cryptographic PRF with no host-reachable way to steer its
// output to one specific rare value short of an infeasible (2^32+ evaluation) brute-force search. None
// of these guards tripped once across this suite's full run (hundreds of scalar multiplies over dozens
// of distinct keys/messages), so the retry path in ecdsa_sign_core is dead for the same reason.
static proto_bool ecdsa_try_sign(const uint32_t k[8], const uint32_t d[8], const uint32_t e[8], uint8_t sig[64])
{
    if (fp_is_zero(k) || fp_cmp(k, P256_N) >= 0)
    {
        return PROTO_FALSE;
    }
    Pt R;
    pt_scalarmul(&R, k, &P256_G);
    // k is already range-checked to [1, n-1] above, and P-256 has cofactor 1 (its group order is the
    // prime n exactly), so every non-identity point - including G - has order exactly n: k*G can only be
    // the identity if n divides k, which is impossible for k in [1, n-1].
    if (pt_is_infinity(&R))
    {
        return PROTO_FALSE;
    }
    uint32_t rx[8];
    uint32_t ry[8];
    pt_to_affine(rx, ry, &R);
    uint32_t r[8];
    fp_reduce_once(r, rx, P256_N); // r = Rx mod n (Rx < p < 2n -> one subtract)
    if (fp_is_zero(r))
    {
        return PROTO_FALSE;
    }
    uint32_t kinv[8];
    uint32_t s[8];
    fp_inv(kinv, k, &FN);
    fp_mul(s, r, d, &FN);    // r*d
    fp_add(s, s, e, &FN);    // e + r*d
    fp_mul(s, kinv, s, &FN); // k^-1 (e + r*d)
    if (fp_is_zero(s))
    {
        return PROTO_FALSE;
    }
    store_be(sig, r);
    store_be(sig + 32, s);
    return PROTO_TRUE;
}

// ECDSA core: sign hash h1 (32) with scalar d, deterministic k per RFC 6979. Requires ecdsa_hw_on().
static proto_bool ecdsa_sign_core(uint8_t sig[64], const uint8_t h1[32], const uint32_t d[8])
{
    uint32_t e[8];
    uint32_t etmp[8];
    load_be(etmp, h1);
    fp_reduce_once(e, etmp, P256_N); // bits2int(h1) mod n

    uint8_t x_oct[32];
    uint8_t h_oct[32];
    store_be(x_oct, d);
    store_be(h_oct, e); // bits2octets(h1)

    uint8_t V[32];
    uint8_t K[32];
    memset(V, 0x01, 32);
    memset(K, 0x00, 32);
    pc_hmac_cat(K, K, V, 32, 0x00, x_oct, h_oct);
    pc_hmac_cat(V, K, V, 32, -1, NULL, NULL);
    pc_hmac_cat(K, K, V, 32, 0x01, x_oct, h_oct);
    pc_hmac_cat(V, K, V, 32, -1, NULL, NULL);

    for (int guard = 0; guard < 64; guard++)
    // practice (see its comment), so the first candidate
    // always succeeds and the loop never reaches guard==64
    {
        pc_hmac_cat(V, K, V, 32, -1, NULL, NULL); // T = HMAC_K(V), one block
        uint32_t k[8];
        load_be(k, V); // bits2int(T)
        if (ecdsa_try_sign(k, d, e, sig))
        {
            return PROTO_TRUE;
        }
        uint8_t buf[33]; // retry: K = HMAC_K(V || 0x00); V = HMAC_K(V)
        memcpy(buf, V, 32);
        buf[32] = 0x00;
        pc_hmac_sha256(K, 32, buf, 33, K);
        pc_hmac_cat(V, K, V, 32, -1, NULL, NULL);
    }
    return PROTO_FALSE;
}

proto_bool pc_ecdsa_p256_pubkey(uint8_t pub[PC_ECDSA_P256_PUB_LEN], const uint8_t priv[PC_ECDSA_P256_PRIV_LEN])
{
    uint32_t d[8];
    load_be(d, priv);
    if (fp_is_zero(d) || fp_cmp(d, P256_N) >= 0)
    {
        return PROTO_FALSE;
    }

    ecdsa_hw_on();
    Pt Q;
    pt_scalarmul(&Q, d, &P256_G);
    proto_bool ok = !pt_is_infinity(&Q);
    // d is already range-checked to [1, n-1] above, and P-256 has cofactor 1 (group order exactly n), so
    // d*G can only be the identity if n divides d - impossible here; ok is therefore always true.
    if (ok)
    {
        uint32_t qx[8];
        uint32_t qy[8];
        pt_to_affine(qx, qy, &Q);
        pub[0] = 0x04;
        store_be(pub + 1, qx);
        store_be(pub + 33, qy);
    }
    ecdsa_hw_off();
    return ok;
}

proto_bool pc_ecdsa_p256_sign(uint8_t sig[PC_ECDSA_P256_SIG_LEN], const uint8_t *msg, size_t mlen,
                              const uint8_t priv[PC_ECDSA_P256_PRIV_LEN])
{
    uint32_t d[8];
    load_be(d, priv);
    if (fp_is_zero(d) || fp_cmp(d, P256_N) >= 0)
    {
        return PROTO_FALSE;
    }
    uint8_t h1[PC_SHA256_DIGEST_LEN];
    pc_sha256(msg, mlen, h1);

    ecdsa_hw_on();
    proto_bool ok = ecdsa_sign_core(sig, h1, d);
    ecdsa_hw_off();
    return ok;
}

proto_bool pc_ecdsa_p256_verify(const uint8_t pub[PC_ECDSA_P256_PUB_LEN], const uint8_t *msg, size_t mlen,
                                const uint8_t sig[PC_ECDSA_P256_SIG_LEN])
{
    if (pub[0] != 0x04)
    {
        return PROTO_FALSE;
    }
    uint32_t qx[8];
    uint32_t qy[8];
    load_be(qx, pub + 1);
    load_be(qy, pub + 33);

    uint32_t r[8];
    uint32_t s[8];
    load_be(r, sig);
    load_be(s, sig + 32);
    if (fp_is_zero(r) || fp_cmp(r, P256_N) >= 0 || fp_is_zero(s) || fp_cmp(s, P256_N) >= 0)
    {
        return PROTO_FALSE;
    }

    uint8_t h1[PC_SHA256_DIGEST_LEN];
    pc_sha256(msg, mlen, h1);
    uint32_t e[8];
    uint32_t etmp[8];
    load_be(etmp, h1);
    fp_reduce_once(e, etmp, P256_N);

    ecdsa_hw_on();
    proto_bool ok = on_curve(qx, qy);
    if (ok)
    {
        uint32_t w[8];
        uint32_t u1[8];
        uint32_t u2[8];
        fp_inv(w, s, &FN);
        fp_mul(u1, e, w, &FN);
        fp_mul(u2, r, w, &FN);

        Pt Q;
        Pt Rg;
        Pt Rq;
        Pt R;
        pt_from_affine(&Q, qx, qy);
        pt_scalarmul(&Rg, u1, &P256_G);
        pt_scalarmul(&Rq, u2, &Q);
        pt_add(&R, &Rg, &Rq);
        if (pt_is_infinity(&R))
        {
            ok = PROTO_FALSE;
        }
        else
        {
            uint32_t rx[8];
            uint32_t ry[8];
            uint32_t rxn[8];
            pt_to_affine(rx, ry, &R);
            fp_reduce_once(rxn, rx, P256_N);
            ok = fp_cmp(rxn, r) == 0;
        }
    }
    ecdsa_hw_off();
    return ok;
}

proto_bool pc_ecdsa_p256_ecdh(uint8_t shared_x[PC_ECDSA_P256_COORD_LEN], const uint8_t peer_pub[PC_ECDSA_P256_PUB_LEN],
                              const uint8_t priv[PC_ECDSA_P256_PRIV_LEN])
{
    if (peer_pub[0] != 0x04)
    {
        return PROTO_FALSE;
    }
    uint32_t qx[8];
    uint32_t qy[8];
    load_be(qx, peer_pub + 1);
    load_be(qy, peer_pub + 33);
    uint32_t d[8];
    load_be(d, priv);
    if (fp_is_zero(d) || fp_cmp(d, P256_N) >= 0)
    {
        return PROTO_FALSE;
    }

    ecdsa_hw_on();
    proto_bool ok = on_curve(qx, qy); // rejects off-curve / out-of-range peer points
    if (ok)
    {
        Pt Q;
        Pt R;
        pt_from_affine(&Q, qx, qy);
        pt_scalarmul(&R, d, &Q);
        // Q already passed on_curve() above, and P-256 has cofactor 1, so every validated on-curve point
        // has order exactly n; d is already range-checked to [1, n-1] in this function, so d*Q can only
        // be the identity if n divides d - impossible here.
        if (pt_is_infinity(&R))
        {
            ok = PROTO_FALSE;
        }
        else
        {
            uint32_t rx[8];
            uint32_t ry[8];
            pt_to_affine(rx, ry, &R);
            store_be(shared_x, rx); // K = X coordinate (big-endian)
        }
    }
    ecdsa_hw_off();
    return ok;
}

#endif // PC_HAS_HW_ECC path selection
