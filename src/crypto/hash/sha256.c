// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sha256.c
 * @brief SHA-256 implementation (FIPS 180-4).
 *
 * HW path: streaming + one-shot delegate to mbedtls. SW path: the implementation below. On
 * native builds the software path below is used. Shared by SSH, TLS 1.3 / QUIC / DTLS, SNMPv3, JWT,
 * CSRF, and SMB 2.x message signing.
 */

#include "crypto/hash/sha256.h"
#include "crypto/crypto_opt.h"
#include "mmgr/protomem.h"

#if PROTOCORE_HAS_HW_SHA
#include <mbedtls/sha256.h> // hardware SHA accelerator
#else
#include "mmgr/endian.h" // native software SHA-256
#endif
PROTOCORE_CRYPTO_HOT

#if PROTOCORE_HAS_HW_SHA

// ---------------------------------------------------------------------------
// HW path: streaming + one-shot via mbedtls.
// ---------------------------------------------------------------------------

void protocore_sha256_init(protocore_sha256_ctx *ctx, uint8_t *work)
{
    (void)work; // the accelerator carries its own
    mbedtls_sha256_init(&ctx->mbed);
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha256_starts(&ctx->mbed, 0 /* 0 = SHA-256 */);
#else
    mbedtls_sha256_starts_ret(&ctx->mbed, 0);
#endif
}

void protocore_sha256_update(protocore_sha256_ctx *ctx, const uint8_t *data, size_t len)
{
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha256_update(&ctx->mbed, data, len);
#else
    mbedtls_sha256_update_ret(&ctx->mbed, data, len);
#endif
}

void protocore_sha256_final(protocore_sha256_ctx *ctx, uint8_t digest[PROTOCORE_SHA256_DIGEST_LEN])
{
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha256_finish(&ctx->mbed, digest);
#else
    mbedtls_sha256_finish_ret(&ctx->mbed, digest);
#endif
    mbedtls_sha256_free(&ctx->mbed);
}

void protocore_sha256(const uint8_t *data, size_t len, uint8_t digest[PROTOCORE_SHA256_DIGEST_LEN])
{
    (void)mbedtls_sha256(data, len, digest, 0 /* 0 = SHA-256, 1 = SHA-224 */);
}

#else // native software path

// ---------------------------------------------------------------------------
// SW path: software SHA-256 (FIPS 180-4).
// ---------------------------------------------------------------------------

static const uint32_t K256[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

// Where the 64-bit message length sits in the final block (FIPS 180-4 §5.1.1).
#define SHA256_LEN_OFF (PROTOCORE_SHA256_BLOCK_LEN - 8u)

// The caller's working bytes, split: the block as it arrives, the padded last one, and the state copy
// the padded blocks compress into so finalizing leaves the running hash alone.
#define SHA256_OFF_RX 0u
#define SHA256_OFF_TX (SHA256_OFF_RX + PROTOCORE_SHA256_BLOCK_LEN)
#define SHA256_OFF_STATE (SHA256_OFF_TX + PROTOCORE_SHA256_BLOCK_LEN)
static_assert(SHA256_OFF_STATE + sizeof(uint32_t) * 8 <= PROTOCORE_SHA256_BORROW,
              "PROTOCORE_SHA256_BORROW is short of the schedule and the two blocks - raise it in protocore_config.h, "
              "which derives PROTOCORE_SECURE_ARENA_SIZE from it");

static const uint32_t H0[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
};

static inline uint32_t rotr32(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32 - n));
}

// The six FIPS 180-4 §4.1.2 functions the rounds are built from.
static inline uint32_t sha256_ch(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (~x & z);
}
static inline uint32_t sha256_maj(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}
static inline uint32_t sha256_bsig0(uint32_t x)
{
    return rotr32(x, 2U) ^ rotr32(x, 13U) ^ rotr32(x, 22U);
}
static inline uint32_t sha256_bsig1(uint32_t x)
{
    return rotr32(x, 6U) ^ rotr32(x, 11U) ^ rotr32(x, 25U);
}
static inline uint32_t sha256_ssig0(uint32_t x)
{
    return rotr32(x, 7U) ^ rotr32(x, 18U) ^ (x >> 3U);
}
static inline uint32_t sha256_ssig1(uint32_t x)
{
    return rotr32(x, 17U) ^ rotr32(x, 19U) ^ (x >> 10U);
}

// Compress one 64-byte block into the running hash state h[0..7] (FIPS 180-4 §6.2.2). The caller
// handles padding and length so this sees full blocks only.
//
// The eight state words and the sixteen-word schedule are shift registers: a round carries the state
// along one place and consumes one schedule word. The state's period is eight and the schedule's is
// sixteen, and sixteen is a whole number of eights, so the compression repeats every sixteen rounds -
// written once and run four times with k stepping. Naming the register a round lands on IS the shift,
// so no word is ever moved and the schedule stays sixteen words rather than sixty-four.
static void sha256_block(uint32_t h[8], const uint8_t blk[PROTOCORE_SHA256_BLOCK_LEN])
{
    uint32_t m0 = protocore_rd32be(blk);
    uint32_t m1 = protocore_rd32be(blk + 4);
    uint32_t m2 = protocore_rd32be(blk + 8);
    uint32_t m3 = protocore_rd32be(blk + 12);
    uint32_t m4 = protocore_rd32be(blk + 16);
    uint32_t m5 = protocore_rd32be(blk + 20);
    uint32_t m6 = protocore_rd32be(blk + 24);
    uint32_t m7 = protocore_rd32be(blk + 28);
    uint32_t m8 = protocore_rd32be(blk + 32);
    uint32_t m9 = protocore_rd32be(blk + 36);
    uint32_t m10 = protocore_rd32be(blk + 40);
    uint32_t m11 = protocore_rd32be(blk + 44);
    uint32_t m12 = protocore_rd32be(blk + 48);
    uint32_t m13 = protocore_rd32be(blk + 52);
    uint32_t m14 = protocore_rd32be(blk + 56);
    uint32_t m15 = protocore_rd32be(blk + 60);

    uint32_t v0 = h[0];
    uint32_t v1 = h[1];
    uint32_t v2 = h[2];
    uint32_t v3 = h[3];
    uint32_t v4 = h[4];
    uint32_t v5 = h[5];
    uint32_t v6 = h[6];
    uint32_t v7 = h[7];

    // Four groups of sixteen. Each round writes exactly two state words: the one holding h becomes the
    // next a, the one holding d becomes the next e. The trailing expansion of the fourth group is
    // unread.
    const uint32_t *k = K256;
    for (int g = 0; g < 4; g++)
    {
        uint32_t t1_00 = v7 + sha256_bsig1(v4) + sha256_ch(v4, v5, v6) + k[0] + m0;
        uint32_t t2_00 = sha256_bsig0(v0) + sha256_maj(v0, v1, v2);
        v3 += t1_00;
        v7 = t1_00 + t2_00;

        uint32_t t1_01 = v6 + sha256_bsig1(v3) + sha256_ch(v3, v4, v5) + k[1] + m1;
        uint32_t t2_01 = sha256_bsig0(v7) + sha256_maj(v7, v0, v1);
        v2 += t1_01;
        v6 = t1_01 + t2_01;

        uint32_t t1_02 = v5 + sha256_bsig1(v2) + sha256_ch(v2, v3, v4) + k[2] + m2;
        uint32_t t2_02 = sha256_bsig0(v6) + sha256_maj(v6, v7, v0);
        v1 += t1_02;
        v5 = t1_02 + t2_02;

        uint32_t t1_03 = v4 + sha256_bsig1(v1) + sha256_ch(v1, v2, v3) + k[3] + m3;
        uint32_t t2_03 = sha256_bsig0(v5) + sha256_maj(v5, v6, v7);
        v0 += t1_03;
        v4 = t1_03 + t2_03;

        uint32_t t1_04 = v3 + sha256_bsig1(v0) + sha256_ch(v0, v1, v2) + k[4] + m4;
        uint32_t t2_04 = sha256_bsig0(v4) + sha256_maj(v4, v5, v6);
        v7 += t1_04;
        v3 = t1_04 + t2_04;

        uint32_t t1_05 = v2 + sha256_bsig1(v7) + sha256_ch(v7, v0, v1) + k[5] + m5;
        uint32_t t2_05 = sha256_bsig0(v3) + sha256_maj(v3, v4, v5);
        v6 += t1_05;
        v2 = t1_05 + t2_05;

        uint32_t t1_06 = v1 + sha256_bsig1(v6) + sha256_ch(v6, v7, v0) + k[6] + m6;
        uint32_t t2_06 = sha256_bsig0(v2) + sha256_maj(v2, v3, v4);
        v5 += t1_06;
        v1 = t1_06 + t2_06;

        uint32_t t1_07 = v0 + sha256_bsig1(v5) + sha256_ch(v5, v6, v7) + k[7] + m7;
        uint32_t t2_07 = sha256_bsig0(v1) + sha256_maj(v1, v2, v3);
        v4 += t1_07;
        v0 = t1_07 + t2_07;

        uint32_t t1_08 = v7 + sha256_bsig1(v4) + sha256_ch(v4, v5, v6) + k[8] + m8;
        uint32_t t2_08 = sha256_bsig0(v0) + sha256_maj(v0, v1, v2);
        v3 += t1_08;
        v7 = t1_08 + t2_08;

        uint32_t t1_09 = v6 + sha256_bsig1(v3) + sha256_ch(v3, v4, v5) + k[9] + m9;
        uint32_t t2_09 = sha256_bsig0(v7) + sha256_maj(v7, v0, v1);
        v2 += t1_09;
        v6 = t1_09 + t2_09;

        uint32_t t1_10 = v5 + sha256_bsig1(v2) + sha256_ch(v2, v3, v4) + k[10] + m10;
        uint32_t t2_10 = sha256_bsig0(v6) + sha256_maj(v6, v7, v0);
        v1 += t1_10;
        v5 = t1_10 + t2_10;

        uint32_t t1_11 = v4 + sha256_bsig1(v1) + sha256_ch(v1, v2, v3) + k[11] + m11;
        uint32_t t2_11 = sha256_bsig0(v5) + sha256_maj(v5, v6, v7);
        v0 += t1_11;
        v4 = t1_11 + t2_11;

        uint32_t t1_12 = v3 + sha256_bsig1(v0) + sha256_ch(v0, v1, v2) + k[12] + m12;
        uint32_t t2_12 = sha256_bsig0(v4) + sha256_maj(v4, v5, v6);
        v7 += t1_12;
        v3 = t1_12 + t2_12;

        uint32_t t1_13 = v2 + sha256_bsig1(v7) + sha256_ch(v7, v0, v1) + k[13] + m13;
        uint32_t t2_13 = sha256_bsig0(v3) + sha256_maj(v3, v4, v5);
        v6 += t1_13;
        v2 = t1_13 + t2_13;

        uint32_t t1_14 = v1 + sha256_bsig1(v6) + sha256_ch(v6, v7, v0) + k[14] + m14;
        uint32_t t2_14 = sha256_bsig0(v2) + sha256_maj(v2, v3, v4);
        v5 += t1_14;
        v1 = t1_14 + t2_14;

        uint32_t t1_15 = v0 + sha256_bsig1(v5) + sha256_ch(v5, v6, v7) + k[15] + m15;
        uint32_t t2_15 = sha256_bsig0(v1) + sha256_maj(v1, v2, v3);
        v4 += t1_15;
        v0 = t1_15 + t2_15;

        k += 16;

        // W[i] = W[i-16] + sigma0(W[i-15]) + W[i-7] + sigma1(W[i-2]), in place: the slot being written
        // still holds W[i-16], and the three it reads are the window's other places.
        m0 += sha256_ssig0(m1) + m9 + sha256_ssig1(m14);
        m1 += sha256_ssig0(m2) + m10 + sha256_ssig1(m15);
        m2 += sha256_ssig0(m3) + m11 + sha256_ssig1(m0);
        m3 += sha256_ssig0(m4) + m12 + sha256_ssig1(m1);
        m4 += sha256_ssig0(m5) + m13 + sha256_ssig1(m2);
        m5 += sha256_ssig0(m6) + m14 + sha256_ssig1(m3);
        m6 += sha256_ssig0(m7) + m15 + sha256_ssig1(m4);
        m7 += sha256_ssig0(m8) + m0 + sha256_ssig1(m5);
        m8 += sha256_ssig0(m9) + m1 + sha256_ssig1(m6);
        m9 += sha256_ssig0(m10) + m2 + sha256_ssig1(m7);
        m10 += sha256_ssig0(m11) + m3 + sha256_ssig1(m8);
        m11 += sha256_ssig0(m12) + m4 + sha256_ssig1(m9);
        m12 += sha256_ssig0(m13) + m5 + sha256_ssig1(m10);
        m13 += sha256_ssig0(m14) + m6 + sha256_ssig1(m11);
        m14 += sha256_ssig0(m15) + m7 + sha256_ssig1(m12);
        m15 += sha256_ssig0(m0) + m8 + sha256_ssig1(m13);
    }

    // Feed-forward: sixty-four rounds is eight whole state periods, so v0..v7 are a..h again.
    h[0] += v0;
    h[1] += v1;
    h[2] += v2;
    h[3] += v3;
    h[4] += v4;
    h[5] += v5;
    h[6] += v6;
    h[7] += v7;
}

void protocore_sha256_init(protocore_sha256_ctx *ctx, uint8_t *work)
{
    mem.cpy(ctx->s, H0, sizeof(H0));
    ctx->n = 0;
    ctx->rx = work + SHA256_OFF_RX;
    ctx->tx = work + SHA256_OFF_TX;
    ctx->fs = (uint32_t *)(work + SHA256_OFF_STATE);
    ctx->rxlen = 0;
}

void protocore_sha256_update(protocore_sha256_ctx *ctx, const uint8_t *data, size_t len)
{
    ctx->n += len;
    while (len > 0)
    {
        uint32_t take = PROTOCORE_SHA256_BLOCK_LEN - ctx->rxlen;
        if (len < take)
        {
            take = (uint32_t)len;
        }
        // rx + rxlen carries no alignment, so this is the raw mover, not the aligned-span one.
        uint8_t *fill = ctx->rx + ctx->rxlen;
        proto_raw_read(fill, data, take);
        ctx->rxlen += take;
        data += take;
        len -= take;
        if (ctx->rxlen == PROTOCORE_SHA256_BLOCK_LEN)
        {
            sha256_block(ctx->s, ctx->rx);
            ctx->rxlen = 0;
        }
    }
}

void protocore_sha256_final(protocore_sha256_ctx *ctx, uint8_t digest[PROTOCORE_SHA256_DIGEST_LEN])
{
    uint64_t bitlen = ctx->n << 3;

    // The padded blocks compress into a copy of the state, so s, rx, rxlen and n all come out of this
    // untouched and the hash can keep taking data afterwards.
    mem.cpy(ctx->fs, ctx->s, sizeof(ctx->s));

    // The last block is composed in tx, whole: what rx holds, the mark, zeros, and the length. rx is
    // read and never written back, so nothing it still carries from an earlier block reaches the wire.
    mem.zero(ctx->tx, PROTOCORE_SHA256_BLOCK_LEN);
    mem.cpy(ctx->tx, ctx->rx, ctx->rxlen);
    ctx->tx[ctx->rxlen] = 0x80;

    // The bit length occupies the block's last 8 bytes, so a mark at or past that offset takes its own.
    if (ctx->rxlen >= SHA256_LEN_OFF)
    {
        sha256_block(ctx->fs, ctx->tx);
        mem.zero(ctx->tx, PROTOCORE_SHA256_BLOCK_LEN);
    }

    protocore_wr64be(ctx->tx + SHA256_LEN_OFF, bitlen);
    sha256_block(ctx->fs, ctx->tx);

    protocore_wr32be(digest, ctx->fs[0]);
    protocore_wr32be(digest + 4, ctx->fs[1]);
    protocore_wr32be(digest + 8, ctx->fs[2]);
    protocore_wr32be(digest + 12, ctx->fs[3]);
    protocore_wr32be(digest + 16, ctx->fs[4]);
    protocore_wr32be(digest + 20, ctx->fs[5]);
    protocore_wr32be(digest + 24, ctx->fs[6]);
    protocore_wr32be(digest + 28, ctx->fs[7]);
}

void protocore_sha256(uint8_t *work, const uint8_t *data, size_t len, uint8_t digest[PROTOCORE_SHA256_DIGEST_LEN])
{
    protocore_sha256_ctx ctx = {0};
    protocore_sha256_init(&ctx, work);
    protocore_sha256_update(&ctx, data, len);
    protocore_sha256_final(&ctx, digest);
}

#endif // !PROTOCORE_HAS_HW_SHA (SW path)
