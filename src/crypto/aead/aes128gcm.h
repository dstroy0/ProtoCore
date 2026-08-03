// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file aes128gcm.h
 * @brief AES-128 block cipher + AEAD_AES_128_GCM (RFC 5116 / NIST SP 800-38D).
 *
 * The generic 128-bit AES primitives: encrypt one 16-byte block under a 128-bit key (ECB - used for
 * GCM's counter mode and for keystream sampling), and the one-shot AEAD_AES_128_GCM seal/open (96-bit
 * nonce, 128-bit tag). Consumed by QUIC Initial packet protection (RFC 9001 sec 5.3/5.4), the DTLS 1.3
 * record layer, and SMB 3.x transport encryption - a single home for the primitive, not per-protocol copies.
 *
 * On Arduino (ESP32) the AES block is mbedtls, routed to the hardware AES accelerator; on native host
 * builds a compact software AES-128 is used so the whole AEAD is unit-testable off-target. GHASH and the
 * counter loop are the same software on both. Pure, zero heap, host-tested against the NIST GCM vectors
 * and RFC 9001 Appendix A.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_AES128GCM_H
#define PROTOCORE_AES128GCM_H

#include "protocore_config.h"

// Shared by the HTTP/3 (QUIC) packet protection and the DTLS 1.3 record layer.
#if (PC_ENABLE_HTTP3 || PC_ENABLE_DTLS || PC_ENABLE_SMB)

#include "shared_primitives/span.h" // pc_cspan: what the seal produced (empty == it did not)

/** @brief AEAD_AES_128_GCM key length in bytes. */
#define PC_AES128GCM_KEY_LEN 16
/** @brief AEAD_AES_128_GCM nonce length in bytes. */
#define PC_AES128GCM_IV_LEN 12
/** @brief AEAD_AES_128_GCM authentication tag length in bytes. */
#define PC_AES128GCM_TAG_LEN 16

// ---------------------------------------------------------------------------
// AES-128 single-block primitive (used by GCM and by header protection)
// ---------------------------------------------------------------------------

// Opaque: the definition is private to the backend under board_drivers/ that this build selected -
// consumers know only the symbol and hold it via struct pc_aes128*. No vendor type is named here.
struct pc_aes128;

/**
 * @brief Storage this module wants for one AES-128 context.
 *
 * The type is opaque, so a consumer cannot size it - this module owns the definition and therefore
 * owns the allocation. Call inside a SecureScope: the scope is how the caller states how long it
 * needs the resource, and the pool wipes the key schedule when that scope ends.
 *
 * @return a context to pass to pc_aes128_init(), or nullptr if the pool could not satisfy it.
 */
struct pc_aes128 *pc_aes128_wants(void);

/** @brief Load a 128-bit key and expand the encryption key schedule. */
void pc_aes128_init(struct pc_aes128 *ctx, const uint8_t key[16]);

/** @brief Encrypt one 16-byte block (ECB). @p in and @p out may alias. */
void pc_aes128_encrypt_block(struct pc_aes128 *ctx, const uint8_t in[16], uint8_t out[16]);

/** @brief Wipe the key schedule (and release mbedtls state on Arduino). */
void pc_aes128_wipe(struct pc_aes128 *ctx);

// ---------------------------------------------------------------------------
// AEAD_AES_128_GCM (96-bit nonce, 128-bit tag) - keyed
// ---------------------------------------------------------------------------

/**
 * @brief Opaque keyed AEAD_AES_128_GCM context. Forward-declared only: the definition belongs to the
 * backend, so consumers hold it by pointer and size its storage with PC_WORK_AES128GCM.
 *
 * This api is keyed, and there is deliberately no raw-key one-shot. A key protects a whole connection's
 * worth of records, but building a cipher context and tearing it down costs ~9,200 cycles on an
 * ESP32-S3 once the AES peripheral has been used - a FIXED cost per record, which is most of the work
 * for the small records QUIC and DTLS actually send. A one-shot entry point would let every caller pay
 * that without seeing it.
 *
 * The context holds an expanded key schedule for as long as the caller holds it, so it belongs in the
 * caller's secure storage and must be wiped on rekey and on close.
 */
struct pc_aes128gcm_key;

/**
 * @brief Bind @p storage as a context keyed with @p key.
 *
 * @param storage secure, PC_WORK_AES128GCM bytes, 8-aligned. Declare it as that macro and it cannot be
 *                wrong - the backend static_asserts its context fits, so the size is settled at compile
 *                time and there is nothing to check here.
 * @return the context, or nullptr if the vendor rejected the key.
 */
struct pc_aes128gcm_key *pc_aes128gcm_key_init(void *storage, const uint8_t key[PC_AES128GCM_KEY_LEN]);

/** @brief Wipe the expanded schedule. Call on rekey and on close; the storage stays the caller's. */
void pc_aes128gcm_key_wipe(struct pc_aes128gcm_key *k);

/**
 * @brief Seal one record: encrypt @p pt and authenticate it together with @p aad.
 *
 * @p ct_out receives @p pt_len ciphertext bytes (may alias @p pt) and @p tag_out the 16-byte tag.
 *
 * The tag is always detached. A wire format that carries the tag immediately after the ciphertext (QUIC,
 * DTLS) is not a different operation - pass `ct_out + pt_len` as @p tag_out and it is written in place.
 * That is why there is no second "attached" pair of entry points.
 */
pc_cspan pc_aes128gcm_seal(struct pc_aes128gcm_key *k, const uint8_t nonce[PC_AES128GCM_IV_LEN], const uint8_t *aad,
                           size_t aad_len, const uint8_t *pt, size_t pt_len, uint8_t *ct_out,
                           uint8_t tag_out[PC_AES128GCM_TAG_LEN]);

/**
 * @brief Open one record: verify @p tag over @p aad || @p ct in constant time, then (only on success)
 *        decrypt @p ct into @p out (may alias @p ct).
 *
 * @p ct_len is the ciphertext length, NOT including the tag.
 * @return true iff the tag is valid; on mismatch nothing is written.
 */
proto_bool pc_aes128gcm_open(struct pc_aes128gcm_key *k, const uint8_t nonce[PC_AES128GCM_IV_LEN], const uint8_t *aad,
                             size_t aad_len, const uint8_t *ct, size_t ct_len, const uint8_t tag[PC_AES128GCM_TAG_LEN],
                             uint8_t *out);

#endif // PC_ENABLE_HTTP3 || PC_ENABLE_DTLS || PC_ENABLE_SMB

#endif // PROTOCORE_AES128GCM_H
