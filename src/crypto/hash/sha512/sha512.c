// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sha512.c
 * @brief SHA-512 implementation (FIPS 180-4).
 *
 * One context and one set of entries; only the block compression has two arms - the accelerator where
 * the part carries one, the FIPS 180-4 rounds below where it does not. Shared by SSH
 * (Ed25519 / kex), PQC, and SMB 3.1.1 preauth integrity.
 *
 * The context is this file's. The module's own borrow is split by offset into the block as it
 * arrives, the padded last one, and the state copy finalizing compresses into.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SHA512

#if PROTOCORE_HAS_HW_SHA
#endif
#include "crypto/crypto_opt.h"
#include "crypto/hash/sha512/sha512.h"
#include "mmgr/endian/endian.h" // protocore_rd64be / protocore_wr64be: the block reader and the digest writer
#include "mmgr/protomem/protomem.h"

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// The one definition, both arms, private to this TU. The accelerator compresses a block; it does not
// pad, buffer a partial block, or hold a digest a caller can keep feeding. Those are this file's, so
// the state a context carries is the same either way and only the compression below has two arms.
//
// Only what is not derivable: the regions live at fixed offsets in the caller's borrow, so a helper
// computes them from the pointer rather than the context storing them.
typedef struct
{
    uint64_t s[8];  ///< Running hash words (H0..H7).
    uint64_t n;     ///< Total bytes processed so far.
    uint32_t rxlen; ///< Bytes valid in rx.
} Sha512Ctx;

// The caller's borrow, split: the running context, the block as it arrives, the padded last one, and
// the state copy the padded blocks compress into so finalizing leaves the running hash alone.
#define SHA512_OFF_CTX 0u
#define SHA512_OFF_RX (SHA512_OFF_CTX + sizeof(Sha512Ctx))
#define SHA512_OFF_TX (SHA512_OFF_RX + PROTOCORE_SHA512_BLOCK_LEN)
#define SHA512_OFF_STATE (SHA512_OFF_TX + PROTOCORE_SHA512_BLOCK_LEN)
static_assert(SHA512_OFF_STATE + sizeof(uint64_t) * 8 <= PROTOCORE_SHA512_BORROW,
              "PROTOCORE_SHA512_BORROW is short of the context, the two blocks and the state copy - "
              "raise it in protocore_config.h, which derives PROTOCORE_SECURE_ARENA_SIZE from it");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(SHA512_OFF_CTX % _Alignof(Sha512Ctx) == 0,
              "SHA512_OFF_CTX is not a multiple of alignof(Sha512Ctx) - SHA512_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");
static_assert(SHA512_OFF_STATE % _Alignof(uint64_t) == 0,
              "SHA512_OFF_STATE is not a multiple of alignof(uint64_t) - SHA512_FS() would return a misaligned "
              "pointer; pad the region ahead of it");

// The regions, at their offsets in the caller's borrow.
#define SHA512_CTX(w) ((Sha512Ctx *)(void *)((w) + SHA512_OFF_CTX))
#define SHA512_RX(w) ((w) + SHA512_OFF_RX)
#define SHA512_TX(w) ((w) + SHA512_OFF_TX)
#define SHA512_FS(w) ((uint64_t *)(void *)((w) + SHA512_OFF_STATE))

// ---------------------------------------------------------------------------
// SHA-512 (FIPS 180-4).
// ---------------------------------------------------------------------------

#if !PROTOCORE_HAS_HW_SHA
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
#endif

// Where the 128-bit message length sits in the final block (FIPS 180-4 §5.1.2).
#define SHA512_LEN_OFF (PROTOCORE_SHA512_BLOCK_LEN - 16u)

static const uint64_t H0[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};

#if PROTOCORE_HAS_HW_SHA
// Compress one block into @p h on the accelerator. SHA-512 state and block are twice SHA-256's, and
// the H and M banks are addressed as 32-bit words, so the counts are 16 and 32.
static void sha512_block(uint64_t h[8], const uint8_t blk[PROTOCORE_SHA512_BLOCK_LEN])
{
    protocore_sha_hw_block(PROTOCORE_SHA_MODE_512, (uint32_t *)(void *)h, 16u, (const uint32_t *)(const void *)blk, 32u,
                           PROTO_FALSE);
}
#endif

#if !PROTOCORE_HAS_HW_SHA
static inline uint64_t rotr64(uint64_t x, unsigned n)
{
    return (x >> n) | (x << (64 - n));
}

// The six FIPS 180-4 §4.1.3 functions the rounds are built from.
static inline uint64_t sha512_ch(uint64_t x, uint64_t y, uint64_t z)
{
    return (x & y) ^ (~x & z);
}
static inline uint64_t sha512_maj(uint64_t x, uint64_t y, uint64_t z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}
static inline uint64_t sha512_bsig0(uint64_t x)
{
    return rotr64(x, 28) ^ rotr64(x, 34) ^ rotr64(x, 39);
}
static inline uint64_t sha512_bsig1(uint64_t x)
{
    return rotr64(x, 14) ^ rotr64(x, 18) ^ rotr64(x, 41);
}
static inline uint64_t sha512_ssig0(uint64_t x)
{
    return rotr64(x, 1) ^ rotr64(x, 8) ^ (x >> 7);
}
static inline uint64_t sha512_ssig1(uint64_t x)
{
    return rotr64(x, 19) ^ rotr64(x, 61) ^ (x >> 6);
}

// Compress one 128-byte block into the running state h[0..7] (FIPS 180-4 §6.4.2).
//
// The same shape as SHA-256 at twice the width: the state's period is eight and the schedule's is
// sixteen, and eighty rounds is five whole sixteens, so the compression is written once and run five
// times with k stepping. Naming the register a round lands on IS the shift, so no word is ever moved
// and the schedule stays sixteen words rather than eighty.
static void sha512_block(uint64_t h[8], const uint8_t blk[PROTOCORE_SHA512_BLOCK_LEN])
{
    uint64_t m0 = protocore_rd64be(blk);
    uint64_t m1 = protocore_rd64be(blk + 8);
    uint64_t m2 = protocore_rd64be(blk + 16);
    uint64_t m3 = protocore_rd64be(blk + 24);
    uint64_t m4 = protocore_rd64be(blk + 32);
    uint64_t m5 = protocore_rd64be(blk + 40);
    uint64_t m6 = protocore_rd64be(blk + 48);
    uint64_t m7 = protocore_rd64be(blk + 56);
    uint64_t m8 = protocore_rd64be(blk + 64);
    uint64_t m9 = protocore_rd64be(blk + 72);
    uint64_t m10 = protocore_rd64be(blk + 80);
    uint64_t m11 = protocore_rd64be(blk + 88);
    uint64_t m12 = protocore_rd64be(blk + 96);
    uint64_t m13 = protocore_rd64be(blk + 104);
    uint64_t m14 = protocore_rd64be(blk + 112);
    uint64_t m15 = protocore_rd64be(blk + 120);

    uint64_t v0 = h[0];
    uint64_t v1 = h[1];
    uint64_t v2 = h[2];
    uint64_t v3 = h[3];
    uint64_t v4 = h[4];
    uint64_t v5 = h[5];
    uint64_t v6 = h[6];
    uint64_t v7 = h[7];

    // Five groups of sixteen. Each round writes exactly two state words: the one holding h becomes the
    // next a, the one holding d becomes the next e. The trailing expansion of the fifth group is
    // unread.
    const uint64_t *k = K512;
    for (int g = 0; g < 5; g++)
    {
        uint64_t t1_00 = v7 + sha512_bsig1(v4) + sha512_ch(v4, v5, v6) + k[0] + m0;
        uint64_t t2_00 = sha512_bsig0(v0) + sha512_maj(v0, v1, v2);
        v3 += t1_00;
        v7 = t1_00 + t2_00;

        uint64_t t1_01 = v6 + sha512_bsig1(v3) + sha512_ch(v3, v4, v5) + k[1] + m1;
        uint64_t t2_01 = sha512_bsig0(v7) + sha512_maj(v7, v0, v1);
        v2 += t1_01;
        v6 = t1_01 + t2_01;

        uint64_t t1_02 = v5 + sha512_bsig1(v2) + sha512_ch(v2, v3, v4) + k[2] + m2;
        uint64_t t2_02 = sha512_bsig0(v6) + sha512_maj(v6, v7, v0);
        v1 += t1_02;
        v5 = t1_02 + t2_02;

        uint64_t t1_03 = v4 + sha512_bsig1(v1) + sha512_ch(v1, v2, v3) + k[3] + m3;
        uint64_t t2_03 = sha512_bsig0(v5) + sha512_maj(v5, v6, v7);
        v0 += t1_03;
        v4 = t1_03 + t2_03;

        uint64_t t1_04 = v3 + sha512_bsig1(v0) + sha512_ch(v0, v1, v2) + k[4] + m4;
        uint64_t t2_04 = sha512_bsig0(v4) + sha512_maj(v4, v5, v6);
        v7 += t1_04;
        v3 = t1_04 + t2_04;

        uint64_t t1_05 = v2 + sha512_bsig1(v7) + sha512_ch(v7, v0, v1) + k[5] + m5;
        uint64_t t2_05 = sha512_bsig0(v3) + sha512_maj(v3, v4, v5);
        v6 += t1_05;
        v2 = t1_05 + t2_05;

        uint64_t t1_06 = v1 + sha512_bsig1(v6) + sha512_ch(v6, v7, v0) + k[6] + m6;
        uint64_t t2_06 = sha512_bsig0(v2) + sha512_maj(v2, v3, v4);
        v5 += t1_06;
        v1 = t1_06 + t2_06;

        uint64_t t1_07 = v0 + sha512_bsig1(v5) + sha512_ch(v5, v6, v7) + k[7] + m7;
        uint64_t t2_07 = sha512_bsig0(v1) + sha512_maj(v1, v2, v3);
        v4 += t1_07;
        v0 = t1_07 + t2_07;

        uint64_t t1_08 = v7 + sha512_bsig1(v4) + sha512_ch(v4, v5, v6) + k[8] + m8;
        uint64_t t2_08 = sha512_bsig0(v0) + sha512_maj(v0, v1, v2);
        v3 += t1_08;
        v7 = t1_08 + t2_08;

        uint64_t t1_09 = v6 + sha512_bsig1(v3) + sha512_ch(v3, v4, v5) + k[9] + m9;
        uint64_t t2_09 = sha512_bsig0(v7) + sha512_maj(v7, v0, v1);
        v2 += t1_09;
        v6 = t1_09 + t2_09;

        uint64_t t1_10 = v5 + sha512_bsig1(v2) + sha512_ch(v2, v3, v4) + k[10] + m10;
        uint64_t t2_10 = sha512_bsig0(v6) + sha512_maj(v6, v7, v0);
        v1 += t1_10;
        v5 = t1_10 + t2_10;

        uint64_t t1_11 = v4 + sha512_bsig1(v1) + sha512_ch(v1, v2, v3) + k[11] + m11;
        uint64_t t2_11 = sha512_bsig0(v5) + sha512_maj(v5, v6, v7);
        v0 += t1_11;
        v4 = t1_11 + t2_11;

        uint64_t t1_12 = v3 + sha512_bsig1(v0) + sha512_ch(v0, v1, v2) + k[12] + m12;
        uint64_t t2_12 = sha512_bsig0(v4) + sha512_maj(v4, v5, v6);
        v7 += t1_12;
        v3 = t1_12 + t2_12;

        uint64_t t1_13 = v2 + sha512_bsig1(v7) + sha512_ch(v7, v0, v1) + k[13] + m13;
        uint64_t t2_13 = sha512_bsig0(v3) + sha512_maj(v3, v4, v5);
        v6 += t1_13;
        v2 = t1_13 + t2_13;

        uint64_t t1_14 = v1 + sha512_bsig1(v6) + sha512_ch(v6, v7, v0) + k[14] + m14;
        uint64_t t2_14 = sha512_bsig0(v2) + sha512_maj(v2, v3, v4);
        v5 += t1_14;
        v1 = t1_14 + t2_14;

        uint64_t t1_15 = v0 + sha512_bsig1(v5) + sha512_ch(v5, v6, v7) + k[15] + m15;
        uint64_t t2_15 = sha512_bsig0(v1) + sha512_maj(v1, v2, v3);
        v4 += t1_15;
        v0 = t1_15 + t2_15;

        k += 16;

        // W[i] = W[i-16] + sigma0(W[i-15]) + W[i-7] + sigma1(W[i-2]), in place: the slot being written
        // still holds W[i-16], and the three it reads are the window's other places.
        m0 += sha512_ssig0(m1) + m9 + sha512_ssig1(m14);
        m1 += sha512_ssig0(m2) + m10 + sha512_ssig1(m15);
        m2 += sha512_ssig0(m3) + m11 + sha512_ssig1(m0);
        m3 += sha512_ssig0(m4) + m12 + sha512_ssig1(m1);
        m4 += sha512_ssig0(m5) + m13 + sha512_ssig1(m2);
        m5 += sha512_ssig0(m6) + m14 + sha512_ssig1(m3);
        m6 += sha512_ssig0(m7) + m15 + sha512_ssig1(m4);
        m7 += sha512_ssig0(m8) + m0 + sha512_ssig1(m5);
        m8 += sha512_ssig0(m9) + m1 + sha512_ssig1(m6);
        m9 += sha512_ssig0(m10) + m2 + sha512_ssig1(m7);
        m10 += sha512_ssig0(m11) + m3 + sha512_ssig1(m8);
        m11 += sha512_ssig0(m12) + m4 + sha512_ssig1(m9);
        m12 += sha512_ssig0(m13) + m5 + sha512_ssig1(m10);
        m13 += sha512_ssig0(m14) + m6 + sha512_ssig1(m11);
        m14 += sha512_ssig0(m15) + m7 + sha512_ssig1(m12);
        m15 += sha512_ssig0(m0) + m8 + sha512_ssig1(m13);
    }

    // Feed-forward: eighty rounds is ten whole state periods, so v0..v7 are a..h again.
    h[0] += v0;
    h[1] += v1;
    h[2] += v2;
    h[3] += v3;
    h[4] += v4;
    h[5] += v5;
    h[6] += v6;
    h[7] += v7;
}
#endif // !PROTOCORE_HAS_HW_SHA (software compression)

// --- framing (one arm, both compressions) ----------------------------------

// Seed the state.
static void sha512_state_init(uint8_t *restrict work)
{
    Sha512Ctx *ctx = SHA512_CTX(work);
    for (int i = 0; i < 8; i++)
    {
        ctx->s[i] = H0[i];
    }
    ctx->n = 0;
    ctx->rxlen = 0;
}

static void sha512_absorb(uint8_t *restrict work, const uint8_t *data, size_t len)
{
    Sha512Ctx *ctx = SHA512_CTX(work);
    uint8_t *rx = SHA512_RX(work);
    ctx->n += len;
    while (len > 0)
    {
        uint32_t take = PROTOCORE_SHA512_BLOCK_LEN - ctx->rxlen;
        if (len < take)
        {
            take = (uint32_t)len;
        }
        // rx + rxlen carries no alignment, so this is the raw mover, not the aligned-span one.
        uint8_t *fill = rx + ctx->rxlen;
        proto_raw_read(fill, data, take);
        ctx->rxlen += take;
        data += take;
        len -= take;
        if (ctx->rxlen == PROTOCORE_SHA512_BLOCK_LEN)
        {
            sha512_block(ctx->s, rx);
            ctx->rxlen = 0;
        }
    }
}

static void sha512_finish(uint8_t *restrict work, uint8_t digest[PROTOCORE_SHA512_DIGEST_LEN])
{
    Sha512Ctx *ctx = SHA512_CTX(work);
    uint8_t *rx = SHA512_RX(work);
    uint8_t *tx = SHA512_TX(work);
    uint64_t *fs = SHA512_FS(work);

    // 128-bit length in bits. Our byte count fits a uint64, so the high word is n >> 61 (bits above 64)
    // and the low word is n << 3.
    uint64_t len_hi = ctx->n >> 61;
    uint64_t len_lo = ctx->n << 3;

    // The padded blocks compress into a copy of the state, so s, rx, rxlen and n all come out of this
    // untouched and the hash can keep taking data afterwards.
    mem.cpy(fs, ctx->s, sizeof(ctx->s));

    // The last block is composed in tx, whole: what rx holds, the mark, zeros, and the length. rx is
    // read and never written back, so nothing it still carries from an earlier block reaches the wire.
    mem.zero(tx, PROTOCORE_SHA512_BLOCK_LEN);
    mem.cpy(tx, rx, ctx->rxlen);
    tx[ctx->rxlen] = 0x80;

    // The 128-bit length occupies the block's last 16 bytes, so a mark at or past that offset takes
    // its own block.
    if (ctx->rxlen >= SHA512_LEN_OFF)
    {
        sha512_block(fs, tx);
        mem.zero(tx, PROTOCORE_SHA512_BLOCK_LEN);
    }

    protocore_wr64be(tx + SHA512_LEN_OFF, len_hi);
    protocore_wr64be(tx + SHA512_LEN_OFF + 8, len_lo);
    sha512_block(fs, tx);

    protocore_wr64be(digest, fs[0]);
    protocore_wr64be(digest + 8, fs[1]);
    protocore_wr64be(digest + 16, fs[2]);
    protocore_wr64be(digest + 24, fs[3]);
    protocore_wr64be(digest + 32, fs[4]);
    protocore_wr64be(digest + 40, fs[5]);
    protocore_wr64be(digest + 48, fs[6]);
    protocore_wr64be(digest + 56, fs[7]);
}

// --- the entries -----------------------------------------------------------

static void sha512_init(uint8_t *restrict work)
{
    sha512_state_init(work);
    Sha512.ok = PROTO_TRUE;
}

static void sha512_update(uint8_t *restrict work)
{
    sha512_absorb(work, Sha512.update_args.data, Sha512.update_args.len);
    Sha512.ok = PROTO_TRUE;
}

static void sha512_final(uint8_t *restrict work)
{
    if (!Sha512.final_args.out)
    {
        Sha512.ok = PROTO_FALSE;
        return;
    }
    sha512_finish(work, Sha512.final_args.out);
    Sha512.ok = PROTO_TRUE;
}

// One-shot over the members already set: init, absorb, finish.
static void sha512_hash(uint8_t *restrict work)
{
    Sha512.ok = PROTO_FALSE;
    if (!Sha512.hash_args.out)
    {
        return;
    }
    sha512_state_init(work);
    sha512_absorb(work, Sha512.hash_args.data, Sha512.hash_args.len);
    sha512_finish(work, Sha512.hash_args.out);
    Sha512.ok = PROTO_TRUE;
}

Sha512Ns Sha512 = {.init = sha512_init, .update = sha512_update, .final = sha512_final, .hash = sha512_hash};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SHA512
