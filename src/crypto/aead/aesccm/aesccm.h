// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

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
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_AESCCM_H
#define PROTOCORE_AESCCM_H

#include "protocore_config.h" // the entry point: protocore_types.h for proto_bool and the widths

#if PROTOCORE_ENABLE_AESCCM

PROTOCORE_BEGIN_DECLS

/** @brief AES-CCM authentication tag length used by SMB 3.x (bytes). */
#define PROTOCORE_AESCCM_TAG_LEN 16

// PROTOCORE_AESCCM_BORROW - the bytes one record runs out of - is stated in protocore_config.h, which
// sums it into the secure arena. A caller takes them once and passes the pointer to every call.

/** @brief One record sealed under the key given with it. */
typedef struct
{
    const uint8_t *key;   ///< 16 bytes (AES-128) or 32 bytes (AES-256)
    size_t key_len;       ///< 16 or 32
    const uint8_t *nonce; ///< 7..13 bytes; SMB uses 11
    size_t nonce_len;     ///< its length
    const uint8_t *aad;   ///< additional authenticated data, NULL when @c aad_len is 0
    size_t aad_len;       ///< its length, below 0xFF00
    const uint8_t *pt;    ///< the plaintext
    size_t pt_len;        ///< its length
    uint8_t *ct_out;      ///< pt_len ciphertext bytes; may alias @c pt
    uint8_t *tag_out;     ///< PROTOCORE_AESCCM_TAG_LEN bytes
} AesCcmSealArgs;

/** @brief One record opened under the key given with it. */
typedef struct
{
    const uint8_t *key;   ///< 16 bytes (AES-128) or 32 bytes (AES-256)
    size_t key_len;       ///< 16 or 32
    const uint8_t *nonce; ///< 7..13 bytes; SMB uses 11
    size_t nonce_len;     ///< its length
    const uint8_t *aad;   ///< additional authenticated data, NULL when @c aad_len is 0
    size_t aad_len;       ///< its length, below 0xFF00
    const uint8_t *ct;    ///< the ciphertext
    size_t ct_len;        ///< its length
    const uint8_t *tag;   ///< PROTOCORE_AESCCM_TAG_LEN bytes to verify against
    uint8_t *out;         ///< ct_len plaintext bytes; may alias @c ct
} AesCcmOpenArgs;

/**
 * @brief AES-CCM (NIST SP 800-38C, RFC 3610).
 *
 * A caller sets the members a call takes, invokes it through ::AesCcm with the bytes it runs out of, and
 * reads the outcome off the same handle. How those bytes are carved is this module's and is never named
 * here.
 *
 *   AesCcm.seal_args.key = key;
 *   AesCcm.seal_args.key_len = key_len;
 *   AesCcm.seal_args.nonce = nonce;
 *   AesCcm.seal_args.nonce_len = nonce_len;
 *   AesCcm.seal_args.aad = aad;
 *   AesCcm.seal_args.aad_len = aad_len;
 *   AesCcm.seal_args.pt = pt;
 *   AesCcm.seal_args.pt_len = pt_len;
 *   AesCcm.seal_args.ct_out = ct;
 *   AesCcm.seal_args.tag_out = tag;
 *   AesCcm.seal(work);
 *
 * @var AesCcmNs::seal_args  one record sealed under the key given with it
 * @var AesCcmNs::open_args  one record opened under the key given with it
 * @var AesCcmNs::ok         a call's true/false outcome
 * @var AesCcmNs::seal       CBC-MAC the record, encrypt the payload from A1, write the detached tag
 * @var AesCcmNs::open       decrypt from A1, recompute the tag over the recovered plaintext, compare it in
 *                           constant time
 *
 * @ref AesCcmNs::open fails closed: on a tag mismatch it zeroes @c out and @ref AesCcmNs::ok comes back
 * false, so no unauthenticated plaintext reaches the caller.
 *
 * @c work is PROTOCORE_AESCCM_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and the
 * pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it. The borrow IS
 * the record context, so two records are two borrows and never collide, and the expanded key schedule dies
 * with the release.
 *
 * No storage member and no context: a caller sets operands and reads @ref AesCcmNs::ok, and that is all
 * the surface there is.
 */
typedef struct
{
    AesCcmSealArgs seal_args;
    AesCcmOpenArgs open_args;
    proto_bool ok;
} AesCcmVars;

/** @brief The operands and the outcome. */
extern AesCcmVars AesCcmV;

/** @brief The entries. */
typedef struct
{
    void (*const seal)(uint8_t *restrict work);
    void (*const open)(uint8_t *restrict work);
} AesCcmNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in AesCcmV or a region of the borrow at a fixed offset.
void protocore_aes_ccm_seal(uint8_t *restrict work);
void protocore_aes_ccm_open(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `AesCcm.seal(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const AesCcmNs AesCcm __attribute__((unused)) = {
    .seal = protocore_aes_ccm_seal,
    .open = protocore_aes_ccm_open,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_AESCCM

#endif // PROTOCORE_AESCCM_H
