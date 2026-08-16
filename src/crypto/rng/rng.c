// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rng.c
 * @brief The generator's seed, and the draw over it - implementation. See rng.h.
 *
 * The context is this file's. The module's own borrow is split by offset into the budget, the seed
 * and its nonce, the ratchet's replacement copy, and the region the nested ChaCha20 runs out of. The
 * seed is key material the caller keeps for the life of the program, so those bytes are the span it
 * took once rather than storage declared here.
 *
 * A draw takes its keystream under the current seed and replaces the seed from that same keystream
 * before returning. Block 0 supplies the replacement and the caller's bytes start at block 1, so no
 * byte is both handed out and kept, and the state that produced a value is gone by the time the
 * value is. Every PROTOCORE_RAND_RESEED_BYTES the seed is redrawn from the platform instead.
 */

#include "protocore_config.h" // the entry point: the enable gate below, the widths, and the platform's entropy source

#if PROTOCORE_ENABLE_RNG

#include "crypto/cipher/chacha20.h"
#include "crypto/crypto_opt.h"
#include "crypto/rng/rng.h"
#include "mmgr/protomem.h"
#include "mmgr/secure.h" // protocore_secure_wipe

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// The nonce beside the seed, and the two together as one platform draw.
#define RNG_IV_LEN 8
#define RNG_SEEDED_LEN (PROTOCORE_RAND_SEED_LEN + RNG_IV_LEN)

// The one definition, private to this TU. It sits at RNG_OFF_CTX in the caller's borrow, so its size
// never leaves this file and no consumer can name it.
//
// Only what is not derivable: the budget spent since the last platform draw, and whether that draw
// has happened. The seed, its nonce and the ratchet copy live at fixed offsets in the borrow, so
// macros compute them from the pointer rather than the context holding them.
typedef struct
{
    size_t drawn;      ///< bytes handed out since the last platform draw
    proto_bool seeded; ///< whether the seed has been drawn from the platform
} RngCtx;

// The caller's borrow, split: the budget, the seed, its nonce, the ratchet's replacement copy, then
// the region the nested ChaCha20 runs out of. That one is driven through its own namespace, so this
// borrow carries a region for it rather than naming any term of it.
#define RNG_OFF_CTX 0u
#define RNG_OFF_KEY (RNG_OFF_CTX + sizeof(RngCtx))
#define RNG_OFF_IV (RNG_OFF_KEY + PROTOCORE_RAND_SEED_LEN)
#define RNG_OFF_NEXT (RNG_OFF_IV + RNG_IV_LEN)
#define RNG_OFF_CHACHA (RNG_OFF_NEXT + PROTOCORE_RAND_SEED_LEN)
static_assert(RNG_OFF_CHACHA + PROTOCORE_CHACHA20_BORROW <= PROTOCORE_RNG_BORROW,
              "PROTOCORE_RNG_BORROW is short of the budget, the seed, its nonce, the ratchet copy and "
              "the nested borrow - raise it in protocore_config.h, which derives "
              "PROTOCORE_SECURE_ARENA_SIZE from it");

// The regions, at their offsets in the caller's borrow. The key and the nonce are adjacent, so one
// platform draw of RNG_SEEDED_LEN bytes at RNG_KEY fills both.
#define RNG_CTX(w) ((RngCtx *)(void *)((w) + RNG_OFF_CTX))
#define RNG_KEY(w) ((w) + RNG_OFF_KEY)
#define RNG_IV(w) ((w) + RNG_OFF_IV)
#define RNG_NEXT(w) ((w) + RNG_OFF_NEXT)
#define RNG_CHACHA(w) ((w) + RNG_OFF_CHACHA)

// The one owned instance, private to this TU: the pointer to the bytes this module took for itself.
// A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_RNG_BORROW persistent bytes, or null while the pool was short
} RngOwnCtx;
static RngOwnCtx s_rng;

// One keystream run through the Chacha20 namespace.
static void rng_chacha(uint8_t *restrict work, const uint8_t *key, const uint8_t *iv, uint64_t counter,
                       const uint8_t *in, uint8_t *out, size_t len)
{
    Chacha20.xor_args.key = key;
    Chacha20.xor_args.iv = iv;
    Chacha20.xor_args.counter = counter;
    Chacha20.xor_args.in = in;
    Chacha20.xor_args.out = out;
    Chacha20.xor_args.len = len;
    Chacha20.xor_(RNG_CHACHA(work));
}

// Redraw the seed and its nonce from the platform, and start the budget over.
static void rng_platform_seed(uint8_t *restrict work)
{
    RngCtx *ctx = RNG_CTX(work);
    protocore_platform_rand_fill(RNG_KEY(work), RNG_SEEDED_LEN);
    ctx->drawn = 0;
    ctx->seeded = PROTO_TRUE;
}

// --- the program's shared generator, beside the namespace not on it ---------

// Not an entry: an entry takes a borrow and this is where that borrow comes from. Stated flat, as
// bn_expmod_group14 is stated beside BignumNs, so the namespace keeps the one shape every module has
// - args in, ok out, every entry over the caller's bytes - and storage stays off it.
//
// The bytes come from the end of the secure pool, which no mark and no release walks, so they last
// the life of the program: the lifetime the seed and the ratchet need. Taken once and handed back on
// every later call. A caller that took its own PROTOCORE_RNG_BORROW span passes that instead and is a
// separate generator.
uint8_t *protocore_rng_span(void)
{
    if (s_rng.span == NULL)
    {
        protocore_span s = protocore_secure_persist_span(PROTOCORE_RNG_BORROW);
        if (span.ok(s))
        {
            s_rng.span = s.buf;
        }
    }
    return s_rng.span; // null while the pool was short, which every entry refuses
}

// --- the entries -----------------------------------------------------------

static void rng_fill(uint8_t *restrict work)
{
    Rng.ok = PROTO_FALSE;
    if (!work || !Rng.fill_args.out || Rng.fill_args.len == 0)
    {
        return;
    }
    RngCtx *ctx = RNG_CTX(work);
    uint8_t *key = RNG_KEY(work);
    uint8_t *iv = RNG_IV(work);
    uint8_t *next = RNG_NEXT(work);
    const size_t len = Rng.fill_args.len;
    if (!ctx->seeded || ctx->drawn >= PROTOCORE_RAND_RESEED_BYTES || len >= PROTOCORE_RAND_RESEED_BYTES - ctx->drawn)
    {
        rng_platform_seed(work);
    }
    rng_chacha(work, key, iv, 0, NULL, next, PROTOCORE_RAND_SEED_LEN);
    rng_chacha(work, key, iv, 1, NULL, Rng.fill_args.out, len);
    mem.cpy(key, next, PROTOCORE_RAND_SEED_LEN);
    protocore_secure_wipe(next, PROTOCORE_RAND_SEED_LEN);
    ctx->drawn += len;
    Rng.ok = PROTO_TRUE;
}

static void rng_reseed(uint8_t *restrict work)
{
    Rng.ok = PROTO_FALSE;
    if (!work)
    {
        return;
    }
    rng_platform_seed(work);
    Rng.ok = PROTO_TRUE;
}

RngNs Rng = {.fill = rng_fill, .reseed = rng_reseed};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_RNG
