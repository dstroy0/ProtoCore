// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hmac_sha512.c
 * @brief HMAC-SHA2-512 implementation (RFC 2104).
 *
 * Implemented in terms of the @ref Sha512Ns entries, so which arm compresses the inner hash is not
 * visible here and this file is the same on every target.
 *
 * RFC 2104 construction: HMAC(K, m) = H((K XOR opad) || H((K XOR ipad) || m)), H = SHA-512, ipad = 0x36
 * repeated, opad = 0x5c repeated. Keys > 128 bytes are pre-hashed; keys <= 128 are zero-padded to the
 * 128-byte block. SSH-derived MAC keys are 64 bytes, so they are padded, not pre-hashed.
 *
 * Nothing here owns storage or touches the pool. The caller hands over PROTOCORE_HMAC_SHA512_BORROW
 * bytes and this file splits them by offset: the inner hash's own bytes, the outer key block, the
 * transient set, and the bytes that set's own hash works out of. A connection takes those bytes once
 * for its slot and reuses them for every packet, so a MAC on the packet path costs no borrow and no
 * wipe.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HMAC_SHA512

#include "crypto/crypto_opt.h"
#include "crypto/hash/sha512/sha512.h" // the Sha512 entries the inner and outer hashes run through
#include "crypto/mac/hmac_sha512/hmac_sha512.h"
#include "mmgr/protomem/protomem.h"

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// The transient half of the caller's bytes: live inside init and inside final, dead between them. The
// two 128-byte key blocks double as key-padding scratch for build_key_block.
typedef struct
{
    uint8_t opad[PROTOCORE_SHA512_BLOCK_LEN];          ///< one-shot opad block (inner->outer); else key-pad scratch
    uint8_t ipad[PROTOCORE_SHA512_BLOCK_LEN];          ///< ipad block; also key-pad scratch once its ipad is consumed
    uint8_t inner_digest[PROTOCORE_SHA512_DIGEST_LEN]; ///< H((K XOR ipad) || m)
} Hmac512Work;

// The module's own borrow, split by offset: the inner hash's bytes, the outer key block, the transient
// set, and the bytes that set's own hash works out of.
#define HMAC512_OFF_INNER 0u
#define HMAC512_OFF_OKEY (HMAC512_OFF_INNER + PROTOCORE_SHA512_BORROW)
#define HMAC512_OFF_WORK (HMAC512_OFF_OKEY + PROTOCORE_SHA512_BLOCK_LEN)
#define HMAC512_OFF_HASH (HMAC512_OFF_WORK + sizeof(Hmac512Work))
static_assert(HMAC512_OFF_HASH + PROTOCORE_SHA512_BORROW <= PROTOCORE_HMAC_SHA512_BORROW,
              "PROTOCORE_HMAC_SHA512_BORROW is short of the split - raise it in protocore_config.h, which "
              "sums it into the secure arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(HMAC512_OFF_WORK % _Alignof(Hmac512Work) == 0,
              "HMAC512_OFF_WORK is not a multiple of alignof(Hmac512Work) - HMAC512_WORK() would return a misaligned "
              "pointer; pad the region ahead of it");

// The regions, at their offsets in the caller's borrow.
#define HMAC512_INNER(w) ((w) + HMAC512_OFF_INNER)
#define HMAC512_OKEY(w) ((w) + HMAC512_OFF_OKEY)
#define HMAC512_WORK(w) ((Hmac512Work *)(void *)((w) + HMAC512_OFF_WORK))
#define HMAC512_HASH(w) ((w) + HMAC512_OFF_HASH)

// One 128-byte HMAC key block into @p block (RFC 2104 sec 2): keys > 128 bytes are pre-hashed, else
// zero-padded, using @p kpad (128 bytes) to hold the padded key and @p hw for the pre-hash. Both
// @p block and @p kpad are pool-resident, never the stack.
static void build_key_block(const uint8_t *key, size_t key_len, uint8_t block[PROTOCORE_SHA512_BLOCK_LEN],
                            uint8_t pad_byte, uint8_t kpad[PROTOCORE_SHA512_BLOCK_LEN], uint8_t *hw)
{
    mem.set(kpad, 0, PROTOCORE_SHA512_BLOCK_LEN);
    if (key_len > PROTOCORE_SHA512_BLOCK_LEN)
    {
        // Keys longer than the block become their SHA-512 hash: 64 bytes, the remaining 64 stay zero.
        Sha512V.hash_args.data = key;
        Sha512V.hash_args.len = key_len;
        Sha512V.hash_args.out = kpad;
        Sha512.hash(hw);
    }
    else
    {
        mem.cpy(kpad, key, key_len);
    }
    for (int i = 0; i < PROTOCORE_SHA512_BLOCK_LEN; i++)
    {
        block[i] = (uint8_t)(kpad[i] ^ pad_byte);
    }
}

// --- the entries -----------------------------------------------------------

static void hmac_init(uint8_t *restrict work)
{
    Hmac512Work *w = HMAC512_WORK(work);
    // ipad -> scratch (opad slot holds the padded key), opad -> the slot final reads it back from
    build_key_block(HmacSha512V.key_args.key, HmacSha512V.key_args.key_len, w->ipad, 0x36u, w->opad,
                    HMAC512_HASH(work));
    build_key_block(HmacSha512V.key_args.key, HmacSha512V.key_args.key_len, HMAC512_OKEY(work), 0x5cu, w->opad,
                    HMAC512_HASH(work));

    Sha512.init(HMAC512_INNER(work));
    Sha512V.update_args.data = w->ipad;
    Sha512V.update_args.len = PROTOCORE_SHA512_BLOCK_LEN;
    Sha512.update(HMAC512_INNER(work));
    HmacSha512V.ok = PROTO_TRUE;
}

static void hmac_update(uint8_t *restrict work)
{
    Sha512V.update_args.data = HmacSha512V.update_args.data;
    Sha512V.update_args.len = HmacSha512V.update_args.len;
    Sha512.update(HMAC512_INNER(work));
    HmacSha512V.ok = PROTO_TRUE;
}

static void hmac_final(uint8_t *restrict work)
{
    if (!HmacSha512V.final_args.out)
    {
        HmacSha512V.ok = PROTO_FALSE;
        return;
    }
    Hmac512Work *w = HMAC512_WORK(work);
    Sha512V.final_args.out = w->inner_digest;
    Sha512.final(HMAC512_INNER(work));

    // Outer hash: H(okey || inner_digest)
    Sha512.init(HMAC512_HASH(work));
    Sha512V.update_args.data = HMAC512_OKEY(work);
    Sha512V.update_args.len = PROTOCORE_SHA512_BLOCK_LEN;
    Sha512.update(HMAC512_HASH(work));
    Sha512V.update_args.data = w->inner_digest;
    Sha512V.update_args.len = PROTOCORE_SHA512_DIGEST_LEN;
    Sha512.update(HMAC512_HASH(work));
    Sha512V.final_args.out = HmacSha512V.final_args.out;
    Sha512.final(HMAC512_HASH(work));
    HmacSha512V.ok = PROTO_TRUE;
}

static void hmac_mac(uint8_t *restrict work)
{
    HmacSha512V.ok = PROTO_FALSE;
    if (!HmacSha512V.mac_args.out)
    {
        return;
    }
    // Self-contained: ipad block first, fold it into the inner hash, then reuse its slot as the opad
    // key-padding scratch - so no key block ever lands on the stack.
    const uint8_t *key = HmacSha512V.mac_args.key;
    const size_t key_len = HmacSha512V.mac_args.key_len;
    Hmac512Work *w = HMAC512_WORK(work);
    uint8_t *hw = HMAC512_HASH(work);
    build_key_block(key, key_len, w->ipad, 0x36u, w->opad, hw); // ipad block (opad slot as key-pad scratch)
    Sha512.init(hw);
    Sha512V.update_args.data = w->ipad;
    Sha512V.update_args.len = PROTOCORE_SHA512_BLOCK_LEN;
    Sha512.update(hw);
    Sha512V.update_args.data = HmacSha512V.mac_args.data;
    Sha512V.update_args.len = HmacSha512V.mac_args.len;
    Sha512.update(hw);
    Sha512V.final_args.out = w->inner_digest;
    Sha512.final(hw); // inner = H((K XOR ipad) || m)

    build_key_block(key, key_len, w->opad, 0x5cu, w->ipad, hw); // opad block (ipad slot now free as scratch)
    Sha512.init(hw);
    Sha512V.update_args.data = w->opad;
    Sha512V.update_args.len = PROTOCORE_SHA512_BLOCK_LEN;
    Sha512.update(hw);
    Sha512V.update_args.data = w->inner_digest;
    Sha512V.update_args.len = PROTOCORE_SHA512_DIGEST_LEN;
    Sha512.update(hw);
    Sha512V.final_args.out = HmacSha512V.mac_args.out;
    Sha512.final(hw); // HMAC = H((K XOR opad) || inner)
    HmacSha512V.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
HmacSha512Vars HmacSha512V;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HMAC_SHA512
