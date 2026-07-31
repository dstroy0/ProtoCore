// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hmac_sha512.cpp
 * @brief HMAC-SHA2-512 implementation (RFC 2104). See hmac_sha512.h.
 *
 * HMAC(K, m) = H((K XOR opad) || H((K XOR ipad) || m)), H = SHA-512, block = 128 bytes,
 * ipad = 0x36 repeated, opad = 0x5c repeated.
 */

#include "crypto/mac/hmac_sha512.h"
#include "crypto/crypto_opt.h"
PC_CRYPTO_HOT

namespace
{
// One 128-byte HMAC key block: keys > 128 bytes are pre-hashed (RFC 2104), else zero-padded.
void build_key_block(const uint8_t *key, size_t key_len, uint8_t block[PC_SHA512_BLOCK_LEN], uint8_t pad_byte)
{
    uint8_t k[PC_SHA512_BLOCK_LEN] = {0};
    if (key_len > PC_SHA512_BLOCK_LEN)
    {
        pc_sha512(key, key_len, k); // 64-byte digest; the remaining 64 bytes stay zero
    }
    else
    {
        for (size_t i = 0; i < key_len; i++)
        {
            k[i] = key[i];
        }
    }
    for (int i = 0; i < PC_SHA512_BLOCK_LEN; i++)
    {
        block[i] = (uint8_t)(k[i] ^ pad_byte);
    }
}
} // namespace

void pc_hmac_sha512_init(pc_hmac_sha512_ctx *ctx, const uint8_t *key, size_t key_len)
{
    uint8_t ikey[PC_SHA512_BLOCK_LEN];
    build_key_block(key, key_len, ikey, 0x36u);      // ipad
    build_key_block(key, key_len, ctx->okey, 0x5cu); // opad (kept for the final step)
    pc_sha512_init(&ctx->inner);
    pc_sha512_update(&ctx->inner, ikey, PC_SHA512_BLOCK_LEN);
}

void pc_hmac_sha512_update(pc_hmac_sha512_ctx *ctx, const uint8_t *data, size_t len)
{
    pc_sha512_update(&ctx->inner, data, len);
}

void pc_hmac_sha512_final(pc_hmac_sha512_ctx *ctx, uint8_t mac[PC_HMAC_SHA512_LEN])
{
    uint8_t inner_digest[PC_SHA512_DIGEST_LEN];
    pc_sha512_final(&ctx->inner, inner_digest);
    pc_sha512_ctx outer;
    pc_sha512_init(&outer);
    pc_sha512_update(&outer, ctx->okey, PC_SHA512_BLOCK_LEN);
    pc_sha512_update(&outer, inner_digest, PC_SHA512_DIGEST_LEN);
    pc_sha512_final(&outer, mac);
}

void pc_hmac_sha512(const uint8_t *key, size_t key_len, const uint8_t *data, size_t len,
                    uint8_t mac[PC_HMAC_SHA512_LEN])
{
    pc_hmac_sha512_ctx ctx;
    pc_hmac_sha512_init(&ctx, key, key_len);
    pc_hmac_sha512_update(&ctx, data, len);
    pc_hmac_sha512_final(&ctx, mac);
}
