// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file aes128gcm.h
 * @brief AEAD_AES_128_GCM (RFC 5116) - keyed, detached tag - and the AES-128 block it is built on.
 *
 * The shared 128-bit AES primitives for the whole library: the AEAD, and one 16-byte ECB block under a
 * key of its own (QUIC header protection per RFC 9001 sec 5.4, the DTLS 1.3 sequence-number mask).
 * Consumed by QUIC Initial packet protection (RFC 9001 sec 5.3/5.4), the DTLS 1.3 record layer, the TLS
 * 1.3 record layer, and SMB 3.x transport encryption. The entries below are one surface over both arms:
 * the GCM construction (GCTR and a table GHASH) is software on both, and only the AES-128 block under it
 * changes, the part's AES accelerator where there is one and software AES-128 where there is not.
 *
 * The entries are keyed: @ref Aes128GcmNs::key_init once from the 16-byte key, then
 * @ref Aes128GcmNs::seal or @ref Aes128GcmNs::open per record against it. There is no raw-key one-shot,
 * so the key schedule and the GHASH table are built once per key rather than once per record.
 * @ref Aes128GcmNs::block_init keys the single block the same way, off its own part of the same borrow,
 * so a caller holding one direction's key material holds one pointer.
 *
 * The tag is detached: seal writes the ciphertext and the 16 tag bytes to separate destinations, which
 * is where the SMB2 TRANSFORM_HEADER Signature field wants them. A wire format that carries the tag
 * immediately after the ciphertext (QUIC, DTLS) passes @c ct_out + @c pt_len as @c tag_out and the tag
 * lands in place.
 *
 * Host-tested against the NIST GCM vectors and RFC 9001 Appendix A.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_AES128GCM_H
#define PROTOCORE_AES128GCM_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_AES128GCM

PROTOCORE_BEGIN_DECLS

/** @brief AEAD_AES_128_GCM key length in bytes. */
#define PROTOCORE_AES128GCM_KEY_LEN 16

/** @brief AEAD_AES_128_GCM nonce length in bytes. */
#define PROTOCORE_AES128GCM_IV_LEN 12

/** @brief AEAD_AES_128_GCM authentication tag length in bytes. */
#define PROTOCORE_AES128GCM_TAG_LEN 16

// PROTOCORE_AES128GCM_BORROW - the bytes a keyed context runs out of - is stated in protocore_config.h,
// which sums it into the secure arena. A caller takes them once and passes the pointer to every call.

/** @brief The key the AEAD context is bound to. */
typedef struct
{
    const uint8_t *key; ///< PROTOCORE_AES128GCM_KEY_LEN bytes
} Aes128GcmKeyArgs;
/** @brief One record sealed under the bound key. */
typedef struct
{
    const uint8_t *nonce; ///< PROTOCORE_AES128GCM_IV_LEN bytes
    const uint8_t *aad;   ///< additional authenticated data, NULL when @c aad_len is 0
    size_t aad_len;       ///< its length
    const uint8_t *pt;    ///< the plaintext
    size_t pt_len;        ///< its length
    uint8_t *ct_out;      ///< pt_len ciphertext bytes; may alias @c pt
    uint8_t *tag_out;     ///< PROTOCORE_AES128GCM_TAG_LEN bytes
} Aes128GcmSealArgs;
/** @brief One record opened under the bound key. */
typedef struct
{
    const uint8_t *nonce; ///< PROTOCORE_AES128GCM_IV_LEN bytes
    const uint8_t *aad;   ///< additional authenticated data, NULL when @c aad_len is 0
    size_t aad_len;       ///< its length
    const uint8_t *ct;    ///< the ciphertext, tag not included
    size_t ct_len;        ///< its length
    const uint8_t *tag;   ///< PROTOCORE_AES128GCM_TAG_LEN bytes to verify against
    uint8_t *out;         ///< ct_len plaintext bytes; may alias @c ct
} Aes128GcmOpenArgs;
/** @brief The key the single-block cipher is bound to. */
typedef struct
{
    const uint8_t *key; ///< PROTOCORE_AES128GCM_KEY_LEN bytes
} Aes128GcmBlockKeyArgs;
/** @brief The one block an ECB encryption runs over. */
typedef struct
{
    const uint8_t *in; ///< 16 input bytes
    uint8_t *out;      ///< 16 output bytes; may alias @c in
} Aes128GcmBlockArgs;
/**
 * @brief AEAD_AES_128_GCM (RFC 5116, NIST SP 800-38D) and the AES-128 block.
 *
 * A caller sets the members a call takes, invokes it through ::Aes128Gcm with the bytes it runs out of,
 * and reads the outcome off the same handle. How those bytes are carved is this module's and is never
 * named here.
 *
 *   Aes128Gcm.key_args.key = key;
 *   Aes128Gcm.key_init(work);
 *   Aes128Gcm.seal_args.nonce = nonce;
 *   Aes128Gcm.seal_args.aad = hdr;
 *   Aes128Gcm.seal_args.aad_len = hdr_len;
 *   Aes128Gcm.seal_args.pt = pt;
 *   Aes128Gcm.seal_args.pt_len = pt_len;
 *   Aes128Gcm.seal_args.ct_out = ct;
 *   Aes128Gcm.seal_args.tag_out = ct + pt_len;
 *   Aes128Gcm.seal(work);
 *
 * @var Aes128GcmNs::key_args        the key the AEAD context is bound to
 * @var Aes128GcmNs::seal_args       one record sealed under the bound key
 * @var Aes128GcmNs::open_args       one record opened under the bound key
 * @var Aes128GcmNs::block_key_args  the key the single-block cipher is bound to
 * @var Aes128GcmNs::block_args      the one block an ECB encryption runs over
 * @var Aes128GcmNs::ok              a call's true/false outcome
 * @var Aes128GcmNs::key_init        bind the borrow as a context keyed with @ref Aes128GcmNs::key_args
 * @var Aes128GcmNs::key_wipe        release what the AEAD context attached; call on rekey and on close
 * @var Aes128GcmNs::seal            encrypt one record and write its detached tag
 * @var Aes128GcmNs::open            verify the tag over aad || ct in constant time, then decrypt
 * @var Aes128GcmNs::block_init      key the single block with @ref Aes128GcmNs::block_key_args
 * @var Aes128GcmNs::block_encrypt   encrypt one 16-byte block (ECB) under that key
 * @var Aes128GcmNs::block_wipe      release what the block context attached; call on rekey and on close
 *
 * @ref Aes128GcmNs::open produces no plaintext on a tag mismatch: it authenticates the received
 * ciphertext first and leaves @c out untouched when @ref Aes128GcmNs::ok comes back false.
 *
 * The AEAD key and the block key are separate: @ref Aes128GcmNs::key_init and
 * @ref Aes128GcmNs::block_init bind different parts of the borrow, so a record and its header mask run
 * under the two keys the protocol derived without either call disturbing the other.
 *
 * @c work is PROTOCORE_AES128GCM_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and the
 * pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it. The borrow
 * IS the keyed context, so two directions are two borrows and never collide, and both block contexts die
 * with the release.
 *
 * No storage member and no context: a caller sets operands and reads @ref Aes128GcmNs::ok, and that is
 * all the surface there is.
 */
typedef struct
{
    Aes128GcmKeyArgs key_args;
    Aes128GcmSealArgs seal_args;
    Aes128GcmOpenArgs open_args;
    Aes128GcmBlockKeyArgs block_key_args;
    Aes128GcmBlockArgs block_args;
    proto_bool ok;
} Aes128GcmVars;

/** @brief The operands and the outcome. */
extern Aes128GcmVars Aes128GcmV;

/** @brief The entries. */
typedef struct
{
    void (*const key_init)(uint8_t *restrict work);
    void (*const key_wipe)(uint8_t *restrict work);
    void (*const seal)(uint8_t *restrict work);
    void (*const open)(uint8_t *restrict work);
    void (*const block_init)(uint8_t *restrict work);
    void (*const block_encrypt)(uint8_t *restrict work);
    void (*const block_wipe)(uint8_t *restrict work);
} Aes128GcmNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Aes128GcmV or a region of the borrow at a fixed offset.
void protocore_aes128gcm_key_init(uint8_t *restrict work);
void protocore_aes128gcm_key_wipe(uint8_t *restrict work);
void protocore_aes128gcm_seal(uint8_t *restrict work);
void protocore_aes128gcm_open(uint8_t *restrict work);
void protocore_aes128gcm_block_init(uint8_t *restrict work);
void protocore_aes128gcm_block_encrypt(uint8_t *restrict work);
void protocore_aes128gcm_block_wipe(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Aes128Gcm.key_init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Aes128GcmNs Aes128Gcm __attribute__((unused)) = {
    .key_init = protocore_aes128gcm_key_init,
    .key_wipe = protocore_aes128gcm_key_wipe,
    .seal = protocore_aes128gcm_seal,
    .open = protocore_aes128gcm_open,
    .block_init = protocore_aes128gcm_block_init,
    .block_encrypt = protocore_aes128gcm_block_encrypt,
    .block_wipe = protocore_aes128gcm_block_wipe,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_AES128GCM

#endif // PROTOCORE_AES128GCM_H
