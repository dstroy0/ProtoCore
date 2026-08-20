// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hmac_sha384.c
 * @brief HMAC-SHA2-384 implementation (RFC 2104).
 *
 * Implemented in terms of the @ref Sha384Ns entries, so which arm compresses the inner hash is not
 * visible here and this file is the same on every target.
 *
 * RFC 2104 construction: HMAC(K, m) = H((K XOR opad) || H((K XOR ipad) || m)), H = SHA-384, ipad = 0x36
 * repeated, opad = 0x5c repeated. The block is SHA-384's 128 octets, not its 48-octet digest, so keys
 * > 128 bytes are pre-hashed and keys <= 128 are zero-padded to 128. TLS 1.3 keys a Finished MAC with
 * a 48-byte secret, so it is padded, not pre-hashed.
 *
 * Nothing here owns storage or touches the pool. The caller hands over PROTOCORE_HMAC_SHA384_BORROW
 * bytes and this file splits them by offset: the inner hash's own bytes, the outer key block, the
 * transient set, and the bytes that set's own hash works out of. A connection takes those bytes once
 * for its slot and reuses them for every record, so a MAC on the record path costs no borrow and no
 * wipe.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HMAC_SHA384

#include "crypto/crypto_opt.h"
#include "crypto/hash/sha384/sha384.h" // the Sha384 entries the inner and outer hashes run through
#include "crypto/mac/hmac_sha384/hmac_sha384.h"
#include "mmgr/protomem/protomem.h"

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// The transient half of the caller's bytes: live inside init and inside final, dead between them. The
// two 128-byte key blocks double as key-padding scratch for build_key_block.
typedef struct
{
    uint8_t opad[PROTOCORE_SHA384_BLOCK_LEN];          ///< one-shot opad block (inner->outer); else key-pad scratch
    uint8_t ipad[PROTOCORE_SHA384_BLOCK_LEN];          ///< ipad block; also key-pad scratch once its ipad is consumed
    uint8_t inner_digest[PROTOCORE_SHA384_DIGEST_LEN]; ///< H((K XOR ipad) || m)
} Hmac384Work;

// The module's own borrow, split by offset: the inner hash's bytes, the outer key block, the transient
// set, and the bytes that set's own hash works out of.
#define HMAC384_OFF_INNER 0u
#define HMAC384_OFF_OKEY (HMAC384_OFF_INNER + PROTOCORE_SHA384_BORROW)
#define HMAC384_OFF_WORK (HMAC384_OFF_OKEY + PROTOCORE_SHA384_BLOCK_LEN)
#define HMAC384_OFF_HASH (HMAC384_OFF_WORK + sizeof(Hmac384Work))
static_assert(HMAC384_OFF_HASH + PROTOCORE_SHA384_BORROW <= PROTOCORE_HMAC_SHA384_BORROW,
              "PROTOCORE_HMAC_SHA384_BORROW is short of the split - raise it in protocore_config.h, which "
              "sums it into the secure arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(HMAC384_OFF_WORK % _Alignof(Hmac384Work) == 0,
              "HMAC384_OFF_WORK is not a multiple of alignof(Hmac384Work) - HMAC384_WORK() would return a misaligned "
              "pointer; pad the region ahead of it");

// The regions, at their offsets in the caller's borrow.
#define HMAC384_INNER(w) ((w) + HMAC384_OFF_INNER)
#define HMAC384_OKEY(w) ((w) + HMAC384_OFF_OKEY)
#define HMAC384_WORK(w) ((Hmac384Work *)(void *)((w) + HMAC384_OFF_WORK))
#define HMAC384_HASH(w) ((w) + HMAC384_OFF_HASH)

// One 128-byte HMAC key block into @p block (RFC 2104 sec 2): keys > 128 bytes are pre-hashed, else
// zero-padded, using @p kpad (128 bytes) to hold the padded key and @p hw for the pre-hash. Both
// @p block and @p kpad are pool-resident, never the stack.
static void build_key_block(const uint8_t *key, size_t key_len, uint8_t block[PROTOCORE_SHA384_BLOCK_LEN],
                            uint8_t pad_byte, uint8_t kpad[PROTOCORE_SHA384_BLOCK_LEN], uint8_t *hw)
{
    mem.set(kpad, 0, PROTOCORE_SHA384_BLOCK_LEN);
    if (key_len > PROTOCORE_SHA384_BLOCK_LEN)
    {
        // Keys longer than the block become their SHA-384 hash: 48 bytes, the remaining 80 stay zero.
        Sha384V.hash_args.data = key;
        Sha384V.hash_args.len = key_len;
        Sha384V.hash_args.out = kpad;
        Sha384.hash(hw);
    }
    else
    {
        mem.cpy(kpad, key, key_len);
    }
    for (int i = 0; i < PROTOCORE_SHA384_BLOCK_LEN; i++)
    {
        block[i] = (uint8_t)(kpad[i] ^ pad_byte);
    }
}

// --- the entries -----------------------------------------------------------

static void hmac_init(uint8_t *restrict work)
{
    Hmac384Work *w = HMAC384_WORK(work);
    // ipad -> scratch (opad slot holds the padded key), opad -> the slot final reads it back from
    build_key_block(HmacSha384.key_args.key, HmacSha384.key_args.key_len, w->ipad, 0x36u, w->opad, HMAC384_HASH(work));
    build_key_block(HmacSha384.key_args.key, HmacSha384.key_args.key_len, HMAC384_OKEY(work), 0x5cu, w->opad,
                    HMAC384_HASH(work));

    Sha384.init(HMAC384_INNER(work));
    Sha384V.update_args.data = w->ipad;
    Sha384V.update_args.len = PROTOCORE_SHA384_BLOCK_LEN;
    Sha384.update(HMAC384_INNER(work));
    HmacSha384.ok = PROTO_TRUE;
}

static void hmac_update(uint8_t *restrict work)
{
    Sha384V.update_args.data = HmacSha384.update_args.data;
    Sha384V.update_args.len = HmacSha384.update_args.len;
    Sha384.update(HMAC384_INNER(work));
    HmacSha384.ok = PROTO_TRUE;
}

static void hmac_final(uint8_t *restrict work)
{
    if (!HmacSha384.final_args.out)
    {
        HmacSha384.ok = PROTO_FALSE;
        return;
    }
    Hmac384Work *w = HMAC384_WORK(work);
    Sha384V.final_args.out = w->inner_digest;
    Sha384.final(HMAC384_INNER(work));

    // Outer hash: H(okey || inner_digest)
    Sha384.init(HMAC384_HASH(work));
    Sha384V.update_args.data = HMAC384_OKEY(work);
    Sha384V.update_args.len = PROTOCORE_SHA384_BLOCK_LEN;
    Sha384.update(HMAC384_HASH(work));
    Sha384V.update_args.data = w->inner_digest;
    Sha384V.update_args.len = PROTOCORE_SHA384_DIGEST_LEN;
    Sha384.update(HMAC384_HASH(work));
    Sha384V.final_args.out = HmacSha384.final_args.out;
    Sha384.final(HMAC384_HASH(work));
    HmacSha384.ok = PROTO_TRUE;
}

static void hmac_mac(uint8_t *restrict work)
{
    HmacSha384.ok = PROTO_FALSE;
    if (!HmacSha384.mac_args.out)
    {
        return;
    }
    // Self-contained: ipad block first, fold it into the inner hash, then reuse its slot as the opad
    // key-padding scratch - so no key block ever lands on the stack.
    const uint8_t *key = HmacSha384.mac_args.key;
    const size_t key_len = HmacSha384.mac_args.key_len;
    Hmac384Work *w = HMAC384_WORK(work);
    uint8_t *hw = HMAC384_HASH(work);
    build_key_block(key, key_len, w->ipad, 0x36u, w->opad, hw); // ipad block (opad slot as key-pad scratch)
    Sha384.init(hw);
    Sha384V.update_args.data = w->ipad;
    Sha384V.update_args.len = PROTOCORE_SHA384_BLOCK_LEN;
    Sha384.update(hw);
    Sha384V.update_args.data = HmacSha384.mac_args.data;
    Sha384V.update_args.len = HmacSha384.mac_args.len;
    Sha384.update(hw);
    Sha384V.final_args.out = w->inner_digest;
    Sha384.final(hw); // inner = H((K XOR ipad) || m)

    build_key_block(key, key_len, w->opad, 0x5cu, w->ipad, hw); // opad block (ipad slot now free as scratch)
    Sha384.init(hw);
    Sha384V.update_args.data = w->opad;
    Sha384V.update_args.len = PROTOCORE_SHA384_BLOCK_LEN;
    Sha384.update(hw);
    Sha384V.update_args.data = w->inner_digest;
    Sha384V.update_args.len = PROTOCORE_SHA384_DIGEST_LEN;
    Sha384.update(hw);
    Sha384V.final_args.out = HmacSha384.mac_args.out;
    Sha384.final(hw); // HMAC = H((K XOR opad) || inner)
    HmacSha384.ok = PROTO_TRUE;
}

HmacSha384Ns HmacSha384 = {.init = hmac_init, .update = hmac_update, .final = hmac_final, .mac = hmac_mac};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HMAC_SHA384
