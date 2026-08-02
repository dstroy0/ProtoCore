// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bignum.c
 * @brief 2048-bit Montgomery modular exponentiation for DH-group14.
 *
 * ─── Montgomery parameter for group-14 ───────────────────────────────────
 * The group-14 prime p ends in ...FFFFFFFF FFFFFFFF (little-endian d[0]=d[1]=
 * 0xFFFFFFFF).  The Montgomery parameter:
 *
 *   p_inv = (-(p mod 2^32))^(-1) mod 2^32
 *
 * p mod 2^32 = 0xFFFFFFFF
 * -(0xFFFFFFFF) mod 2^32 = 0x00000001
 * 0x00000001^(-1) mod 2^32 = 1
 *
 * So p_inv = 1 for the group-14 prime.
 * In the SOS reduction pass: m_i = t[i] * p_inv mod 2^32 = t[i].
 *
 * ─── R² mod p ────────────────────────────────────────────────────────────
 * R = 2^2048.  R² mod p = 2^4096 mod p.
 * It is computed once at startup by bn_init() via 4096 doublings mod p,
 * and stored in the static s_g14.r2 constant.
 * ─────────────────────────────────────────────────────────────────────────
 */

#include "crypto/asymmetric/bignum.h"
#include "crypto/crypto_opt.h"
#include "mmgr/secure.h"
#include <string.h>
PC_CRYPTO_HOT

// The modexp below borrows its Montgomery temporaries from the secure pool as one working set. It does not
// know where that memory comes from or that releasing it wipes - it asks for a resource and uses it.

// ---------------------------------------------------------------------------
// Group-14 prime and generator (RFC 3526, §3)
// Little-endian 32-bit limbs: d[0] = least significant.
// ---------------------------------------------------------------------------

const pc_bignum group14_p = {{
    // 2048-bit MODP group-14 prime
    0xFFFFFFFFu, 0xFFFFFFFFu, 0x8AACaa68u, 0x15728E5Au, 0x98FA0510u, 0x15D22618u, 0xEA956AE5u, 0x3995497Cu,
    0x95581718u, 0xDE2BCBF6u, 0x6F4C52C9u, 0xB5C55DF0u, 0xEC07A28Fu, 0x9B2783A2u, 0x180E8603u, 0xE39E772Cu,
    0x2E36CE3Bu, 0x32905E46u, 0xCA18217Cu, 0xF1746C08u, 0x4ABC9804u, 0x670C354Eu, 0x7096966Du, 0x9ED52907u,
    0x208552BBu, 0x1C62F356u, 0xDCA3AD96u, 0x83655D23u, 0xFD24CF5Fu, 0x69163FA8u, 0x1C55D39Au, 0x98DA4836u,
    0xA163BF05u, 0xC2007CB8u, 0xECE45B3Du, 0x49286651u, 0x7C4B1FE6u, 0xAE9F2411u, 0x5A899FA5u, 0xEE386BFBu,
    0xF406B7EDu, 0x0BFF5CB6u, 0xA637ED6Bu, 0xF44C42E9u, 0x625E7EC6u, 0xE485B576u, 0x6D51C245u, 0x4FE1356Du,
    0xF25F1437u, 0x302B0A6Du, 0xCD3A431Bu, 0xEF9519B3u, 0x8E3404DDu, 0x514A0879u, 0x3B139B22u, 0x020BBEa6u,
    0x8A67CC74u, 0x29024E08u, 0x80DC1CD1u, 0xC4C6628Bu, 0x2168C234u, 0xC90FDAA2u, 0xFFFFFFFFu, 0xFFFFFFFFu,
}};

const pc_bignum group14_g = {{
    2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
}};

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

int bn_cmp_raw(const uint32_t *a, const uint32_t *b, int n)
{
    for (int i = n - 1; i >= 0; i--)
    {
        if (a[i] < b[i])
        {
            return -1;
        }
        if (a[i] > b[i])
        {
            return 1;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Public API (bn_from_bytes / bn_to_bytes / bn_cmp / ... shared both platforms)
// ---------------------------------------------------------------------------

void bn_from_bytes(pc_bignum *out, const uint8_t *bytes, size_t len)
{
    memset(out->d, 0, sizeof(pc_bignum));
    // bytes are big-endian; map to little-endian limbs.
    size_t blen = len < 256 ? len : 256;
    for (size_t i = 0; i < blen; i++)
    {
        size_t byte_pos = i; // byte i from LSB (bytes[len-1-i] is the i-th byte from the end)
        out->d[byte_pos / 4] |= (uint32_t)bytes[len - 1 - i] << ((byte_pos % 4) * 8);
    }
}

void bn_to_bytes(uint8_t bytes[256], const pc_bignum *in)
{
    for (int i = 0; i < PC_BN_LIMBS; i++)
    {
        uint32_t v = in->d[PC_BN_LIMBS - 1 - i];
        bytes[i * 4 + 0] = (uint8_t)(v >> 24);
        bytes[i * 4 + 1] = (uint8_t)(v >> 16);
        bytes[i * 4 + 2] = (uint8_t)(v >> 8);
        bytes[i * 4 + 3] = (uint8_t)(v);
    }
}

int bn_cmp(const pc_bignum *a, const pc_bignum *b)
{
    return bn_cmp_raw(a->d, b->d, PC_BN_LIMBS);
}

int bn_is_zero(const pc_bignum *a)
{
    for (int i = 0; i < PC_BN_LIMBS; i++)
    {
        if (a->d[i])
        {
            return 0;
        }
    }
    return 1;
}

int bn_dh_validate(const pc_bignum *v)
{
    // Must be > 1
    int ok = 0;
    for (int i = 1; i < PC_BN_LIMBS; i++)
    {
        if (v->d[i])
        {
            ok = 1;
            break;
        }
    }
    if (!ok && v->d[0] <= 1u)
    {
        return -1;
    }
    // Must be < p-1
    // p-1: subtract 1 from p
    pc_bignum pm1 = group14_p;
    pm1.d[0]--;
    if (bn_cmp(v, &pm1) >= 0)
    {
        return -1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Modular exponentiation: out = base^exp mod group14_p
// ---------------------------------------------------------------------------
