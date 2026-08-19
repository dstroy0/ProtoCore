// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_kexhash.c
 * @brief The SSH key-exchange digest (see ssh_kexhash.h).
 *
 * One context and one set of entries; the negotiated method picks which hash namespace each entry
 * drives, and nothing else differs between the two.
 *
 * The context is this file's. The module's own borrow is split by offset into the region the bound
 * hash runs in and the one octet naming which hash that is.
 */

#include "protocore_config.h" // the entry point: the widths

#include "crypto/hash/sha256/sha256.h"
#include "crypto/hash/sha512/sha512.h"
#include "network_drivers/presentation/ssh/transport/ssh_kexhash/ssh_kexhash.h"

PROTOCORE_BEGIN_DECLS

// The one definition of SshKexHashCtx - private to this TU. It sits at KEXHASH_OFF_CTX in the
// caller's borrow, so its size never leaves this file and no consumer can name it.
//
// Only what is not derivable: which hash the negotiated method bound this digest to. The regions live
// at fixed offsets in the caller's borrow, so a macro computes them from the pointer rather than the
// context storing them.
typedef struct
{
    proto_bool is512; ///< which hash the last init bound
} SshKexHashCtx;

// The caller's borrow, split: the region the bound hash runs in, then the selection. SHA-512 is the
// wider of the two, so its borrow is the region and a SHA-256 digest leaves the tail unused. The hash
// takes the front so its own alignment is the borrow's.
#define KEXHASH_OFF_HASH 0u
#define KEXHASH_OFF_CTX (KEXHASH_OFF_HASH + PROTOCORE_SHA512_BORROW)
static_assert(KEXHASH_OFF_CTX + sizeof(SshKexHashCtx) <= PROTOCORE_SSH_KEXHASH_BORROW,
              "PROTOCORE_SSH_KEXHASH_BORROW is short of the SHA-512 region and the selection - raise it "
              "in protocore_config.h, which derives PROTOCORE_SECURE_ARENA_SIZE from it");
static_assert(PROTOCORE_SHA256_BORROW <= PROTOCORE_SHA512_BORROW,
              "the SHA-256 arm runs in the SHA-512 region, so it must be the narrower of the two");

// The regions, at their offsets in the caller's borrow.
#define KEXHASH_HASH(w) ((w) + KEXHASH_OFF_HASH)
#define KEXHASH_CTX(w) ((SshKexHashCtx *)(void *)((w) + KEXHASH_OFF_CTX))

// --- the entries -----------------------------------------------------------

static void kexhash_init(uint8_t *restrict work)
{
    SshKexHash.ok = PROTO_FALSE;
    SshKexHash.len = 0;
    if (!work)
    {
        return;
    }
    SshKexHashCtx *ctx = KEXHASH_CTX(work);
    ctx->is512 = SshKexHash.init_args.is512;
    if (ctx->is512)
    {
        Sha512.init(KEXHASH_HASH(work));
        SshKexHash.len = PROTOCORE_SHA512_DIGEST_LEN;
    }
    else
    {
        Sha256.init(KEXHASH_HASH(work));
        SshKexHash.len = PROTOCORE_SHA256_DIGEST_LEN;
    }
    SshKexHash.ok = PROTO_TRUE;
}

static void kexhash_update(uint8_t *restrict work)
{
    SshKexHash.ok = PROTO_FALSE;
    if (!work)
    {
        return;
    }
    SshKexHashCtx *ctx = KEXHASH_CTX(work);
    if (ctx->is512)
    {
        Sha512.update_args.data = SshKexHash.update_args.data;
        Sha512.update_args.len = SshKexHash.update_args.len;
        Sha512.update(KEXHASH_HASH(work));
    }
    else
    {
        Sha256.update_args.data = SshKexHash.update_args.data;
        Sha256.update_args.len = SshKexHash.update_args.len;
        Sha256.update(KEXHASH_HASH(work));
    }
    SshKexHash.ok = PROTO_TRUE;
}

static void kexhash_final(uint8_t *restrict work)
{
    SshKexHash.ok = PROTO_FALSE;
    if (!work || !SshKexHash.final_args.out)
    {
        return;
    }
    SshKexHashCtx *ctx = KEXHASH_CTX(work);
    if (ctx->is512)
    {
        Sha512.final_args.out = SshKexHash.final_args.out;
        Sha512.final(KEXHASH_HASH(work));
        SshKexHash.len = PROTOCORE_SHA512_DIGEST_LEN;
    }
    else
    {
        Sha256.final_args.out = SshKexHash.final_args.out;
        Sha256.final(KEXHASH_HASH(work));
        SshKexHash.len = PROTOCORE_SHA256_DIGEST_LEN;
    }
    SshKexHash.ok = PROTO_TRUE;
}

SshKexHashNs SshKexHash = {.init = kexhash_init, .update = kexhash_update, .final = kexhash_final};

PROTOCORE_END_DECLS
