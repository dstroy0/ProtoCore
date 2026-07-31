// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "crypto/hash/sha3.h"

#if PC_ENABLE_PQC_KEX

// Keccak-f[1600] permutation constants (FIPS 202): iota round constants, rho rotation offsets, and
// the rho/pi lane-permutation order.
static const uint64_t pc_keccak_rc[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL, 0x8000000080008000ULL, 0x000000000000808bULL,
    0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000aULL, 0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL,
    0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL};

static const uint8_t pc_keccak_rot[24] = {1,  3,  6,  10, 15, 21, 28, 36, 45, 55, 2,  14,
                                          27, 41, 56, 8,  25, 43, 62, 18, 39, 61, 20, 44};

static const uint8_t pc_keccak_pi[24] = {10, 7,  11, 17, 18, 3, 5,  16, 8,  21, 24, 4,
                                         15, 23, 19, 13, 12, 2, 20, 14, 22, 9,  6,  1};

static inline uint64_t rotl64(uint64_t x, unsigned n)
{
    return (x << n) | (x >> (64 - n));
}

static void keccakf(uint64_t st[25])
{
    for (int r = 0; r < 24; r++)
    {
        uint64_t bc[5];
        // Theta
        for (int i = 0; i < 5; i++)
        {
            bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];
        }
        for (int i = 0; i < 5; i++)
        {
            uint64_t t = bc[(i + 4) % 5] ^ rotl64(bc[(i + 1) % 5], 1);
            for (int j = 0; j < 25; j += 5)
            {
                st[j + i] ^= t;
            }
        }
        // Rho + Pi
        uint64_t t = st[1];
        for (int i = 0; i < 24; i++)
        {
            int j = pc_keccak_pi[i];
            uint64_t tmp = st[j];
            st[j] = rotl64(t, pc_keccak_rot[i]);
            t = tmp;
        }
        // Chi
        for (int j = 0; j < 25; j += 5)
        {
            for (int i = 0; i < 5; i++)
            {
                bc[i] = st[j + i];
            }
            for (int i = 0; i < 5; i++)
            {
                st[j + i] ^= (~bc[(i + 1) % 5]) & bc[(i + 2) % 5];
            }
        }
        // Iota
        st[0] ^= pc_keccak_rc[r];
    }
}

// The sponge state is addressed as a little-endian octet string: octet p lives in lane p/8 at byte
// p%8. This makes absorb/squeeze byte order independent of the host's.
static inline void st_xor_byte(uint64_t st[25], size_t p, uint8_t b)
{
    st[p >> 3] ^= (uint64_t)b << (8 * (p & 7));
}

static inline uint8_t st_get_byte(const uint64_t st[25], size_t p)
{
    return (uint8_t)(st[p >> 3] >> (8 * (p & 7)));
}

void pc_keccak_absorb(KeccakCtx *c, uint32_t rate, const uint8_t *in, size_t inlen, uint8_t domain)
{
    for (int i = 0; i < 25; i++)
    {
        c->st[i] = 0;
    }
    c->rate = rate;

    while (inlen >= rate)
    {
        for (uint32_t p = 0; p < rate; p++)
        {
            st_xor_byte(c->st, p, in[p]);
        }
        keccakf(c->st);
        in += rate;
        inlen -= rate;
    }
    for (size_t p = 0; p < inlen; p++)
    {
        st_xor_byte(c->st, p, in[p]);
    }
    // Pad10*1 with the domain-separation byte, and the high bit of the last rate octet.
    st_xor_byte(c->st, inlen, domain);
    st_xor_byte(c->st, rate - 1, 0x80);
    c->out_pos = rate; // force a permutation on the first squeeze
}

void pc_keccak_squeeze(KeccakCtx *c, uint8_t *out, size_t outlen)
{
    while (outlen)
    {
        if (c->out_pos == c->rate)
        {
            keccakf(c->st);
            c->out_pos = 0;
        }
        uint32_t n = c->rate - c->out_pos;
        if (n > outlen)
        {
            n = (uint32_t)outlen;
        }
        for (uint32_t k = 0; k < n; k++)
        {
            out[k] = st_get_byte(c->st, c->out_pos + k);
        }
        out += n;
        outlen -= n;
        c->out_pos += n;
    }
}

void sha3_256(uint8_t out[32], const uint8_t *in, size_t inlen)
{
    KeccakCtx c;
    pc_keccak_absorb(&c, KECCAK_RATE_SHA3_256, in, inlen, 0x06);
    pc_keccak_squeeze(&c, out, 32);
}

void sha3_512(uint8_t out[64], const uint8_t *in, size_t inlen)
{
    KeccakCtx c;
    pc_keccak_absorb(&c, KECCAK_RATE_SHA3_512, in, inlen, 0x06);
    pc_keccak_squeeze(&c, out, 64);
}

void shake128(uint8_t *out, size_t outlen, const uint8_t *in, size_t inlen)
{
    KeccakCtx c;
    pc_keccak_absorb(&c, KECCAK_RATE_SHAKE128, in, inlen, 0x1F);
    pc_keccak_squeeze(&c, out, outlen);
}

void shake256(uint8_t *out, size_t outlen, const uint8_t *in, size_t inlen)
{
    KeccakCtx c;
    pc_keccak_absorb(&c, KECCAK_RATE_SHAKE256, in, inlen, 0x1F);
    pc_keccak_squeeze(&c, out, outlen);
}

void pc_shake128_absorb(KeccakCtx *c, const uint8_t *in, size_t inlen)
{
    pc_keccak_absorb(c, KECCAK_RATE_SHAKE128, in, inlen, 0x1F);
}

#endif // PC_ENABLE_PQC_KEX
