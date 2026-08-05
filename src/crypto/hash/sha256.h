// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sha256.h
 * @brief SHA-256 (FIPS 180-4) - streaming context and one-shot API.
 *
 * The shared SHA-256 primitive for the whole library (SSH per-packet HMAC and KEX exchange-hash, TLS
 * 1.3 / QUIC / DTLS key schedules, SNMPv3, JWT, CSRF, SMB 2.x message signing). On Arduino (ESP32)
 * BOTH the streaming context and the one-shot delegate to mbedtls, which routes SHA-256 to the
 * hardware accelerator; on native builds the software FIPS-180-4 implementation below is used.
 * Mirrors the sha512 dual-path structure.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SHA256_H
#define PROTOCORE_SHA256_H

#include "protocore_config.h" // the entry point: types.h for the widths, PROTOCORE_HOT for the context below

/** @brief SHA-256 digest length in bytes. */
#define PC_SHA256_DIGEST_LEN 32

/** @brief SHA-256 block size in bytes. */
#define PC_SHA256_BLOCK_LEN 64

/**
 * @brief Streaming SHA-256 context.
 *
 * Holds the running hash state so data can be fed in multiple chunks. Backs the per-packet HMAC (run
 * on every inbound and outbound SSH packet) and the KEX exchange-hash assembled from several
 * separately-encoded fields, so hardware acceleration matters for bulk throughput, not just handshakes.
 */
#if PROTOCORE_HOT
#include <mbedtls/sha256.h>
typedef struct
{
    mbedtls_sha256_context mbed; ///< HW-accelerated SHA-256 state (ESP32 mbedtls).
} pc_sha256_ctx;
#else
typedef struct
{
    uint32_t s[8];   ///< Running hash words (H0..H7).
    uint64_t n;      ///< Total bytes processed so far.
    uint8_t buf[64]; ///< Partial block accumulator.
    uint32_t buflen; ///< Bytes valid in buf[].
} pc_sha256_ctx;
#endif

/** @brief Initialize a streaming SHA-256 context (@p ctx must not be NULL). */
void pc_sha256_init(pc_sha256_ctx *ctx);

/** @brief Feed @p len bytes of @p data into the running hash. */
void pc_sha256_update(pc_sha256_ctx *ctx, const uint8_t *data, size_t len);

/**
 * @brief Finalize the hash and write the 32-byte digest. The context is undefined afterwards; call
 *        init() again before reuse.
 * @param digest  Output buffer, PC_SHA256_DIGEST_LEN bytes.
 */
void pc_sha256_final(pc_sha256_ctx *ctx, uint8_t digest[PC_SHA256_DIGEST_LEN]);

/** @brief One-shot SHA-256: hash @p len bytes of @p data into @p digest (32 bytes). */
void pc_sha256(const uint8_t *data, size_t len, uint8_t digest[PC_SHA256_DIGEST_LEN]);

#endif // PROTOCORE_SHA256_H
