// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file southbound.c
 * @brief Southbound protocol-driver framework registry + dispatch (see southbound.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SOUTHBOUND

#include "services/southbound/southbound/southbound.h"
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protostr/protostr.h"   // str.eq: the driver registry name lookup

#ifndef PROTOCORE_SOUTHBOUND_MAX_DRIVERS
#define PROTOCORE_SOUTHBOUND_MAX_DRIVERS 8 ///< bounded registry; no heap.
#endif

/**
 * @brief The whole registry: the bounded driver table and its count.
 *
 * All of it BSS, so a registration costs no heap.
 */
struct SouthboundStorage
{
    const SouthboundDriver *drivers[PROTOCORE_SOUTHBOUND_MAX_DRIVERS]; ///< the borrowed drivers, in registration order
    size_t count;                                                      ///< how many entries of @c drivers are live
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define SOUTHBOUND_OFF_CTX 0u
static_assert(SOUTHBOUND_OFF_CTX + sizeof(struct SouthboundStorage) <= PROTOCORE_SOUTHBOUND_BORROW,
              "PROTOCORE_SOUTHBOUND_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define SOUTHBOUND_CTX(w) ((struct SouthboundStorage *)(void *)((w) + SOUTHBOUND_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_SOUTHBOUND_BORROW persistent bytes
} SouthboundOwnCtx;
static SouthboundOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_southbound_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_SOUTHBOUND_BORROW).buf;
    }
    return s_own.span;
}

// The registered driver of that name, or null.
//
// Only the first && arm's false side is structurally unreachable: store->drivers[i] can never be
// null for i < count, because add() only ever stores a drv already proven non-null (its own !drv
// check) in the same statement that increments count, and clear() resets the whole array and count
// together - no public-API path leaves a live index holding a null pointer. The second arm
// (drivers[i]->name) is NOT similarly guaranteed: the registry stores a *borrowed* pointer, so a
// caller can null out a registered driver's name field after registration (see
// test_find_skips_driver_mutated_name_null) - that guard is live, tested defensive code, not dead
// code; the exclusion below covers only the first arm's genuinely dead branch.
static const SouthboundDriver *lookup(const struct SouthboundStorage *store, const char *name)
{
    if (!name)
    {
        return NULL;
    }
    for (size_t i = 0; i < store->count; i++)
    {
        if (store->drivers[i] && store->drivers[i]->name &&
            str.eq(store->drivers[i]->name, name, MAX_KEY_LEN, PROTO_FALSE))
        {
            return store->drivers[i];
        }
    }
    return NULL;
}

// Append a borrowed driver to the table.
void protocore_southbound_add(uint8_t *restrict work)
{
    const SouthboundDriver *drv = SouthboundV.drv;
    if (!drv || !drv->name)
    {
        SouthboundV.i32 = SB_ERR_ARG;
        return;
    }
    if (lookup(SOUTHBOUND_CTX(work), drv->name))
    {
        SouthboundV.i32 = SB_ERR_DUP;
        return;
    }
    if (SOUTHBOUND_CTX(work)->count >= PROTOCORE_SOUTHBOUND_MAX_DRIVERS)
    {
        SouthboundV.i32 = SB_ERR_FULL;
        return;
    }
    SOUTHBOUND_CTX(work)->drivers[SOUTHBOUND_CTX(work)->count++] = drv;
    SouthboundV.i32 = SB_OK;
}

// Empty the table and its count together.
void protocore_southbound_clear(uint8_t *restrict work)
{
    for (size_t i = 0; i < PROTOCORE_SOUTHBOUND_MAX_DRIVERS; i++)
    {
        SOUTHBOUND_CTX(work)->drivers[i] = NULL;
    }
    SOUTHBOUND_CTX(work)->count = 0;
}

void protocore_southbound_count(uint8_t *restrict work)
{
    SouthboundV.n = SOUTHBOUND_CTX(work)->count;
}

void protocore_southbound_find(uint8_t *restrict work)
{
    SouthboundV.driver = lookup(SOUTHBOUND_CTX(work), SouthboundV.name);
}

// Read one point through the named driver's read callback.
void protocore_southbound_read(uint8_t *restrict work)
{
    if (!SouthboundV.point.value_out)
    {
        SouthboundV.i32 = SB_ERR_ARG;
        return;
    }
    const SouthboundDriver *d = lookup(SOUTHBOUND_CTX(work), SouthboundV.name);
    if (!d)
    {
        SouthboundV.i32 = SB_ERR_NOT_FOUND;
        return;
    }
    if (!d->read)
    {
        SouthboundV.i32 = SB_ERR_UNSUPPORTED;
        return;
    }
    SouthboundV.i32 = d->read(d->ctx, SouthboundV.point.point, SouthboundV.point.value_out);
}

// Write one point through the named driver's write callback.
void protocore_southbound_write(uint8_t *restrict work)
{
    const SouthboundDriver *d = lookup(SOUTHBOUND_CTX(work), SouthboundV.name);
    if (!d)
    {
        SouthboundV.i32 = SB_ERR_NOT_FOUND;
        return;
    }
    if (!d->write)
    {
        SouthboundV.i32 = SB_ERR_UNSUPPORTED;
        return;
    }
    SouthboundV.i32 = d->write(d->ctx, SouthboundV.point.point, SouthboundV.point.value);
}

// Read a contiguous span of points in one driver call.
void protocore_southbound_read_block(uint8_t *restrict work)
{
    if (!SouthboundV.block.out || SouthboundV.block.n == 0)
    {
        SouthboundV.i32 = SB_ERR_ARG;
        return;
    }
    const SouthboundDriver *d = lookup(SOUTHBOUND_CTX(work), SouthboundV.name);
    if (!d)
    {
        SouthboundV.i32 = SB_ERR_NOT_FOUND;
        return;
    }
    if (!d->read_block)
    {
        SouthboundV.i32 = SB_ERR_UNSUPPORTED;
        return;
    }
    SouthboundV.i32 = d->read_block(d->ctx, SouthboundV.block.first, SouthboundV.block.out, SouthboundV.block.n);
}

// Write a contiguous span of points in one driver call.
void protocore_southbound_write_block(uint8_t *restrict work)
{
    if (!SouthboundV.block.in || SouthboundV.block.n == 0)
    {
        SouthboundV.i32 = SB_ERR_ARG;
        return;
    }
    const SouthboundDriver *d = lookup(SOUTHBOUND_CTX(work), SouthboundV.name);
    if (!d)
    {
        SouthboundV.i32 = SB_ERR_NOT_FOUND;
        return;
    }
    if (!d->write_block)
    {
        SouthboundV.i32 = SB_ERR_UNSUPPORTED;
        return;
    }
    SouthboundV.i32 = d->write_block(d->ctx, SouthboundV.block.first, SouthboundV.block.in, SouthboundV.block.n);
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
SouthboundVars SouthboundV;

#endif // PROTOCORE_ENABLE_SOUTHBOUND
