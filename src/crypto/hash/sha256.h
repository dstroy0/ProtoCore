// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths, PROTOCORE_HAS_HW_SHA for the context below

#if PROTOCORE_ENABLE_SHA256

PROTOCORE_BEGIN_DECLS

/** @brief SHA-256 digest length in bytes. */
#define PROTOCORE_SHA256_DIGEST_LEN 32

/** @brief SHA-256 block size in bytes. */
#define PROTOCORE_SHA256_BLOCK_LEN 64

// PROTOCORE_SHA256_BORROW - the working bytes a context takes from its caller - is stated in
// protocore_config.h, which gates it on the accelerator and sums it into the secure arena.

/**
 * @brief Streaming SHA-256 context. Forward-declared only: the definition is the compiled arm's, in
 * sha256.c, so a consumer holds it by pointer and sizes its storage with PROTOCORE_SHA256_BORROW.
 */
struct protocore_sha256;

/**
 * @brief Bind @p storage as a context and start a digest.
 * @param storage  PROTOCORE_SHA256_BORROW bytes, aligned for uint32_t, alive until final() returns.
 * @return the context, or NULL if @p storage is NULL.
 */
struct protocore_sha256 *protocore_sha256_init_impl(void *storage);

/** @brief Feed @p len bytes of @p data into the running hash. */
void protocore_sha256_update_impl(struct protocore_sha256 *ctx, const uint8_t *data, size_t len);

/**
 * @brief Pad, compress the last block, and write the 32-byte digest.
 *
 * The context survives: the padded blocks compress into a copy of the state, so the running hash is
 * exactly where it was and can keep taking data. That is what lets TLS 1.3 read Transcript-Hash at
 * every stage the key schedule asks for without snapshotting anything.
 *
 * @param digest  Output buffer, PROTOCORE_SHA256_DIGEST_LEN bytes.
 */
void protocore_sha256_final_impl(struct protocore_sha256 *ctx, uint8_t digest[PROTOCORE_SHA256_DIGEST_LEN]);

/** @brief One call: hash @p len bytes of @p data out of @p storage into @p digest. */
void protocore_sha256_hash_impl(void *storage, const uint8_t *data, size_t len,
                                uint8_t digest[PROTOCORE_SHA256_DIGEST_LEN]);

/**
 * @brief SHA-256: hand it storage, take back a digest.
 *
 * @var Sha256Ns::init    bind PROTOCORE_SHA256_BORROW bytes as a context and start a digest
 * @var Sha256Ns::update  feed the running digest a chunk
 * @var Sha256Ns::final   pad, compress the last block, write the 32 bytes out
 * @var Sha256Ns::hash    the three above in one call, for a message already whole
 *
 * No storage member. Every entry takes the bytes it works in from its caller, so the same table
 * serves every worker and nothing here reaches a pool.
 */
typedef struct
{
    struct protocore_sha256 *(*const init)(void *storage);
    void (*const update)(struct protocore_sha256 *ctx, const uint8_t *data, size_t len);
    void (*const final)(struct protocore_sha256 *ctx, uint8_t digest[PROTOCORE_SHA256_DIGEST_LEN]);
    void (*const hash)(void *storage, const uint8_t *data, size_t len, uint8_t digest[PROTOCORE_SHA256_DIGEST_LEN]);
} Sha256Ns;

/**
 * @brief The names, aliased.
 *
 * Initialized here rather than in the .c so the member read resolves in the reading translation unit
 * and the table is dropped, leaving `--gc-sections` free to reclaim what a build never calls.
 *
 * `unused` because this header reaches files that take none of it.
 */
static const Sha256Ns sha256 __attribute__((unused)) = {
    .init = protocore_sha256_init_impl,
    .update = protocore_sha256_update_impl,
    .final = protocore_sha256_final_impl,
    .hash = protocore_sha256_hash_impl,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SHA256

#endif // PROTOCORE_SHA256_H
