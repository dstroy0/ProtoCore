// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file aesgcm.c
 * @brief AES-256-GCM implementation (RFC 5116, NIST SP 800-38D).
 *
 * One keyed context and one set of entries. The GCM construction - H and the 4-bit GHASH table, J0 and
 * the GCTR counter, the length block and the tag mask - is one body on both arms; only the AES-256
 * block under it changes arm.
 *
 * HW path: the block runs on the part's AES accelerator (core_setup/hal/esp/esp_aes_hal.h).
 * SW path: the table-free software AES-256 of crypto/cipher/aes_block.h.
 *
 * The context is this file's. The module's own borrow carries it at the base, so the block context, the
 * GHASH table, the keystream, the accumulator, the tag mask and the counters all live in the caller's
 * secure bytes and none of them touches the stack or BSS.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_AESGCM

#if PROTOCORE_HAS_HW_AESGCM
#endif
#if !PROTOCORE_HAS_HW_AESGCM
#include "crypto/cipher/aes_block.h" // software AES-256 key schedule and single-block encrypt
#endif
#include "crypto/aead/aesgcm.h"
#include "crypto/crypto_opt.h"
#include "crypto/ct_eq.h"     // protocore_ct_eq
#include "crypto/mac/ghash.h" // the 4-bit-table GF(2^128) hash
#include "mmgr/protomem.h"
#include "mmgr/rawmemcpy.h" // proto_raw_u32 - the aliasing-permitted word load

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// --- the AES-256 block context - one member, two platform shapes -----------

#if PROTOCORE_HAS_HW_AESGCM

typedef struct
{
    uint8_t key[32]; ///< reloaded into the accelerator's key bank per block
} AesBlk;

#endif

#if !PROTOCORE_HAS_HW_AESGCM

typedef struct
{
    uint32_t rk[60]; ///< AES-256 expanded round-key schedule (software).
} AesBlk;

#endif

// The one definition of GcmWork - private to this TU, and the same members on both arms. It sits at
// AESGCM_OFF_CTX in the caller's borrow, so its size never leaves this file and no consumer can name it.
// AesBlk is the arm's own block context, so the context's size follows the arm.
//
// Only what is not derivable: the region lives at a fixed offset in the borrow, so a macro computes it
// from the pointer rather than anything storing it.
typedef struct
{
    AesBlk blk;      ///< the arm's AES-256 block context.
    uint8_t h[16];   ///< GHASH subkey H = E(K, 0^128).
    uint8_t ks[16];  ///< GCTR keystream block (also the zero input used to derive H).
    uint8_t acc[16]; ///< GHASH accumulator.
    uint8_t lb[16];  ///< length block (aad_len || cipher_len, in bits).
    uint8_t ej0[16]; ///< E(K, J0), the tag mask.
    uint8_t j0[16];  ///< pre-counter block J0 = nonce || 0^31 || 1.
    uint8_t ctr[16]; ///< running GCTR counter.
} GcmWork;

// The caller's borrow, split: the keyed context at the base, then the region the nested GHASH runs out
// of. GHASH is driven through its own namespace, so this borrow carries a region for it rather than
// naming any term of its.
#define AESGCM_OFF_CTX 0u
#define AESGCM_OFF_GHASH (AESGCM_OFF_CTX + sizeof(GcmWork))
#define AESGCM_OFF_END (AESGCM_OFF_GHASH + PROTOCORE_GHASH_BORROW)
static_assert(AESGCM_OFF_END <= PROTOCORE_AESGCM_BORROW,
              "PROTOCORE_AESGCM_BORROW is short of the keyed context and the nested GHASH borrow - raise "
              "it in protocore_config.h, which derives PROTOCORE_SECURE_ARENA_SIZE from it");

// The regions, at their offsets in the caller's borrow.
#define AESGCM_CTX(w) ((GcmWork *)(void *)((w) + AESGCM_OFF_CTX))
#define AESGCM_GHASH(w) ((w) + AESGCM_OFF_GHASH)

// --- the AES-256 block seam - one wrapper set, two platform bodies ---------

#if PROTOCORE_HAS_HW_AESGCM

// AES-256 single-block primitive (operates on the key held in GcmWork).
static inline void aes256_ecb(GcmWork *w, const uint8_t in[16], uint8_t out[16])
{
    protocore_aes_hw_acquire();
    protocore_aes_hw_setkey(w->blk.key, 32);
    protocore_aes_hw_block(in, out);
    protocore_aes_hw_release();
}
static inline void aes256_load_key(GcmWork *w, const uint8_t key[32])
{
    mem.cpy(w->blk.key, key, 32);
}
static inline void aes256_free_key(GcmWork *w)
{
    (void)w; // the key bytes are the caller's borrow, released and wiped with it
}

#endif // PROTOCORE_HAS_HW_AESGCM

#if !PROTOCORE_HAS_HW_AESGCM

// AES-256 single-block primitive (operates on the schedule inside GcmWork).
static inline void aes256_ecb(GcmWork *w, const uint8_t in[16], uint8_t out[16])
{
    protocore_aes_encrypt_block(w->blk.rk, 14, in, out);
}
static inline void aes256_load_key(GcmWork *w, const uint8_t key[32])
{
    protocore_aes_key_expand(key, 8, w->blk.rk);
}
static inline void aes256_free_key(GcmWork *w)
{
    (void)w; // software schedule lives in-place in GcmWork; nothing external to release
}

#endif // !PROTOCORE_HAS_HW_AESGCM

// --- the GCM construction, one body on both arms ---------------------------

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
    // A single-byte carry (ctr[15] 0xff -> 0x00 into ctr[14]) is cheap to reach and exercised by
    // test_aesgcm_gctr_counter_byte_carry; the full 2^32 wrap (~64 GiB in one call) is the only branch a
    // host test cannot reach.
    for (int i = 15; i >= 12; i--)
    {
        if (++ctr[i])
        {
            break;
        }
    }
}

// Derive the key-dependent state: H and the GHASH table. Done once per key, not once per record.
static void gcm_key_setup(uint8_t *restrict work)
{
    GcmWork *w = AESGCM_CTX(work);
    mem.zero(w->ks, 16);        // zero input for H (reuses the keystream slot; overwritten by gctr later)
    aes256_ecb(w, w->ks, w->h); // H = E(K, 0^128)
    Ghash.key_args.h = w->h;
    Ghash.key_init(AESGCM_GHASH(work));
}

// Per-record: J0 = nonce || 0^31 || 1.
static void gcm_set_nonce(GcmWork *w, const uint8_t nonce[12])
{
    mem.cpy(w->j0, nonce, 12);
    w->j0[12] = 0;
    w->j0[13] = 0;
    w->j0[14] = 0;
    w->j0[15] = 1;
}

// GCTR (NIST SP 800-38D sec 6.5): out = in XOR AES-CTR keystream from @p w->ctr, advanced in place.
static void gctr(GcmWork *w, const uint8_t *in, size_t len, uint8_t *out)
{
    size_t off = 0;
    while (off < len)
    {
        aes256_ecb(w, w->ctr, w->ks);
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

// GHASH over aad || cipher, fold in the lengths, and produce the 16-byte tag (acc XOR E(K, J0)).
static void gcm_tag(uint8_t *restrict work, const uint8_t *aad, size_t aad_len, const uint8_t *cipher,
                    size_t cipher_len, uint8_t tag_out[16])
{
    GcmWork *w = AESGCM_CTX(work);
    mem.zero(w->acc, 16);
    Ghash.update_args.acc = w->acc;
    Ghash.update_args.data = aad;
    Ghash.update_args.len = aad_len;
    Ghash.update(AESGCM_GHASH(work));
    Ghash.update_args.data = cipher;
    Ghash.update_args.len = cipher_len;
    Ghash.update(AESGCM_GHASH(work));
    put_be64(w->lb, (uint64_t)aad_len * 8);
    put_be64(w->lb + 8, (uint64_t)cipher_len * 8);
    xor16(w->acc, w->lb);
    Ghash.mul_args.acc = w->acc;
    Ghash.mul(AESGCM_GHASH(work));
    aes256_ecb(w, w->j0, w->ej0);
    for (int i = 0; i < 16; i++)
    {
        tag_out[i] = w->acc[i] ^ w->ej0[i];
    }
}

static proto_bool aesgcm_key_load(uint8_t *restrict work, const uint8_t *key)
{
    GcmWork *w = AESGCM_CTX(work);
    aes256_load_key(w, key);
    gcm_key_setup(work);
    return PROTO_TRUE;
}

static void aesgcm_key_release(uint8_t *restrict work)
{
    GcmWork *w = AESGCM_CTX(work);
    aes256_free_key(w);
}

static proto_bool aesgcm_seal_record(uint8_t *restrict work, const uint8_t *nonce, const uint8_t *aad, size_t aad_len,
                                     const uint8_t *pt, size_t pt_len, uint8_t *ct_out, uint8_t *tag_out)
{
    GcmWork *w = AESGCM_CTX(work);
    gcm_set_nonce(w, nonce);
    // Encrypt with the CTR starting at inc32(J0), then GHASH the resulting ciphertext.
    mem.cpy(w->ctr, w->j0, 16);
    inc32(w->ctr);
    gctr(w, pt, pt_len, ct_out);
    gcm_tag(work, aad, aad_len, ct_out, pt_len, tag_out);
    return PROTO_TRUE;
}

static proto_bool aesgcm_open_record(uint8_t *restrict work, const uint8_t *nonce, const uint8_t *aad, size_t aad_len,
                                     const uint8_t *ct, size_t ct_len, const uint8_t *tag, uint8_t *out)
{
    GcmWork *w = AESGCM_CTX(work);
    gcm_set_nonce(w, nonce);
    // Authenticate over the received ciphertext BEFORE producing any plaintext (tag reuses the ej0 slot).
    gcm_tag(work, aad, aad_len, ct, ct_len, w->ej0);
    if (!protocore_ct_eq(w->ej0, tag, PROTOCORE_AESGCM_TAG_LEN))
    {
        return PROTO_FALSE; // tag mismatch: nothing written
    }
    mem.cpy(w->ctr, w->j0, 16);
    inc32(w->ctr);
    gctr(w, ct, ct_len, out);
    return PROTO_TRUE;
}

// --- the entries -----------------------------------------------------------

static void aesgcm_key_init(uint8_t *restrict work)
{
    AesGcm.ok = PROTO_FALSE;
    if (!work || !AesGcm.key_args.key)
    {
        return;
    }
    AesGcm.ok = aesgcm_key_load(work, AesGcm.key_args.key);
}

// Release what the context attached. The bytes themselves are the caller's: it releases the borrow and
// the pool wipes it.
static void aesgcm_key_wipe(uint8_t *restrict work)
{
    AesGcm.ok = PROTO_FALSE;
    if (!work)
    {
        return;
    }
    aesgcm_key_release(work);
    AesGcm.ok = PROTO_TRUE;
}

static void aesgcm_seal(uint8_t *restrict work)
{
    AesGcm.ok = PROTO_FALSE;
    if (!work || !AesGcm.seal_args.nonce || !AesGcm.seal_args.ct_out || !AesGcm.seal_args.tag_out)
    {
        return;
    }
    AesGcm.ok = aesgcm_seal_record(work, AesGcm.seal_args.nonce, AesGcm.seal_args.aad, AesGcm.seal_args.aad_len,
                                   AesGcm.seal_args.pt, AesGcm.seal_args.pt_len, AesGcm.seal_args.ct_out,
                                   AesGcm.seal_args.tag_out);
}

static void aesgcm_open(uint8_t *restrict work)
{
    AesGcm.ok = PROTO_FALSE;
    if (!work || !AesGcm.open_args.nonce || !AesGcm.open_args.tag || !AesGcm.open_args.out)
    {
        return;
    }
    AesGcm.ok =
        aesgcm_open_record(work, AesGcm.open_args.nonce, AesGcm.open_args.aad, AesGcm.open_args.aad_len,
                           AesGcm.open_args.ct, AesGcm.open_args.ct_len, AesGcm.open_args.tag, AesGcm.open_args.out);
}

// Advance the RFC 5647 invocation counter: the low 8 bytes of the 12-byte nonce, big-endian; the 4-byte
// fixed field never changes. The nonce is the caller's own, so the borrow goes unread.
static void aesgcm_iv_increment(uint8_t *restrict work)
{
    (void)work;
    AesGcm.ok = PROTO_FALSE;
    if (!AesGcm.iv_args.iv)
    {
        return;
    }
    uint8_t *nonce = AesGcm.iv_args.iv;
    for (int j = PROTOCORE_AESGCM_IV_LEN - 1; j >= 4; j--)
    {
        if (++nonce[j])
        {
            break;
        }
    }
    AesGcm.ok = PROTO_TRUE;
}

AesGcmNs AesGcm = {.key_init = aesgcm_key_init,
                   .key_wipe = aesgcm_key_wipe,
                   .seal = aesgcm_seal,
                   .open = aesgcm_open,
                   .iv_increment = aesgcm_iv_increment};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_AESGCM
