// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file aes256ctr.c
 * @brief AES-256-CTR implementation (stateless, see aes256ctr.h).
 *
 * The ephemeral key schedule and keystream block are borrowed from the shared crypto scratch
 * from the secure pool - never a stack or BSS local - and wiped on release, so expanded key material is
 * funneled through the one hardened, single-op region rather than scattered across the address space.
 *
 * Hot path: mbedtls_aes_crypt_ctr(), which the ESP32 mbedtls port routes to the hardware AES
 * accelerator. Test build: a compact software AES-256 (256-byte forward S-box + GF(2^8) MixColumns),
 * for host-side unit tests only.
 */

#include "crypto/cipher/aes256ctr.h"
#include "crypto/crypto_opt.h"
#include "mmgr/secure.h"

#if PC_HAS_HW_AES
#include <mbedtls/aes.h>
#else
#include "crypto/cipher/aes_block.h" // native software AES S-box/blocks (the hot path uses the mbedtls block above)
#endif
PC_CRYPTO_HOT

// ============================================================================
// Hot path - hardware-accelerated via mbedtls
// ============================================================================

#if PC_HAS_HW_AES

// The whole working set in one borrow: expanded key schedule + one keystream block. Its size is a
// per-vendor constant that belongs in core_setup/ (see the handover) - stated here for now.
typedef struct
{
    mbedtls_aes_context aes;
    uint8_t ks[16];
} Aes256CtrWork;
static_assert(sizeof(Aes256CtrWork) <= PC_WORK_AES256CTR,
              "Aes256CtrWork outgrew PC_WORK_AES256CTR - raise it in protocore_config.h, which derives "
              "PC_SECURE_ARENA_SIZE from it");

void pc_aes256ctr_crypt(const uint8_t key[PC_AES256CTR_KEY_LEN], uint8_t counter[PC_AES256CTR_CTR_LEN],
                        const uint8_t *in, uint8_t *out, size_t len)
{
    // Schedule + keystream block are borrowed, not local; the pool wipes them on release.
    size_t mark = pc_secure_mark();
    pc_span ws = pc_secure_span(sizeof(Aes256CtrWork), _Alignof(Aes256CtrWork));
    if (!pc_span_ok(ws))
    {
        pc_secure_release(mark);
        return;
    }
    Aes256CtrWork *w = (Aes256CtrWork *)ws.buf;
    mbedtls_aes_context *aes = &w->aes;
    uint8_t *ks = w->ks;
    mbedtls_aes_init(aes);
    mbedtls_aes_setkey_enc(aes, key, 256);
    size_t nc_off = 0; // block-aligned callers (SSH) leave this 0; the counter alone carries the stream state
    mbedtls_aes_crypt_ctr(aes, len, &nc_off, counter, ks, in, out);
    mbedtls_aes_free(aes);
    pc_secure_release(mark);
}

// ============================================================================
// SW path: software AES-256.
// ============================================================================

#else

// The whole working set in one borrow: 60-word round-key schedule + one keystream block.
typedef struct
{
    uint32_t rk[60];
    uint8_t ks[16];
} Aes256CtrWork;

void pc_aes256ctr_crypt(const uint8_t key[PC_AES256CTR_KEY_LEN], uint8_t counter[PC_AES256CTR_CTR_LEN],
                        const uint8_t *in, uint8_t *out, size_t len)
{
    // Round-key schedule and the keystream block are borrowed; the pool wipes them on release.
    size_t mark = pc_secure_mark();
    pc_span ws = pc_secure_span(sizeof(Aes256CtrWork), _Alignof(Aes256CtrWork));
    if (!pc_span_ok(ws))
    {
        pc_secure_release(mark);
        return;
    }
    Aes256CtrWork *w = (Aes256CtrWork *)ws.buf;
    uint32_t *rk = w->rk;
    uint8_t *ks = w->ks;
    pc_aes_key_expand(key, 8, rk);
    uint8_t pos = 0;
    for (size_t i = 0; i < len; i++)
    {
        if (pos == 0)
        {
            pc_aes_encrypt_block(rk, 14, counter, ks); // keystream = AES(counter)
            for (int j = 15; j >= 0; j--)              // then advance the big-endian counter by one block
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
    pc_secure_release(mark);
}

#endif // PC_HAS_HW_AES

// ---------------------------------------------------------------------------
// Length peek (used by the SSH recv path; mirrors pc_chachapoly_get_length)
// ---------------------------------------------------------------------------

uint32_t pc_aes256ctr_get_length(const uint8_t key[PC_AES256CTR_KEY_LEN], const uint8_t counter[PC_AES256CTR_CTR_LEN],
                                 const uint8_t enc4[4])
{
    // Produce the keystream block for @p counter (AES-ECB) in the shared crypto scratch, then XOR the first
    // 4 bytes to recover the length. @p counter is not advanced and no cipher state touches the stack.
    size_t mark = pc_secure_mark();
    pc_span ws = pc_secure_span(sizeof(Aes256CtrWork), _Alignof(Aes256CtrWork));
    if (!pc_span_ok(ws))
    {
        pc_secure_release(mark);
        return 0;
    }
    Aes256CtrWork *w = (Aes256CtrWork *)ws.buf;
    uint8_t *ks = w->ks;
#if PC_HAS_HW_AES
    mbedtls_aes_init(&w->aes);
    mbedtls_aes_setkey_enc(&w->aes, key, 256);
    mbedtls_aes_crypt_ecb(&w->aes, MBEDTLS_AES_ENCRYPT, counter, ks);
    mbedtls_aes_free(&w->aes);
#else
    pc_aes_key_expand(key, 8, w->rk);
    pc_aes_encrypt_block(w->rk, 14, counter, ks);
#endif
    // Read the keystream into the result before releasing: the release wipes ks.
    uint32_t len = ((uint32_t)(enc4[0] ^ ks[0]) << 24) | ((uint32_t)(enc4[1] ^ ks[1]) << 16) |
                   ((uint32_t)(enc4[2] ^ ks[2]) << 8) | (uint32_t)(enc4[3] ^ ks[3]);
    pc_secure_release(mark);
    return len;
}
