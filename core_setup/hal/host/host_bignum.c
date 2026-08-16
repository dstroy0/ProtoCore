// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file host_bignum.c
 * @brief The host arm of the accelerated bignum backend: DH-2048 modexp on the RSA/MPI accelerator.
 *
 * The bignum backend for a HOST profile that sets PROTOCORE_HAS_HW_BIGNUM 1. A part reaches this
 * capability through its RSA peripheral - core_setup/hal/esp/esp_bignum.c hands the modexp to mbedtls,
 * which drives that peripheral - so the same capability on a host is the same ladder run on the HAL's
 * host arm of the multiply. That makes PROTOCORE_HAS_HW_BIGNUM a real capability on a native build,
 * so the accelerated backend compiles and runs off target against the same vectors as the portable
 * one.
 *
 * It is the backend's CONTRACT - out = base^exp mod group14_p - answered against the host MODMULT.
 * It is not the peripheral's timing and not its register sequence; those are the vendor HAL's and are
 * checked on the part.
 */

#include "core_setup/board_profiles/protocore_platform.h" // PROTOCORE_HAS_HW_BIGNUM, PROTOCORE_HOST
#include "crypto/asymmetric/bignum.h"
#include "crypto/crypto_opt.h"
#include "mmgr/protomem.h"
#include "mmgr/secure.h"

#if PROTOCORE_HOST && PROTOCORE_HAS_HW_BIGNUM

#if !PROTOCORE_RSA_MODMUL_HW
#error                                                                                                                 \
    "ProtoCore: a host build that states PROTOCORE_HAS_HW_BIGNUM 1 reaches the modexp through the RSA/MPI accelerator, so it must also state PROTOCORE_RSA_MODMUL_HW 1 and link core_setup/hal/host/host_crypto_hal.c. Set PROTOCORE_HAS_HW_BIGNUM 0 for the portable software Montgomery backend."
#endif

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// The Montgomery constants the accelerator's MODMULT takes, for the one modulus this backend runs
// against. They are functions of group14_p alone, so one owned instance holds them for the process.
typedef struct
{
    protocore_bignum rr; ///< R^2 mod p, R = 2^2048
    uint32_t mprime;     ///< -p^-1 mod 2^32
    proto_bool ready;    ///< whether the two above are filled
} HostBignumCtx;
static HostBignumCtx s_bn = {0};

// The working set one modexp borrows. The base and the accumulator both carry the DH shared secret,
// so they live in the pool the caller releases and wipes rather than on the stack.
typedef struct
{
    protocore_bignum b;   ///< the base, canonical
    protocore_bignum acc; ///< the accumulator, and the result
} BnExpmodHost;

// Worst-case bytes this backend borrows in one modexp. PROTOCORE_SECURE_ARENA_SIZE is derived from
// declarations like this one; the static_assert below is what proves it.
static_assert(sizeof(BnExpmodHost) <= PROTOCORE_WORK_BIGNUM_HW,
              "BnExpmodHost outgrew PROTOCORE_WORK_BIGNUM_HW - raise it; PROTOCORE_SECURE_ARENA_SIZE derives from it");

// a -= p over the group's limbs.
static void sub_p(uint32_t *a)
{
    uint64_t borrow = 0;
    for (int i = 0; i < PROTOCORE_BN_LIMBS; i++)
    {
        const uint64_t v = (uint64_t)a[i] - group14_p.d[i] - borrow;
        a[i] = (uint32_t)v;
        borrow = (v >> 32) & 1u;
    }
}

// m' = -p^-1 mod 2^32 by Newton, and R^2 mod p by 2048 conditional doublings from R mod p. p's top
// bit is set, so R mod p is 2^2048 - p, which the wrap of a borrow subtraction from zero leaves.
static void bn_init(void)
{
    if (s_bn.ready)
    {
        return;
    }
    const uint32_t p0 = group14_p.d[0];
    uint32_t inv = p0;
    for (int i = 0; i < 5; i++)
    {
        inv *= 2u - p0 * inv;
    }
    s_bn.mprime = 0u - inv;

    uint64_t borrow = 0;
    for (int i = 0; i < PROTOCORE_BN_LIMBS; i++)
    {
        const uint64_t v = (uint64_t)0 - group14_p.d[i] - borrow;
        s_bn.rr.d[i] = (uint32_t)v;
        borrow = (v >> 32) & 1u;
    }
    for (int k = 0; k < 2048; k++)
    {
        uint32_t carry = 0;
        for (int i = 0; i < PROTOCORE_BN_LIMBS; i++)
        {
            const uint32_t nc = s_bn.rr.d[i] >> 31;
            s_bn.rr.d[i] = (s_bn.rr.d[i] << 1) | carry;
            carry = nc;
        }
        if (carry || bn_cmp_raw(s_bn.rr.d, group14_p.d, PROTOCORE_BN_LIMBS) >= 0)
        {
            sub_p(s_bn.rr.d);
        }
    }
    s_bn.ready = PROTO_TRUE;
}

void bn_expmod_group14(protocore_bignum *out, const protocore_bignum *base, const protocore_bignum *exp)
{
    bn_init();

    size_t mark = protocore_secure_mark();
    protocore_span ws = protocore_secure_span(sizeof(BnExpmodHost), _Alignof(BnExpmodHost));
    if (!span.ok(ws))
    {
        protocore_secure_release(mark);
        mem.zero(out, sizeof(*out)); // pool exhausted: a zero result fails every downstream check
        return;
    }
    BnExpmodHost *w = (BnExpmodHost *)(ws.buf);

    // The MODMULT takes canonical operands. p is above 2^2047 and the base is a 2048-bit value, so
    // one conditional subtract canonicalizes any input this can be handed.
    mem.cpy(w->b.d, base->d, sizeof(w->b.d));
    if (bn_cmp_raw(w->b.d, group14_p.d, PROTOCORE_BN_LIMBS) >= 0)
    {
        sub_p(w->b.d);
    }

    mem.zero(w->acc.d, sizeof(w->acc.d));
    w->acc.d[0] = 1u;

    protocore_rsa_hw_acquire();
    for (int i = PROTOCORE_BN_LIMBS - 1; i >= 0; i--)
    {
        for (int b = 31; b >= 0; b--)
        {
            protocore_rsa_modmul(w->acc.d, w->acc.d, w->acc.d, group14_p.d, s_bn.mprime, s_bn.rr.d, PROTOCORE_BN_LIMBS);
            if ((exp->d[i] >> b) & 1u)
            {
                protocore_rsa_modmul(w->acc.d, w->acc.d, w->b.d, group14_p.d, s_bn.mprime, s_bn.rr.d,
                                     PROTOCORE_BN_LIMBS);
            }
        }
    }
    protocore_rsa_hw_release();

    mem.cpy(out->d, w->acc.d, sizeof(out->d));
    protocore_secure_release(mark);
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_HOST && PROTOCORE_HAS_HW_BIGNUM
