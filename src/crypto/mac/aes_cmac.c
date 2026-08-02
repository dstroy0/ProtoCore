// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file aes_cmac.c
 * @brief AES-128-CMAC implementation (see aes_cmac.h).
 *
 * Arduino path: the AES-128 block runs on mbedtls_aes_crypt_ecb() (HW AES accelerator on ESP32).
 * Native path: the shared table-free software AES-128 (crypto/cipher/aes_block.h). The CMAC
 * construction (subkey derivation + CBC-MAC + last-block handling) is identical on both.
 */

#include "crypto/mac/aes_cmac.h"
#include "crypto/crypto_opt.h"
#include <string.h>
#if PROTOCORE_HOT
#include <mbedtls/aes.h> // AES-128 single-block via the ESP32 AES peripheral
#else
#include "crypto/cipher/aes_block.h" // native software AES-128 block
#endif
PC_CRYPTO_HOT

// ---------------------------------------------------------------------------
// AES-128 single-block encrypt seam - one small wrapper, two platform bodies
// ---------------------------------------------------------------------------

#if PROTOCORE_HOT

typedef struct
{
    mbedtls_aes_context c;
} AesBlk;
static inline void blk_init(AesBlk *b, const uint8_t key[16])
{
    mbedtls_aes_init(&b->c);
    mbedtls_aes_setkey_enc(&b->c, key, 128);
}
static inline void blk_enc(AesBlk *b, const uint8_t in[16], uint8_t out[16])
{
    mbedtls_aes_crypt_ecb(&b->c, MBEDTLS_AES_ENCRYPT, in, out);
}
static inline void blk_free(AesBlk *b)
{
    mbedtls_aes_free(&b->c);
}

#else

typedef struct
{
    uint32_t rk[44]; ///< AES-128 expanded round-key schedule (44 words).
} AesBlk;
static inline void blk_init(AesBlk *b, const uint8_t key[16])
{
    pc_aes_key_expand(key, 4, b->rk);
}
static inline void blk_enc(AesBlk *b, const uint8_t in[16], uint8_t out[16])
{
    pc_aes_encrypt_block(b->rk, 10, in, out);
}
static inline void blk_free(AesBlk *b)
{
    (void)b; // the software path holds no vendor allocation to release
}

#endif // PROTOCORE_HOT

// ---------------------------------------------------------------------------
// CMAC construction (RFC 4493 / NIST SP800-38B)
// ---------------------------------------------------------------------------

// Left-shift a 16-byte big-endian value by one bit; return the bit shifted out of the MSB.
static uint8_t shl1(const uint8_t in[16], uint8_t out[16])
{
    uint8_t carry = 0;
    for (int i = 15; i >= 0; i--)
    {
        uint8_t next = (uint8_t)(in[i] >> 7);
        out[i] = (uint8_t)((in[i] << 1) | carry);
        carry = next;
    }
    return carry; // the bit that left the MSB
}

// RFC 4493 subkey generation: L = AES(key, 0^128); K1 = L<<1 (^Rb if MSB(L)); K2 = K1<<1 (^Rb if MSB(K1)).
static void subkeys(AesBlk *blk, uint8_t k1[16], uint8_t k2[16])
{
    static const uint8_t RB = 0x87; // the 128-bit-block CMAC constant
    uint8_t zero[16] = {0};
    uint8_t l[16];
    blk_enc(blk, zero, l);
    if (shl1(l, k1))
    {
        k1[15] ^= RB;
    }
    if (shl1(k1, k2))
    {
        k2[15] ^= RB;
    }
}

void pc_aes_cmac(const uint8_t key[16], const uint8_t *msg, size_t msg_len, uint8_t mac[PC_AES_CMAC_LEN])
{
    AesBlk blk;
    blk_init(&blk, key);
    uint8_t k1[16];
    uint8_t k2[16];
    subkeys(&blk, k1, k2);

    // n = number of blocks; the message is a whole number of blocks iff msg_len > 0 && msg_len % 16 == 0.
    const size_t n = msg_len == 0 ? 1 : (msg_len + 15) / 16;
    const proto_bool complete = msg_len != 0 && (msg_len % 16) == 0;

    // Build the last block: the final 16 bytes XOR K1 when complete, else the 10*-padded remainder XOR K2.
    uint8_t last[16];
    if (complete)
    {
        for (int i = 0; i < 16; i++)
        {
            last[i] = (uint8_t)(msg[(n - 1) * 16 + i] ^ k1[i]);
        }
    }
    else
    {
        const size_t rem = msg_len - (n - 1) * 16; // 0..15 bytes in the final partial block
        for (size_t i = 0; i < 16; i++)
        {
            uint8_t m = i < rem ? msg[(n - 1) * 16 + i] : (i == rem ? 0x80 : 0x00); // pad 10*
            last[i] = (uint8_t)(m ^ k2[i]);
        }
    }

    // CBC-MAC: X starts at 0; fold blocks 1..n-1, then the prepared last block.
    uint8_t x[16] = {0};
    uint8_t y[16];
    for (size_t i = 0; i + 1 < n; i++)
    {
        for (int j = 0; j < 16; j++)
        {
            y[j] = (uint8_t)(x[j] ^ msg[i * 16 + j]);
        }
        blk_enc(&blk, y, x);
    }
    for (int j = 0; j < 16; j++)
    {
        y[j] = (uint8_t)(x[j] ^ last[j]);
    }
    blk_enc(&blk, y, mac);

    blk_free(&blk);
}
