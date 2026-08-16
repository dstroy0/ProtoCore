// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file kdf.c
 * @brief SP800-108 counter-mode KDF with HMAC-SHA256 PRF (see kdf.h).
 *
 * K(i) = HMAC-SHA256(Ki, [i]_32be || fixed) for i = 1, 2, ..., concatenated and truncated to the
 * requested length. The PRF is the @ref HmacSha256Ns entries, which carry the hardware acceleration
 * underneath them, so this file is the same on every target.
 *
 * Nothing here owns storage or touches the pool. The caller hands over PROTOCORE_KDF_BORROW bytes and
 * this file splits them by offset: the PRF's own bytes, then the counter and the block it writes.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_KDF

#include "crypto/crypto_opt.h"
#include "crypto/kdf/kdf.h"
#include "crypto/mac/hmac_sha256.h"
#include "mmgr/endian.h"
#include "mmgr/protomem.h"

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// The one definition, private to this TU. Only what is not derivable: the counter and the block K(i)
// lands in. K(i) is derived from Ki, so it is key material and never lands on the stack.
typedef struct
{
    uint8_t block[PROTOCORE_HMAC_SHA256_LEN]; ///< K(i)
    uint8_t ctr[4];                           ///< the counter, big-endian
} KdfCtx;

// The caller's borrow, split: the PRF's own bytes first so they land at the alignment its context
// wants, then the counter and the block.
#define KDF_OFF_MAC 0u
#define KDF_OFF_CTX (KDF_OFF_MAC + PROTOCORE_HMAC_SHA256_BORROW)
static_assert(KDF_OFF_CTX + sizeof(KdfCtx) <= PROTOCORE_KDF_BORROW,
              "PROTOCORE_KDF_BORROW is short of the PRF's bytes, the counter and the block - raise it in "
              "protocore_config.h, which derives PROTOCORE_SECURE_ARENA_SIZE from it");

// The regions, at their offsets in the caller's borrow.
#define KDF_MAC(w) ((w) + KDF_OFF_MAC)
#define KDF_CTX(w) ((KdfCtx *)(void *)((w) + KDF_OFF_CTX))

// --- the entries -----------------------------------------------------------

static void kdf_ctr_hmac_sha256(uint8_t *restrict work)
{
    Kdf.ok = PROTO_FALSE;
    if (!work || !Kdf.ctr_args.ki || !Kdf.ctr_args.fixed || !Kdf.ctr_args.out || Kdf.ctr_args.out_len == 0)
    {
        return;
    }
    const uint8_t *ki = Kdf.ctr_args.ki;
    const size_t ki_len = Kdf.ctr_args.ki_len;
    const uint8_t *fixed = Kdf.ctr_args.fixed;
    const size_t fixed_len = Kdf.ctr_args.fixed_len;
    uint8_t *out = Kdf.ctr_args.out;
    const size_t out_len = Kdf.ctr_args.out_len;
    KdfCtx *c = KDF_CTX(work);
    uint8_t *hw = KDF_MAC(work);

    size_t done = 0;
    for (uint32_t counter = 1; done < out_len; counter++)
    {
        protocore_wr32be(c->ctr, counter);
        HmacSha256.key_args.key = ki;
        HmacSha256.key_args.key_len = ki_len;
        HmacSha256.init(hw);
        HmacSha256.update_args.data = c->ctr;
        HmacSha256.update_args.len = sizeof(c->ctr);
        HmacSha256.update(hw);
        HmacSha256.update_args.data = fixed;
        HmacSha256.update_args.len = fixed_len;
        HmacSha256.update(hw);
        HmacSha256.final_args.out = c->block;
        HmacSha256.final(hw);
        size_t take = PROTOCORE_HMAC_SHA256_LEN;
        if (out_len - done < PROTOCORE_HMAC_SHA256_LEN)
        {
            take = out_len - done;
        }
        mem.cpy(out + done, c->block, take);
        done += take;
    }
    Kdf.ok = PROTO_TRUE;
}

KdfNs Kdf = {.ctr_hmac_sha256 = kdf_ctr_hmac_sha256};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_KDF
