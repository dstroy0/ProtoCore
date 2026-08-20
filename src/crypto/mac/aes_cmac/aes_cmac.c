// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file aes_cmac.c
 * @brief AES-128-CMAC implementation (see aes_cmac.h).
 *
 * HW path: the AES-128 block runs on the part's AES accelerator (test/core_setup/hal/esp/esp_aes_hal.h).
 * SW path: the shared table-free software AES-128 (crypto/cipher/aes_block.h). The CMAC
 * construction (subkey derivation + CBC-MAC + last-block handling) is identical on both.
 *
 * The context is this file's. The module's own borrow is split by offset into the block context with
 * its subkeys, the prepared last block, the CBC-MAC chaining value, and the block fed to the cipher.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_AES_CMAC

#if !PROTOCORE_HAS_HW_AES
#include "crypto/cipher/aes_block/aes_block.h" // native software AES-128 block
#endif
#include "crypto/crypto_opt.h"
#include "crypto/mac/aes_cmac/aes_cmac.h"
#include "mmgr/protomem/protomem.h"

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// AES-128 single-block encrypt seam - one small wrapper, two platform bodies
// ---------------------------------------------------------------------------

#if PROTOCORE_HAS_HW_AES

typedef struct
{
    uint8_t key[16]; ///< reloaded into the accelerator's key bank per block
} AesBlk;
static inline void blk_init(AesBlk *b, const uint8_t key[16])
{
    mem.cpy(b->key, key, 16);
}
static inline void blk_enc(AesBlk *b, const uint8_t in[16], uint8_t out[16])
{
    protocore_aes_hw_acquire();
    protocore_aes_hw_setkey(b->key, 16);
    protocore_aes_hw_block(in, out);
    protocore_aes_hw_release();
}
static inline void blk_free(AesBlk *b)
{
    (void)b; // the key bytes are the caller's borrow, released and wiped with it
}

#endif

#if !PROTOCORE_HAS_HW_AES

typedef struct
{
    uint32_t rk[44]; ///< AES-128 expanded round-key schedule (44 words).
} AesBlk;
static inline void blk_init(AesBlk *b, const uint8_t key[16])
{
    protocore_aes_key_expand(key, 4, b->rk);
}
static inline void blk_enc(AesBlk *b, const uint8_t in[16], uint8_t out[16])
{
    protocore_aes_encrypt_block(b->rk, 10, in, out);
}
static inline void blk_free(AesBlk *b)
{
    (void)b; // the software path holds no vendor allocation to release
}

#endif // !PROTOCORE_HAS_HW_AES (SW path)

// The one definition of AesCmacCtx - private to this TU. It sits at AES_CMAC_OFF_CTX in the caller's
// borrow, so its size never leaves this file and no consumer can name it. AesBlk is the arm's own
// block context, so the context's size follows the arm and the offsets below follow it.
//
// Only what is not derivable: the three CMAC blocks live at fixed offsets in the caller's borrow, so
// a macro computes them from the pointer rather than the context storing them.
typedef struct AesCmacCtx
{
    AesBlk blk;                         ///< the arm's AES-128 block context
    uint8_t l[PROTOCORE_AES_CMAC_LEN];  ///< AES(key, 0^128), the subkey precursor
    uint8_t k1[PROTOCORE_AES_CMAC_LEN]; ///< subkey XORed into a whole final block
    uint8_t k2[PROTOCORE_AES_CMAC_LEN]; ///< subkey XORed into the 10*-padded final block
} AesCmacCtx;

// The caller's borrow, split: the block context with its subkeys, the prepared last block, the
// CBC-MAC chaining value, and the block fed to the cipher.
#define AES_CMAC_OFF_CTX 0u
#define AES_CMAC_OFF_LAST (AES_CMAC_OFF_CTX + sizeof(struct AesCmacCtx))
#define AES_CMAC_OFF_X (AES_CMAC_OFF_LAST + PROTOCORE_AES_CMAC_LEN)
#define AES_CMAC_OFF_Y (AES_CMAC_OFF_X + PROTOCORE_AES_CMAC_LEN)
static_assert(AES_CMAC_OFF_Y + PROTOCORE_AES_CMAC_LEN <= PROTOCORE_AES_CMAC_BORROW,
              "PROTOCORE_AES_CMAC_BORROW is short of the block context, the subkeys and the three CMAC "
              "blocks - raise it in protocore_config.h, which sums it into the secure arena");

// The regions, at their offsets in the caller's borrow.
#define AES_CMAC_CTX(w) ((struct AesCmacCtx *)(void *)((w) + AES_CMAC_OFF_CTX))
#define AES_CMAC_LAST(w) ((w) + AES_CMAC_OFF_LAST)
#define AES_CMAC_X(w) ((w) + AES_CMAC_OFF_X)
#define AES_CMAC_Y(w) ((w) + AES_CMAC_OFF_Y)

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
static void subkeys(uint8_t *restrict work)
{
    static const uint8_t RB = 0x87; // the 128-bit-block CMAC constant
    struct AesCmacCtx *ctx = AES_CMAC_CTX(work);
    uint8_t zero[16] = {0};
    blk_enc(&ctx->blk, zero, ctx->l);
    if (shl1(ctx->l, ctx->k1))
    {
        ctx->k1[15] ^= RB;
    }
    if (shl1(ctx->k1, ctx->k2))
    {
        ctx->k2[15] ^= RB;
    }
}

// --- the entry -------------------------------------------------------------

// One-shot over the members already set: expand the key, derive the subkeys, CBC-MAC the message.
static void aes_cmac_mac(uint8_t *restrict work)
{
    AesCmac.ok = PROTO_FALSE;
    if (!AesCmac.mac_args.key || !AesCmac.mac_args.out)
    {
        return;
    }
    const uint8_t *msg = AesCmac.mac_args.msg;
    const size_t msg_len = AesCmac.mac_args.msg_len;
    uint8_t *mac = AesCmac.mac_args.out;

    struct AesCmacCtx *ctx = AES_CMAC_CTX(work);
    blk_init(&ctx->blk, AesCmac.mac_args.key);
    subkeys(work);

    // n = number of blocks; the message is a whole number of blocks iff msg_len > 0 && msg_len % 16 == 0.
    const size_t n = msg_len == 0 ? 1 : (msg_len + 15) / 16;
    const proto_bool complete = msg_len != 0 && (msg_len % 16) == 0;

    // Build the last block: the final 16 bytes XOR K1 when complete, else the 10*-padded remainder XOR K2.
    uint8_t *last = AES_CMAC_LAST(work);
    if (complete)
    {
        for (int i = 0; i < 16; i++)
        {
            last[i] = (uint8_t)(msg[(n - 1) * 16 + i] ^ ctx->k1[i]);
        }
    }
    else
    {
        const size_t rem = msg_len - (n - 1) * 16; // 0..15 bytes in the final partial block
        for (size_t i = 0; i < 16; i++)
        {
            uint8_t m = i < rem ? msg[(n - 1) * 16 + i] : (i == rem ? 0x80 : 0x00); // pad 10*
            last[i] = (uint8_t)(m ^ ctx->k2[i]);
        }
    }

    // CBC-MAC: X starts at 0; fold blocks 1..n-1, then the prepared last block.
    uint8_t *x = AES_CMAC_X(work);
    uint8_t *y = AES_CMAC_Y(work);
    mem.zero(x, PROTOCORE_AES_CMAC_LEN);
    for (size_t i = 0; i + 1 < n; i++)
    {
        for (int j = 0; j < 16; j++)
        {
            y[j] = (uint8_t)(x[j] ^ msg[i * 16 + j]);
        }
        blk_enc(&ctx->blk, y, x);
    }
    for (int j = 0; j < 16; j++)
    {
        y[j] = (uint8_t)(x[j] ^ last[j]);
    }
    blk_enc(&ctx->blk, y, mac);

    blk_free(&ctx->blk);
    AesCmac.ok = PROTO_TRUE;
}

AesCmacNs AesCmac = {.mac = aes_cmac_mac};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_AES_CMAC
