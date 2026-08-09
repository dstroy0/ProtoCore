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

#include "protocore_config.h" // the entry point: types.h for the widths, PC_HAS_HW_SHA for the context below

/** @brief SHA-512 digest length in bytes. */
#define PC_SHA512_DIGEST_LEN 64

/** @brief SHA-512 block size in bytes. */
#define PC_SHA512_BLOCK_LEN 128

/**
 * @brief Streaming SHA-512 context.
 *
 * The context owns nothing. The caller hands it ::PC_SHA512_BORROW working bytes, aligned for
 * @c uint64_t and alive until the digest comes back, and the context splits them into the regions
 * below at fixed offsets.
 */
#if PC_HAS_HW_SHA
#include <mbedtls/sha512.h>
typedef struct
{
    mbedtls_sha512_context mbed; ///< mbedtls SHA-512 state (ESP32).
} pc_sha512_ctx;
#else
typedef struct
{
    uint64_t s[8];  ///< Running hash words (H0..H7).
    uint64_t n;     ///< Total bytes processed so far.
    uint8_t *rx;    ///< Caller storage: bytes as they arrive, compressed when a block fills.
    uint8_t *tx;    ///< Caller storage: the padded last block, composed whole so no rx byte carries in.
    uint64_t *fs;   ///< Caller storage: the state copy the padded blocks compress into.
    uint32_t rxlen; ///< Bytes valid in rx.
} pc_sha512_ctx;
// The three pointers above are the caller's, so a struct copy aliases the original's storage and
// finalizing the copy writes through it. final() leaves the context running, so read it in place.
#endif

PROTO_BEGIN_DECLS

/**
 * @brief Start a digest in @p ctx, working out of the caller's @p work.
 * @param work  PC_SHA512_BORROW bytes, aligned for uint64_t, alive until final() returns.
 */
void pc_sha512_init(pc_sha512_ctx *ctx, uint8_t *work);

/** @brief Feed @p len bytes of @p data into the running hash. */
void pc_sha512_update(pc_sha512_ctx *ctx, const uint8_t *data, size_t len);

/**
 * @brief Pad, compress the last block, and write the 64-byte digest.
 *
 * The context survives: the padded blocks compress into a copy of the state, so the running hash is
 * exactly where it was and can keep taking data.
 *
 * @param digest  Output buffer, PC_SHA512_DIGEST_LEN bytes.
 */
void pc_sha512_final(pc_sha512_ctx *ctx, uint8_t digest[PC_SHA512_DIGEST_LEN]);

/** @brief One call: hash @p len bytes of @p data out of @p work into @p digest (64 bytes). */
void pc_sha512(uint8_t *work, const uint8_t *data, size_t len, uint8_t digest[PC_SHA512_DIGEST_LEN]);

PROTO_END_DECLS

#endif // PROTOCORE_SHA512_H
