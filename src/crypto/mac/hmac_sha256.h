// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hmac_sha256.h
 * @brief HMAC-SHA2-256 (RFC 2104 + FIPS 198-1) - streaming context and one-shot API.
 *
 * The shared keyed-MAC primitive for the whole library: SSH binary-packet MAC (RFC 4253 §6.4), the
 * TLS 1.3 / QUIC / DTLS HKDF PRF, SNMPv3 usmHMACSHAAuthProtocol, JWT HS256, CSRF tokens, and SMB 2.x
 * message signing / the SP800-108 KDF. Built over the pc_sha256 streaming functions so the inner
 * SHA-256 hardware acceleration (where present) is transparent.
 *
 * RFC 2104 construction: HMAC(K, m) = H((K XOR opad) || H((K XOR ipad) || m)), H = SHA-256.
 *
 * SECURITY NOTE - a MAC must be verified before the covered plaintext is acted upon; that ordering
 * guarantee lives in each protocol's packet layer, not here. These functions are pure crypto.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HMAC_SHA256_H
#define PROTOCORE_HMAC_SHA256_H

#include "crypto/hash/sha256.h"
#include <stddef.h>
#include <stdint.h>

/** @brief HMAC-SHA2-256 output length in bytes. */
#define PC_HMAC_SHA256_LEN 32

/**
 * @brief Compute HMAC-SHA2-256 over a single contiguous buffer.
 *
 * @param key      MAC key bytes.
 * @param key_len  Key length in bytes. Keys > 64 bytes are pre-hashed per RFC 2104; keys <= 64 bytes
 *                 are zero-padded to the block length. SSH-derived keys are always 32 bytes.
 * @param data     Input bytes.
 * @param len      Input length.
 * @param mac      Output buffer, must be PC_HMAC_SHA256_LEN bytes.
 */
void pc_hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t len,
                    uint8_t mac[PC_HMAC_SHA256_LEN]);

/**
 * @brief Streaming HMAC-SHA2-256 context.
 *
 * For MACs assembled from separate pieces - e.g. the SSH packet MAC over
 * (uint32_be(seq_num) || plaintext_packet):
 *
 *   pc_hmac_sha256_init(&ctx, key, key_len);
 *   pc_hmac_sha256_update(&ctx, seq_bytes, 4);
 *   pc_hmac_sha256_update(&ctx, packet, pkt_len);
 *   pc_hmac_sha256_final(&ctx, mac_out);
 */
typedef struct
{
    pc_sha256_ctx inner; ///< Inner hash context (key XOR ipad prepended).
    uint8_t okey[64];    ///< Outer key block (key XOR opad), stored for the final step.
} pc_hmac_sha256_ctx;

/**
 * @brief Initialize a streaming HMAC-SHA2-256 context.
 * @param ctx      Context to initialize.
 * @param key      Key bytes (keys > 64 are pre-hashed per RFC 2104).
 * @param key_len  Length of key in bytes.
 */
void pc_hmac_sha256_init(pc_hmac_sha256_ctx *ctx, const uint8_t *key, size_t key_len);

/** @brief Feed @p len bytes into the running HMAC. */
void pc_hmac_sha256_update(pc_hmac_sha256_ctx *ctx, const uint8_t *data, size_t len);

/** @brief Finalize and write the 32-byte MAC. */
void pc_hmac_sha256_final(pc_hmac_sha256_ctx *ctx, uint8_t mac[PC_HMAC_SHA256_LEN]);

#endif // PROTOCORE_HMAC_SHA256_H
