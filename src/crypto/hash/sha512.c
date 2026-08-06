// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sha512.c
 * @brief SHA-512 implementation (FIPS 180-4).
 *
 * HW path: streaming + one-shot delegate to mbedtls. SW path: the implementation below,
 * is used. Shared by SSH (Ed25519 / kex), PQC, and SMB 3.1.1 preauth integrity.
 */

#include "crypto/hash/sha512.h"
#include "crypto/crypto_opt.h"

#if PROTOCORE_HOT
#include <mbedtls/sha512.h> // hardware SHA accelerator
#else
#include "mmgr/endian.h" // native software SHA-512
#endif
PC_CRYPTO_HOT

#if PROTOCORE_HOT

// ---------------------------------------------------------------------------
// HW path: streaming + one-shot via mbedtls.
// ---------------------------------------------------------------------------

void pc_sha512_init(pc_sha512_ctx *ctx)
{
    mbedtls_sha512_init(&ctx->mbed);
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha512_starts(&ctx->mbed, 0 /* 0 = SHA-512 */);
#else
    mbedtls_sha512_starts_ret(&ctx->mbed, 0);
#endif
}

void pc_sha512_update(pc_sha512_ctx *ctx, const uint8_t *data, size_t len)
{
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha512_update(&ctx->mbed, data, len);
#else
    mbedtls_sha512_update_ret(&ctx->mbed, data, len);
#endif
}

void pc_sha512_final(pc_sha512_ctx *ctx, uint8_t digest[PC_SHA512_DIGEST_LEN])
{
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha512_finish(&ctx->mbed, digest);
#else
    mbedtls_sha512_finish_ret(&ctx->mbed, digest);
#endif
    mbedtls_sha512_free(&ctx->mbed);
}

void pc_sha512(const uint8_t *data, size_t len, uint8_t digest[PC_SHA512_DIGEST_LEN])
{
    (void)mbedtls_sha512(data, len, digest, 0 /* 0 = SHA-512, 1 = SHA-384 */);
}

#else // native software path

// ---------------------------------------------------------------------------
// SW path: software SHA-512 (FIPS 180-4).
// ---------------------------------------------------------------------------

static const uint64_t K512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL,
    0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
    0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL, 0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
    0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL, 0x983e5152ee66dfabULL,
    0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL,
    0x53380d139d95b3dfULL, 0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
    0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL, 0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
    0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL,
    0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL,
    0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
    0x113f9804bef90daeULL, 0x1b710b35131c471bULL, 0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
    0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

static const uint64_t H0[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};

static inline uint64_t rotr64(uint64_t x, unsigned n)
{
    return (x >> n) | (x << (64 - n));
}

// Compress one 128-byte block into the running state h[0..7] (FIPS 180-4 §6.4.2).
static void sha512_block(uint64_t h[8], const uint8_t blk[128])
{
    uint64_t W[80];
    for (int i = 0; i < 16; i++)
    {
        W[i] = pc_rd64be(blk + i * 8);
    }
    for (int i = 16; i < 80; i++)
    {
        uint64_t s0 = rotr64(W[i - 15], 1) ^ rotr64(W[i - 15], 8) ^ (W[i - 15] >> 7); // σ0
        uint64_t s1 = rotr64(W[i - 2], 19) ^ rotr64(W[i - 2], 61) ^ (W[i - 2] >> 6);  // σ1
        W[i] = W[i - 16] + s0 + W[i - 7] + s1;
    }

    uint64_t a = h[0];
    uint64_t b = h[1];
    uint64_t c = h[2];
    uint64_t d = h[3];
    uint64_t e = h[4];
    uint64_t f = h[5];
    uint64_t g = h[6];
    uint64_t hh = h[7];

    for (int i = 0; i < 80; i++)
    {
        uint64_t S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41); // Σ1
        uint64_t ch = (e & f) ^ (~e & g);                            // Ch(e,f,g)
        uint64_t t1 = hh + S1 + ch + K512[i] + W[i];                 // T1
        uint64_t S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39); // Σ0
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);                  // Maj(a,b,c)
        uint64_t t2 = S0 + maj;                                      // T2
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

void pc_sha512_init(pc_sha512_ctx *ctx)
{
    for (int i = 0; i < 8; i++)
    {
        ctx->s[i] = H0[i];
    }
    ctx->n = 0;
    ctx->buflen = 0;
    memset(ctx->buf, 0, sizeof(ctx->buf));
}

void pc_sha512_update(pc_sha512_ctx *ctx, const uint8_t *data, size_t len)
{
    ctx->n += len;
    while (len > 0)
    {
        uint32_t space = 128 - ctx->buflen;
        uint32_t take = (uint32_t)len < space ? (uint32_t)len : space;
        memcpy(ctx->buf + ctx->buflen, data, take);
        ctx->buflen += take;
        data += take;
        len -= take;
        if (ctx->buflen == 128)
        {
            sha512_block(ctx->s, ctx->buf);
            ctx->buflen = 0;
        }
    }
}

void pc_sha512_final(pc_sha512_ctx *ctx, uint8_t digest[PC_SHA512_DIGEST_LEN])
{
    // 128-bit length in bits. Our byte count fits a uint64, so the high word is n >> 61 (bits above 64)
    // and the low word is n << 3.
    uint64_t len_hi = ctx->n >> 61;
    uint64_t len_lo = ctx->n << 3;

    ctx->buf[ctx->buflen++] = 0x80;

    if (ctx->buflen > 112)
    {
        while (ctx->buflen < 128)
        {
            ctx->buf[ctx->buflen++] = 0x00;
        }
        sha512_block(ctx->s, ctx->buf);
        ctx->buflen = 0;
    }
    while (ctx->buflen < 112)
    {
        ctx->buf[ctx->buflen++] = 0x00;
    }

    pc_wr64be(ctx->buf + 112, len_hi);
    pc_wr64be(ctx->buf + 120, len_lo);
    sha512_block(ctx->s, ctx->buf);

    for (int i = 0; i < 8; i++)
    {
        pc_wr64be(digest + i * 8, ctx->s[i]);
    }
}

void pc_sha512(const uint8_t *data, size_t len, uint8_t digest[PC_SHA512_DIGEST_LEN])
{
    pc_sha512_ctx ctx;
    pc_sha512_init(&ctx);
    pc_sha512_update(&ctx, data, len);
    pc_sha512_final(&ctx, digest);
}

#endif // !PROTOCORE_HOT (SW path)
