// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file portable_bignum.cpp
 * @brief Software Montgomery modexp - the bignum backend for a vendor with no accelerator.
 *
 * Selected EXPLICITLY by a vendor profile that sets PC_HAS_HW_BIGNUM 0. It is never a fallback: the
 * core declares bn_expmod_group14() and links whatever backend the vendor provides, so linking none
 * is an undefined reference and linking two is a duplicate definition. Both fail the build.
 *
 * Software crypto is a legitimate choice and on some parts the only one - what must not happen is
 * arriving here by default. This implementation is data-dependent and NOT constant time
 * (SECURITY.md, timing), so a vendor whose silicon can do the modexp in hardware must never end up
 * on it silently; with no weak symbol anywhere in the chain, it cannot.
 */

#include "board_drivers/board_profiles/pc_platform.h" // PC_HAS_HW_BIGNUM
#include "crypto/asymmetric/bignum.h"
#include "crypto/crypto_opt.h"
#include "mmgr/secure.h"
#include <string.h>

#if !PC_HAS_HW_BIGNUM

PC_CRYPTO_HOT

// Group14 Montgomery constants, owned by one instance (internal linkage): R mod p, R^2 mod p,
// and the init flag (all filled by bn_init()). One named owner, unreachable cross-TU.
typedef struct
{
    pc_bignum r1; // R mod p = 2^2048 - p (two's complement of p in 2048 bits)
    pc_bignum r2; // R^2 mod p = 2^4096 mod p (bn_init() via repeated doubling)
    proto_bool initialized = PROTO_FALSE;
} Group14Ctx;
static Group14Ctx s_g14;

// Subtract b from a in place (a -= b).  Assumes a >= b.  Both are n limbs.
static void bn_sub_inplace(uint32_t *a, const uint32_t *b, int n)
{
    uint64_t borrow = 0;
    for (int i = 0; i < n; i++)
    {
        uint64_t v = (uint64_t)a[i] - b[i] - borrow;
        a[i] = (uint32_t)v;
        borrow = (v >> 32) & 1u;
    }
}

// Left-shift n-limb value by 1 bit.  Returns the shifted-out MSB.
static uint32_t bn_shl1(uint32_t *a, int n)
{
    uint32_t carry = 0;
    for (int i = 0; i < n; i++)
    {
        uint32_t nc = a[i] >> 31;
        a[i] = (a[i] << 1) | carry;
        carry = nc;
    }
    return carry;
}

// ---------------------------------------------------------------------------
// Montgomery initialization (compute R mod p and R^2 mod p for group-14)
// ---------------------------------------------------------------------------

static void bn_init(void)
{
    if (s_g14.initialized)
    {
        return;
    }

    // R mod p = 2^2048 mod p = 2^2048 - p
    // (Since 2^2047 <= p < 2^2048, R mod p = 2^2048 - p which is positive and < p.)
    // Compute via borrow subtraction: 0 - p with 2048-bit wrap.
    {
        uint64_t borrow = 0;
        for (int i = 0; i < PC_BN_LIMBS; i++)
        {
            uint64_t v = (uint64_t)0 - group14_p.d[i] - borrow;
            s_g14.r1.d[i] = (uint32_t)v;
            borrow = (v >> 32) & 1u;
        }
        // borrow == 1 here (expected - we wrapped around 2^2048), which is
        // the carry that represents the implicit 2^2048 in R mod p. Correct.
    }

    // R^2 mod p = 2^4096 mod p.
    // Compute by starting from R mod p and doubling it 2048 times mod p.
    memcpy(s_g14.r2.d, s_g14.r1.d, sizeof(pc_bignum));
    for (int i = 0; i < 2048; i++)
    {
        uint32_t overflow = bn_shl1(s_g14.r2.d, PC_BN_LIMBS);
        // If overflow bit set OR result >= p, subtract p.
        // bn_init() takes no parameters and reads no state besides the compile-time constant
        // group14_p; combined with the s_g14.initialized memo guard above, this doubling loop
        // runs as exactly one fixed 2048-step trace for the entire process lifetime - no test
        // input, call order, or code path can make any step see different data. Verified
        // independently by a bit-exact simulation of all 2048 steps (not the gcov run itself):
        // for this specific prime/starting-value pair, every time bn_shl1() does not overflow,
        // the doubled 2048-bit value is already < p, so the cmp_raw() half of this OR is never
        // the deciding vote - it only ever agrees with an overflow that is already false.
        // Structurally unreachable (a mathematical constant of this trace), not merely untested.
        if (overflow || bn_cmp_raw(s_g14.r2.d, group14_p.d, PC_BN_LIMBS) >= 0) // GCOVR_EXCL_BR_LINE
        {
            bn_sub_inplace(s_g14.r2.d, group14_p.d, PC_BN_LIMBS);
        }
    }

    s_g14.initialized = PROTO_TRUE;
}

// bn_expmod_group14's working set, borrowed whole. Both paths hold the DH private exponent and the
// shared secret, so the secure pool wipes them when the borrow is released - on every exit path.

// Native path: the Montgomery temporaries plus bn_monpro's 129-limb accumulator.
typedef struct
{
    pc_bignum base_mont;
    pc_bignum result;
    pc_bignum tmp;
    uint32_t t[129];
} BnExpmodWork;

// Worst-case bytes this backend borrows in one modexp. PC_SECURE_ARENA_SIZE is derived
// from declarations like this one; the static_assert below is what proves it.
static_assert(sizeof(BnExpmodWork) <= PC_WORK_BIGNUM_SW,
              "BnExpmodWork outgrew PC_WORK_BIGNUM_SW - raise it; PC_SECURE_ARENA_SIZE derives from it");

// ---------------------------------------------------------------------------
// Montgomery SOS multiplication: out = a * b * R^-1 mod p
// Requires: 0 <= a, b < p.
// The 129-limb temporary t[] is supplied by the caller (a member of its working set).
// p_inv = 1 for group-14 (see file header).
// ---------------------------------------------------------------------------

static void bn_monpro(pc_bignum *out, const pc_bignum *a, const pc_bignum *b, uint32_t *t)
{
    memset(t, 0, 129 * sizeof(uint32_t));

    for (int i = 0; i < PC_BN_LIMBS; i++)
    {
        // Multiply step: t[0..63] += a[i] * b[0..63]
        uint64_t carry = 0;
        for (int j = 0; j < PC_BN_LIMBS; j++)
        {
            uint64_t uv = (uint64_t)t[i + j] + (uint64_t)a->d[i] * (uint64_t)b->d[j] + carry;
            t[i + j] = (uint32_t)uv;
            carry = uv >> 32;
        }
        t[i + PC_BN_LIMBS] += (uint32_t)carry;

        // Reduction step: m = t[i] * p_inv = t[i] * 1 = t[i]
        uint32_t m = t[i];
        carry = 0;
        for (int j = 0; j < PC_BN_LIMBS; j++)
        {
            uint64_t uv = (uint64_t)t[i + j] + (uint64_t)m * (uint64_t)group14_p.d[j] + carry;
            t[i + j] = (uint32_t)uv;
            carry = uv >> 32;
        }
        // Add carry into the high word (t[i+64]); t[128] absorbs final overflow.
        uint64_t hi = (uint64_t)t[i + PC_BN_LIMBS] + carry;
        t[i + PC_BN_LIMBS] = (uint32_t)hi;
        t[i + PC_BN_LIMBS + 1] += (uint32_t)(hi >> 32);
    }

    // Result is in t[64..127].  Conditionally subtract p if result >= p.
    uint32_t *res = t + PC_BN_LIMBS;
    // For 0 <= a,b < p the raw (pre-correction) SOS value is < 2p, so it needs the guard
    // limb t[128] for the case where it reaches/exceeds 2^2048. group14_p's top two limbs are
    // both 0xFFFFFFFF, i.e. p sits within about 2^-64 of 2^2048, so "the raw value is >= p but
    // still < 2^2048" (t[128] == 0 yet the cmp_raw() half alone is true) is a razor-thin sliver
    // of the [0, 2p) output range for a "generic" (a, b) - but it is directly constructible:
    // base = R^-1 mod p (R = 2^2048) makes the very first MonPro call inside
    // bn_expmod_group14() - MonPro(base, R^2 mod p), i.e. converting base to Montgomery form -
    // compute base*R mod p = 1 exactly, landing the pre-correction raw value at exactly p+1
    // (t[128]==0, cmp_raw()>=0 true). Exercised by
    // test_bn_expmod_group14_hits_correction_sliver_without_overflow_limb (test_ssh_conn.cpp).
    if (t[128] || bn_cmp_raw(res, group14_p.d, PC_BN_LIMBS) >= 0)
    {
        bn_sub_inplace(res, group14_p.d, PC_BN_LIMBS);
    }

    memcpy(out->d, res, PC_BN_LIMBS * sizeof(uint32_t));
}

void bn_expmod_group14(pc_bignum *out, const pc_bignum *base, const pc_bignum *exp)
{
    bn_init(); // ensure s_g14.r1, s_g14.r2 are computed

    // The whole working set in one borrow. These are the same bytes the fixed layout carved by hand;
    // as struct members they cannot drift from a layout documented somewhere else.
    SecureBorrow ws_b(sizeof(BnExpmodWork), alignof(BnExpmodWork));
    const pc_span &ws = ws_b.span();
    if (!pc_span_ok(ws))
    {
        memset(out, 0, sizeof(*out)); // pool exhausted: a zero result fails every downstream check
        return;
    }
    BnExpmodWork *w = (BnExpmodWork *)(ws.buf);
    pc_bignum *base_mont = &w->base_mont;
    pc_bignum *result = &w->result;
    pc_bignum *tmp = &w->tmp;

    // Convert base to Montgomery form: base_mont = base * R mod p
    //   = MonPro(base, R^2 mod p)
    bn_monpro(base_mont, base, &s_g14.r2, w->t);

    // result = 1 in Montgomery form = R mod p = s_g14.r1
    memcpy(result->d, s_g14.r1.d, sizeof(pc_bignum));

    // Left-to-right binary square-and-multiply (MSB first: d[63]..d[0], bit 31..0)
    for (int i = PC_BN_LIMBS - 1; i >= 0; i--)
    {
        for (int b = 31; b >= 0; b--)
        {
            // Square: result = result^2 * R^-1 mod p
            bn_monpro(tmp, result, result, w->t);
            memcpy(result->d, tmp->d, sizeof(pc_bignum));

            if ((exp->d[i] >> b) & 1u)
            {
                // Multiply: result = result * base_mont * R^-1 mod p
                bn_monpro(tmp, result, base_mont, w->t);
                memcpy(result->d, tmp->d, sizeof(pc_bignum));
            }
        }
    }

    // Convert back from Montgomery form: out = result * R^-1 mod p
    //   = MonPro(result, 1)
    pc_bignum one;
    memset(one.d, 0, sizeof(pc_bignum));
    one.d[0] = 1u;
    bn_monpro(out, result, &one, w->t);
}

#endif // !PC_HAS_HW_BIGNUM
