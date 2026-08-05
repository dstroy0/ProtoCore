// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file kdf.c
 * @brief SP800-108 counter-mode KDF with HMAC-SHA256 PRF (see kdf.h).
 *
 * The HMAC key is constant across counter iterations, so the ipad/opad key blocks are derived once and
 * reused for every block; only the (counter || fixed) message changes. Streaming SHA-256 (pc_sha256_*)
 * inherits hardware acceleration on ESP32 transparently.
 */

#include "crypto/kdf/kdf.h"
#include "crypto/crypto_opt.h"
#include "crypto/hash/sha256.h"
#include "shared_primitives/endian.h"

PC_CRYPTO_HOT

proto_bool pc_kdf_ctr_hmac_sha256(const uint8_t *ki, size_t ki_len, const uint8_t *fixed, size_t fixed_len,
                                  uint8_t *out, size_t out_len)
{
    if (!ki || !fixed || !out || out_len == 0)
    {
        return PROTO_FALSE;
    }
    // The HMAC key is constant across counter iterations, so derive its pads once (RFC 2104).
    uint8_t k[64];
    memset(k, 0, sizeof(k));
    if (ki_len > 64)
    {
        pc_sha256(ki, ki_len, k); // a key longer than the block is hashed to 32 octets first
    }
    else
    {
        memcpy(k, ki, ki_len);
    }
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++)
    {
        ipad[i] = (uint8_t)(k[i] ^ 0x36u);
        opad[i] = (uint8_t)(k[i] ^ 0x5cu);
    }

    // K(i) = HMAC-SHA256(Ki, [i]_32be || fixed); concatenate blocks for i = 1, 2, ... then truncate.
    size_t done = 0;
    for (uint32_t counter = 1; done < out_len; counter++)
    {
        uint8_t ctr[4];
        pc_wr32be(ctr, counter);
        pc_sha256_ctx c;
        uint8_t inner[32];
        pc_sha256_init(&c);
        pc_sha256_update(&c, ipad, 64);
        pc_sha256_update(&c, ctr, 4);
        pc_sha256_update(&c, fixed, fixed_len);
        pc_sha256_final(&c, inner);
        uint8_t block[32];
        pc_sha256_init(&c);
        pc_sha256_update(&c, opad, 64);
        pc_sha256_update(&c, inner, 32);
        pc_sha256_final(&c, block);
        size_t take = (out_len - done < 32) ? out_len - done : 32;
        memcpy(out + done, block, take);
        done += take;
    }
    return PROTO_TRUE;
}
