// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hmac_sha512.c
 * @brief HMAC-SHA2-512 implementation (RFC 2104). See hmac_sha512.h.
 *
 * HMAC(K, m) = H((K XOR opad) || H((K XOR ipad) || m)), H = SHA-512, block = 128 bytes,
 * ipad = 0x36 repeated, opad = 0x5c repeated.
 */

#include "crypto/mac/hmac_sha512.h"
#include "crypto/crypto_opt.h"
#include "mmgr/protomem.h"
PROTOCORE_CRYPTO_HOT

// The transient half of the caller's bytes: live inside init and inside final, dead between them.
typedef struct
{
    uint8_t ipad[PROTOCORE_SHA512_BLOCK_LEN];          ///< K XOR 0x36, folded into the inner hash by init
    uint8_t kpad[PROTOCORE_SHA512_BLOCK_LEN];          ///< the key zero-padded to the block, or its hash
    uint8_t inner_digest[PROTOCORE_SHA512_DIGEST_LEN]; ///< H((K XOR ipad) || m)
    protocore_sha512_ctx hash;                         ///< the outer hash final runs
} Hmac512Work;

// The caller's working bytes, split: the inner hash's own, the outer key block, the transient set, and
// the bytes that set's own hash works out of.
#define HMAC512_OFF_INNER 0u
#define HMAC512_OFF_OKEY (HMAC512_OFF_INNER + PROTOCORE_SHA512_BORROW)
#define HMAC512_OFF_WORK (HMAC512_OFF_OKEY + PROTOCORE_SHA512_BLOCK_LEN)
#define HMAC512_OFF_HASH (HMAC512_OFF_WORK + sizeof(Hmac512Work))
static_assert(HMAC512_OFF_HASH + PROTOCORE_SHA512_BORROW <= PROTOCORE_HMAC_SHA512_BORROW,
              "PROTOCORE_HMAC_SHA512_BORROW is short of the split - raise it in protocore_config.h, which "
              "every consumer sizes its own borrow from");

// One 128-byte HMAC key block into @p block: keys > 128 bytes are pre-hashed (RFC 2104), else
// zero-padded, using @p kpad to hold the padded key and @p hw for the pre-hash.
static void build_key_block(const uint8_t *key, size_t key_len, uint8_t block[PROTOCORE_SHA512_BLOCK_LEN],
                            uint8_t pad_byte, uint8_t kpad[PROTOCORE_SHA512_BLOCK_LEN], uint8_t *hw)
{
    mem.set(kpad, 0, PROTOCORE_SHA512_BLOCK_LEN);
    if (key_len > PROTOCORE_SHA512_BLOCK_LEN)
    {
        protocore_sha512(hw, key, key_len, kpad); // 64-byte digest; the remaining 64 bytes stay zero
    }
    else
    {
        mem.cpy(kpad, key, key_len);
    }
    for (int i = 0; i < PROTOCORE_SHA512_BLOCK_LEN; i++)
    {
        block[i] = (uint8_t)(kpad[i] ^ pad_byte);
    }
}

void protocore_hmac_sha512_init(protocore_hmac_sha512_ctx *ctx, uint8_t *work, const uint8_t *key, size_t key_len)
{
    ctx->work = work;
    Hmac512Work *w = (Hmac512Work *)(work + HMAC512_OFF_WORK);
    // ipad -> the inner hash, opad -> the slot final reads it back from
    build_key_block(key, key_len, w->ipad, 0x36u, w->kpad, work + HMAC512_OFF_HASH);
    build_key_block(key, key_len, work + HMAC512_OFF_OKEY, 0x5cu, w->kpad, work + HMAC512_OFF_HASH);
    protocore_sha512_init(&ctx->inner, work + HMAC512_OFF_INNER);
    protocore_sha512_update(&ctx->inner, w->ipad, PROTOCORE_SHA512_BLOCK_LEN);
}

void protocore_hmac_sha512_update(protocore_hmac_sha512_ctx *ctx, const uint8_t *data, size_t len)
{
    protocore_sha512_update(&ctx->inner, data, len);
}

void protocore_hmac_sha512_final(protocore_hmac_sha512_ctx *ctx, uint8_t mac[PROTOCORE_HMAC_SHA512_LEN])
{
    Hmac512Work *w = (Hmac512Work *)(ctx->work + HMAC512_OFF_WORK);
    protocore_sha512_final(&ctx->inner, w->inner_digest);
    protocore_sha512_init(&w->hash, ctx->work + HMAC512_OFF_HASH);
    protocore_sha512_update(&w->hash, ctx->work + HMAC512_OFF_OKEY, PROTOCORE_SHA512_BLOCK_LEN);
    protocore_sha512_update(&w->hash, w->inner_digest, PROTOCORE_SHA512_DIGEST_LEN);
    protocore_sha512_final(&w->hash, mac);
}

void protocore_hmac_sha512(uint8_t *work, const uint8_t *key, size_t key_len, const uint8_t *data, size_t len,
                           uint8_t mac[PROTOCORE_HMAC_SHA512_LEN])
{
    protocore_hmac_sha512_ctx ctx = {0};
    protocore_hmac_sha512_init(&ctx, work, key, key_len);
    protocore_hmac_sha512_update(&ctx, data, len);
    protocore_hmac_sha512_final(&ctx, mac);
}
