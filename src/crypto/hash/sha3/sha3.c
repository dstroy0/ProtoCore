// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sha3.c
 * @brief Keccak-f[1600] sponge implementation (see sha3.h). Little-endian octet addressing.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SHA3

#include "crypto/crypto_opt.h"
#include "crypto/hash/sha3/sha3.h"

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// The one definition of KeccakCtx - private to this TU. It sits at SHA3_OFF_CTX in the caller's
// borrow, so its size never leaves this file and no consumer can name it. `out_pos` is how many
// octets of the current block are spent; `absorbed` says the sponge has been padded and can squeeze.
typedef struct KeccakCtx
{
    uint64_t st[25];
    uint32_t rate;
    uint32_t out_pos;
    uint8_t absorbed;
} KeccakCtx;

// The caller's borrow: one sponge. The borrow IS the sponge, so a streaming XOF and a one-shot are
// two borrows and neither disturbs the other.
#define SHA3_OFF_CTX 0u
static_assert(SHA3_OFF_CTX + sizeof(struct KeccakCtx) <= PROTOCORE_SHA3_BORROW,
              "PROTOCORE_SHA3_BORROW is short of the sponge - raise it in protocore_config.h, "
              "which sums it into the secure arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(SHA3_OFF_CTX % _Alignof(KeccakCtx) == 0,
              "SHA3_OFF_CTX is not a multiple of alignof(KeccakCtx) - SHA3_SPONGE() would return a misaligned "
              "pointer; pad the region ahead of it");

// The sponge, at its offset in the caller's borrow.
#define SHA3_SPONGE(w) ((KeccakCtx *)(void *)((w) + SHA3_OFF_CTX))

// Keccak-f[1600] permutation constants (FIPS 202): iota round constants, rho rotation offsets, and
// the rho/pi lane-permutation order.
static const uint64_t protocore_keccak_rc[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL, 0x8000000080008000ULL, 0x000000000000808bULL,
    0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000aULL, 0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL,
    0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL};

static const uint8_t protocore_keccak_rot[24] = {1,  3,  6,  10, 15, 21, 28, 36, 45, 55, 2,  14,
                                                 27, 41, 56, 8,  25, 43, 62, 18, 39, 61, 20, 44};

static const uint8_t protocore_keccak_pi[24] = {10, 7,  11, 17, 18, 3, 5,  16, 8,  21, 24, 4,
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
            int j = protocore_keccak_pi[i];
            uint64_t tmp = st[j];
            st[j] = rotl64(t, protocore_keccak_rot[i]);
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
        st[0] ^= protocore_keccak_rc[r];
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

// Absorb the whole message with domain-separation byte @p domain (0x06 SHA3, 0x1F SHAKE) and pad,
// leaving @p c ready to squeeze. Handles any input length (multi-block).
static void protocore_keccak_absorb(KeccakCtx *c, uint32_t rate, const uint8_t *in, size_t inlen, uint8_t domain)
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

// Squeeze @p outlen octets, permuting between blocks. May be called repeatedly for XOF use.
static void protocore_keccak_squeeze(KeccakCtx *c, uint8_t *out, size_t outlen)
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

static void sha3_absorb(uint8_t *restrict work)
{
    protocore_keccak_absorb(SHA3_SPONGE(work), Sha3.absorb_args.rate, Sha3.absorb_args.in, Sha3.absorb_args.inlen,
                            Sha3.absorb_args.domain);
    SHA3_SPONGE(work)->absorbed = 1;
    Sha3.ok = PROTO_TRUE;
}

static void sha3_squeeze(uint8_t *restrict work)
{
    if (!SHA3_SPONGE(work)->absorbed || !Sha3.squeeze_args.out)
    {
        Sha3.ok = PROTO_FALSE;
        return;
    }
    protocore_keccak_squeeze(SHA3_SPONGE(work), Sha3.squeeze_args.out, Sha3.squeeze_args.outlen);
    Sha3.ok = PROTO_TRUE;
}

// One-shot digest: absorb the message at @p rate with the SHA3 domain byte, squeeze @p outlen octets.
static void sha3_digest(uint8_t *restrict work, uint32_t rate, size_t outlen)
{
    if (!Sha3.digest_args.out)
    {
        Sha3.ok = PROTO_FALSE;
        return;
    }
    KeccakCtx *c = SHA3_SPONGE(work);
    protocore_keccak_absorb(c, rate, Sha3.digest_args.in, Sha3.digest_args.inlen, 0x06);
    protocore_keccak_squeeze(c, Sha3.digest_args.out, outlen);
    c->absorbed = 1;
    Sha3.ok = PROTO_TRUE;
}

static void sha3_sha3_256(uint8_t *restrict work)
{
    sha3_digest(work, KECCAK_RATE_SHA3_256, 32);
}

static void sha3_sha3_512(uint8_t *restrict work)
{
    sha3_digest(work, KECCAK_RATE_SHA3_512, 64);
}

// One-shot XOF: absorb the message at @p rate with the SHAKE domain byte, squeeze the requested run.
static void sha3_xof(uint8_t *restrict work, uint32_t rate)
{
    if (!Sha3.xof_args.out)
    {
        Sha3.ok = PROTO_FALSE;
        return;
    }
    KeccakCtx *c = SHA3_SPONGE(work);
    protocore_keccak_absorb(c, rate, Sha3.xof_args.in, Sha3.xof_args.inlen, 0x1F);
    protocore_keccak_squeeze(c, Sha3.xof_args.out, Sha3.xof_args.outlen);
    c->absorbed = 1;
    Sha3.ok = PROTO_TRUE;
}

static void sha3_shake128(uint8_t *restrict work)
{
    sha3_xof(work, KECCAK_RATE_SHAKE128);
}

static void sha3_shake256(uint8_t *restrict work)
{
    sha3_xof(work, KECCAK_RATE_SHAKE256);
}

static void sha3_shake128_absorb(uint8_t *restrict work)
{
    protocore_keccak_absorb(SHA3_SPONGE(work), KECCAK_RATE_SHAKE128, Sha3.shake128_absorb_args.in,
                            Sha3.shake128_absorb_args.inlen, 0x1F);
    SHA3_SPONGE(work)->absorbed = 1;
    Sha3.ok = PROTO_TRUE;
}

Sha3Ns Sha3 = {.absorb = sha3_absorb,
               .squeeze = sha3_squeeze,
               .sha3_256 = sha3_sha3_256,
               .sha3_512 = sha3_sha3_512,
               .shake128 = sha3_shake128,
               .shake256 = sha3_shake256,
               .shake128_absorb = sha3_shake128_absorb};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SHA3
