// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file chacha20.c
 * @brief ChaCha20 (RFC 8439) - implementation. See chacha20.h.
 *
 * One core and two entries: the OpenSSH state layout XORs its keystream over a run of bytes, the RFC
 * 8439 layout hands one block back. Both run the same 20 rounds.
 *
 * The context is this file's. The module's own borrow is split by offset into the state the
 * permutation runs over and the 64-byte keystream block it serializes into.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_CHACHA20

#include "crypto/cipher/chacha20/chacha20.h"
#include "crypto/crypto_opt.h"

PROTOCORE_CRYPTO_HOT

PROTOCORE_BEGIN_DECLS

// The one definition, private to this TU. Only what is not derivable: the keystream block sits at a
// fixed offset in the caller's borrow, so a macro computes it from the pointer rather than the context
// holding it.
typedef struct
{
    uint32_t st[16]; ///< Input state: constants, key, counter, nonce.
    uint32_t x[16];  ///< Round state.
} Chacha20Ctx;

// The caller's borrow, split: the permutation's state, then the block it serializes the keystream into.
#define CHACHA20_OFF_CTX 0u
#define CHACHA20_OFF_KS (CHACHA20_OFF_CTX + sizeof(Chacha20Ctx))
static_assert(CHACHA20_OFF_KS + PROTOCORE_CHACHA20_BLOCK_LEN <= PROTOCORE_CHACHA20_BORROW,
              "PROTOCORE_CHACHA20_BORROW is short of the state and the keystream block - raise it in "
              "protocore_config.h, which derives PROTOCORE_SECURE_ARENA_SIZE from it");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(CHACHA20_OFF_CTX % _Alignof(Chacha20Ctx) == 0,
              "CHACHA20_OFF_CTX is not a multiple of alignof(Chacha20Ctx) - CHACHA20_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The regions, at their offsets in the caller's borrow.
#define CHACHA20_CTX(w) ((Chacha20Ctx *)(void *)((w) + CHACHA20_OFF_CTX))
#define CHACHA20_KS(w) ((w) + CHACHA20_OFF_KS)

// ---------------------------------------------------------------------------
// ChaCha20 (RFC 8439).
// ---------------------------------------------------------------------------

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t rotl32(uint32_t v, int c)
{
    return (v << c) | (v >> (32 - c));
}

#define QR(a, b, c, d)                                                                                                 \
    a += b;                                                                                                            \
    d ^= a;                                                                                                            \
    d = rotl32(d, 16);                                                                                                 \
    c += d;                                                                                                            \
    b ^= c;                                                                                                            \
    b = rotl32(b, 12);                                                                                                 \
    a += b;                                                                                                            \
    d ^= a;                                                                                                            \
    d = rotl32(d, 8);                                                                                                  \
    c += d;                                                                                                            \
    b ^= c;                                                                                                            \
    b = rotl32(b, 7)

// The ChaCha20 core: 20 rounds over w->st, add the original state, serialize LE into @p out (64 bytes).
static void chacha_core(Chacha20Ctx *w, uint8_t out[64])
{
    for (int i = 0; i < 16; i++)
    {
        w->x[i] = w->st[i];
    }
    for (int i = 0; i < 10; i++) // 10 double-rounds = 20 rounds
    {
        QR(w->x[0], w->x[4], w->x[8], w->x[12]);
        QR(w->x[1], w->x[5], w->x[9], w->x[13]);
        QR(w->x[2], w->x[6], w->x[10], w->x[14]);
        QR(w->x[3], w->x[7], w->x[11], w->x[15]);
        QR(w->x[0], w->x[5], w->x[10], w->x[15]);
        QR(w->x[1], w->x[6], w->x[11], w->x[12]);
        QR(w->x[2], w->x[7], w->x[8], w->x[13]);
        QR(w->x[3], w->x[4], w->x[9], w->x[14]);
    }
    for (int i = 0; i < 16; i++)
    {
        uint32_t v = w->x[i] + w->st[i];
        out[4 * i + 0] = (uint8_t)v;
        out[4 * i + 1] = (uint8_t)(v >> 8);
        out[4 * i + 2] = (uint8_t)(v >> 16);
        out[4 * i + 3] = (uint8_t)(v >> 24);
    }
}

// "expand 32-byte k"
static const uint32_t SIGMA0 = 0x61707865;
static const uint32_t SIGMA1 = 0x3320646e;
static const uint32_t SIGMA2 = 0x79622d32;
static const uint32_t SIGMA3 = 0x6b206574;

// --- the entries -----------------------------------------------------------

void protocore_chacha20_xor_(uint8_t *restrict work)
{
    Chacha20V.ok = PROTO_FALSE;
    if (!Chacha20V.xor_args.key || !Chacha20V.xor_args.iv || !Chacha20V.xor_args.out)
    {
        return;
    }
    const uint8_t *key = Chacha20V.xor_args.key;
    const uint8_t *iv = Chacha20V.xor_args.iv;
    const uint8_t *in = Chacha20V.xor_args.in;
    uint8_t *out = Chacha20V.xor_args.out;
    size_t len = Chacha20V.xor_args.len;
    uint64_t counter = Chacha20V.xor_args.counter;
    Chacha20Ctx *w = CHACHA20_CTX(work);
    uint8_t *ks = CHACHA20_KS(work);

    w->st[0] = SIGMA0;
    w->st[1] = SIGMA1;
    w->st[2] = SIGMA2;
    w->st[3] = SIGMA3;
    for (int i = 0; i < 8; i++)
    {
        w->st[4 + i] = rd_le32(key + 4 * i);
    }
    w->st[14] = rd_le32(iv + 0); // OpenSSH layout: 64-bit nonce in words 14-15
    w->st[15] = rd_le32(iv + 4);
    size_t off = 0;
    while (off < len)
    {
        w->st[12] = (uint32_t)(counter & 0xffffffffu); // 64-bit little-endian counter in words 12-13
        w->st[13] = (uint32_t)(counter >> 32);
        chacha_core(w, ks);
        size_t n = (len - off < 64) ? (len - off) : 64;
        for (size_t i = 0; i < n; i++)
        {
            out[off + i] = (uint8_t)((in ? in[off + i] : 0) ^ ks[i]);
        }
        off += n;
        counter++;
    }
    Chacha20V.ok = PROTO_TRUE;
}

void protocore_chacha20_block_ietf(uint8_t *restrict work)
{
    Chacha20V.ok = PROTO_FALSE;
    if (!Chacha20V.block_ietf_args.key || !Chacha20V.block_ietf_args.nonce || !Chacha20V.block_ietf_args.out)
    {
        return;
    }
    const uint8_t *key = Chacha20V.block_ietf_args.key;
    const uint8_t *nonce = Chacha20V.block_ietf_args.nonce;
    uint32_t counter = Chacha20V.block_ietf_args.counter;
    uint8_t *out = Chacha20V.block_ietf_args.out;
    Chacha20Ctx *w = CHACHA20_CTX(work);

    w->st[0] = SIGMA0;
    w->st[1] = SIGMA1;
    w->st[2] = SIGMA2;
    w->st[3] = SIGMA3;
    for (int i = 0; i < 8; i++)
    {
        w->st[4 + i] = rd_le32(key + 4 * i);
    }
    w->st[12] = counter; // RFC 8439 layout: 32-bit counter, 96-bit nonce
    w->st[13] = rd_le32(nonce + 0);
    w->st[14] = rd_le32(nonce + 4);
    w->st[15] = rd_le32(nonce + 8);
    chacha_core(w, out);
    Chacha20V.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
Chacha20Vars Chacha20V;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CHACHA20
