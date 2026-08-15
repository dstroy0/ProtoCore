// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hmac_sha256.c
 * @brief HMAC-SHA2-256 implementation (RFC 2104).
 *
 * Implemented in terms of the protocore_sha256 streaming functions so it compiles identically on Arduino and
 * native. The inner SHA-256 hardware acceleration (where present) is transparent through those calls.
 *
 * RFC 2104 construction: HMAC(K, m) = H((K XOR opad) || H((K XOR ipad) || m)), H = SHA-256, ipad = 0x36
 * repeated, opad = 0x5c repeated. Keys > 64 bytes are pre-hashed; keys <= 64 are zero-padded to the
 * 64-byte block. SSH-derived MAC keys are 32 bytes, so they are padded, not pre-hashed.
 *
 * Nothing here owns storage or touches the pool. The caller hands over PROTOCORE_HMAC_SHA256_BORROW bytes and
 * this file splits them by offset: the inner hash's own bytes, the outer key block, and the transient
 * set init and final work in. A connection takes those bytes once for its slot and reuses them for
 * every packet, so a MAC on the packet path costs no borrow and no wipe.
 */

#if PROTOCORE_ENABLE_HMAC_SHA256

#include "crypto/mac/hmac_sha256.h"
#include "crypto/crypto_opt.h"
#include "mmgr/protomem.h"

// HMAC-SHA256 is HW-SHA-dominated; the only -O lever is its SW key-block glue. On the P4 that rides the per-die
// -O3 default (whose win is -O3's loop-unroll parameter budget). The S3's ~4% O3 edge is the same parameter
// class (bisected on-device: -fpeel-loops and -funswitch-loops both no-op), not a single transform, and not
// worth -O3's code-size / miscompile baggage on a HW-dominated MAC - so the S3 keeps the -O2 default. Capturing
// that 4% deliberately would take a source #pragma GCC unroll on the key-block loops (a code change, not a flag).
// See crypto_opt.h caveat 1.
PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// The transient half of the caller's bytes: live inside init and inside final, dead between them. The
// two 64-byte key blocks double as key-padding scratch for build_key_block.
typedef struct
{
    uint8_t opad[64]; ///< one-shot opad block (persists inner->outer); else key-pad scratch
    uint8_t ipad[64]; ///< ipad block; also key-pad scratch once its ipad is consumed
    uint8_t inner_digest[PROTOCORE_SHA256_DIGEST_LEN]; ///< H((K XOR ipad) || m)
    protocore_sha256_ctx hash; ///< transient hash: one-shot inner then outer; streaming final outer
} HmacWork;

// The caller's working bytes, split: the inner hash's own, the outer key block, the transient set, and
// the bytes that set's own hash works out of.
#define HMAC_OFF_INNER 0u
#define HMAC_OFF_OKEY (HMAC_OFF_INNER + PROTOCORE_SHA256_BORROW)
#define HMAC_OFF_WORK (HMAC_OFF_OKEY + PROTOCORE_SHA256_BLOCK_LEN)
#define HMAC_OFF_HASH (HMAC_OFF_WORK + sizeof(HmacWork))
static_assert(HMAC_OFF_HASH + PROTOCORE_SHA256_BORROW <= PROTOCORE_HMAC_SHA256_BORROW,
              "PROTOCORE_HMAC_SHA256_BORROW is short of the split - raise it in protocore_config.h, which "
              "every consumer sizes its own borrow from");

// Build one 64-byte HMAC key block into @p block (RFC 2104 sec 2), using @p scratch (64 bytes) to hold the
// zero-padded / pre-hashed key. Both @p block and @p scratch are pool-resident, never the stack.
static void build_key_block(const uint8_t *key, size_t key_len, uint8_t block[64], uint8_t pad_byte,
                            uint8_t scratch[64], uint8_t *hash_work)
{
    mem.set(scratch, 0, 64);
    if (key_len > 64)
    {
        protocore_sha256(hash_work, key, key_len,
                         scratch); // keys longer than the block are replaced by their SHA-256 hash
    }
    else
    {
        for (size_t i = 0; i < key_len; i++)
        {
            scratch[i] = key[i];
        }
    }
    for (int i = 0; i < 64; i++)
    {
        block[i] = scratch[i] ^ pad_byte;
    }
}

void protocore_hmac_sha256_init(protocore_hmac_sha256_ctx *ctx, uint8_t *work, const uint8_t *key, size_t key_len)
{
    ctx->work = work;
    HmacWork *w = (HmacWork *)(work + HMAC_OFF_WORK);
    // ipad -> scratch (opad slot holds the padded key), opad -> the slot final reads it back from
    build_key_block(key, key_len, w->ipad, 0x36u, w->opad, work + HMAC_OFF_HASH);
    build_key_block(key, key_len, work + HMAC_OFF_OKEY, 0x5cu, w->opad, work + HMAC_OFF_HASH);

    protocore_sha256_init(&ctx->inner, work + HMAC_OFF_INNER);
    protocore_sha256_update(&ctx->inner, w->ipad, PROTOCORE_SHA256_BLOCK_LEN);
}

void protocore_hmac_sha256_update(protocore_hmac_sha256_ctx *ctx, const uint8_t *data, size_t len)
{
    protocore_sha256_update(&ctx->inner, data, len);
}

void protocore_hmac_sha256_final(protocore_hmac_sha256_ctx *ctx, uint8_t mac[PROTOCORE_HMAC_SHA256_LEN])
{
    HmacWork *w = (HmacWork *)(ctx->work + HMAC_OFF_WORK);
    protocore_sha256_final(&ctx->inner, w->inner_digest);

    // Outer hash: H(okey || inner_digest)
    protocore_sha256_init(&w->hash, ctx->work + HMAC_OFF_HASH);
    protocore_sha256_update(&w->hash, ctx->work + HMAC_OFF_OKEY, PROTOCORE_SHA256_BLOCK_LEN);
    protocore_sha256_update(&w->hash, w->inner_digest, PROTOCORE_SHA256_DIGEST_LEN);
    protocore_sha256_final(&w->hash, mac);
}

void protocore_hmac_sha256(uint8_t *work, const uint8_t *key, size_t key_len, const uint8_t *data, size_t len,
                           uint8_t mac[PROTOCORE_HMAC_SHA256_LEN])
{
    // Self-contained (does not build a caller-facing context): ipad block first, fold it into the inner hash,
    // then reuse its slot as the opad key-padding scratch - so no key block ever lands on the stack.
    HmacWork *w = (HmacWork *)(work + HMAC_OFF_WORK);
    uint8_t *hw = work + HMAC_OFF_HASH;
    build_key_block(key, key_len, w->ipad, 0x36u, w->opad, hw); // ipad block (opad slot as key-pad scratch)
    protocore_sha256_init(&w->hash, hw);
    protocore_sha256_update(&w->hash, w->ipad, PROTOCORE_SHA256_BLOCK_LEN);
    protocore_sha256_update(&w->hash, data, len);
    protocore_sha256_final(&w->hash, w->inner_digest); // inner = H((K XOR ipad) || m)

    build_key_block(key, key_len, w->opad, 0x5cu, w->ipad, hw); // opad block (ipad slot now free as scratch)
    protocore_sha256_init(&w->hash, hw);
    protocore_sha256_update(&w->hash, w->opad, PROTOCORE_SHA256_BLOCK_LEN);
    protocore_sha256_update(&w->hash, w->inner_digest, PROTOCORE_SHA256_DIGEST_LEN);
    protocore_sha256_final(&w->hash, mac); // HMAC = H((K XOR opad) || inner)
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HMAC_SHA256
