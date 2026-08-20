// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file aesgcm.h
 * @brief AES-256-GCM AEAD (RFC 5116) - keyed, detached tag.
 *
 * The shared AES-256-GCM primitive for the whole library (SSH aes256-gcm@openssh.com per RFC 5647, and
 * SMB 3.x transport encryption). The entries below are one surface over both arms: the GCM construction
 * and the table GHASH run in software on both, and only the AES-256 block under them changes arm - the
 * part's AES accelerator where it carries one, software AES-256 where it does not.
 *
 * The entries are keyed: @ref AesGcmNs::key_init once from the 32-byte key, then @ref AesGcmNs::seal or
 * @ref AesGcmNs::open per record against it. There is no raw-key one-shot; key_init binds the key,
 * derives H = E(K, 0^128) and builds the 4-bit GHASH table, a per-key cost every record would otherwise
 * repeat.
 *
 * The tag is detached: seal writes the ciphertext and the 16 tag bytes to separate destinations, which
 * is where the SSH packet and the SMB2 TRANSFORM_HEADER Signature field each want them. No nonce state
 * is kept or advanced by a seal or an open; @ref AesGcmNs::iv_increment advances the caller's.
 *
 * Host-tested byte-exact against the NIST/McGrew AES-256-GCM vectors.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_AESGCM_H
#define PROTOCORE_AESGCM_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_AESGCM

PROTOCORE_BEGIN_DECLS

/** @brief AES-256-GCM key length in bytes. */
#define PROTOCORE_AESGCM_KEY_LEN 32

/** @brief GCM nonce length in bytes = fixed_field(4) || invocation_counter(8). */
#define PROTOCORE_AESGCM_IV_LEN 12

/** @brief GCM authentication tag length in bytes. */
#define PROTOCORE_AESGCM_TAG_LEN 16

// PROTOCORE_AESGCM_BORROW - the bytes a keyed context runs out of - is stated in protocore_config.h,
// which sums it into the secure arena. A caller takes them once and passes the pointer to every call.

/** @brief The key a context is bound to. */
typedef struct
{
    const uint8_t *key; ///< PROTOCORE_AESGCM_KEY_LEN bytes
} AesGcmKeyArgs;

/** @brief One record sealed under the bound key. */
typedef struct
{
    const uint8_t *nonce; ///< PROTOCORE_AESGCM_IV_LEN bytes
    const uint8_t *aad;   ///< additional authenticated data, NULL when @c aad_len is 0
    size_t aad_len;       ///< its length
    const uint8_t *pt;    ///< the plaintext
    size_t pt_len;        ///< its length
    uint8_t *ct_out;      ///< pt_len ciphertext bytes; may alias @c pt
    uint8_t *tag_out;     ///< PROTOCORE_AESGCM_TAG_LEN bytes
} AesGcmSealArgs;

/** @brief One record opened under the bound key. */
typedef struct
{
    const uint8_t *nonce; ///< PROTOCORE_AESGCM_IV_LEN bytes
    const uint8_t *aad;   ///< additional authenticated data, NULL when @c aad_len is 0
    size_t aad_len;       ///< its length
    const uint8_t *ct;    ///< the ciphertext
    size_t ct_len;        ///< its length
    const uint8_t *tag;   ///< PROTOCORE_AESGCM_TAG_LEN bytes to verify against
    uint8_t *out;         ///< ct_len plaintext bytes; may alias @c ct
} AesGcmOpenArgs;

/** @brief The nonce an invocation counter is advanced in. */
typedef struct
{
    uint8_t *iv; ///< PROTOCORE_AESGCM_IV_LEN bytes, advanced in place
} AesGcmIvArgs;

/**
 * @brief AES-256-GCM (RFC 5116, NIST SP 800-38D).
 *
 * A caller sets the members a call takes, invokes it through ::AesGcm with the bytes it runs out of,
 * and reads the outcome off the same handle. How those bytes are carved is this module's and is never
 * named here.
 *
 *   AesGcm.key_args.key = key;
 *   AesGcm.key_init(work);
 *   AesGcm.seal_args.nonce = iv;
 *   AesGcm.seal_args.aad = aad;
 *   AesGcm.seal_args.aad_len = aad_len;
 *   AesGcm.seal_args.pt = pt;
 *   AesGcm.seal_args.pt_len = pt_len;
 *   AesGcm.seal_args.ct_out = ct;
 *   AesGcm.seal_args.tag_out = tag;
 *   AesGcm.seal(work);
 *   AesGcm.iv_args.iv = iv;
 *   AesGcm.iv_increment(work);
 *
 * @var AesGcmNs::key_args     the key a context is bound to
 * @var AesGcmNs::seal_args    one record sealed under the bound key
 * @var AesGcmNs::open_args    one record opened under the bound key
 * @var AesGcmNs::iv_args      the nonce an invocation counter is advanced in
 * @var AesGcmNs::ok           a call's true/false outcome
 * @var AesGcmNs::key_init     bind the borrow as a context keyed with @ref AesGcmNs::key_args
 * @var AesGcmNs::key_wipe     release what the context attached; call on rekey and on close
 * @var AesGcmNs::seal         encrypt one record and write its detached tag
 * @var AesGcmNs::open         verify the tag over aad || ct in constant time, then decrypt
 * @var AesGcmNs::iv_increment advance the RFC 5647 invocation counter, the nonce's low 8 bytes as a
 *                             big-endian integer; the 4-byte fixed field never changes
 *
 * @ref AesGcmNs::open produces no plaintext on a tag mismatch: it authenticates the received ciphertext
 * first and leaves @c out untouched when @ref AesGcmNs::ok comes back false.
 *
 * @c work is PROTOCORE_AESGCM_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and
 * the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it. The
 * borrow IS the keyed context, so two connections are two borrows and never collide, and the key
 * material dies with the release. @ref AesGcmNs::iv_increment works on the caller's nonce and
 * reads nothing out of the borrow.
 *
 * No storage member and no context: a caller sets operands and reads @ref AesGcmNs::ok, and that is
 * all the surface there is.
 */
typedef struct
{
    AesGcmKeyArgs key_args;
    AesGcmSealArgs seal_args;
    AesGcmOpenArgs open_args;
    AesGcmIvArgs iv_args;

    proto_bool ok;

    void (*const key_init)(uint8_t *restrict work);
    void (*const key_wipe)(uint8_t *restrict work);
    void (*const seal)(uint8_t *restrict work);
    void (*const open)(uint8_t *restrict work);
    void (*const iv_increment)(uint8_t *restrict work);
} AesGcmNs;

/** @brief The one symbol this module exports. */
extern AesGcmNs AesGcm;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_AESGCM

#endif // PROTOCORE_AESGCM_H
