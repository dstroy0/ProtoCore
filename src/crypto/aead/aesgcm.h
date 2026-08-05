// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file aesgcm.h
 * @brief AES-256-GCM AEAD (RFC 5116) - stateless, detached-tag API.
 *
 * A key is a per-connection object, so this is a KEYED api: build a context once from the 32-byte key
 * with pc_aesgcm_key_init(), then seal or open each record against it. There is deliberately no
 * raw-key one-shot. Standing a cipher context up and tearing it down costs ~9,200 cycles on an
 * ESP32-S3 once the AES peripheral has actually been used - a FIXED cost per call, so roughly 10%% of a
 * 1 KiB record, 30%% of a 256 B TLS record, and most of a small interactive SSH packet. A one-shot
 * entry point would have let every caller pay that without seeing it.
 *
 * The context holds an expanded key schedule for as long as the caller holds it, so it belongs in the
 * caller's secure storage and must be wiped on rekey and on close. This is not a new exposure: the raw
 * key is already resident for exactly that long, and the schedule is derivable from it.
 *
 * Used by SSH aes256-gcm@openssh.com (RFC 5647: advance the invocation counter with
 * pc_aesgcm_iv_increment after every packet) and SMB 3.x transport encryption.
 *
 * The implementation is a backend under board_drivers/ selected by the vendor's PC_HAS_HW_AESGCM.
 * Host-tested byte-exact against the NIST/McGrew AES-256-GCM vectors.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_AESGCM_H
#define PROTOCORE_AESGCM_H

#include "mmgr/span.h"        // pc_cspan: what the seal produced (empty == it did not)
#include "protocore_config.h" // PC_WORK_AESGCM sizes a context

PROTO_BEGIN_DECLS

/** @brief AES-256-GCM key length (bytes). */
#define PC_AESGCM_KEY_LEN 32
/** @brief GCM nonce length (bytes) = fixed_field(4) || invocation_counter(8). */
#define PC_AESGCM_IV_LEN 12
/** @brief GCM authentication tag length (bytes). */
#define PC_AESGCM_TAG_LEN 16

/**
 * @brief Opaque keyed AES-256-GCM context. Forward-declared only: the definition is the backend's, so
 * consumers hold it by pointer and size its storage with PC_WORK_AESGCM.
 */
struct pc_aesgcm_key;

/**
 * @brief Bind @p storage as a context keyed with @p key.
 *
 * @param storage exactly PC_WORK_AESGCM bytes of the caller's secure storage, 8-aligned. Declare it as
 *                that macro and it cannot be wrong - the backend static_asserts its context fits, so
 *                the size is settled at compile time and there is nothing to check here. Must outlive
 *                every seal/open against it, and must be wiped on rekey and on close.
 * @return the context, or NULL if the vendor rejected the key.
 */
struct pc_aesgcm_key *pc_aesgcm_key_init(void *storage, const uint8_t key[PC_AESGCM_KEY_LEN]);

/** @brief Wipe the expanded schedule. Call on rekey and on close; the storage stays the caller's. */
void pc_aesgcm_key_wipe(struct pc_aesgcm_key *k);

/**
 * @brief Seal one record under @p k and @p nonce.
 *
 * @p ct_out receives @p pt_len ciphertext bytes (may alias @p pt) and @p tag_out the 16-byte tag. No
 * state is kept or advanced - the caller owns the nonce.
 */
pc_cspan pc_aesgcm_seal(struct pc_aesgcm_key *k, const uint8_t nonce[PC_AESGCM_IV_LEN], const uint8_t *aad,
                        size_t aad_len, const uint8_t *pt, size_t pt_len, uint8_t *ct_out,
                        uint8_t tag_out[PC_AESGCM_TAG_LEN]);

/**
 * @brief Open one record: verify @p tag over @p aad || @p ct in constant time, then (only on success)
 *        decrypt @p ct into @p out (may alias @p ct). @return true iff the tag is valid.
 */
proto_bool pc_aesgcm_open(struct pc_aesgcm_key *k, const uint8_t nonce[PC_AESGCM_IV_LEN], const uint8_t *aad,
                          size_t aad_len, const uint8_t *ct, size_t ct_len, const uint8_t tag[PC_AESGCM_TAG_LEN],
                          uint8_t *out);

/**
 * @brief Advance the RFC 5647 invocation counter: the low 8 bytes of the 12-byte nonce as a big-endian
 *        integer; the 4-byte fixed field never changes. SSH calls this after each sealed/opened packet.
 */
void pc_aesgcm_iv_increment(uint8_t iv[PC_AESGCM_IV_LEN]);

PROTO_END_DECLS

#endif // PROTOCORE_AESGCM_H
