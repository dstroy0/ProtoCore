// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file aesccm.h
 * @brief AEAD AES-CCM (NIST SP 800-38C / RFC 3610), 128- and 256-bit keys, detached tag.
 *
 * CCM = CTR encryption + CBC-MAC authentication under one key. SMB 3.x offers it as
 * SMB2_ENCRYPTION_AES128_CCM (0x0001) and SMB2_ENCRYPTION_AES256_CCM (0x0003); the transport uses an
 * 11-byte nonce and a 16-byte tag (MS-SMB2 §3.1.4.3). The tag is kept detached (returned separately, not
 * appended to the ciphertext) because the SMB2 TRANSFORM_HEADER carries it in its own Signature field.
 *
 * On Arduino (ESP32) the block cipher is mbedtls_ccm, which routes AES through the hardware accelerator; on
 * the native host a compact software AES (crypto/cipher/aes_block.h, shared with the GCM modules) drives the same
 * CCM construction so the whole AEAD is unit-testable off-target. Pure, zero heap; host-tested against
 * reference AES-CCM vectors (nonce 11, tag 16, AES-128 and AES-256).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_AESCCM_H
#define PROTOCORE_AESCCM_H

#include "protocore_config.h"

// SMB 3.x is the only consumer of AES-CCM today; gate it so non-SMB builds do not carry the code.
#if PC_ENABLE_SMB

#if PC_HAS_HW_AES
#include <mbedtls/ccm.h> // hardware-backed AES-CCM on ESP32
#endif

/** @brief AES-CCM authentication tag length used by SMB 3.x (bytes). */
#define PC_AESCCM_TAG_LEN 16

/**
 * @brief Seal: AES-CCM encrypt-and-authenticate with a detached tag.
 *
 * @param key       16-byte (AES-128) or 32-byte (AES-256) key.
 * @param key_len   16 or 32.
 * @param nonce     Nonce (7..13 bytes; SMB uses 11).
 * @param nonce_len Nonce length.
 * @param aad       Additional authenticated data (may be NULL when @p aad_len is 0).
 * @param aad_len   AAD length (< 0xFF00).
 * @param pt        Plaintext.
 * @param pt_len    Plaintext length.
 * @param ct_out    Output: @p pt_len ciphertext bytes (may alias @p pt).
 * @param tag_out   Output: the 16-byte authentication tag.
 * @return true on success, false on a bad argument.
 */
proto_bool pc_aesccm_seal_tag(const uint8_t *key, size_t key_len, const uint8_t *nonce, size_t nonce_len,
                              const uint8_t *aad, size_t aad_len, const uint8_t *pt, size_t pt_len, uint8_t *ct_out,
                              uint8_t tag_out[PC_AESCCM_TAG_LEN]);

/**
 * @brief Open: AES-CCM verify-and-decrypt with a detached tag.
 *
 * The tag is recomputed over the recovered plaintext and compared in constant time; on mismatch @p out is
 * zeroed and false is returned (fails closed - no unauthenticated plaintext is exposed to the caller).
 * @p out receives @p ct_len plaintext bytes and may alias @p ct.
 * @return true iff the tag is valid.
 */
proto_bool pc_aesccm_open_tag(const uint8_t *key, size_t key_len, const uint8_t *nonce, size_t nonce_len,
                              const uint8_t *aad, size_t aad_len, const uint8_t *ct, size_t ct_len,
                              const uint8_t tag[PC_AESCCM_TAG_LEN], uint8_t *out);

#endif // PC_ENABLE_SMB

#endif // PROTOCORE_AESCCM_H
