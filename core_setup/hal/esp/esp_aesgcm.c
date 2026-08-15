// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp_aesgcm.c
 * @brief AES-256-GCM on Espressif silicon, through the vendor's own AEAD.
 *
 * Hands the whole AEAD to mbedtls rather than driving the block cipher and folding GHASH in software.
 * That is not a style preference - measured on an ESP32-S3 at 240 MHz, sealing 1 KiB:
 *
 *     this path (vendor GCM)                     81,085 cycles   3.0 MB/s
 *     manual HW-AES block + software GHASH      616,567 cycles   0.4 MB/s
 *
 * 7.6x, same chip, same data. The old code took the manual path whenever SOC_AES_SUPPORT_GCM was
 * unset, i.e. whenever we concluded the die had no GCM mode - and then hand-rolled a slower
 * replacement. The vendor's implementation knows what its own silicon can do; deciding that is its
 * job, not the core's.
 *
 * Vendor headers are fine here: this is board_drivers, the partition vendor code is segregated to.
 */

#include "core_setup/board_profiles/protocore_platform.h"
#include "crypto/aead/aesgcm.h"
#include "crypto/crypto_opt.h"
#include "mmgr/secure.h"

#if PROTOCORE_HAS_HW_AESGCM

#include <mbedtls/gcm.h>

PROTOCORE_CRYPTO_HOT

// ===========================================================================
// Hardware GCM path (mbedtls_gcm -> the ESP32 AES peripheral).
// ===========================================================================

// The context is the vendor's, held for the life of the key. Standing one up and tearing it down
// costs ~9,200 cycles once the AES peripheral has been used - measured, and the whole reason this
// api is keyed rather than one-shot. The size assert is what keeps PROTOCORE_WORK_AESGCM honest against a
// vendor header we do not control.
static_assert(sizeof(mbedtls_gcm_context) <= PROTOCORE_WORK_AESGCM,
              "mbedtls_gcm_context outgrew PROTOCORE_WORK_AESGCM - raise it in protocore_config.h, which derives "
              "PROTOCORE_SECURE_ARENA_SIZE from it");

struct protocore_aesgcm_key *protocore_aesgcm_key_init(void *storage, const uint8_t key[PROTOCORE_AESGCM_KEY_LEN])
{
    mbedtls_gcm_context *g = (mbedtls_gcm_context *)(storage);
    mbedtls_gcm_init(g);
    if (mbedtls_gcm_setkey(g, MBEDTLS_CIPHER_ID_AES, key, 256) != 0)
    {
        mbedtls_gcm_free(g);
        return NULL;
    }
    return (struct protocore_aesgcm_key *)(g);
}

void protocore_aesgcm_key_wipe(struct protocore_aesgcm_key *k)
{
    mbedtls_gcm_context *g = (mbedtls_gcm_context *)(k);
    mbedtls_gcm_free(g); // releases whatever the vendor attached
    protocore_secure_wipe((uint8_t *)(g), sizeof(mbedtls_gcm_context));
}

protocore_cspan protocore_aesgcm_seal(struct protocore_aesgcm_key *k, const uint8_t nonce[PROTOCORE_AESGCM_IV_LEN],
                                      const uint8_t *aad, size_t aad_len, const uint8_t *pt, size_t pt_len,
                                      uint8_t *ct_out, uint8_t tag_out[PROTOCORE_AESGCM_TAG_LEN])
{
    mbedtls_gcm_context *g = (mbedtls_gcm_context *)(k);
    if (mbedtls_gcm_crypt_and_tag(g, MBEDTLS_GCM_ENCRYPT, pt_len, nonce, PROTOCORE_AESGCM_IV_LEN, aad, aad_len, pt,
                                  ct_out, PROTOCORE_AESGCM_TAG_LEN, tag_out) != 0)
    {
        return span.cfrom(NULL, 0);
    }
    return span.cfrom(ct_out, pt_len); // the tag rides in tag_out, not in this span
}

proto_bool protocore_aesgcm_open(struct protocore_aesgcm_key *k, const uint8_t nonce[PROTOCORE_AESGCM_IV_LEN],
                                 const uint8_t *aad, size_t aad_len, const uint8_t *ct, size_t ct_len,
                                 const uint8_t tag[PROTOCORE_AESGCM_TAG_LEN], uint8_t *out)
{
    mbedtls_gcm_context *g = (mbedtls_gcm_context *)(k);
    return mbedtls_gcm_auth_decrypt(g, ct_len, nonce, PROTOCORE_AESGCM_IV_LEN, aad, aad_len, tag,
                                    PROTOCORE_AESGCM_TAG_LEN, ct, out) == 0;
}

#endif // PROTOCORE_HAS_HW_AESGCM
