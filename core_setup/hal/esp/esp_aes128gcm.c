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

#include "core_setup/board_profiles/protocore_platform.h"
#include "crypto/aead/aes128gcm.h"
#include "crypto/crypto_opt.h"
#include "mmgr/secure.h"
#include "protocore_config.h" // PROTOCORE_ENABLE_* gate the whole file; protocore_platform.h does not pull this in

#if (PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_ENABLE_SMB)
#if PROTOCORE_HAS_HW_AESGCM

#include <mbedtls/aes.h>
#include <mbedtls/gcm.h>

PROTOCORE_CRYPTO_HOT

// protocore_aes128 - definition private to this backend; aes128gcm.h forward-declares the symbol only.
typedef struct protocore_aes128
{
    mbedtls_aes_context mbed; ///< mbedtls context (HW-accelerated), key schedule loaded.
} protocore_aes128;

static_assert(sizeof(protocore_aes128) <= PROTOCORE_WORK_AES128, "protocore_aes128 outgrew PROTOCORE_WORK_AES128 - raise it in protocore_config.h");

struct protocore_aes128 *protocore_aes128_wants(void)
{
    protocore_span ws = protocore_secure_span(sizeof(protocore_aes128), 8);
    return span.ok(ws) ? (struct protocore_aes128 *)(ws.buf) : NULL;
}

void protocore_aes128_init(struct protocore_aes128 *ctx, const uint8_t key[16])
{
    mbedtls_aes_init(&ctx->mbed);
    mbedtls_aes_setkey_enc(&ctx->mbed, key, 128);
}

void protocore_aes128_encrypt_block(struct protocore_aes128 *ctx, const uint8_t in[16], uint8_t out[16])
{
    mbedtls_aes_crypt_ecb(&ctx->mbed, MBEDTLS_AES_ENCRYPT, in, out);
}

void protocore_aes128_wipe(struct protocore_aes128 *ctx)
{
    mbedtls_aes_free(&ctx->mbed);
}

// The size assert is what keeps PROTOCORE_WORK_AES128GCM honest against a vendor header we do not own.
static_assert(sizeof(mbedtls_gcm_context) <= PROTOCORE_WORK_AES128GCM,
              "mbedtls_gcm_context outgrew PROTOCORE_WORK_AES128GCM - raise it in protocore_config.h, which derives "
              "PROTOCORE_SECURE_ARENA_SIZE from it");

struct protocore_aes128gcm_key *protocore_aes128gcm_key_init(void *storage, const uint8_t key[PROTOCORE_AES128GCM_KEY_LEN])
{
    mbedtls_gcm_context *g = (mbedtls_gcm_context *)(storage);
    mbedtls_gcm_init(g);
    if (mbedtls_gcm_setkey(g, MBEDTLS_CIPHER_ID_AES, key, 128) != 0)
    {
        mbedtls_gcm_free(g);
        return NULL;
    }
    return (struct protocore_aes128gcm_key *)(g);
}

void protocore_aes128gcm_key_wipe(struct protocore_aes128gcm_key *k)
{
    mbedtls_gcm_context *g = (mbedtls_gcm_context *)(k);
    mbedtls_gcm_free(g); // releases whatever the vendor attached
    protocore_secure_wipe((uint8_t *)(g), sizeof(mbedtls_gcm_context));
}

protocore_cspan protocore_aes128gcm_seal(struct protocore_aes128gcm_key *k, const uint8_t nonce[PROTOCORE_AES128GCM_IV_LEN], const uint8_t *aad,
                           size_t aad_len, const uint8_t *pt, size_t pt_len, uint8_t *ct_out,
                           uint8_t tag_out[PROTOCORE_AES128GCM_TAG_LEN])
{
    mbedtls_gcm_context *g = (mbedtls_gcm_context *)(k);
    if (mbedtls_gcm_crypt_and_tag(g, MBEDTLS_GCM_ENCRYPT, pt_len, nonce, PROTOCORE_AES128GCM_IV_LEN, aad, aad_len, pt, ct_out,
                                  PROTOCORE_AES128GCM_TAG_LEN, tag_out) != 0)
    {
        return span.cfrom(NULL, 0);
    }
    return span.cfrom(ct_out, pt_len); // the tag rides in tag_out, not in this span
}

proto_bool protocore_aes128gcm_open(struct protocore_aes128gcm_key *k, const uint8_t nonce[PROTOCORE_AES128GCM_IV_LEN], const uint8_t *aad,
                             size_t aad_len, const uint8_t *ct, size_t ct_len, const uint8_t tag[PROTOCORE_AES128GCM_TAG_LEN],
                             uint8_t *out)
{
    mbedtls_gcm_context *g = (mbedtls_gcm_context *)(k);
    return mbedtls_gcm_auth_decrypt(g, ct_len, nonce, PROTOCORE_AES128GCM_IV_LEN, aad, aad_len, tag, PROTOCORE_AES128GCM_TAG_LEN, ct,
                                    out) == 0;
}

#endif // PROTOCORE_HAS_HW_AESGCM
#endif // PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_ENABLE_SMB
