// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#if PROTOCORE_HOT
#include <mbedtls/sha256.h> // hardware SHA accelerator
#else
#include "mmgr/endian.h" // native software SHA-256
#endif
PC_CRYPTO_HOT

#if PROTOCORE_HOT

// ---------------------------------------------------------------------------
// HW path: streaming + one-shot via mbedtls.
// ---------------------------------------------------------------------------

void pc_sha256_init(pc_sha256_ctx *ctx)
{
    mbedtls_sha256_init(&ctx->mbed);
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha256_starts(&ctx->mbed, 0 /* 0 = SHA-256 */);
#else
    mbedtls_sha256_starts_ret(&ctx->mbed, 0);
#endif
}

void pc_sha256_update(pc_sha256_ctx *ctx, const uint8_t *data, size_t len)
{
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha256_update(&ctx->mbed, data, len);
#else
    mbedtls_sha256_update_ret(&ctx->mbed, data, len);
#endif
}

void pc_sha256_final(pc_sha256_ctx *ctx, uint8_t digest[PC_SHA256_DIGEST_LEN])
{
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha256_finish(&ctx->mbed, digest);
#else
    mbedtls_sha256_finish_ret(&ctx->mbed, digest);
#endif
    mbedtls_sha256_free(&ctx->mbed);
}

void pc_sha256(const uint8_t *data, size_t len, uint8_t digest[PC_SHA256_DIGEST_LEN])
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

static const uint32_t H0[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
};

static inline uint32_t rotr32(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32 - n));
}

// Compress one 64-byte block into the running hash state h[0..7] (FIPS 180-4 §6.2.2). The caller
// handles padding and length so this sees full blocks only.
static void sha256_block(uint32_t h[8], const uint8_t blk[64])
{
    // Message schedule W[0..63]. The first 16 words are the block read as big-endian; the rest are
    // extended with the sigma-0/sigma-1 recurrence.
    uint32_t W[64];
    for (int i = 0; i < 16; i++)
    {
        W[i] = pc_rd32be(blk + i * 4);
    }
    for (int i = 16; i < 64; i++)
    {
        uint32_t s0 = rotr32(W[i - 15], 7U) ^ rotr32(W[i - 15], 18U) ^ (W[i - 15] >> 3U); // σ0
        uint32_t s1 = rotr32(W[i - 2], 17U) ^ rotr32(W[i - 2], 19U) ^ (W[i - 2] >> 10U);  // σ1
        W[i] = W[i - 16] + s0 + W[i - 7] + s1;
    }

    // Working variables seeded from the current hash state.
    uint32_t a = h[0];
    uint32_t b = h[1];
    uint32_t c = h[2];
    uint32_t d = h[3];
    uint32_t e = h[4];
    uint32_t f = h[5];
    uint32_t g = h[6];
    uint32_t hh = h[7];

    // 64 compression rounds. Each mixes in one schedule word W[i] and round constant K256[i] using the
    // Ch/Maj choice/majority functions and the Sigma-0/Sigma-1 rotations.
    for (int i = 0; i < 64; i++)
    {
        uint32_t S1 = rotr32(e, 6U) ^ rotr32(e, 11U) ^ rotr32(e, 25U); // Σ1
        uint32_t ch = (e & f) ^ (~e & g);                              // Ch(e,f,g)
        uint32_t tmp1 = hh + S1 + ch + K256[i] + W[i];                 // T1
        uint32_t S0 = rotr32(a, 2U) ^ rotr32(a, 13U) ^ rotr32(a, 22U); // Σ0
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);                    // Maj(a,b,c)
        uint32_t tmp2 = S0 + maj;                                      // T2
        // Rotate the eight working variables; only e and a take new values.
        hh = g;
        g = f;
        f = e;
        e = d + tmp1;
        d = c;
        c = b;
        b = a;
        a = tmp1 + tmp2;
    }

    // Feed-forward: add the compressed working variables back into the state.
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

void pc_sha256_init(pc_sha256_ctx *ctx)
{
    for (int i = 0; i < 8; i++)
    {
        ctx->s[i] = H0[i];
    }
    ctx->n = 0;
    ctx->buflen = 0;
    memset(ctx->buf, 0, sizeof(ctx->buf));
}

void pc_sha256_update(pc_sha256_ctx *ctx, const uint8_t *data, size_t len)
{
    ctx->n += len;
    while (len > 0)
    {
        uint32_t space = 64 - ctx->buflen;
        uint32_t take = (uint32_t)len < space ? (uint32_t)len : space;
        memcpy(ctx->buf + ctx->buflen, data, take);
        ctx->buflen += take;
        data += take;
        len -= take;
        if (ctx->buflen == 64)
        {
            sha256_block(ctx->s, ctx->buf);
            ctx->buflen = 0;
        }
    }
}

void pc_sha256_final(pc_sha256_ctx *ctx, uint8_t digest[PC_SHA256_DIGEST_LEN])
{
    uint64_t bitlen = ctx->n * 8;

    ctx->buf[ctx->buflen++] = 0x80;

    if (ctx->buflen > 56)
    {
        while (ctx->buflen < 64)
        {
            ctx->buf[ctx->buflen++] = 0x00;
        }
        sha256_block(ctx->s, ctx->buf);
        ctx->buflen = 0;
    }

    while (ctx->buflen < 56)
    {
        ctx->buf[ctx->buflen++] = 0x00;
    }

    pc_wr64be(ctx->buf + 56, bitlen);
    sha256_block(ctx->s, ctx->buf);

    for (int i = 0; i < 8; i++)
    {
        pc_wr32be(digest + i * 4, ctx->s[i]);
    }
}

void pc_sha256(const uint8_t *data, size_t len, uint8_t digest[PC_SHA256_DIGEST_LEN])
{
    pc_sha256_ctx ctx;
    pc_sha256_init(&ctx);
    pc_sha256_update(&ctx, data, len);
    pc_sha256_final(&ctx, digest);
}

#endif // !PROTOCORE_HOT (SW path)
