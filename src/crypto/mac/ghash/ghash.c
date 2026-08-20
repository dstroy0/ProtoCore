// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ghash.c
 * @brief GHASH implementation (NIST SP 800-38D sec 6.3), 4-bit table. See ghash.h.
 *
 * One set of entries and one arm: no die in the list carries a GF(2^128) multiply, so the table build
 * and the folds below are the whole implementation.
 *
 * The 128-bit state is held as FOUR uint32 words (z[0] most significant), NOT two uint64: on a 32-bit
 * machine a uint64 >>4 / <<60 compiles to a libgcc call, where the 32-bit-word shifts are native
 * register ops.
 *
 * The context is this file's. The module's own borrow carries the table a bound subkey builds, so the
 * entries reach it without a caller naming it; the accumulator a fold runs in is the caller's, named by
 * the args members.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_GHASH

#include "crypto/crypto_opt.h"
#include "crypto/mac/ghash/ghash.h"
#include "mmgr/endian/endian.h" // protocore_rd32be / protocore_wr32be

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// The one definition, private to this TU. It sits at GHASH_OFF_CTX in the caller's borrow, so its size
// never leaves this file and no consumer can name it. The table is what a key_init leaves behind and
// what every fold reads, so it is all the context carries: M[i] = i*H as four big-endian uint32 words.
typedef struct
{
    uint32_t M[16][4];
} GhashCtx;

// The caller's borrow, split: the table at the base. Nothing else is carried across a call.
#define GHASH_OFF_CTX 0u
#define GHASH_OFF_END (GHASH_OFF_CTX + sizeof(GhashCtx))
static_assert(GHASH_OFF_END <= PROTOCORE_GHASH_BORROW,
              "PROTOCORE_GHASH_BORROW is short of the 4-bit table - raise it in protocore_config.h, "
              "which derives PROTOCORE_SECURE_ARENA_SIZE from it");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(GHASH_OFF_CTX % _Alignof(GhashCtx) == 0,
              "GHASH_OFF_CTX is not a multiple of alignof(GhashCtx) - GHASH_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define GHASH_CTX(w) ((GhashCtx *)(void *)((w) + GHASH_OFF_CTX))

// Reduction contribution (into the top 16 bits of word 0) of the low nibble shifted out per step.
static const uint16_t LAST4[16] = {0x0000, 0x1c20, 0x3840, 0x2460, 0x7080, 0x6ca0, 0x48c0, 0x54e0,
                                   0xe100, 0xfd20, 0xd940, 0xc560, 0x9180, 0x8da0, 0xa9c0, 0xb5e0};

// Build the 4-bit multiplication table in the borrow from the subkey the args name.
static void gf_key_init(uint8_t *restrict work)
{
    GhashCtx *t = GHASH_CTX(work);
    const uint8_t *h = Ghash.key_args.h;
    // M[8] = H; M[4]=H/x, M[2]=H/x^2, M[1]=H/x^3 (one GF right-shift each, reducing by R=0xe1<<120).
    uint32_t z0 = protocore_rd32be(h);
    uint32_t z1 = protocore_rd32be(h + 4);
    uint32_t z2 = protocore_rd32be(h + 8);
    uint32_t z3 = protocore_rd32be(h + 12);
    t->M[8][0] = z0;
    t->M[8][1] = z1;
    t->M[8][2] = z2;
    t->M[8][3] = z3;
    t->M[0][0] = 0;
    t->M[0][1] = 0;
    t->M[0][2] = 0;
    t->M[0][3] = 0;
    for (int i = 4; i > 0; i >>= 1)
    {
        uint32_t lsb = z3 & 1u;
        z3 = (z3 >> 1) | (z2 << 31);
        z2 = (z2 >> 1) | (z1 << 31);
        z1 = (z1 >> 1) | (z0 << 31);
        z0 = (z0 >> 1) ^ (0xe1000000u & (0u - lsb)); // lsb is 0 or 1, so the negation is the mask
        t->M[i][0] = z0;
        t->M[i][1] = z1;
        t->M[i][2] = z2;
        t->M[i][3] = z3;
    }
    // Composite entries: (i + j) * H = i*H XOR j*H (i a power of two, 0 < j < i).
    for (int i = 2; i < 16; i <<= 1)
    {
        for (int j = 1; j < i; j++)
        {
            t->M[i + j][0] = t->M[i][0] ^ t->M[j][0];
            t->M[i + j][1] = t->M[i][1] ^ t->M[j][1];
            t->M[i + j][2] = t->M[i][2] ^ t->M[j][2];
            t->M[i + j][3] = t->M[i][3] ^ t->M[j][3];
        }
    }
}

// acc = acc * H in GF(2^128) with the GCM reduction, under the table in the borrow. The accumulator is
// the caller's own buffer, so it is the one operand that stays a parameter.
static void gf_mul(uint8_t *restrict work, uint8_t *acc)
{
    const GhashCtx *t = GHASH_CTX(work);
    uint8_t idx = acc[15] & 0x0f;
    uint32_t z0 = t->M[idx][0];
    uint32_t z1 = t->M[idx][1];
    uint32_t z2 = t->M[idx][2];
    uint32_t z3 = t->M[idx][3];
    for (int i = 15; i >= 0; i--)
    {
        uint8_t lo = acc[i] & 0x0f;
        uint8_t hi = (acc[i] >> 4) & 0x0f;
        if (i != 15)
        {
            uint32_t rem = z3 & 0x0f;
            z3 = (z3 >> 4) | (z2 << 28);
            z2 = (z2 >> 4) | (z1 << 28);
            z1 = (z1 >> 4) | (z0 << 28);
            z0 = (z0 >> 4) ^ ((uint32_t)LAST4[rem] << 16);
            z0 ^= t->M[lo][0];
            z1 ^= t->M[lo][1];
            z2 ^= t->M[lo][2];
            z3 ^= t->M[lo][3];
        }
        uint32_t rem = z3 & 0x0f;
        z3 = (z3 >> 4) | (z2 << 28);
        z2 = (z2 >> 4) | (z1 << 28);
        z1 = (z1 >> 4) | (z0 << 28);
        z0 = (z0 >> 4) ^ ((uint32_t)LAST4[rem] << 16);
        z0 ^= t->M[hi][0];
        z1 ^= t->M[hi][1];
        z2 ^= t->M[hi][2];
        z3 ^= t->M[hi][3];
    }
    protocore_wr32be(acc, z0);
    protocore_wr32be(acc + 4, z1);
    protocore_wr32be(acc + 8, z2);
    protocore_wr32be(acc + 12, z3);
}

// --- the entries -----------------------------------------------------------

static void ghash_key_init(uint8_t *restrict work)
{
    Ghash.ok = PROTO_FALSE;
    if (!Ghash.key_args.h)
    {
        return;
    }
    gf_key_init(work);
    Ghash.ok = PROTO_TRUE;
}

static void ghash_mul(uint8_t *restrict work)
{
    Ghash.ok = PROTO_FALSE;
    if (!Ghash.mul_args.acc)
    {
        return;
    }
    gf_mul(work, Ghash.mul_args.acc);
    Ghash.ok = PROTO_TRUE;
}

// acc = (acc XOR block) * H per 16 bytes, a final short block MSB-zero-padded.
static void ghash_update(uint8_t *restrict work)
{
    Ghash.ok = PROTO_FALSE;
    if (!Ghash.update_args.acc)
    {
        return;
    }
    uint8_t *acc = Ghash.update_args.acc;
    const uint8_t *data = Ghash.update_args.data;
    const size_t len = Ghash.update_args.len;
    size_t off = 0;
    while (off < len)
    {
        size_t take = len - off;
        if (take > PROTOCORE_GHASH_ACC_LEN)
        {
            take = PROTOCORE_GHASH_ACC_LEN;
        }
        for (size_t i = 0; i < take; i++)
        {
            acc[i] ^= data[off + i];
        }
        gf_mul(work, acc);
        off += take;
    }
    Ghash.ok = PROTO_TRUE;
}

GhashNs Ghash = {.key_init = ghash_key_init, .mul = ghash_mul, .update = ghash_update};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_GHASH
