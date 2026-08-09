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

#include "protocore_config.h" // the entry point: types.h for the widths, PC_HAS_HW_SHA for the context below

/** @brief SHA-256 digest length in bytes. */
#define PC_SHA256_DIGEST_LEN 32

/** @brief SHA-256 block size in bytes. */
#define PC_SHA256_BLOCK_LEN 64

// PC_SHA256_BORROW - the working bytes a context takes from its caller - is stated in
// protocore_config.h, which gates it on the accelerator and sums it into the secure arena.

/**
 * @brief Streaming SHA-256 context.
 *
 * Holds the running hash state so data can be fed in multiple chunks. Backs the per-packet HMAC (run
 * on every inbound and outbound SSH packet) and the KEX exchange-hash assembled from several
 * separately-encoded fields, so hardware acceleration matters for bulk throughput, not just handshakes.
 *
 * The context owns nothing. The caller hands it ::PC_SHA256_BORROW working bytes, aligned for
 * @c uint32_t and alive until the digest comes back, and the context splits them into the regions
 * below at fixed offsets.
 */
#if PC_HAS_HW_SHA
#include <mbedtls/sha256.h>
typedef struct
{
    mbedtls_sha256_context mbed; ///< HW-accelerated SHA-256 state (ESP32 mbedtls).
} pc_sha256_ctx;
#else
typedef struct
{
    uint32_t s[8];  ///< Running hash words (H0..H7).
    uint64_t n;     ///< Total bytes processed so far.
    uint8_t *rx;    ///< Caller storage: bytes as they arrive, compressed when a block fills.
    uint8_t *tx;    ///< Caller storage: the padded last block, composed whole so no rx byte carries in.
    uint32_t *fs;   ///< Caller storage: the state copy the padded blocks compress into.
    uint32_t rxlen; ///< Bytes valid in rx.
} pc_sha256_ctx;
#endif

PROTO_BEGIN_DECLS

/**
 * @brief Start a digest in @p ctx, working out of the caller's @p work.
 * @param work  PC_SHA256_BORROW bytes, aligned for uint32_t, alive until final() returns.
 */
void pc_sha256_init(pc_sha256_ctx *ctx, uint8_t *work);

/** @brief Feed @p len bytes of @p data into the running hash. */
void pc_sha256_update(pc_sha256_ctx *ctx, const uint8_t *data, size_t len);

/**
 * @brief Pad, compress the last block, and write the 32-byte digest.
 *
 * The context survives: the padded blocks compress into a copy of the state, so the running hash is
 * exactly where it was and can keep taking data. That is what lets TLS 1.3 read Transcript-Hash at
 * every stage the key schedule asks for without snapshotting anything.
 *
 * @param digest  Output buffer, PC_SHA256_DIGEST_LEN bytes.
 */
void pc_sha256_final(pc_sha256_ctx *ctx, uint8_t digest[PC_SHA256_DIGEST_LEN]);

/** @brief One call: hash @p len bytes of @p data out of @p work into @p digest. */
void pc_sha256(uint8_t *work, const uint8_t *data, size_t len, uint8_t digest[PC_SHA256_DIGEST_LEN]);

/**
 * @brief SHA-256: hand it storage, take back a digest.
 *
 * @var Sha256Ns::init    bind @c ctx to the caller's working bytes and start a digest
 * @var Sha256Ns::update  feed the running digest a chunk
 * @var Sha256Ns::final   pad, compress the last block, write the 32 bytes out
 * @var Sha256Ns::hash    the three above in one call, for a message already whole
 *
 * No storage member. Every entry takes the bytes it works in from its caller, so the same table
 * serves every worker and nothing here reaches a pool.
 */
typedef struct
{
    void (*init)(pc_sha256_ctx *ctx, uint8_t *work);
    void (*update)(pc_sha256_ctx *ctx, const uint8_t *data, size_t len);
    void (*final)(pc_sha256_ctx *ctx, uint8_t digest[PC_SHA256_DIGEST_LEN]);
    void (*hash)(uint8_t *work, const uint8_t *data, size_t len, uint8_t digest[PC_SHA256_DIGEST_LEN]);
} Sha256Ns;

/**
 * @brief The names, aliased.
 *
 * Initialized here rather than in the .c so the member read resolves in the reading translation unit
 * and the table is dropped, leaving `--gc-sections` free to reclaim what a build never calls.
 *
 * `unused` because this header reaches files that take none of it.
 */
static const Sha256Ns sha256 __attribute__((unused)) = {pc_sha256_init, pc_sha256_update, pc_sha256_final, pc_sha256};

PROTO_END_DECLS

#endif // PROTOCORE_SHA256_H
