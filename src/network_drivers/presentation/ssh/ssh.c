// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh.c
 * @brief Every byte the connections use, one span per slot.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SSH

#include "mmgr/secure/secure.h" // the persistent end this module's key material is taken from
#include "network_drivers/presentation/ssh/common.h"
#include "network_drivers/presentation/ssh/ssh.h"

// The connections' storage, owned by one instance (internal linkage). Reached only through
// ssh_conn_slot(), at the offsets common.h names.
typedef struct
{
    uint8_t mem[MAX_SSH_CONNS][SSH_SLOT_BORROW];
} SshMemCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define SSH_OFF_CTX 0u
static_assert(SSH_OFF_CTX + sizeof(SshMemCtx) <= PROTOCORE_SSH_BORROW,
              "PROTOCORE_SSH_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(SSH_OFF_CTX % _Alignof(SshMemCtx) == 0,
              "SSH_OFF_CTX is not a multiple of alignof(SshMemCtx) - SSH_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define SSH_CTX(w) ((SshMemCtx *)(void *)((w) + SSH_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_SSH_BORROW persistent bytes
} SshOwnCtx;
static SshOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_ssh_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_SSH_BORROW).buf;
    }
    return s_own.span;
}

void protocore_ssh_conn_slot(uint8_t *restrict work)
{
    uint8_t i = SshV.conn_slot_args.i;

    if (i >= MAX_SSH_CONNS)
    {
        SshV.ptr = NULL;
        return;
    }
    SshV.ptr = SSH_CTX(work)->mem[i];
}
/** @brief The operands and the outcome. */
SshVars SshV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSH
