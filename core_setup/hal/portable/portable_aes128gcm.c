// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file portable_aes128gcm.c
 * @brief AES-128-GCM and the AES-128 block in software - the backend for a target with no accelerated AEAD.
 *
 * Software AES-128 plus a 4-bit-table GHASH. Selected explicitly by a vendor profile that sets
 * PC_HAS_HW_AESGCM 0; never a fallback. There is no weak symbol anywhere in the chain, so linking no
 * backend is an undefined reference and linking two is a duplicate definition.
 *
 * The context is keyed: the AES schedule, H and the GHASH table are built once per key. Only J0 and the
 * counter are per record.
 */

#include "core_setup/board_profiles/pc_platform.h"
#include "crypto/aead/aes128gcm.h"
#include "crypto/cipher/aes_block.h"
#include "crypto/crypto_opt.h"
#include "crypto/ct_eq.h" // pc_ct_eq
#include "crypto/mac/ghash.h"
#include "mmgr/rawmemcpy.h" // proto_raw_u32 - the aliasing-permitted word load
#include "mmgr/secure.h"
#include "protocore_config.h" // PC_ENABLE_* gate the whole file; pc_platform.h does not pull this in

#if (PC_ENABLE_HTTP3 || PC_ENABLE_DTLS || PC_ENABLE_SMB || PC_TLS_SOFTWARE)
#if !PC_HAS_HW_AESGCM

PC_CRYPTO_HOT

// pc_aes128 - definition private to this backend; aes128gcm.h forward-declares the symbol only.
typedef struct pc_aes128
{
    uint32_t rk[44]; ///< AES-128 expanded round-key schedule (11 round keys x 4 words).
} pc_aes128;

static_assert(sizeof(pc_aes128) <= PC_WORK_AES128, "pc_aes128 outgrew PC_WORK_AES128 - raise it in protocore_config.h");

struct pc_aes128 *pc_aes128_wants(void)
{
    pc_span ws = pc_secure_span(sizeof(pc_aes128), _Alignof(pc_aes128));
    return pc_span_ok(ws) ? (struct pc_aes128 *)(ws.buf) : NULL;
}

void pc_aes128_init(struct pc_aes128 *ctx, const uint8_t key[16])
{
    pc_aes_key_expand(key, 4, ctx->rk);
}

void pc_aes128_encrypt_block(struct pc_aes128 *ctx, const uint8_t in[16], uint8_t out[16])
{
    pc_aes_encrypt_block(ctx->rk, 10, in, out);
}

void pc_aes128_wipe(struct pc_aes128 *ctx)
{
    pc_secure_wipe(ctx, sizeof(pc_aes128));
}

// ===========================================================================
// AEAD_AES_128_GCM (NIST SP 800-38D). The struct IS the keyed context: aes/h/ghk are built once per key
// by pc_aes128gcm_key_init, the rest is per-record scratch.
// ===========================================================================

typedef struct
{
    pc_aes128 aes;   ///< AES-128 key schedule.
    uint8_t h[16];   ///< GHASH subkey H = E(K, 0^128).
    GhashKey ghk;    ///< 4-bit GHASH table built from H.
    uint8_t ks[16];  ///< GCTR keystream block.
    uint8_t acc[16]; ///< GHASH accumulator.
    uint8_t lb[16];  ///< length block (aad_len || cipher_len, in bits).
    uint8_t ej0[16]; ///< E(K, J0), the tag mask.
    uint8_t j0[16];  ///< pre-counter block J0 = nonce || 0^31 || 1.
    uint8_t ctr[16]; ///< running GCTR counter.
    uint8_t tag[16]; ///< computed tag (open: compared; seal writes the caller's buffer directly).
} Aes128GcmWork;
static_assert(sizeof(Aes128GcmWork) <= PC_WORK_AES128GCM,
              "Aes128GcmWork outgrew PC_WORK_AES128GCM - raise it in protocore_config.h, which derives "
              "PC_SECURE_ARENA_SIZE from it");

static inline void xor16(uint8_t *dst, const uint8_t *src)
{
    // One block = four words. The raw accessors carry aligned(1) and may_alias, so each step is the
    // machine's own load and store at any alignment, and the fixup sequence only where the die needs it.
    for (int i = 0; i < 16; i += 4)
    {
        proto_raw_put_u32(dst + i, proto_raw_u32(dst + i) ^ proto_raw_u32(src + i));
    }
}

// GHASH (acc *= H, and fold buffers into acc) is the shared 4-bit-table primitive in crypto/ghash.h:
// ghash_key_init(&w->ghk, w->h) once, then ghash_update / ghash_mul on w->ghk.

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
    // i.e. ~256 GCTR blocks (~4 KiB of plaintext) through the public seal()/open() API - well within
    // reach of a host test, so that carry-continue arm is exercised below and is NOT excluded.
    // What genuinely cannot be reached is the loop running all 4 iterations to exhaustion with no
    // break at all, i.e. every one of the 4 bytes carrying in the SAME inc32() call, which only
    // happens when the full 32-bit counter was 0xffffffff before this call. ctr always starts at
    // inc32(J0) with J0[12..15] fixed to 0,0,0,1 (96-bit-nonce J0, NIST SP 800-38D) - not
    // caller-controlled - so reaching that requires one seal()/open() call over ~2^32 contiguous
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
        pc_aes128_encrypt_block(&w->aes, w->ctr, w->ks);
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

// Compute H, the GHASH table, J0, GHASH(aad || cipher) and the 16-byte tag for a 96-bit-nonce GCM
// operation. @p cipher is the ciphertext to authenticate (== output for seal, == input for open). Uses
// w->h/ghk/j0/acc/lb/ej0; writes @p tag_out.
static void gcm_core(Aes128GcmWork *w, const uint8_t nonce[12], const uint8_t *aad, size_t aad_len,
                     const uint8_t *cipher, size_t cipher_len, uint8_t tag_out[16])
{
    memset(w->h, 0, 16);
    pc_aes128_encrypt_block(&w->aes, w->h, w->h); // H = E(K, 0^128)
    ghash_key_init(&w->ghk, w->h);

    // 96-bit nonce: J0 = nonce || 0^31 || 1.
    memcpy(w->j0, nonce, 12);
    w->j0[12] = 0;
    w->j0[13] = 0;
    w->j0[14] = 0;
    w->j0[15] = 1;

    memset(w->acc, 0, 16);
    ghash_update(&w->ghk, w->acc, aad, aad_len);
    ghash_update(&w->ghk, w->acc, cipher, cipher_len);
    put_be64(w->lb, (uint64_t)aad_len * 8);
    put_be64(w->lb + 8, (uint64_t)cipher_len * 8);
    xor16(w->acc, w->lb);
    ghash_mul(&w->ghk, w->acc);

    pc_aes128_encrypt_block(&w->aes, w->j0, w->ej0);
    for (int i = 0; i < 16; i++)
    {
        tag_out[i] = w->acc[i] ^ w->ej0[i];
    }
}

// 96-bit nonce: J0 = nonce || 0^31 || 1. Per record.
static inline void set_j0(Aes128GcmWork *w, const uint8_t nonce[12])
{
    memcpy(w->j0, nonce, 12);
    w->j0[12] = 0;
    w->j0[13] = 0;
    w->j0[14] = 0;
    w->j0[15] = 1;
}

// GHASH(aad || cipher) and the 16-byte tag, against the table already built for this key. Requires
// set_j0() first; leaves w->ctr alone so the caller controls the GCTR pass order.
static void gcm_tag(Aes128GcmWork *w, const uint8_t *aad, size_t aad_len, const uint8_t *cipher, size_t cipher_len,
                    uint8_t tag_out[16])
{
    memset(w->acc, 0, 16);
    ghash_update(&w->ghk, w->acc, aad, aad_len);
    ghash_update(&w->ghk, w->acc, cipher, cipher_len);
    put_be64(w->lb, (uint64_t)aad_len * 8);
    put_be64(w->lb + 8, (uint64_t)cipher_len * 8);
    xor16(w->acc, w->lb);
    ghash_mul(&w->ghk, w->acc);

    pc_aes128_encrypt_block(&w->aes, w->j0, w->ej0);
    for (int i = 0; i < 16; i++)
    {
        tag_out[i] = w->acc[i] ^ w->ej0[i];
    }
}

struct pc_aes128gcm_key *pc_aes128gcm_key_init(void *storage, const uint8_t key[PC_AES128GCM_KEY_LEN])
{
    Aes128GcmWork *w = (Aes128GcmWork *)(storage);
    pc_aes128_init(&w->aes, key);
    memset(w->h, 0, 16);
    pc_aes128_encrypt_block(&w->aes, w->h, w->h); // H = E(K, 0^128)
    ghash_key_init(&w->ghk, w->h);
    return (struct pc_aes128gcm_key *)(w);
}

void pc_aes128gcm_key_wipe(struct pc_aes128gcm_key *k)
{
    Aes128GcmWork *w = (Aes128GcmWork *)(k);
    pc_aes128_wipe(&w->aes);
    pc_secure_wipe((uint8_t *)(w), sizeof(Aes128GcmWork));
}

pc_cspan pc_aes128gcm_seal(struct pc_aes128gcm_key *k, const uint8_t nonce[PC_AES128GCM_IV_LEN], const uint8_t *aad,
                           size_t aad_len, const uint8_t *pt, size_t pt_len, uint8_t *ct_out,
                           uint8_t tag_out[PC_AES128GCM_TAG_LEN])
{
    Aes128GcmWork *w = (Aes128GcmWork *)(k);
    set_j0(w, nonce);
    // Encrypt first (counter starts at inc32(J0)), then GHASH the resulting ciphertext.
    memcpy(w->ctr, w->j0, 16);
    inc32(w->ctr);
    gctr(w, pt, pt_len, ct_out);
    gcm_tag(w, aad, aad_len, ct_out, pt_len, tag_out);
    return pc_cspan_from(ct_out, pt_len); // the tag rides in tag_out, not in this span
}

proto_bool pc_aes128gcm_open(struct pc_aes128gcm_key *k, const uint8_t nonce[PC_AES128GCM_IV_LEN], const uint8_t *aad,
                             size_t aad_len, const uint8_t *ct, size_t ct_len, const uint8_t tag[PC_AES128GCM_TAG_LEN],
                             uint8_t *out)
{
    Aes128GcmWork *w = (Aes128GcmWork *)(k);
    set_j0(w, nonce);
    // Authenticate over the received ciphertext BEFORE producing any plaintext.
    gcm_tag(w, aad, aad_len, ct, ct_len, w->tag);
    if (!pc_ct_eq(w->tag, tag, PC_AES128GCM_TAG_LEN))
    {
        return PROTO_FALSE; // tag mismatch: nothing written
    }
    memcpy(w->ctr, w->j0, 16);
    inc32(w->ctr);
    gctr(w, ct, ct_len, out);
    return PROTO_TRUE;
}

#endif // !PC_HAS_HW_AESGCM
#endif // PC_ENABLE_HTTP3 || PC_ENABLE_DTLS || PC_ENABLE_SMB || PC_TLS_SOFTWARE
