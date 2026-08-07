// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file portable_aesgcm.c
 * @brief AES-256-GCM in software - the backend for a target with no accelerated AEAD.
 *
 * Software AES-256 plus a 4-bit-table GHASH. Selected explicitly by a vendor profile that sets
 * PC_HAS_HW_AESGCM 0; never a fallback. Software crypto is a legitimate choice and on some parts the
 * only one, but arriving here by default is not - there is no weak symbol anywhere in the chain, so
 * linking no backend is an undefined reference and linking two is a duplicate definition.
 *
 * Expect roughly an order of magnitude less throughput than a vendor AEAD (measured on an ESP32-S3:
 * 616k cycles/KiB for the software GHASH path against 81k for the vendor's). A part that HAS an
 * accelerated GCM should never be pointed here.
 */

#include "core_setup/board_profiles/pc_platform.h"
#include "crypto/aead/aesgcm.h"
#include "crypto/cipher/aes_block.h"
#include "crypto/crypto_opt.h"
#include "crypto/ct_eq.h" // pc_ct_eq
#include "crypto/mac/ghash.h"
#include "mmgr/rawmemcpy.h" // proto_raw_u32 - the aliasing-permitted word load
#include "mmgr/secure.h"

#if !PC_HAS_HW_AESGCM

PC_CRYPTO_HOT

// ===========================================================================
// GcmWork: the entire AES-256-GCM working set, laid over the shared crypto scratch. No cipher state on
// the stack; the whole struct is wiped after each operation.
// ===========================================================================
typedef struct
{
    uint32_t rk[60]; ///< AES-256 expanded round-key schedule (software).
    uint8_t h[16];   ///< GHASH subkey H = E(K, 0^128).
    GhashKey ghk;    ///< 4-bit GHASH table built from H.
    uint8_t ks[16];  ///< GCTR keystream block (also the zero input used to derive H).
    uint8_t acc[16]; ///< GHASH accumulator.
    uint8_t lb[16];  ///< length block (aad_len || cipher_len, in bits).
    uint8_t ej0[16]; ///< E(K, J0), the tag mask.
    uint8_t j0[16];  ///< pre-counter block J0 = nonce || 0^31 || 1.
    uint8_t ctr[16]; ///< running GCTR counter.
} GcmWork;
static_assert(
    sizeof(GcmWork) <= PC_WORK_AESGCM,
    "GcmWork outgrew PC_WORK_AESGCM - raise it in protocore_config.h, which derives PC_SECURE_ARENA_SIZE from it");

// ---------------------------------------------------------------------------
// AES-256 single-block primitive (operates on the schedule inside GcmWork)
// ---------------------------------------------------------------------------
static inline void aes256_ecb(GcmWork *w, const uint8_t in[16], uint8_t out[16])
{
    pc_aes_encrypt_block(w->rk, 14, in, out);
}
static inline void aes256_load_key(GcmWork *w, const uint8_t key[32])
{
    pc_aes_key_expand(key, 8, w->rk);
}
static inline void aes256_free_key(GcmWork *w)
{
    (void)w; // software schedule lives in-place in GcmWork; nothing external to release
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
static void gcm_key_setup(GcmWork *w)
{
    memset(w->ks, 0, 16);       // zero input for H (reuses the keystream slot; overwritten by gctr later)
    aes256_ecb(w, w->ks, w->h); // H = E(K, 0^128)
    ghash_key_init(&w->ghk, w->h);
}

// Per-record: J0 = nonce || 0^31 || 1.
static void gcm_set_nonce(GcmWork *w, const uint8_t nonce[12])
{
    memcpy(w->j0, nonce, 12);
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
static void gcm_tag(GcmWork *w, const uint8_t *aad, size_t aad_len, const uint8_t *cipher, size_t cipher_len,
                    uint8_t tag_out[16])
{
    memset(w->acc, 0, 16);
    ghash_update(&w->ghk, w->acc, aad, aad_len);
    ghash_update(&w->ghk, w->acc, cipher, cipher_len);
    put_be64(w->lb, (uint64_t)aad_len * 8);
    put_be64(w->lb + 8, (uint64_t)cipher_len * 8);
    xor16(w->acc, w->lb);
    ghash_mul(&w->ghk, w->acc);
    aes256_ecb(w, w->j0, w->ej0);
    for (int i = 0; i < 16; i++)
    {
        tag_out[i] = w->acc[i] ^ w->ej0[i];
    }
}

// ===========================================================================
// Public API (keyed)
// ===========================================================================

struct pc_aesgcm_key *pc_aesgcm_key_init(void *storage, const uint8_t key[PC_AESGCM_KEY_LEN])
{
    GcmWork *w = (GcmWork *)(storage);
    aes256_load_key(w, key);
    gcm_key_setup(w);
    return (struct pc_aesgcm_key *)(w);
}

void pc_aesgcm_key_wipe(struct pc_aesgcm_key *k)
{
    GcmWork *w = (GcmWork *)(k);
    aes256_free_key(w);
    pc_secure_wipe((uint8_t *)(w), sizeof(GcmWork));
}

pc_cspan pc_aesgcm_seal(struct pc_aesgcm_key *k, const uint8_t nonce[PC_AESGCM_IV_LEN], const uint8_t *aad,
                        size_t aad_len, const uint8_t *pt, size_t pt_len, uint8_t *ct_out,
                        uint8_t tag_out[PC_AESGCM_TAG_LEN])
{
    GcmWork *w = (GcmWork *)(k);
    gcm_set_nonce(w, nonce);
    // Encrypt with the CTR starting at inc32(J0), then GHASH the resulting ciphertext.
    memcpy(w->ctr, w->j0, 16);
    inc32(w->ctr);
    gctr(w, pt, pt_len, ct_out);
    gcm_tag(w, aad, aad_len, ct_out, pt_len, tag_out);
    return pc_cspan_from(ct_out, pt_len); // the tag rides in tag_out, not in this span
}

proto_bool pc_aesgcm_open(struct pc_aesgcm_key *k, const uint8_t nonce[PC_AESGCM_IV_LEN], const uint8_t *aad,
                          size_t aad_len, const uint8_t *ct, size_t ct_len, const uint8_t tag[PC_AESGCM_TAG_LEN],
                          uint8_t *out)
{
    GcmWork *w = (GcmWork *)(k);
    gcm_set_nonce(w, nonce);
    // Authenticate over the received ciphertext BEFORE producing any plaintext (tag reuses the ej0 slot).
    gcm_tag(w, aad, aad_len, ct, ct_len, w->ej0);
    if (!pc_ct_eq(w->ej0, tag, PC_AESGCM_TAG_LEN))
    {
        return PROTO_FALSE; // tag mismatch: nothing written
    }
    memcpy(w->ctr, w->j0, 16);
    inc32(w->ctr);
    gctr(w, ct, ct_len, out);
    return PROTO_TRUE;
}

#endif // !PC_HAS_HW_AESGCM
