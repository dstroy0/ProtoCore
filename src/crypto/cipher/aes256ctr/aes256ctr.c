// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file aes256ctr.c
 * @brief AES-256-CTR implementation (see aes256ctr.h).
 *
 * HW path: the AES-256 block runs on the part's AES accelerator (test/core_setup/hal/esp/esp_aes_hal.h).
 * SW path: the compact software AES-256 of aes_block.h (256-byte forward S-box + GF(2^8)
 * MixColumns). The CTR framing over that block - keystream, counter advance, XOR - is one body.
 *
 * The context is this file's. The module's own borrow is split by offset into the expanded key
 * schedule and the one keystream block, so expanded key material never lands in BSS or on the stack.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_AES256CTR

#if PROTOCORE_HAS_HW_AES
#include "mmgr/protomem/protomem.h"
#endif
#if !PROTOCORE_HAS_HW_AES
#include "crypto/cipher/aes_block/aes_block.h" // native software AES-256 block
#endif
#include "crypto/cipher/aes256ctr/aes256ctr.h"
#include "crypto/crypto_opt.h"

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// AES-256 single-block encrypt seam - one small wrapper, two platform bodies
// ---------------------------------------------------------------------------

#if PROTOCORE_HAS_HW_AES

typedef struct
{
    uint8_t key[32]; ///< reloaded into the accelerator's key bank per block
} AesBlk;
static inline void blk_init(AesBlk *b, const uint8_t key[32])
{
    mem.cpy(b->key, key, 32);
}
static inline void blk_enc(AesBlk *b, const uint8_t in[16], uint8_t out[16])
{
    protocore_aes_hw_acquire();
    protocore_aes_hw_setkey(b->key, 32);
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
    uint32_t rk[60]; ///< AES-256 round keys, 4 * (8 + 7) words (FIPS 197 §5.2)
} AesBlk;
static inline void blk_init(AesBlk *b, const uint8_t key[32])
{
    protocore_aes_key_expand(key, 8, b->rk);
}
static inline void blk_enc(AesBlk *b, const uint8_t in[16], uint8_t out[16])
{
    protocore_aes_encrypt_block(b->rk, 14, in, out);
}
static inline void blk_free(AesBlk *b)
{
    (void)b; // the software path holds no vendor allocation to release
}

#endif // !PROTOCORE_HAS_HW_AES (SW path)

// The expanded key, private to this TU, in the form the arm's block cipher takes. Only what is not
// derivable: the keystream block lives at a fixed offset in the caller's borrow, so a macro computes
// it from the pointer rather than the context storing it.
typedef struct
{
    AesBlk blk; ///< the arm's AES-256 block context
} Aes256CtrCtx;

// The caller's borrow, split: the expanded key, then the one keystream block AES(counter) lands in.
#define AES256CTR_OFF_CTX 0u
#define AES256CTR_OFF_KS (AES256CTR_OFF_CTX + sizeof(Aes256CtrCtx))
static_assert(AES256CTR_OFF_KS + PROTOCORE_AES256CTR_CTR_LEN <= PROTOCORE_AES256CTR_BORROW,
              "PROTOCORE_AES256CTR_BORROW is short of the expanded key and the keystream block - raise it in "
              "protocore_config.h, which derives PROTOCORE_SECURE_ARENA_SIZE from it");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(
    AES256CTR_OFF_CTX % _Alignof(Aes256CtrCtx) == 0,
    "AES256CTR_OFF_CTX is not a multiple of alignof(Aes256CtrCtx) - AES256CTR_CTX() would return a misaligned "
    "pointer; pad the region ahead of it");

// The regions, at their offsets in the caller's borrow.
#define AES256CTR_CTX(w) ((Aes256CtrCtx *)(void *)((w) + AES256CTR_OFF_CTX))
#define AES256CTR_KS(w) ((w) + AES256CTR_OFF_KS)

// ---------------------------------------------------------------------------
// CTR framing (RFC 3686 / NIST SP800-38A), one body over the seam above
// ---------------------------------------------------------------------------

// XOR the CTR keystream over @p len bytes, advancing @p counter in place.
static void aes256ctr_stream(uint8_t *restrict work, const uint8_t *key, uint8_t *counter, const uint8_t *in,
                             uint8_t *out, size_t len)
{
    AesBlk *blk = &AES256CTR_CTX(work)->blk;
    uint8_t *ks = AES256CTR_KS(work);
    blk_init(blk, key);
    uint8_t pos = 0;
    for (size_t i = 0; i < len; i++)
    {
        if (pos == 0)
        {
            blk_enc(blk, counter, ks);    // keystream = AES(counter)
            for (int j = 15; j >= 0; j--) // then advance the big-endian counter by one block
            {
                if (++counter[j])
                {
                    break;
                }
            }
        }
        out[i] = in[i] ^ ks[pos];
        pos = (uint8_t)((pos + 1u) & 0x0fu);
    }
    blk_free(blk);
}

// One keystream block AES(@p counter) into the borrow's ks region; @p counter is read, not advanced.
static void aes256ctr_keystream(uint8_t *restrict work, const uint8_t *key, const uint8_t *counter)
{
    AesBlk *blk = &AES256CTR_CTX(work)->blk;
    uint8_t *ks = AES256CTR_KS(work);
    blk_init(blk, key);
    blk_enc(blk, counter, ks);
    blk_free(blk);
}

// --- the entries -----------------------------------------------------------

void protocore_aes256ctr_crypt(uint8_t *restrict work)
{
    Aes256CtrV.ok = PROTO_FALSE;
    if (!Aes256CtrV.crypt_args.key || !Aes256CtrV.crypt_args.counter || !Aes256CtrV.crypt_args.in ||
        !Aes256CtrV.crypt_args.out)
    {
        return;
    }
    aes256ctr_stream(work, Aes256CtrV.crypt_args.key, Aes256CtrV.crypt_args.counter, Aes256CtrV.crypt_args.in,
                     Aes256CtrV.crypt_args.out, Aes256CtrV.crypt_args.len);
    Aes256CtrV.ok = PROTO_TRUE;
}

// The keystream block for the current counter, XOR'd over the 4 length bytes; the counter stands still.
void protocore_aes256ctr_get_length(uint8_t *restrict work)
{
    Aes256CtrV.ok = PROTO_FALSE;
    Aes256CtrV.length = 0;
    if (!Aes256CtrV.get_length_args.key || !Aes256CtrV.get_length_args.counter || !Aes256CtrV.get_length_args.enc4)
    {
        return;
    }
    aes256ctr_keystream(work, Aes256CtrV.get_length_args.key, Aes256CtrV.get_length_args.counter);
    const uint8_t *enc4 = Aes256CtrV.get_length_args.enc4;
    const uint8_t *ks = AES256CTR_KS(work);
    Aes256CtrV.length = ((uint32_t)(enc4[0] ^ ks[0]) << 24) | ((uint32_t)(enc4[1] ^ ks[1]) << 16) |
                        ((uint32_t)(enc4[2] ^ ks[2]) << 8) | (uint32_t)(enc4[3] ^ ks[3]);
    Aes256CtrV.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
Aes256CtrVars Aes256CtrV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_AES256CTR
