// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sha1.c
 * @brief SHA-1 implementation (FIPS 180-4).
 *
 * One framing and one set of entries; only the block compression has two arms - the accelerator where
 * the part carries one, the FIPS 180-4 rounds below where it does not. Padding, the padded final
 * blocks and the digest output are software on both arms.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SHA1

#if PROTOCORE_HAS_HW_SHA
#endif
#include "mmgr/endian.h" // the big-endian serializers the framing and the rounds step with
#include "crypto/hash/sha1.h"
#include "crypto/crypto_opt.h"
#include "mmgr/protomem.h"

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// The one definition of Sha1Ctx - private to this TU. It sits at SHA1_OFF_CTX in the caller's borrow,
// so its size never leaves this file and no consumer can name it.
typedef struct Sha1Ctx
{
    uint32_t h[5]; ///< running hash words H0..H4
} Sha1Ctx;

// The caller's borrow, split by offset: the running state, then the two padded final blocks a message
// whose tail reaches 56 bytes composes.
#define SHA1_OFF_CTX 0u
#define SHA1_OFF_PAD (SHA1_OFF_CTX + sizeof(struct Sha1Ctx))
static_assert(SHA1_OFF_PAD + 128u <= PROTOCORE_SHA1_BORROW,
              "PROTOCORE_SHA1_BORROW is short of the state and the padded final blocks - raise it in "
              "protocore_config.h, which sums it into the secure arena");


#if PROTOCORE_HAS_HW_SHA

// --- HW path: the accelerator's compression --------------------------------

// Process one 64-byte block into the running state h[0..4] on the accelerator. The state is written
// back per block, so two contexts interleaving never share what the H bank holds.
static void sha1_block(uint32_t h[5], const uint8_t block[64])
{
    protocore_sha_hw_acquire();
    protocore_sha_hw_block(PROTOCORE_SHA_MODE_1, h, 5u, (const uint32_t *)(const void *)block, 16u, PROTO_FALSE);
    protocore_sha_hw_release();
}

#endif

#if !PROTOCORE_HAS_HW_SHA

// --- SW path: the FIPS 180-4 rounds ----------------------------------------

static inline uint32_t rot32(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

// Process one 64-byte block into the running state h[0..4] (FIPS 180-4 §6.1.2).
static void sha1_block(uint32_t h[5], const uint8_t block[64])
{
    // Message schedule: 16 big-endian words from the block, extended to 80 by XOR-and-rotate (the
    // SHA-1 recurrence; the rotate-by-1 is what SHA-0 lacked).
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
    {
        w[i] = protocore_rd32be(block + i * 4);
    }
    for (int i = 16; i < 80; i++)
    {
        w[i] = rot32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = h[0];
    uint32_t b = h[1];
    uint32_t c = h[2];
    uint32_t d = h[3];
    uint32_t e = h[4];

    // 80 rounds in four 20-round regimes, each with its own mixing function f and constant k.
    for (int i = 0; i < 80; i++)
    {
        uint32_t f;
        uint32_t k;
        if (i < 20)
        {
            f = (b & c) | (~b & d); // choice
            k = 0x5A827999u;
        }
        else if (i < 40)
        {
            f = b ^ c ^ d; // parity
            k = 0x6ED9EBA1u;
        }
        else if (i < 60)
        {
            f = (b & c) | (b & d) | (c & d); // majority
            k = 0x8F1BBCDCu;
        }
        else
        {
            f = b ^ c ^ d; // parity
            k = 0xCA62C1D6u;
        }

        // Round update; b is rotated 30 as it shifts into c.
        uint32_t tmp = rot32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rot32(b, 30);
        b = a;
        a = tmp;
    }

    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

#endif // !PROTOCORE_HAS_HW_SHA (software compression)

// --- framing (one arm, both compressions) ----------------------------------

static void sha1_run(uint8_t *restrict work, const uint8_t *data, size_t len,
                     uint8_t digest[PROTOCORE_SHA1_DIGEST_LEN])
{
    // State and padded blocks at their offsets in the caller's borrow.
    uint32_t *h = ((struct Sha1Ctx *)(void *)(work + SHA1_OFF_CTX))->h;
    uint8_t *pad = work + SHA1_OFF_PAD;

    h[0] = 0x67452301u;
    h[1] = 0xEFCDAB89u;
    h[2] = 0x98BADCFEu;
    h[3] = 0x10325476u;
    h[4] = 0xC3D2E1F0u;

    // Process full 64-byte blocks
    size_t blocks = len / 64;
    for (size_t i = 0; i < blocks; i++)
    {
        sha1_block(h, data + i * 64);
    }

    // Build the padded final block(s)
    mem.set(pad, 0, 128);
    size_t tail = len - blocks * 64;
    mem.cpy(pad, data + blocks * 64, tail);
    pad[tail] = 0x80;

    // Bit-length goes in the last 8 bytes of the final block
    uint64_t bit_len = (uint64_t)len * 8;
    uint8_t *bl = (tail < 56) ? pad + 56 : pad + 120;
    for (int i = 7; i >= 0; i--, bit_len >>= 8)
    {
        bl[i] = (uint8_t)bit_len;
    }

    sha1_block(h, pad);
    if (tail >= 56)
    {
        sha1_block(h, pad + 64);
    }

    for (int i = 0; i < 5; i++)
    {
        protocore_wr32be(digest + i * 4, h[i]);
    }
}

static void sha1_hash(uint8_t *restrict work)
{
    if (!work || !Sha1.hash_args.out)
    {
        Sha1.ok = PROTO_FALSE;
        return;
    }
    sha1_run(work, Sha1.hash_args.data, Sha1.hash_args.len, Sha1.hash_args.out);
    Sha1.ok = PROTO_TRUE;
}

Sha1Ns Sha1 = {.hash = sha1_hash};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SHA1
