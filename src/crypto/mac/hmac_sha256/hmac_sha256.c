// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hmac_sha256.c
 * @brief HMAC-SHA2-256 implementation (RFC 2104).
 *
 * Implemented in terms of the @ref Sha256Ns entries, so which arm compresses the inner hash is not
 * visible here and this file is the same on every target.
 *
 * RFC 2104 construction: HMAC(K, m) = H((K XOR opad) || H((K XOR ipad) || m)), H = SHA-256, ipad = 0x36
 * repeated, opad = 0x5c repeated. Keys > 64 bytes are pre-hashed; keys <= 64 are zero-padded to the
 * 64-byte block. SSH-derived MAC keys are 32 bytes, so they are padded, not pre-hashed.
 *
 * Nothing here owns storage or touches the pool. The caller hands over PROTOCORE_HMAC_SHA256_BORROW bytes and
 * this file splits them by offset: the context, the inner hash's own bytes, the outer key block, and
 * the transient set init and final work in. A connection takes those bytes once for its slot and
 * reuses them for every packet, so a MAC on the packet path costs no borrow and no wipe.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HMAC_SHA256

#include "crypto/crypto_opt.h"
#include "crypto/hash/sha256/sha256.h" // Sha256 - the digest this MAC drives, and its lengths
#include "crypto/mac/hmac_sha256/hmac_sha256.h"
#include "mmgr/protomem/protomem.h"

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// The transient half of the caller's bytes: live inside init and inside final, dead between them. The
// two 64-byte key blocks double as key-padding scratch for build_key_block.
typedef struct
{
    uint8_t opad[64]; ///< one-shot opad block (persists inner->outer); else key-pad scratch
    uint8_t ipad[64]; ///< ipad block; also key-pad scratch once its ipad is consumed
    uint8_t inner_digest[PROTOCORE_SHA256_DIGEST_LEN]; ///< H((K XOR ipad) || m)
} HmacWork;

// The module's own borrow, split by offset: the inner hash's bytes, the outer key block, the transient
// set, and the bytes that set's own hash works out of.
#define HMAC_OFF_INNER 0u
#define HMAC_OFF_OKEY (HMAC_OFF_INNER + PROTOCORE_SHA256_BORROW)
#define HMAC_OFF_WORK (HMAC_OFF_OKEY + PROTOCORE_SHA256_BLOCK_LEN)
#define HMAC_OFF_HASH (HMAC_OFF_WORK + sizeof(HmacWork))
static_assert(HMAC_OFF_HASH + PROTOCORE_SHA256_BORROW <= PROTOCORE_HMAC_SHA256_BORROW,
              "PROTOCORE_HMAC_SHA256_BORROW is short of the split - raise it in protocore_config.h, which "
              "sums it into the secure arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(HMAC_OFF_WORK % _Alignof(HmacWork) == 0,
              "HMAC_OFF_WORK is not a multiple of alignof(HmacWork) - HMAC_WORK() would return a misaligned "
              "pointer; pad the region ahead of it");

// The regions, at their offsets in the caller's borrow.
#define HMAC_INNER(w) ((w) + HMAC_OFF_INNER)
#define HMAC_OKEY(w) ((w) + HMAC_OFF_OKEY)
#define HMAC_WORK(w) ((HmacWork *)(void *)((w) + HMAC_OFF_WORK))
#define HMAC_HASH(w) ((w) + HMAC_OFF_HASH)

// Build one 64-byte HMAC key block into @p block (RFC 2104 sec 2), using @p scratch (64 bytes) to hold the
// zero-padded / pre-hashed key. Both @p block and @p scratch are pool-resident, never the stack.
static void build_key_block(const uint8_t *key, size_t key_len, uint8_t block[64], uint8_t pad_byte,
                            uint8_t scratch[64], uint8_t *hash_work)
{
    mem.set(scratch, 0, 64);
    if (key_len > 64)
    {
        // Keys longer than the block become their SHA-256 hash.
        Sha256V.hash_args.data = key;
        Sha256V.hash_args.len = key_len;
        Sha256V.hash_args.out = scratch;
        Sha256.hash(hash_work);
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

void protocore_hmac_sha256_init(uint8_t *restrict work)
{
    HmacWork *w = HMAC_WORK(work);
    // ipad -> scratch (opad slot holds the padded key), opad -> the slot final reads it back from
    build_key_block(HmacSha256V.key_args.key, HmacSha256V.key_args.key_len, w->ipad, 0x36u, w->opad, HMAC_HASH(work));
    build_key_block(HmacSha256V.key_args.key, HmacSha256V.key_args.key_len, HMAC_OKEY(work), 0x5cu, w->opad,
                    HMAC_HASH(work));

    Sha256.init(HMAC_INNER(work));
    Sha256V.update_args.data = w->ipad;
    Sha256V.update_args.len = PROTOCORE_SHA256_BLOCK_LEN;
    Sha256.update(HMAC_INNER(work));
    HmacSha256V.ok = PROTO_TRUE;
}

void protocore_hmac_sha256_update(uint8_t *restrict work)
{
    Sha256V.update_args.data = HmacSha256V.update_args.data;
    Sha256V.update_args.len = HmacSha256V.update_args.len;
    Sha256.update(HMAC_INNER(work));
    HmacSha256V.ok = PROTO_TRUE;
}

void protocore_hmac_sha256_final(uint8_t *restrict work)
{
    if (!HmacSha256V.final_args.out)
    {
        HmacSha256V.ok = PROTO_FALSE;
        return;
    }
    HmacWork *w = HMAC_WORK(work);
    Sha256V.final_args.out = w->inner_digest;
    Sha256.final(HMAC_INNER(work));

    // Outer hash: H(okey || inner_digest)
    Sha256.init(HMAC_HASH(work));
    Sha256V.update_args.data = HMAC_OKEY(work);
    Sha256V.update_args.len = PROTOCORE_SHA256_BLOCK_LEN;
    Sha256.update(HMAC_HASH(work));
    Sha256V.update_args.data = w->inner_digest;
    Sha256V.update_args.len = PROTOCORE_SHA256_DIGEST_LEN;
    Sha256.update(HMAC_HASH(work));
    Sha256V.final_args.out = HmacSha256V.final_args.out;
    Sha256.final(HMAC_HASH(work));
    HmacSha256V.ok = PROTO_TRUE;
}

void protocore_hmac_sha256_mac(uint8_t *restrict work)
{
    HmacSha256V.ok = PROTO_FALSE;
    if (!HmacSha256V.mac_args.out)
    {
        return;
    }
    // Self-contained: ipad block first, fold it into the inner hash, then reuse its slot as the opad
    // key-padding scratch - so no key block ever lands on the stack.
    const uint8_t *key = HmacSha256V.mac_args.key;
    const size_t key_len = HmacSha256V.mac_args.key_len;
    HmacWork *w = HMAC_WORK(work);
    uint8_t *hw = HMAC_HASH(work);
    build_key_block(key, key_len, w->ipad, 0x36u, w->opad, hw); // ipad block (opad slot as key-pad scratch)
    Sha256.init(hw);
    Sha256V.update_args.data = w->ipad;
    Sha256V.update_args.len = PROTOCORE_SHA256_BLOCK_LEN;
    Sha256.update(hw);
    Sha256V.update_args.data = HmacSha256V.mac_args.data;
    Sha256V.update_args.len = HmacSha256V.mac_args.len;
    Sha256.update(hw);
    Sha256V.final_args.out = w->inner_digest;
    Sha256.final(hw); // inner = H((K XOR ipad) || m)

    build_key_block(key, key_len, w->opad, 0x5cu, w->ipad, hw); // opad block (ipad slot now free as scratch)
    Sha256.init(hw);
    Sha256V.update_args.data = w->opad;
    Sha256V.update_args.len = PROTOCORE_SHA256_BLOCK_LEN;
    Sha256.update(hw);
    Sha256V.update_args.data = w->inner_digest;
    Sha256V.update_args.len = PROTOCORE_SHA256_DIGEST_LEN;
    Sha256.update(hw);
    Sha256V.final_args.out = HmacSha256V.mac_args.out;
    Sha256.final(hw); // HMAC = H((K XOR opad) || inner)
    HmacSha256V.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
HmacSha256Vars HmacSha256V;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HMAC_SHA256
