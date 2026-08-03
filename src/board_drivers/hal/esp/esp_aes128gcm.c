// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp_aes128gcm.c
 * @brief AES-128-GCM and the AES-128 block on Espressif silicon, through the vendor's own primitives.
 *
 * The AEAD is one mbedtls_gcm call. The core used to drive the block cipher 16 bytes at a time and fold
 * GHASH in software on every target - on the AES-256 path that measured 616,567 cyc/KiB against the
 * vendor AEAD's 81,085, and there is no reason to expect a different ratio here.
 *
 * The context is keyed and held for the life of the key: standing an mbedtls GCM context up and tearing
 * it down costs ~9,200 cycles once the AES peripheral has been used, a FIXED cost per record that
 * dominates QUIC and DTLS traffic, where records are small.
 *
 * Vendor headers are fine here: this is board_drivers, the partition vendor code is segregated to.
 */

#include "board_drivers/board_profiles/pc_platform.h"
#include "crypto/aead/aes128gcm.h"
#include "crypto/crypto_opt.h"
#include "mmgr/secure.h"
#include "protocore_config.h" // PC_ENABLE_* gate the whole file; pc_platform.h does not pull this in
#include <string.h>

#if (PC_ENABLE_HTTP3 || PC_ENABLE_DTLS || PC_ENABLE_SMB)
#if PC_HAS_HW_AESGCM

#include <mbedtls/aes.h>
#include <mbedtls/gcm.h>

PC_CRYPTO_HOT

// pc_aes128 - definition private to this backend; aes128gcm.h forward-declares the symbol only.
typedef struct pc_aes128
{
    mbedtls_aes_context mbed; ///< mbedtls context (HW-accelerated), key schedule loaded.
} pc_aes128;

static_assert(sizeof(pc_aes128) <= PC_WORK_AES128, "pc_aes128 outgrew PC_WORK_AES128 - raise it in protocore_config.h");

struct pc_aes128 *pc_aes128_wants(void)
{
    pc_span ws = pc_secure_span(sizeof(pc_aes128), alignof(pc_aes128));
    return pc_span_ok(ws) ? (struct pc_aes128 *)(ws.buf) : NULL;
}

void pc_aes128_init(struct pc_aes128 *ctx, const uint8_t key[16])
{
    mbedtls_aes_init(&ctx->mbed);
    mbedtls_aes_setkey_enc(&ctx->mbed, key, 128);
}

void pc_aes128_encrypt_block(struct pc_aes128 *ctx, const uint8_t in[16], uint8_t out[16])
{
    mbedtls_aes_crypt_ecb(&ctx->mbed, MBEDTLS_AES_ENCRYPT, in, out);
}

void pc_aes128_wipe(struct pc_aes128 *ctx)
{
    mbedtls_aes_free(&ctx->mbed);
}

// The size assert is what keeps PC_WORK_AES128GCM honest against a vendor header we do not own.
static_assert(sizeof(mbedtls_gcm_context) <= PC_WORK_AES128GCM,
              "mbedtls_gcm_context outgrew PC_WORK_AES128GCM - raise it in protocore_config.h, which derives "
              "PC_SECURE_ARENA_SIZE from it");

struct pc_aes128gcm_key *pc_aes128gcm_key_init(void *storage, const uint8_t key[PC_AES128GCM_KEY_LEN])
{
    mbedtls_gcm_context *g = (mbedtls_gcm_context *)(storage);
    mbedtls_gcm_init(g);
    if (mbedtls_gcm_setkey(g, MBEDTLS_CIPHER_ID_AES, key, 128) != 0)
    {
        mbedtls_gcm_free(g);
        return NULL;
    }
    return (struct pc_aes128gcm_key *)(g);
}

void pc_aes128gcm_key_wipe(struct pc_aes128gcm_key *k)
{
    mbedtls_gcm_context *g = (mbedtls_gcm_context *)(k);
    mbedtls_gcm_free(g); // releases whatever the vendor attached
    pc_secure_wipe((uint8_t *)(g), sizeof(mbedtls_gcm_context));
}

pc_cspan pc_aes128gcm_seal(struct pc_aes128gcm_key *k, const uint8_t nonce[PC_AES128GCM_IV_LEN], const uint8_t *aad,
                           size_t aad_len, const uint8_t *pt, size_t pt_len, uint8_t *ct_out,
                           uint8_t tag_out[PC_AES128GCM_TAG_LEN])
{
    mbedtls_gcm_context *g = (mbedtls_gcm_context *)(k);
    if (mbedtls_gcm_crypt_and_tag(g, MBEDTLS_GCM_ENCRYPT, pt_len, nonce, PC_AES128GCM_IV_LEN, aad, aad_len, pt, ct_out,
                                  PC_AES128GCM_TAG_LEN, tag_out) != 0)
    {
        return {};
    }
    return pc_cspan_from(ct_out, pt_len); // the tag rides in tag_out, not in this span
}

proto_bool pc_aes128gcm_open(struct pc_aes128gcm_key *k, const uint8_t nonce[PC_AES128GCM_IV_LEN], const uint8_t *aad,
                             size_t aad_len, const uint8_t *ct, size_t ct_len, const uint8_t tag[PC_AES128GCM_TAG_LEN],
                             uint8_t *out)
{
    mbedtls_gcm_context *g = (mbedtls_gcm_context *)(k);
    return mbedtls_gcm_auth_decrypt(g, ct_len, nonce, PC_AES128GCM_IV_LEN, aad, aad_len, tag, PC_AES128GCM_TAG_LEN, ct,
                                    out) == 0;
}

#endif // PC_HAS_HW_AESGCM
#endif // PC_ENABLE_HTTP3 || PC_ENABLE_DTLS || PC_ENABLE_SMB
