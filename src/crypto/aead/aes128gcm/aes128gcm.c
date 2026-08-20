// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file aes128gcm.c
 * @brief AEAD_AES_128_GCM implementation (RFC 5116, NIST SP 800-38D).
 *
 * Two keyed concerns and one set of entries.
 *
 * HW path: the AES-128 block runs on the part's AES accelerator (test/core_setup/hal/esp/esp_aes_hal.h).
 * SW path: the shared table-free software AES-128 (crypto/cipher/aes_block.h). The GCM construction
 * (GCTR, the 4-bit-table GHASH, the length block and the tag mask) is identical on both.
 *
 * The contexts are this file's. The module's own borrow carries both: the AEAD context at the base and
 * the single-block context after it, so the two block contexts, the GHASH table, the keystream, the
 * accumulator, the tag mask and the counters all live in the caller's secure bytes and none of them
 * touches the stack or BSS.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_AES128GCM

#include "mmgr/secure/secure.h" // protocore_secure_wipe: a schedule that is done is zeroed

#if PROTOCORE_HAS_HW_AESGCM
#endif
#if !PROTOCORE_HAS_HW_AESGCM
#include "crypto/cipher/aes_block/aes_block.h" // native software AES-128 key schedule and single-block encrypt
#endif
#include "crypto/aead/aes128gcm/aes128gcm.h"
#include "crypto/crypto_opt.h"
#include "crypto/ct_eq.h"           // protocore_ct_eq
#include "crypto/mac/ghash/ghash.h" // the 4-bit-table GF(2^128) hash
#include "mmgr/protomem/protomem.h"
#include "mmgr/rawmemcpy/rawmemcpy.h" // proto_raw_u32 - the aliasing-permitted word load
PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// AES-128 single-block encrypt seam - one small wrapper, two platform bodies
// ---------------------------------------------------------------------------

#if PROTOCORE_HAS_HW_AESGCM

typedef struct
{
    uint8_t key[16]; ///< reloaded into the accelerator's key bank per block
} Aes128Blk;
static inline void blk_init(Aes128Blk *b, const uint8_t key[16])
{
    mem.cpy(b->key, key, 16);
}
static inline void blk_enc(Aes128Blk *b, const uint8_t in[16], uint8_t out[16])
{
    protocore_aes_hw_acquire();
    protocore_aes_hw_setkey(b->key, 16);
    protocore_aes_hw_block(in, out);
    protocore_aes_hw_release();
}
static inline void blk_free(Aes128Blk *b)
{
    // The accelerator's key bank is reloaded per block and holds nothing between them, but this
    // copy of the key does. It outlives the caller whenever the context is not a scratch borrow -
    // a TLS connection's keys live as long as the connection - so the bytes go here.
    protocore_secure_wipe(b->key, sizeof(b->key));
}

#endif

#if !PROTOCORE_HAS_HW_AESGCM

typedef struct
{
    uint32_t rk[44]; ///< AES-128 expanded round-key schedule (11 round keys x 4 words).
} Aes128Blk;
static inline void blk_init(Aes128Blk *b, const uint8_t key[16])
{
    protocore_aes_key_expand(key, 4, b->rk);
}
static inline void blk_enc(Aes128Blk *b, const uint8_t in[16], uint8_t out[16])
{
    protocore_aes_encrypt_block(b->rk, 10, in, out);
}
static inline void blk_free(Aes128Blk *b)
{
    // No vendor allocation to release, but the expanded schedule IS the key: every round key is
    // derived from it and the original is recoverable from the last one. It outlives the caller
    // whenever the context is not a scratch borrow, so the bytes go here.
    protocore_secure_wipe(b->rk, sizeof(b->rk));
}

#endif // !PROTOCORE_HAS_HW_AESGCM (SW path)

// The one definition of Aes128GcmWork - private to this TU. It sits at AES128GCM_OFF_GCM in the
// caller's borrow, so its size never leaves this file and no consumer can name it. Aes128Blk is the
// arm's own block context, so the context's size follows the arm and the offsets below follow it.
//
// Only what is not derivable: the region lives at a fixed offset in the borrow, so a macro computes it
// from the pointer rather than anything storing it.
typedef struct
{
    Aes128Blk blk;   ///< the arm's AES-128 block context
    uint8_t h[16];   ///< GHASH subkey H = E(K, 0^128).
    uint8_t ks[16];  ///< GCTR keystream block.
    uint8_t acc[16]; ///< GHASH accumulator.
    uint8_t lb[16];  ///< length block (aad_len || cipher_len, in bits).
    uint8_t ej0[16]; ///< E(K, J0), the tag mask.
    uint8_t j0[16];  ///< pre-counter block J0 = nonce || 0^31 || 1.
    uint8_t ctr[16]; ///< running GCTR counter.
    uint8_t tag[16]; ///< computed tag: an open compares it, a seal writes the caller's buffer directly.
} Aes128GcmWork;

// The caller's borrow, split: the keyed AEAD context at the base, the region the nested GHASH runs out
// of, then the single-block context the header protection runs under. GHASH is driven through its own
// namespace, so this borrow carries a region for it rather than naming any term of its.
#define AES128GCM_OFF_GCM 0u
#define AES128GCM_OFF_GHASH (AES128GCM_OFF_GCM + sizeof(Aes128GcmWork))
#define AES128GCM_OFF_BLOCK (AES128GCM_OFF_GHASH + PROTOCORE_GHASH_BORROW)
#define AES128GCM_OFF_END (AES128GCM_OFF_BLOCK + sizeof(Aes128Blk))
static_assert(AES128GCM_OFF_END <= PROTOCORE_AES128GCM_BORROW,
              "PROTOCORE_AES128GCM_BORROW is short of the AEAD context, the nested GHASH borrow and the "
              "single-block context - raise it in protocore_config.h, which derives "
              "PROTOCORE_SECURE_ARENA_SIZE from it");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(
    AES128GCM_OFF_GCM % _Alignof(Aes128GcmWork) == 0,
    "AES128GCM_OFF_GCM is not a multiple of alignof(Aes128GcmWork) - AES128GCM_GCM() would return a misaligned "
    "pointer; pad the region ahead of it");
static_assert(AES128GCM_OFF_BLOCK % _Alignof(Aes128Blk) == 0,
              "AES128GCM_OFF_BLOCK is not a multiple of alignof(Aes128Blk) - AES128GCM_BLK() would return a misaligned "
              "pointer; pad the region ahead of it");

// The regions, at their offsets in the caller's borrow.
#define AES128GCM_GCM(w) ((Aes128GcmWork *)(void *)((w) + AES128GCM_OFF_GCM))
#define AES128GCM_GHASH(w) ((w) + AES128GCM_OFF_GHASH)
#define AES128GCM_BLK(w) ((Aes128Blk *)(void *)((w) + AES128GCM_OFF_BLOCK))

// --- the GCM construction: software AES-128 + table GHASH on both arms ------

// AES-128 single-block primitive (operates on the block context inside Aes128GcmWork).
static inline void aes128_ecb(Aes128GcmWork *w, const uint8_t in[16], uint8_t out[16])
{
    blk_enc(&w->blk, in, out);
}

static inline void xor16(uint8_t *dst, const uint8_t *src)
{
    // One block = four words. The raw accessors carry aligned(1) and may_alias, so each step is the
    // machine's own load and store at any alignment, and the fixup sequence only where the die needs it.
    for (int i = 0; i < 16; i += 4)
    {
        proto_raw_put_u32(dst + i, proto_raw_u32(dst + i) ^ proto_raw_u32(src + i));
    }
}

static inline void put_be64(uint8_t *p, uint64_t v)
{
    for (int i = 7; i >= 0; i--)
    {
        p[i] = (uint8_t)(v & 0xff);
        v >>= 8;
    }
}

// Increment the low 32 bits of a 16-byte counter block, big-endian, mod 2^32 (GCM inc32).
static inline void inc32(uint8_t ctr[16])
{
    // A single-byte carry (i=15 wrapping into i=14, etc.) only needs ctr[15] to roll 0xff -> 0x00,
    // i.e. ~256 GCTR blocks (~4 KiB of plaintext) through the public seal/open entries - well within
    // reach of a host test, so that carry-continue arm is exercised and is NOT excluded.
    // What genuinely cannot be reached is the loop running all 4 iterations to exhaustion with no
    // break at all, i.e. every one of the 4 bytes carrying in the SAME inc32() call, which only
    // happens when the full 32-bit counter was 0xffffffff before this call. ctr always starts at
    // inc32(J0) with J0[12..15] fixed to 0,0,0,1 (96-bit-nonce J0, NIST SP 800-38D) - not
    // caller-controlled - so reaching that requires one seal/open call over ~2^32 contiguous
    // GCTR blocks (~64 GiB of plaintext in one call): infeasible in a host test.
    for (int i = 15; i >= 12; i--)
    {
        if (++ctr[i])
        {
            break;
        }
    }
}

// GCTR (NIST SP 800-38D sec 6.5): out = in XOR AES-CTR keystream from w->ctr, advanced in place. Uses
// w->ks. @p in / @p out may alias.
static void gctr(Aes128GcmWork *w, const uint8_t *in, size_t len, uint8_t *out)
{
    size_t off = 0;
    while (off < len)
    {
        aes128_ecb(w, w->ctr, w->ks);
        inc32(w->ctr);
        size_t take = len - off;
        if (take > 16)
        {
            take = 16;
        }
        for (size_t i = 0; i < take; i++)
        {
            out[off + i] = in[off + i] ^ w->ks[i];
        }
        off += take;
    }
}

// 96-bit nonce: J0 = nonce || 0^31 || 1. Per record.
static inline void set_j0(Aes128GcmWork *w, const uint8_t nonce[12])
{
    mem.cpy(w->j0, nonce, 12);
    w->j0[12] = 0;
    w->j0[13] = 0;
    w->j0[14] = 0;
    w->j0[15] = 1;
}

// GHASH(aad || cipher) and the 16-byte tag, against the table already built for this key. Requires
// set_j0 first; leaves w->ctr alone so the caller controls the GCTR pass order.
static void gcm_tag(uint8_t *restrict work, const uint8_t *aad, size_t aad_len, const uint8_t *cipher,
                    size_t cipher_len, uint8_t tag_out[16])
{
    Aes128GcmWork *w = AES128GCM_GCM(work);
    mem.zero(w->acc, 16);
    GhashV.update_args.acc = w->acc;
    GhashV.update_args.data = aad;
    GhashV.update_args.len = aad_len;
    Ghash.update(AES128GCM_GHASH(work));
    GhashV.update_args.data = cipher;
    GhashV.update_args.len = cipher_len;
    Ghash.update(AES128GCM_GHASH(work));
    put_be64(w->lb, (uint64_t)aad_len * 8);
    put_be64(w->lb + 8, (uint64_t)cipher_len * 8);
    xor16(w->acc, w->lb);
    GhashV.mul_args.acc = w->acc;
    Ghash.mul(AES128GCM_GHASH(work));

    aes128_ecb(w, w->j0, w->ej0);
    for (int i = 0; i < 16; i++)
    {
        tag_out[i] = w->acc[i] ^ w->ej0[i];
    }
}

static proto_bool aes128gcm_key_load(uint8_t *restrict work)
{
    Aes128GcmWork *w = AES128GCM_GCM(work);
    blk_init(&w->blk, Aes128GcmV.key_args.key);
    mem.zero(w->h, 16);
    aes128_ecb(w, w->h, w->h); // H = E(K, 0^128)
    GhashV.key_args.h = w->h;
    Ghash.key_init(AES128GCM_GHASH(work));
    return PROTO_TRUE;
}

static void aes128gcm_key_release(uint8_t *restrict work)
{
    Aes128GcmWork *w = AES128GCM_GCM(work);
    blk_free(&w->blk);
    // H = E(K, 0^128) is derived from the key and forges a tag on its own, so it goes with it. The
    // rest of the context is per-record scratch over bytes the caller already holds.
    protocore_secure_wipe(w->h, sizeof(w->h));
    protocore_secure_wipe(w->ej0, sizeof(w->ej0));
}

static proto_bool aes128gcm_seal_record(uint8_t *restrict work)
{
    Aes128GcmWork *w = AES128GCM_GCM(work);
    set_j0(w, Aes128GcmV.seal_args.nonce);
    // Encrypt first (counter starts at inc32(J0)), then GHASH the resulting ciphertext.
    mem.cpy(w->ctr, w->j0, 16);
    inc32(w->ctr);
    gctr(w, Aes128GcmV.seal_args.pt, Aes128GcmV.seal_args.pt_len, Aes128GcmV.seal_args.ct_out);
    gcm_tag(work, Aes128GcmV.seal_args.aad, Aes128GcmV.seal_args.aad_len, Aes128GcmV.seal_args.ct_out,
            Aes128GcmV.seal_args.pt_len, Aes128GcmV.seal_args.tag_out);
    return PROTO_TRUE;
}

static proto_bool aes128gcm_open_record(uint8_t *restrict work)
{
    Aes128GcmWork *w = AES128GCM_GCM(work);
    set_j0(w, Aes128GcmV.open_args.nonce);
    // Authenticate over the received ciphertext BEFORE producing any plaintext.
    gcm_tag(work, Aes128GcmV.open_args.aad, Aes128GcmV.open_args.aad_len, Aes128GcmV.open_args.ct,
            Aes128GcmV.open_args.ct_len, w->tag);
    if (!protocore_ct_eq(w->tag, Aes128GcmV.open_args.tag, PROTOCORE_AES128GCM_TAG_LEN))
    {
        return PROTO_FALSE; // tag mismatch: nothing written
    }
    mem.cpy(w->ctr, w->j0, 16);
    inc32(w->ctr);
    gctr(w, Aes128GcmV.open_args.ct, Aes128GcmV.open_args.ct_len, Aes128GcmV.open_args.out);
    return PROTO_TRUE;
}

static void aes128gcm_blk_load(uint8_t *restrict work)
{
    blk_init(AES128GCM_BLK(work), Aes128GcmV.block_key_args.key);
}

static void aes128gcm_blk_run(uint8_t *restrict work)
{
    blk_enc(AES128GCM_BLK(work), Aes128GcmV.block_args.in, Aes128GcmV.block_args.out);
}

static void aes128gcm_blk_release(uint8_t *restrict work)
{
    blk_free(AES128GCM_BLK(work));
}

// --- the entries -----------------------------------------------------------

void protocore_aes128_gcm_key_init(uint8_t *restrict work)
{
    Aes128GcmV.ok = PROTO_FALSE;
    if (!Aes128GcmV.key_args.key)
    {
        return;
    }
    Aes128GcmV.ok = aes128gcm_key_load(work);
}

// Release what the context attached. The bytes themselves are the caller's: it releases the borrow and
// the pool wipes it.
void protocore_aes128_gcm_key_wipe(uint8_t *restrict work)
{
    Aes128GcmV.ok = PROTO_FALSE;
    aes128gcm_key_release(work);
    Aes128GcmV.ok = PROTO_TRUE;
}

void protocore_aes128_gcm_seal(uint8_t *restrict work)
{
    Aes128GcmV.ok = PROTO_FALSE;
    if (!Aes128GcmV.seal_args.nonce || !Aes128GcmV.seal_args.ct_out || !Aes128GcmV.seal_args.tag_out)
    {
        return;
    }
    Aes128GcmV.ok = aes128gcm_seal_record(work);
}

void protocore_aes128_gcm_open(uint8_t *restrict work)
{
    Aes128GcmV.ok = PROTO_FALSE;
    if (!Aes128GcmV.open_args.nonce || !Aes128GcmV.open_args.tag || !Aes128GcmV.open_args.out)
    {
        return;
    }
    Aes128GcmV.ok = aes128gcm_open_record(work);
}

void protocore_aes128_gcm_block_init(uint8_t *restrict work)
{
    Aes128GcmV.ok = PROTO_FALSE;
    if (!Aes128GcmV.block_key_args.key)
    {
        return;
    }
    aes128gcm_blk_load(work);
    Aes128GcmV.ok = PROTO_TRUE;
}

void protocore_aes128_gcm_block_encrypt(uint8_t *restrict work)
{
    Aes128GcmV.ok = PROTO_FALSE;
    if (!Aes128GcmV.block_args.in || !Aes128GcmV.block_args.out)
    {
        return;
    }
    aes128gcm_blk_run(work);
    Aes128GcmV.ok = PROTO_TRUE;
}

void protocore_aes128_gcm_block_wipe(uint8_t *restrict work)
{
    Aes128GcmV.ok = PROTO_FALSE;
    aes128gcm_blk_release(work);
    Aes128GcmV.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
Aes128GcmVars Aes128GcmV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_AES128GCM
