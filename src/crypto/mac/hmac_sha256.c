// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hmac_sha256.c
 * @brief HMAC-SHA2-256 implementation (RFC 2104).
 *
 * Implemented in terms of the pc_sha256 streaming functions so it compiles identically on Arduino and
 * native. The inner SHA-256 hardware acceleration (where present) is transparent through those calls.
 *
 * RFC 2104 construction: HMAC(K, m) = H((K XOR opad) || H((K XOR ipad) || m)), H = SHA-256, ipad = 0x36
 * repeated, opad = 0x5c repeated. Keys > 64 bytes are pre-hashed; keys <= 64 are zero-padded to the
 * 64-byte block. SSH-derived MAC keys are 32 bytes, so they are padded, not pre-hashed.
 *
 * The transient working memory that touches the key (the padded ipad/opad blocks, the intermediate inner
 * digest, and the one outer / one-shot hash context) lives in the shared crypto scratch (HMAC-256 region)
 * and is wiped on the way out - never on the stack. The caller-owned streaming context (pc_hmac_sha256_ctx:
 * the opad key block + the inner hash state) is per-session state the caller wipes at teardown, so it is
 * NOT kept in the scratch (a long-lived value there would be clobbered by the next op).
 */

#include "crypto/mac/hmac_sha256.h"
#include "crypto/crypto_opt.h"
#include "mmgr/secure.h" // the secure pool: HMAC working state, wiped on release
#include <string.h>
// HMAC-SHA256 is HW-SHA-dominated; the only -O lever is its SW key-block glue. On the P4 that rides the per-die
// -O3 default (whose win is -O3's loop-unroll parameter budget). The S3's ~4% O3 edge is the same parameter
// class (bisected on-device: -fpeel-loops and -funswitch-loops both no-op), not a single transform, and not
// worth -O3's code-size / miscompile baggage on a HW-dominated MAC - so the S3 keeps the -O2 default. Capturing
// that 4% deliberately would take a source #pragma GCC unroll on the key-block loops (a code change, not a flag).
// See crypto_opt.h caveat 1.
PC_CRYPTO_HOT

// Transient HMAC-SHA256 working set, borrowed from the secure pool per call and wiped on release. No
// hand-assigned address: HMAC runs nested under the KDFs (SP800-108 / HKDF / TLS1.3), whose own
// borrows are still live, so the pool separates them by construction. The two 64-byte key blocks
// double as key-padding scratch for build_key_block.
typedef struct
{
    uint8_t opad[64];                           ///< one-shot opad block (persists inner->outer); else key-pad scratch
    uint8_t ipad[64];                           ///< ipad block; also key-pad scratch once its ipad is consumed
    uint8_t inner_digest[PC_SHA256_DIGEST_LEN]; ///< H((K XOR ipad) || m)
    pc_sha256_ctx hash;                         ///< transient hash: one-shot inner then outer; streaming final outer
} HmacWork;
static_assert(sizeof(HmacWork) <= PC_WORK_HMAC_SHA256,
              "HmacWork outgrew PC_WORK_HMAC_SHA256 - raise it in protocore_config.h, which derives "
              "PC_SECURE_ARENA_SIZE from it");

// Build one 64-byte HMAC key block into @p block (RFC 2104 sec 2), using @p scratch (64 bytes) to hold the
// zero-padded / pre-hashed key. Both @p block and @p scratch are pool-resident, never the stack.
static void build_key_block(const uint8_t *key, size_t key_len, uint8_t block[64], uint8_t pad_byte,
                            uint8_t scratch[64])
{
    memset(scratch, 0, 64);
    if (key_len > 64)
    {
        pc_sha256(key, key_len, scratch); // keys longer than the block are replaced by their SHA-256 hash
    }
    else
    {
        for (size_t i = 0; i < key_len; i++)
        {
            scratch[i] = key[i];
        }
    }
    for (int i = 0; i < 64; i++)
    {
        block[i] = scratch[i] ^ pad_byte;
    }
}

void pc_hmac_sha256_init(pc_hmac_sha256_ctx *ctx, const uint8_t *key, size_t key_len)
{
    size_t mark = pc_secure_mark();
    pc_span ws = pc_secure_span(sizeof(HmacWork), _Alignof(HmacWork));
    if (!pc_span_ok(ws))
    {
        pc_secure_release(mark);
        return; // pool exhausted: the empty borrow is the supported failure signal
    }
    HmacWork *w = (HmacWork *)ws.buf;
    build_key_block(key, key_len, w->ipad, 0x36u, w->opad);   // ipad -> scratch (opad slot holds the padded key)
    build_key_block(key, key_len, ctx->okey, 0x5cu, w->opad); // opad -> caller ctx (stored for the final step)

    pc_sha256_init(&ctx->inner);
    pc_sha256_update(&ctx->inner, w->ipad, 64);
    pc_secure_release(mark);
}

void pc_hmac_sha256_update(pc_hmac_sha256_ctx *ctx, const uint8_t *data, size_t len)
{
    pc_sha256_update(&ctx->inner, data, len);
}

void pc_hmac_sha256_final(pc_hmac_sha256_ctx *ctx, uint8_t mac[PC_HMAC_SHA256_LEN])
{
    size_t mark = pc_secure_mark();
    pc_span ws = pc_secure_span(sizeof(HmacWork), _Alignof(HmacWork));
    if (!pc_span_ok(ws))
    {
        pc_secure_release(mark);
        return; // pool exhausted: the empty borrow is the supported failure signal
    }
    HmacWork *w = (HmacWork *)ws.buf;
    pc_sha256_final(&ctx->inner, w->inner_digest);

    // Outer hash: H(okey || inner_digest)
    pc_sha256_init(&w->hash);
    pc_sha256_update(&w->hash, ctx->okey, 64);
    pc_sha256_update(&w->hash, w->inner_digest, PC_SHA256_DIGEST_LEN);
    pc_sha256_final(&w->hash, mac);
    pc_secure_release(mark);
}

void pc_hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t len,
                    uint8_t mac[PC_HMAC_SHA256_LEN])
{
    // Self-contained (does not build a caller-facing context): ipad block first, fold it into the inner hash,
    // then reuse its slot as the opad key-padding scratch - so no key block ever lands on the stack.
    size_t mark = pc_secure_mark();
    pc_span ws = pc_secure_span(sizeof(HmacWork), _Alignof(HmacWork));
    if (!pc_span_ok(ws))
    {
        pc_secure_release(mark);
        return; // pool exhausted: the empty borrow is the supported failure signal
    }
    HmacWork *w = (HmacWork *)ws.buf;
    build_key_block(key, key_len, w->ipad, 0x36u, w->opad); // ipad block (opad slot as key-pad scratch)
    pc_sha256_init(&w->hash);
    pc_sha256_update(&w->hash, w->ipad, 64);
    pc_sha256_update(&w->hash, data, len);
    pc_sha256_final(&w->hash, w->inner_digest); // inner = H((K XOR ipad) || m)

    build_key_block(key, key_len, w->opad, 0x5cu, w->ipad); // opad block (ipad slot now free as scratch)
    pc_sha256_init(&w->hash);
    pc_sha256_update(&w->hash, w->opad, 64);
    pc_sha256_update(&w->hash, w->inner_digest, PC_SHA256_DIGEST_LEN);
    pc_sha256_final(&w->hash, mac); // HMAC = H((K XOR opad) || inner)
    pc_secure_release(mark);
}
