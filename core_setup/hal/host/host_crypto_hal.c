// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file host_crypto_hal.c
 * @brief The host arm of the RSA/MPI accelerator. See host_crypto_hal.h.
 *
 * Schoolbook multiply into a double-width product, then a bit-serial reduction from the top. The
 * peripheral reaches the same residue by Montgomery, which is why it needs m' and R^2; the answer is
 * the same value either way, which is what the arms above this are checked against.
 */

#include "core_setup/hal/host/host_crypto_hal.h"

#if PROTOCORE_HOST && PROTOCORE_RSA_MODMUL_HW

PROTOCORE_BEGIN_DECLS

// a >= b over n limbs, little-endian. Retained for the conditional subtract below.
static proto_bool ge_n(const uint32_t *a, const uint32_t *b, unsigned n)
{
    for (unsigned i = n; i-- > 0;)
    {
        if (a[i] != b[i])
        {
            return a[i] > b[i] ? PROTO_TRUE : PROTO_FALSE;
        }
    }
    return PROTO_TRUE; // equal
}

// a -= b over n limbs, little-endian.
static void sub_n(uint32_t *a, const uint32_t *b, unsigned n)
{
    uint64_t borrow = 0;
    for (unsigned i = 0; i < n; i++)
    {
        uint64_t d = (uint64_t)a[i] - (uint64_t)b[i] - borrow;
        a[i] = (uint32_t)d;
        borrow = (d >> 63) & 1u; // the subtraction wrapped
    }
}

void protocore_rsa_hw_acquire(void)
{
    // No clock, no reset, no second user. The bracket is kept so the arm's shape is the same
    // natively as on the part.
}

void protocore_rsa_hw_release(void)
{
}

// One Montgomery multiply (CIOS): t = a*b*R^-1 mod m, R = 2^(32*words). Interleaving the reduction
// with the product keeps this O(words^2) machine words, which is what the peripheral's single-shot
// MODMULT costs and what the ladders above this are written against - a bit-serial reduction would be
// 32x the work per multiply and is what makes a 1,000-round X25519 iteration crawl.
static void montmul(uint32_t *t, const uint32_t *a, const uint32_t *b, const uint32_t *m, uint32_t mprime, unsigned n)
{
    uint32_t s[PROTOCORE_RSA_HOST_MAX_WORDS + 2] = {0};
    for (unsigned i = 0; i < n; i++)
    {
        uint64_t c = 0;
        for (unsigned j = 0; j < n; j++)
        {
            uint64_t v = (uint64_t)s[j] + (uint64_t)a[j] * (uint64_t)b[i] + c;
            s[j] = (uint32_t)v;
            c = v >> 32;
        }
        uint64_t v = (uint64_t)s[n] + c;
        s[n] = (uint32_t)v;
        s[n + 1] = (uint32_t)(v >> 32);

        // u zeroes the low limb, so the whole accumulator shifts down one limb per round.
        uint32_t u = (uint32_t)((uint64_t)s[0] * (uint64_t)mprime);
        v = (uint64_t)s[0] + (uint64_t)u * (uint64_t)m[0];
        c = v >> 32;
        for (unsigned j = 1; j < n; j++)
        {
            v = (uint64_t)s[j] + (uint64_t)u * (uint64_t)m[j] + c;
            s[j - 1] = (uint32_t)v;
            c = v >> 32;
        }
        v = (uint64_t)s[n] + c;
        s[n - 1] = (uint32_t)v;
        s[n] = s[n + 1] + (uint32_t)(v >> 32);
    }
    if (s[n] || ge_n(s, m, n)) // the accumulator is below 2m, so one subtract canonicalizes it
    {
        sub_n(s, m, n);
    }
    for (unsigned i = 0; i < n; i++)
    {
        t[i] = s[i];
    }
}

void protocore_rsa_modmul(uint32_t *z, const uint32_t *x, const uint32_t *y, const uint32_t *m, uint32_t mprime,
                          const uint32_t *rinv, unsigned words)
{
    if (!z || !x || !y || !m || !rinv || words == 0u || words > PROTOCORE_RSA_HOST_MAX_WORDS)
    {
        return;
    }
    // The peripheral preloads R^2 into the result block so MODMULT hands back the plain residue
    // rather than a Montgomery form. The same two steps reach it here: lift x into the Montgomery
    // domain against R^2, then multiply by y, which drops the R back out.
    uint32_t xr[PROTOCORE_RSA_HOST_MAX_WORDS];
    montmul(xr, x, rinv, m, mprime, words); // x * R^2 * R^-1 = x*R
    montmul(z, xr, y, m, mprime, words);    // x*R * y * R^-1 = x*y
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_HOST && PROTOCORE_RSA_MODMUL_HW
