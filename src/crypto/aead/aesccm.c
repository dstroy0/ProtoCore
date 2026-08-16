// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file aesccm.c
 * @brief AEAD AES-CCM implementation (NIST SP 800-38C) - see aesccm.h.
 *
 * One record context and one set of entries. Both arms run the same SP 800-38C construction: CBC-MAC over
 * B0 || fmt(AAD) || fmt(PT), CTR encryption from A1, and the tag encrypted with the counter block A0. Only
 * the AES block under it follows the part - the accelerator's single-block encrypt where the part carries
 * one, the shared software AES block where it does not.
 *
 * The context is this file's. The module's own borrow carries it at the base, so the key material, the
 * CBC-MAC accumulator, the keystream and the counter/format blocks all live in the caller's secure bytes
 * and none of them touches the stack or BSS.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_AESCCM

#if PROTOCORE_HAS_HW_AES
#endif
#if !PROTOCORE_HAS_HW_AES
#include "crypto/cipher/aes_block.h" // native software AES-128/256 key schedule and single-block encrypt
#endif
#include "crypto/aead/aesccm.h"
#include "crypto/crypto_opt.h"
#include "crypto/ct_eq.h" // protocore_ct_eq
#include "mmgr/protomem.h"

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// AES single-block encrypt seam - one small wrapper, two platform bodies
// ---------------------------------------------------------------------------

#if PROTOCORE_HAS_HW_AES

typedef struct
{
    uint8_t key[32];    ///< reloaded into the accelerator's key bank per block
    unsigned key_bytes; ///< 16 for AES-128, 32 for AES-256
} AesBlk;
static inline void blk_init(AesBlk *b, const uint8_t *key, size_t key_len)
{
    mem.cpy(b->key, key, key_len);
    b->key_bytes = (unsigned)key_len;
}
static inline void blk_enc(const AesBlk *b, const uint8_t in[16], uint8_t out[16])
{
    protocore_aes_hw_acquire();
    protocore_aes_hw_setkey(b->key, b->key_bytes);
    protocore_aes_hw_block(in, out);
    protocore_aes_hw_release();
}

#endif

#if !PROTOCORE_HAS_HW_AES

typedef struct
{
    uint32_t rk[60]; ///< AES key schedule (44 words for AES-128, 60 for AES-256).
    int nr;          ///< rounds (10 or 14).
} AesBlk;
static inline void blk_init(AesBlk *b, const uint8_t *key, size_t key_len)
{
    if (key_len == 32)
    {
        protocore_aes_key_expand(key, 8, b->rk);
        b->nr = 14;
    }
    else
    {
        protocore_aes_key_expand(key, 4, b->rk);
        b->nr = 10;
    }
}
static inline void blk_enc(const AesBlk *b, const uint8_t in[16], uint8_t out[16])
{
    protocore_aes_encrypt_block(b->rk, b->nr, in, out);
}

#endif // !PROTOCORE_HAS_HW_AES (SW block)

// The one definition of CcmWork - private to this TU. It sits at AESCCM_OFF_CTX in the caller's borrow, so
// its size never leaves this file and no consumer can name it. AesBlk is the arm's own block context, so
// the context's size follows the arm.
//
// Only what is not derivable: the region lives at a fixed offset in the borrow, so a macro computes it from
// the pointer rather than anything storing it.
typedef struct
{
    AesBlk aes;      ///< the arm's AES block context
    uint8_t X[16];   ///< CBC-MAC accumulator (holds the raw MAC T when done).
    uint8_t blk[16]; ///< formatting block (B0 / AAD / payload / computed tag).
    uint8_t A[16];   ///< CTR counter block.
    uint8_t S[16];   ///< keystream / ECB output.
} CcmWork;

// The caller's borrow, split: the record context at the base. Nothing else is carried across a call.
#define AESCCM_OFF_CTX 0u
#define AESCCM_OFF_END (AESCCM_OFF_CTX + sizeof(CcmWork))
static_assert(AESCCM_OFF_END <= PROTOCORE_AESCCM_BORROW,
              "PROTOCORE_AESCCM_BORROW is short of the record context - raise it in protocore_config.h, "
              "which sums it into the secure arena");

// The region, at its offset in the caller's borrow.
#define AESCCM_CTX(w) ((CcmWork *)(void *)((w) + AESCCM_OFF_CTX))

// --- CBC-MAC and CTR over that block (SP 800-38C), both arms ---------------

static inline void ccm_key_init(CcmWork *w, const uint8_t *key, size_t key_len)
{
    blk_init(&w->aes, key, key_len);
}

static inline void ecb(const CcmWork *w, const uint8_t in[16], uint8_t out[16])
{
    blk_enc(&w->aes, in, out);
}

// Build the counter block A_i = flags(L-1) || nonce || [i]_L (SP 800-38C Appendix A, the CTR formatting).
static inline void ctr_block(uint8_t A[16], const uint8_t *nonce, size_t nonce_len, size_t i)
{
    const size_t L = 15 - nonce_len;
    mem.set(A, 0, 16);
    A[0] = (uint8_t)(L - 1);
    mem.cpy(A + 1, nonce, nonce_len);
    for (size_t j = 0; j < L; j++)
    {
        A[15 - j] = (uint8_t)((i >> (8 * j)) & 0xff);
    }
}

// CBC-MAC of B0 || fmt(aad) || fmt(pt) -> w->X (SP 800-38C §6.1 / Appendix A formatting). Uses w->blk.
static void cbc_mac(CcmWork *w, const uint8_t *nonce, size_t nonce_len, const uint8_t *aad, size_t aad_len,
                    const uint8_t *pt, size_t pt_len)
{
    const size_t L = 15 - nonce_len;
    mem.set(w->X, 0, 16);

    // B0: flags = 64*Adata + 8*((M-2)/2) + (L-1); then nonce; then Q = pt_len big-endian in L bytes.
    mem.set(w->blk, 0, 16);
    w->blk[0] = (uint8_t)((aad_len > 0 ? 0x40 : 0x00) | (((PROTOCORE_AESCCM_TAG_LEN - 2) / 2) << 3) | (L - 1));
    mem.cpy(w->blk + 1, nonce, nonce_len);
    for (size_t j = 0; j < L; j++)
    {
        w->blk[15 - j] = (uint8_t)((pt_len >> (8 * j)) & 0xff);
    }
    for (int i = 0; i < 16; i++)
    {
        w->X[i] ^= w->blk[i];
    }
    ecb(w, w->X, w->X);

    // Associated data: a 2-byte big-endian length prefix (for 0 < aad_len < 0xFF00) then the AAD, packed
    // into 16-byte blocks and zero-padded.
    if (aad_len > 0)
    {
        mem.set(w->blk, 0, 16);
        w->blk[0] = (uint8_t)((aad_len >> 8) & 0xff);
        w->blk[1] = (uint8_t)(aad_len & 0xff);
        size_t fill = 2;
        size_t off = 0;
        while (off < aad_len)
        {
            size_t take = 16 - fill;
            if (take > aad_len - off)
            {
                take = aad_len - off;
            }
            mem.cpy(w->blk + fill, aad + off, take);
            off += take;
            fill += take;
            for (int i = 0; i < 16; i++)
            {
                w->X[i] ^= w->blk[i];
            }
            ecb(w, w->X, w->X);
            mem.set(w->blk, 0, 16);
            fill = 0;
        }
    }

    // Payload blocks, zero-padded to 16.
    size_t off = 0;
    while (off < pt_len)
    {
        mem.set(w->blk, 0, 16);
        size_t take = pt_len - off;
        if (take > 16)
        {
            take = 16;
        }
        mem.cpy(w->blk, pt + off, take);
        for (int i = 0; i < 16; i++)
        {
            w->X[i] ^= w->blk[i];
        }
        ecb(w, w->X, w->X);
        off += take;
    }
}

// AES-CTR from counter block index @p i0 (A_{i0}, A_{i0+1}, ...). @p in / @p out may alias. Uses w->A/w->S.
static void ctr_crypt(CcmWork *w, const uint8_t *nonce, size_t nonce_len, size_t i0, const uint8_t *in, size_t len,
                      uint8_t *out)
{
    size_t off = 0;
    size_t i = i0;
    while (off < len)
    {
        ctr_block(w->A, nonce, nonce_len, i);
        ecb(w, w->A, w->S);
        size_t take = len - off;
        if (take > 16)
        {
            take = 16;
        }
        for (size_t j = 0; j < take; j++)
        {
            out[off + j] = in[off + j] ^ w->S[j];
        }
        off += take;
        i++;
    }
}

// Encrypted tag = T XOR AES(A0) (the counter block for i = 0 protects the MAC). Reads the MAC from w->X,
// uses w->A/w->S, writes @p out_tag.
static void tag_encrypt(CcmWork *w, const uint8_t *nonce, size_t nonce_len, uint8_t out_tag[PROTOCORE_AESCCM_TAG_LEN])
{
    ctr_block(w->A, nonce, nonce_len, 0);
    ecb(w, w->A, w->S);
    for (int i = 0; i < PROTOCORE_AESCCM_TAG_LEN; i++)
    {
        out_tag[i] = (uint8_t)(w->X[i] ^ w->S[i]);
    }
}

static proto_bool aesccm_seal_record(uint8_t *restrict work, const uint8_t *key, size_t key_len, const uint8_t *nonce,
                                     size_t nonce_len, const uint8_t *aad, size_t aad_len, const uint8_t *pt,
                                     size_t pt_len, uint8_t *ct_out, uint8_t *tag_out)
{
    // Reject a nonce outside 7..13: the formatting blocks carry L = 15 - nonce_len as the length field.
    if (nonce_len < 7 || nonce_len > 13)
    {
        return PROTO_FALSE;
    }
    CcmWork *w = AESCCM_CTX(work);
    ccm_key_init(w, key, key_len);
    cbc_mac(w, nonce, nonce_len, aad, aad_len, pt, pt_len); // MAC -> w->X
    ctr_crypt(w, nonce, nonce_len, 1, pt, pt_len, ct_out);  // payload from A1
    tag_encrypt(w, nonce, nonce_len, tag_out);              // MAC protected by A0
    return PROTO_TRUE;
}

static proto_bool aesccm_open_record(uint8_t *restrict work, const uint8_t *key, size_t key_len, const uint8_t *nonce,
                                     size_t nonce_len, const uint8_t *aad, size_t aad_len, const uint8_t *ct,
                                     size_t ct_len, const uint8_t *tag, uint8_t *out)
{
    // Reject a nonce outside 7..13: the formatting blocks carry L = 15 - nonce_len as the length field.
    if (nonce_len < 7 || nonce_len > 13)
    {
        return PROTO_FALSE;
    }
    CcmWork *w = AESCCM_CTX(work);
    ccm_key_init(w, key, key_len);
    ctr_crypt(w, nonce, nonce_len, 1, ct, ct_len, out);      // recover plaintext into out
    cbc_mac(w, nonce, nonce_len, aad, aad_len, out, ct_len); // MAC over the recovered plaintext -> w->X
    tag_encrypt(w, nonce, nonce_len, w->blk);                // computed tag into w->blk (free after cbc_mac)
    proto_bool ok = protocore_ct_eq(w->blk, tag, PROTOCORE_AESCCM_TAG_LEN);
    if (!ok)
    {
        mem.set(out, 0, ct_len); // fail closed: no unauthenticated plaintext escapes
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

// --- the entries -----------------------------------------------------------

static void aesccm_seal(uint8_t *restrict work)
{
    AesCcm.ok = PROTO_FALSE;
    if (!work || !AesCcm.seal_args.key || !AesCcm.seal_args.nonce || !AesCcm.seal_args.ct_out ||
        !AesCcm.seal_args.tag_out || (AesCcm.seal_args.key_len != 16 && AesCcm.seal_args.key_len != 32))
    {
        return;
    }
    AesCcm.ok = aesccm_seal_record(work, AesCcm.seal_args.key, AesCcm.seal_args.key_len, AesCcm.seal_args.nonce,
                                   AesCcm.seal_args.nonce_len, AesCcm.seal_args.aad, AesCcm.seal_args.aad_len,
                                   AesCcm.seal_args.pt, AesCcm.seal_args.pt_len, AesCcm.seal_args.ct_out,
                                   AesCcm.seal_args.tag_out);
}

static void aesccm_open(uint8_t *restrict work)
{
    AesCcm.ok = PROTO_FALSE;
    if (!work || !AesCcm.open_args.key || !AesCcm.open_args.nonce || !AesCcm.open_args.ct || !AesCcm.open_args.out ||
        !AesCcm.open_args.tag || (AesCcm.open_args.key_len != 16 && AesCcm.open_args.key_len != 32))
    {
        return;
    }
    AesCcm.ok =
        aesccm_open_record(work, AesCcm.open_args.key, AesCcm.open_args.key_len, AesCcm.open_args.nonce,
                           AesCcm.open_args.nonce_len, AesCcm.open_args.aad, AesCcm.open_args.aad_len,
                           AesCcm.open_args.ct, AesCcm.open_args.ct_len, AesCcm.open_args.tag, AesCcm.open_args.out);
}

AesCcmNs AesCcm = {.seal = aesccm_seal, .open = aesccm_open};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_AESCCM
