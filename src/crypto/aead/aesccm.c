// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file aesccm.c
 * @brief AEAD AES-CCM (NIST SP 800-38C) - see aesccm.h.
 *
 * Hot path: mbedtls_ccm (AES routed to the ESP32 HW accelerator), detached tag natively. Test build: a
 * software CCM built on the shared table-free AES block (crypto/cipher/aes_block.h) - CBC-MAC for authentication
 * (formatting function per SP 800-38C Appendix A) and AES-CTR for encryption, both under the one key.
 *
 * All working memory (key schedule, CBC-MAC accumulator, keystream, counter/format blocks) lives in the
 * secure pool and is wiped when the borrow is released - no cipher state on the stack or in BSS.
 */

#include "crypto/aead/aesccm.h"
#include "mmgr/secure.h"

#if PC_ENABLE_SMB

#include "crypto/crypto_opt.h"
#include "crypto/ct_eq.h" // pc_ct_eq

#if !PC_HAS_HW_AES
#include "crypto/cipher/aes_block.h" // native software AES-128/256 (mbedtls path uses its own on the hot path)
#endif
PC_CRYPTO_HOT

#if PC_HAS_HW_AES
// ===========================================================================
// Hardware path: mbedtls_ccm -> ESP32 AES peripheral. Detached tag is native. The mbedtls context (AES
// key schedule) lives in the shared crypto scratch, never on the stack.
// ===========================================================================

proto_bool pc_aesccm_seal_tag(const uint8_t *key, size_t key_len, const uint8_t *nonce, size_t nonce_len,
                              const uint8_t *aad, size_t aad_len, const uint8_t *pt, size_t pt_len, uint8_t *ct_out,
                              uint8_t tag_out[PC_AESCCM_TAG_LEN])
{
    if (!key || !nonce || !ct_out || !tag_out || (key_len != 16 && key_len != 32))
    {
        return PROTO_FALSE;
    }
    size_t mark = pc_secure_mark();
    pc_span ws = pc_secure_span(sizeof(mbedtls_ccm_context), _Alignof(mbedtls_ccm_context));
    if (!pc_span_ok(ws))
    {
        pc_secure_release(mark);
        return PROTO_FALSE;
    }
    mbedtls_ccm_context *c = (mbedtls_ccm_context *)ws.buf;
    mbedtls_ccm_init(c);
    if (mbedtls_ccm_setkey(c, MBEDTLS_CIPHER_ID_AES, key, (unsigned)(key_len * 8)) != 0)
    {
        mbedtls_ccm_free(c);
        pc_secure_release(mark);
        return PROTO_FALSE;
    }
    int rc =
        mbedtls_ccm_encrypt_and_tag(c, pt_len, nonce, nonce_len, aad, aad_len, pt, ct_out, tag_out, PC_AESCCM_TAG_LEN);
    mbedtls_ccm_free(c);
    pc_secure_release(mark);
    return rc == 0;
}

proto_bool pc_aesccm_open_tag(const uint8_t *key, size_t key_len, const uint8_t *nonce, size_t nonce_len,
                              const uint8_t *aad, size_t aad_len, const uint8_t *ct, size_t ct_len,
                              const uint8_t tag[PC_AESCCM_TAG_LEN], uint8_t *out)
{
    if (!key || !nonce || !ct || !out || !tag || (key_len != 16 && key_len != 32))
    {
        return PROTO_FALSE;
    }
    size_t mark = pc_secure_mark();
    pc_span ws = pc_secure_span(sizeof(mbedtls_ccm_context), _Alignof(mbedtls_ccm_context));
    if (!pc_span_ok(ws))
    {
        pc_secure_release(mark);
        return PROTO_FALSE;
    }
    mbedtls_ccm_context *c = (mbedtls_ccm_context *)ws.buf;
    mbedtls_ccm_init(c);
    if (mbedtls_ccm_setkey(c, MBEDTLS_CIPHER_ID_AES, key, (unsigned)(key_len * 8)) != 0)
    {
        mbedtls_ccm_free(c);
        pc_secure_release(mark);
        return PROTO_FALSE;
    }
    // mbedtls verifies the tag in constant time and only then keeps the plaintext; non-zero => bad tag.
    int rc = mbedtls_ccm_auth_decrypt(c, ct_len, nonce, nonce_len, aad, aad_len, ct, out, tag, PC_AESCCM_TAG_LEN);
    mbedtls_ccm_free(c);
    pc_secure_release(mark);
    if (rc != 0)
    {
        memset(out, 0, ct_len);
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

#else // native software CCM
// ===========================================================================
// Software path (NIST SP 800-38C): CBC-MAC over B0 || fmt(AAD) || fmt(PT), CTR encryption from A1, and
// the tag encrypted with the counter block A0. The whole working set (CcmWork) is one pool borrow.
// ===========================================================================

// The entire CCM working set, laid over the shared crypto scratch. No key schedule or keystream on the
// stack; the whole struct is wiped after each operation.
typedef struct
{
    uint32_t rk[60]; ///< AES key schedule (44 words for AES-128, 60 for AES-256).
    int nr;          ///< rounds (10 or 14).
    uint8_t X[16];   ///< CBC-MAC accumulator (holds the raw MAC T when done).
    uint8_t blk[16]; ///< formatting block (B0 / AAD / payload / computed tag).
    uint8_t A[16];   ///< CTR counter block.
    uint8_t S[16];   ///< keystream / ECB output.
} CcmWork;
static_assert(
    sizeof(CcmWork) <= PC_WORK_AESCCM,
    "CcmWork outgrew PC_WORK_AESCCM - raise it in protocore_config.h, which derives PC_SECURE_ARENA_SIZE from it");

static inline void ccm_key_init(CcmWork *w, const uint8_t *key, size_t key_len)
{
    if (key_len == 32)
    {
        pc_aes_key_expand(key, 8, w->rk);
        w->nr = 14;
    }
    else
    {
        pc_aes_key_expand(key, 4, w->rk);
        w->nr = 10;
    }
}

static inline void ecb(const CcmWork *w, const uint8_t in[16], uint8_t out[16])
{
    pc_aes_encrypt_block(w->rk, w->nr, in, out);
}

// Build the counter block A_i = flags(L-1) || nonce || [i]_L (SP 800-38C Appendix A, the CTR formatting).
static inline void ctr_block(uint8_t A[16], const uint8_t *nonce, size_t nonce_len, size_t i)
{
    const size_t L = 15 - nonce_len;
    memset(A, 0, 16);
    A[0] = (uint8_t)(L - 1);
    memcpy(A + 1, nonce, nonce_len);
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
    memset(w->X, 0, 16);

    // B0: flags = 64*Adata + 8*((M-2)/2) + (L-1); then nonce; then Q = pt_len big-endian in L bytes.
    memset(w->blk, 0, 16);
    w->blk[0] = (uint8_t)((aad_len > 0 ? 0x40 : 0x00) | (((PC_AESCCM_TAG_LEN - 2) / 2) << 3) | (L - 1));
    memcpy(w->blk + 1, nonce, nonce_len);
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
        memset(w->blk, 0, 16);
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
            memcpy(w->blk + fill, aad + off, take);
            off += take;
            fill += take;
            for (int i = 0; i < 16; i++)
            {
                w->X[i] ^= w->blk[i];
            }
            ecb(w, w->X, w->X);
            memset(w->blk, 0, 16);
            fill = 0;
        }
    }

    // Payload blocks, zero-padded to 16.
    size_t off = 0;
    while (off < pt_len)
    {
        memset(w->blk, 0, 16);
        size_t take = pt_len - off;
        if (take > 16)
        {
            take = 16;
        }
        memcpy(w->blk, pt + off, take);
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
static void tag_encrypt(CcmWork *w, const uint8_t *nonce, size_t nonce_len, uint8_t out_tag[PC_AESCCM_TAG_LEN])
{
    ctr_block(w->A, nonce, nonce_len, 0);
    ecb(w, w->A, w->S);
    for (int i = 0; i < PC_AESCCM_TAG_LEN; i++)
    {
        out_tag[i] = (uint8_t)(w->X[i] ^ w->S[i]);
    }
}

proto_bool pc_aesccm_seal_tag(const uint8_t *key, size_t key_len, const uint8_t *nonce, size_t nonce_len,
                              const uint8_t *aad, size_t aad_len, const uint8_t *pt, size_t pt_len, uint8_t *ct_out,
                              uint8_t tag_out[PC_AESCCM_TAG_LEN])
{
    if (!key || !nonce || !ct_out || !tag_out || (key_len != 16 && key_len != 32) || nonce_len < 7 || nonce_len > 13)
    {
        return PROTO_FALSE;
    }
    size_t mark = pc_secure_mark();
    pc_span ws = pc_secure_span(sizeof(CcmWork), _Alignof(CcmWork));
    if (!pc_span_ok(ws))
    {
        pc_secure_release(mark);
        return PROTO_FALSE;
    }
    CcmWork *w = (CcmWork *)ws.buf;
    ccm_key_init(w, key, key_len);
    cbc_mac(w, nonce, nonce_len, aad, aad_len, pt, pt_len); // MAC -> w->X
    ctr_crypt(w, nonce, nonce_len, 1, pt, pt_len, ct_out);  // payload from A1
    tag_encrypt(w, nonce, nonce_len, tag_out);              // MAC protected by A0
    pc_secure_release(mark);
    return PROTO_TRUE;
}

proto_bool pc_aesccm_open_tag(const uint8_t *key, size_t key_len, const uint8_t *nonce, size_t nonce_len,
                              const uint8_t *aad, size_t aad_len, const uint8_t *ct, size_t ct_len,
                              const uint8_t tag[PC_AESCCM_TAG_LEN], uint8_t *out)
{
    if (!key || !nonce || !ct || !out || !tag || (key_len != 16 && key_len != 32) || nonce_len < 7 || nonce_len > 13)
    {
        return PROTO_FALSE;
    }
    size_t mark = pc_secure_mark();
    pc_span ws = pc_secure_span(sizeof(CcmWork), _Alignof(CcmWork));
    if (!pc_span_ok(ws))
    {
        pc_secure_release(mark);
        return PROTO_FALSE;
    }
    CcmWork *w = (CcmWork *)ws.buf;
    ccm_key_init(w, key, key_len);
    ctr_crypt(w, nonce, nonce_len, 1, ct, ct_len, out);      // recover plaintext into out
    cbc_mac(w, nonce, nonce_len, aad, aad_len, out, ct_len); // MAC over the recovered plaintext -> w->X
    tag_encrypt(w, nonce, nonce_len, w->blk);                // computed tag into w->blk (free after cbc_mac)
    // Compare before releasing: the release wipes w->blk, which holds the computed tag.
    proto_bool ok = pc_ct_eq(w->blk, tag, PC_AESCCM_TAG_LEN);
    pc_secure_release(mark);
    if (!ok)
    {
        memset(out, 0, ct_len); // fail closed: no unauthenticated plaintext escapes
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

#endif // PC_HAS_HW_AES
#endif // PC_ENABLE_SMB
