// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_AESCCM_H
#define PROTOCORE_AESCCM_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file aesccm.h
 * @brief AEAD AES-CCM (NIST SP 800-38C / RFC 3610), 128- and 256-bit keys, detached tag.
 *
 * CCM = CTR encryption + CBC-MAC authentication under one key. SMB 3.x offers it as
 * SMB2_ENCRYPTION_AES128_CCM (0x0001) and SMB2_ENCRYPTION_AES256_CCM (0x0003); the transport uses an
 * 11-byte nonce and a 16-byte tag (MS-SMB2 §3.1.4.3). The entries below are one surface over both arms: the
 * SP 800-38C construction is the same on either, and the AES block under it is the part's accelerator where
 * it carries one and the shared software block (crypto/cipher/aes_block.h) where it does not, so the whole
 * AEAD is unit-testable off-target.
 *
 * The key rides with the record: a seal or an open expands it into the borrow and runs that one record
 * under it. The tag is detached: a seal writes the ciphertext and the 16 tag bytes to separate
 * destinations, which is where the SMB2 TRANSFORM_HEADER carries them - the Signature field holds the tag.
 *
 * Host-tested against reference AES-CCM vectors (nonce 11, tag 16, AES-128 and AES-256).
 *
 * @ref AesCcmNs::open fails closed: on a tag mismatch it zeroes @c out and @ref AesCcmNs::ok comes back
 * false, so no unauthenticated plaintext reaches the caller.
 *
 * @c work is PROTOCORE_AESCCM_BORROW secure bytes the CALLER took, at an address it knows. It is not held past the
 * call, so nothing here aliases it. The caller releases it, and the pool wipes on release; this module neither takes
 * it, holds it, releases it, nor wipes it. The borrow IS the record context, so two records are two borrows and never
 * collide, and the expanded key schedule dies with the release.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

/** @brief AES-CCM authentication tag length used by SMB 3.x (bytes). */
#define PROTOCORE_AESCCM_TAG_LEN 16

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*seal)(uint8_t *, const uint8_t *, size_t, const uint8_t *, size_t, const uint8_t *, size_t,
                       const uint8_t *, size_t, uint8_t *, uint8_t *);
    proto_bool (*open)(uint8_t *, const uint8_t *, size_t, const uint8_t *, size_t, const uint8_t *, size_t,
                       const uint8_t *, size_t, const uint8_t *, uint8_t *);
} AesCcmNs;
PROTOCORE_NS_LAYOUT(AesCcmNs, seal, open);

/**
 * @brief CBC-MAC the record, encrypt the payload from A1, write the detached tag.
 * @param work PROTOCORE_AES_CCM_BORROW bytes the caller took. Not held past the call.
 * @param key 16 bytes (AES-128) or 32 bytes (AES-256)
 * @param key_len 16 or 32
 * @param nonce 7..13 bytes; SMB uses 11
 * @param nonce_len its length
 * @param aad additional authenticated data, NULL when aad_len is 0
 * @param aad_len its length, below 0xFF00
 * @param pt the plaintext
 * @param pt_len its length
 * @param ct_out pt_len ciphertext bytes; may alias pt
 * @param tag_out PROTOCORE_AESCCM_TAG_LEN bytes
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_aes_ccm_seal(uint8_t *work, const uint8_t *key, size_t key_len, const uint8_t *nonce,
                                  size_t nonce_len, const uint8_t *aad, size_t aad_len, const uint8_t *pt,
                                  size_t pt_len, uint8_t *ct_out, uint8_t *tag_out);
/**
 * @brief Decrypt from A1, recompute the tag over the recovered plaintext, compare it in.
 * @param work PROTOCORE_AES_CCM_BORROW bytes the caller took. Not held past the call.
 * @param key 16 bytes (AES-128) or 32 bytes (AES-256)
 * @param key_len 16 or 32
 * @param nonce 7..13 bytes; SMB uses 11
 * @param nonce_len its length
 * @param aad additional authenticated data, NULL when aad_len is 0
 * @param aad_len its length, below 0xFF00
 * @param ct the ciphertext
 * @param ct_len its length
 * @param tag PROTOCORE_AESCCM_TAG_LEN bytes to verify against
 * @param out ct_len plaintext bytes; may alias ct
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_aes_ccm_open(uint8_t *work, const uint8_t *key, size_t key_len, const uint8_t *nonce,
                                  size_t nonce_len, const uint8_t *aad, size_t aad_len, const uint8_t *ct,
                                  size_t ct_len, const uint8_t *tag, uint8_t *out);

/** @brief Module namespace. */
PROTOCORE_NS AesCcmNs AesCcm PROTOCORE_UNUSED = {.seal = protocore_aes_ccm_seal, .open = protocore_aes_ccm_open};

PROTOCORE_END_DECLS

#endif // PROTOCORE_AESCCM_H
