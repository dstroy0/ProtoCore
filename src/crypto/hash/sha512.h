// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sha512.h
 * @brief SHA-512 (FIPS 180-4) - streaming context and one-shot API.
 *
 * The shared SHA-512 primitive for the whole library (SSH Ed25519 / kex hashing, PQC, SMB 3.1.1
 * preauth integrity). On Arduino (ESP32) the streaming context and one-shot delegate to mbedtls
 * (hardware-accelerated where available); on native builds the software FIPS-180-4 implementation is
 * used. Mirrors the sha256 dual-path structure.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SHA512_H
#define PROTOCORE_SHA512_H

#include "protocore_config.h" // the entry point: types.h for the widths, PROTOCORE_HOT for the context below

/** @brief SHA-512 digest length in bytes. */
#define PC_SHA512_DIGEST_LEN 64

/** @brief SHA-512 block size in bytes. */
#define PC_SHA512_BLOCK_LEN 128

/** @brief Streaming SHA-512 context. */
#if PROTOCORE_HOT
#include <mbedtls/sha512.h>
typedef struct
{
    mbedtls_sha512_context mbed; ///< mbedtls SHA-512 state (ESP32).
} pc_sha512_ctx;
#else
typedef struct
{
    uint64_t s[8];    ///< Running hash words (H0..H7).
    uint64_t n;       ///< Total bytes processed so far.
    uint8_t buf[128]; ///< Partial block accumulator.
    uint32_t buflen;  ///< Bytes valid in buf[].
} pc_sha512_ctx;
#endif

/** @brief Initialize a streaming SHA-512 context (@p ctx must not be NULL). */
void pc_sha512_init(pc_sha512_ctx *ctx);

/** @brief Feed @p len bytes of @p data into the running hash. */
void pc_sha512_update(pc_sha512_ctx *ctx, const uint8_t *data, size_t len);

/**
 * @brief Finalize the hash and write the 64-byte digest. The context is undefined afterwards; call
 *        init() again before reuse.
 * @param digest  Output buffer, PC_SHA512_DIGEST_LEN bytes.
 */
void pc_sha512_final(pc_sha512_ctx *ctx, uint8_t digest[PC_SHA512_DIGEST_LEN]);

/** @brief One-shot SHA-512: hash @p len bytes of @p data into @p digest (64 bytes). */
void pc_sha512(const uint8_t *data, size_t len, uint8_t digest[PC_SHA512_DIGEST_LEN]);

#endif // PROTOCORE_SHA512_H
