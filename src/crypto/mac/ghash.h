// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ghash.h
 * @brief GHASH (the GF(2^128) universal hash under AES-GCM, NIST SP 800-38D sec 6.3), 4-bit table.
 *
 * Shared by pc_aesgcm (AES-256-GCM) and aes128gcm (AES-128-GCM, also DTLS 1.3). The textbook GHASH
 * is a 128-iteration bitwise GF(2^128) multiply per 16-byte block (~3,700 cyc/byte on an ESP32-S3),
 * which made AES-GCM ~350x slower than raw AES-CTR and is the throughput floor of every AEAD record
 * layer (measured, docs/FEATURE_PERFORMANCE.md). There is no hardware GF-multiply on the S3 (unlike
 * the RSA/MPI MODMULT that accelerates curve25519), so the lever is algorithmic: the standard 4-bit
 * table method (Shoup) - build a 16-entry table of i*H once per key, then fold four bits of the
 * accumulator per step.
 *
 * The 128-bit state is held as FOUR uint32 words (z[0] most significant), NOT two uint64: on the
 * 32-bit Xtensa LX7 a uint64 `>>4`/`<<60` compiles to a libgcc call (__lshrdi3/__ashldi3), which
 * dominated an early uint64 version (~8,100 cyc/block); the 32-bit-word shifts are native register
 * ops. Byte-exact versus the bitwise reference (the NIST/McGrew GCM KATs plus a direct fuzz
 * cross-check, test_ssh_crypto). Header-only, zero heap, deterministic; the 256-byte table lives in
 * the per-direction context (SSH, built once at key install) or on the stack (QUIC/DTLS, per packet).
 */

#ifndef PROTOCORE_GHASH_H
#define PROTOCORE_GHASH_H

#include "mmgr/endian.h"
#include "protocore_config.h" // the entry point: PC_INLINE, and types.h for the widths

/** @brief 4-bit GHASH table for a fixed subkey H = E(K, 0^128): M[i] = i*H as four big-endian
 *         uint32 words (M[i][0] most significant). 256 bytes. */
typedef struct
{
    uint32_t M[16][4];
} GhashKey;

/** @brief Build the 4-bit multiplication table from the 16-byte subkey @p h. Call once per key. */
PC_INLINE void ghash_key_init(GhashKey *t, const uint8_t h[16])
{
    // M[8] = H; M[4]=H/x, M[2]=H/x^2, M[1]=H/x^3 (one GF right-shift each, reducing by R=0xe1<<120).
    uint32_t z0 = pc_rd32be(h);
    uint32_t z1 = pc_rd32be(h + 4);
    uint32_t z2 = pc_rd32be(h + 8);
    uint32_t z3 = pc_rd32be(h + 12);
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
        z0 = (z0 >> 1) ^ (lsb ? 0xe1000000u : 0u);
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

/** @brief acc = acc * H in GF(2^128) with the GCM reduction, using the precomputed table @p t. */
PC_INLINE void ghash_mul(const GhashKey *t, uint8_t acc[16])
{
    // Reduction contribution (into the top 16 bits of word 0) of the low nibble shifted out per step.
    static const uint16_t LAST4[16] = {0x0000, 0x1c20, 0x3840, 0x2460, 0x7080, 0x6ca0, 0x48c0, 0x54e0,
                                       0xe100, 0xfd20, 0xd940, 0xc560, 0x9180, 0x8da0, 0xa9c0, 0xb5e0};
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
    pc_wr32be(acc, z0);
    pc_wr32be(acc + 4, z1);
    pc_wr32be(acc + 8, z2);
    pc_wr32be(acc + 12, z3);
}

/** @brief Fold @p len bytes of @p data into @p acc: acc = (acc XOR block) * H per 16 bytes, a final
 *         short block MSB-zero-padded. */
PC_INLINE void ghash_update(const GhashKey *t, uint8_t acc[16], const uint8_t *data, size_t len)
{
    size_t off = 0;
    while (off < len)
    {
        size_t take = len - off;
        if (take > 16)
        {
            take = 16;
        }
        for (size_t i = 0; i < take; i++)
        {
            acc[i] ^= data[off + i];
        }
        ghash_mul(t, acc);
        off += take;
    }
}

#endif // PROTOCORE_GHASH_H
