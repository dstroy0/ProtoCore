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

#include "protocore_config.h" // the entry point: the widths

#include "crypto/kdf/kdf/kdf.h"
#include "crypto/mac/hmac_sha256/hmac_sha256.h"
#include "mmgr/endian/endian.h"
#include "mmgr/protomem/protomem.h"

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

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(KDF_OFF_CTX % _Alignof(KdfCtx) == 0,
              "KDF_OFF_CTX is not a multiple of alignof(KdfCtx) - KDF_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The regions, at their offsets in the caller's borrow.
#define KDF_MAC(w) ((w) + KDF_OFF_MAC)
#define KDF_CTX(w) ((KdfCtx *)(void *)((w) + KDF_OFF_CTX))

// --- the entries -----------------------------------------------------------

proto_bool protocore_kdf_ctr_hmac_sha256(uint8_t *restrict work, const uint8_t *ki, size_t ki_len, const uint8_t *fixed,
                                         size_t fixed_len, uint8_t *out, size_t out_len)
{
    if (!ki || !fixed || !out || out_len == 0)
    {
        return PROTO_FALSE;
    }
    KdfCtx *c = KDF_CTX(work);
    uint8_t *hw = KDF_MAC(work);

    size_t done = 0;
    for (uint32_t counter = 1; done < out_len; counter++)
    {
        protocore_wr32be(c->ctr, counter);
        HmacSha256.init(hw, ki, ki_len);
        HmacSha256.update(hw, c->ctr, sizeof(c->ctr));
        HmacSha256.update(hw, fixed, fixed_len);
        HmacSha256.final(hw, c->block);
        size_t take = PROTOCORE_HMAC_SHA256_LEN;
        if (out_len - done < PROTOCORE_HMAC_SHA256_LEN)
        {
            take = out_len - done;
        }
        mem.cpy(out + done, c->block, take);
        done += take;
    }
    return PROTO_TRUE;
}
