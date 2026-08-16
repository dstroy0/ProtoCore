// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file host_sha_hal.c
 * @brief The host arm of the SHA accelerator. See host_sha_hal.h.
 *
 * One compression per family (FIPS 180-4 sec 6.1.2, 6.2.2, 6.4.2), over the block the caller hands
 * in. The block arrives as words in memory order and the peripheral reads it big-endian, so the
 * message schedule is built by assembling the bytes rather than by reading the words directly.
 */

#include "core_setup/hal/host/host_sha_hal.h"

#if PROTOCORE_HOST && PROTOCORE_HAS_HW_SHA

PROTOCORE_BEGIN_DECLS

static const uint32_t IV1[5] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u};

static const uint32_t IV224[8] = {0xc1059ed8u, 0x367cd507u, 0x3070dd17u, 0xf70e5939u,
                                  0xffc00b31u, 0x68581511u, 0x64f98fa7u, 0xbefa4fa4u};

static const uint32_t IV256[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                  0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

static const uint64_t IV384[8] = {0xcbbb9d5dc1059ed8ull, 0x629a292a367cd507ull, 0x9159015a3070dd17ull,
                                  0x152fecd8f70e5939ull, 0x67332667ffc00b31ull, 0x8eb44a8768581511ull,
                                  0xdb0c2e0d64f98fa7ull, 0x47b5481dbefa4fa4ull};

static const uint64_t IV512[8] = {0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull,
                                  0xa54ff53a5f1d36f1ull, 0x510e527fade682d1ull, 0x9b05688c2b3e6c1full,
                                  0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull};

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

static const uint64_t K512[80] = {
    0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull,
    0x3956c25bf348b538ull, 0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull,
    0xd807aa98a3030242ull, 0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
    0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull, 0xc19bf174cf692694ull,
    0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
    0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
    0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull,
    0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull, 0x06ca6351e003826full, 0x142929670a0e6e70ull,
    0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
    0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
    0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull,
    0xd192e819d6ef5218ull, 0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
    0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull,
    0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull,
    0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
    0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull,
    0xca273eceea26619cull, 0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull,
    0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
    0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull,
    0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull,
};

static uint32_t rol32(uint32_t x, unsigned n)
{
    return (x << n) | (x >> (32u - n));
}

static uint32_t ror32(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32u - n));
}

static uint64_t ror64(uint64_t x, unsigned n)
{
    return (x >> n) | (x << (64u - n));
}

// The block as the peripheral reads it: message bytes in memory order, assembled big-endian.
static uint32_t be32_at(const uint32_t *blk, unsigned i)
{
    const uint8_t *b = (const uint8_t *)(const void *)blk + 4u * i;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

static uint64_t be64_at(const uint32_t *blk, unsigned i)
{
    return ((uint64_t)be32_at(blk, 2u * i) << 32) | (uint64_t)be32_at(blk, 2u * i + 1u);
}

// FIPS 180-4 sec 6.1.2: eighty rounds over a five-word state.
static void sha1_compress(uint32_t *h, const uint32_t *blk)
{
    uint32_t w[80];
    for (unsigned i = 0; i < 16u; i++)
    {
        w[i] = be32_at(blk, i);
    }
    for (unsigned i = 16u; i < 80u; i++)
    {
        w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1u);
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (unsigned i = 0; i < 80u; i++)
    {
        uint32_t f, k;
        if (i < 20u)
        {
            f = (b & c) | (~b & d);
            k = 0x5a827999u;
        }
        else if (i < 40u)
        {
            f = b ^ c ^ d;
            k = 0x6ed9eba1u;
        }
        else if (i < 60u)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdcu;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xca62c1d6u;
        }
        uint32_t t = rol32(a, 5u) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol32(b, 30u);
        b = a;
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

// FIPS 180-4 sec 6.2.2: sixty-four rounds over an eight-word state. SHA-224 differs only by its IV.
static void sha256_compress(uint32_t *h, const uint32_t *blk)
{
    uint32_t w[64];
    for (unsigned i = 0; i < 16u; i++)
    {
        w[i] = be32_at(blk, i);
    }
    for (unsigned i = 16u; i < 64u; i++)
    {
        uint32_t s0 = ror32(w[i - 15], 7u) ^ ror32(w[i - 15], 18u) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror32(w[i - 2], 17u) ^ ror32(w[i - 2], 19u) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (unsigned i = 0; i < 64u; i++)
    {
        uint32_t S1 = ror32(e, 6u) ^ ror32(e, 11u) ^ ror32(e, 25u);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + S1 + ch + K256[i] + w[i];
        uint32_t S0 = ror32(a, 2u) ^ ror32(a, 13u) ^ ror32(a, 22u);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
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

// FIPS 180-4 sec 6.4.2: eighty rounds over an eight-lane 64-bit state. SHA-384 differs only by its IV.
static void sha512_compress(uint64_t *h, const uint32_t *blk)
{
    uint64_t w[80];
    for (unsigned i = 0; i < 16u; i++)
    {
        w[i] = be64_at(blk, i);
    }
    for (unsigned i = 16u; i < 80u; i++)
    {
        uint64_t s0 = ror64(w[i - 15], 1u) ^ ror64(w[i - 15], 8u) ^ (w[i - 15] >> 7);
        uint64_t s1 = ror64(w[i - 2], 19u) ^ ror64(w[i - 2], 61u) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (unsigned i = 0; i < 80u; i++)
    {
        uint64_t S1 = ror64(e, 14u) ^ ror64(e, 18u) ^ ror64(e, 41u);
        uint64_t ch = (e & f) ^ (~e & g);
        uint64_t t1 = hh + S1 + ch + K512[i] + w[i];
        uint64_t S0 = ror64(a, 28u) ^ ror64(a, 34u) ^ ror64(a, 39u);
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t t2 = S0 + maj;
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

void protocore_sha_hw_acquire(void)
{
    // No clock, no reset, no second user. The bracket is kept so the arm's shape is the same
    // natively as on the part.
}

void protocore_sha_hw_release(void)
{
}

void protocore_sha_hw_block(uint32_t mode, uint32_t *h, unsigned hwords, const uint32_t *blk, unsigned bwords,
                            proto_bool first)
{
    (void)bwords;
    if (!h || !blk)
    {
        return;
    }
    if (mode == PROTOCORE_SHA_MODE_384 || mode == PROTOCORE_SHA_MODE_512)
    {
        // A 64-bit family's caller carries its state as uint64_t lanes and hands the same storage in
        // as words, because the H bank is addressed 32 bits at a time. The lanes are read back
        // through that native view rather than reassembled from halves, so the pairing follows the
        // machine's own layout instead of an assumed order.
        uint64_t *lanes = (uint64_t *)(void *)h;
        const uint64_t *iv = (mode == PROTOCORE_SHA_MODE_384) ? IV384 : IV512;
        if (first)
        {
            for (unsigned i = 0; i < 8u && 2u * i + 1u < hwords; i++)
            {
                lanes[i] = iv[i];
            }
        }
        sha512_compress(lanes, blk);
        return;
    }
    if (mode == PROTOCORE_SHA_MODE_1)
    {
        if (first)
        {
            for (unsigned i = 0; i < 5u && i < hwords; i++)
            {
                h[i] = IV1[i];
            }
        }
        sha1_compress(h, blk);
        return;
    }
    if (first)
    {
        const uint32_t *iv = (mode == PROTOCORE_SHA_MODE_224) ? IV224 : IV256;
        for (unsigned i = 0; i < 8u && i < hwords; i++)
        {
            h[i] = iv[i];
        }
    }
    sha256_compress(h, blk);
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_HOST && PROTOCORE_HAS_HW_SHA
