// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file southbound.c
 * @brief Southbound protocol-driver framework registry + dispatch (see southbound.h).
 */

#include "services/southbound/southbound.h"
#include "mmgr/protostr.h" // str.eq: the driver registry name lookup

#if PROTOCORE_ENABLE_SOUTHBOUND

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

/**
 * @brief The registry and the calls that reach it - what SouthboundNs points at.
 *
 * @var SouthboundInternal::store  the driver table and its count
 * @var SouthboundInternal::ns     the handle a caller sets a call's members on
 */
struct SouthboundInternal
{
    struct SouthboundStorage *store;
    SouthboundNs *ns;
};

static struct SouthboundStorage s_store;

static struct SouthboundInternal s_sb = {.store = &s_store, .ns = &Southbound};

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
static void add(struct SouthboundInternal *restrict ctx)
{
    const SouthboundDriver *drv = ctx->ns->drv;
    if (!drv || !drv->name)
    {
        ctx->ns->i32 = SB_ERR_ARG;
        return;
    }
    if (lookup(ctx->store, drv->name))
    {
        ctx->ns->i32 = SB_ERR_DUP;
        return;
    }
    if (ctx->store->count >= PROTOCORE_SOUTHBOUND_MAX_DRIVERS)
    {
        ctx->ns->i32 = SB_ERR_FULL;
        return;
    }
    ctx->store->drivers[ctx->store->count++] = drv;
    ctx->ns->i32 = SB_OK;
}

// Empty the table and its count together.
static void clear(struct SouthboundInternal *restrict ctx)
{
    for (size_t i = 0; i < PROTOCORE_SOUTHBOUND_MAX_DRIVERS; i++)
    {
        ctx->store->drivers[i] = NULL;
    }
    ctx->store->count = 0;
}

static void count(struct SouthboundInternal *restrict ctx)
{
    ctx->ns->n = ctx->store->count;
}

static void find(struct SouthboundInternal *restrict ctx)
{
    ctx->ns->driver = lookup(ctx->store, ctx->ns->name);
}

// Read one point through the named driver's read callback.
static void read(struct SouthboundInternal *restrict ctx)
{
    if (!ctx->ns->point.value_out)
    {
        ctx->ns->i32 = SB_ERR_ARG;
        return;
    }
    const SouthboundDriver *d = lookup(ctx->store, ctx->ns->name);
    if (!d)
    {
        ctx->ns->i32 = SB_ERR_NOT_FOUND;
        return;
    }
    if (!d->read)
    {
        ctx->ns->i32 = SB_ERR_UNSUPPORTED;
        return;
    }
    ctx->ns->i32 = d->read(d->ctx, ctx->ns->point.point, ctx->ns->point.value_out);
}

// Write one point through the named driver's write callback.
static void write(struct SouthboundInternal *restrict ctx)
{
    const SouthboundDriver *d = lookup(ctx->store, ctx->ns->name);
    if (!d)
    {
        ctx->ns->i32 = SB_ERR_NOT_FOUND;
        return;
    }
    if (!d->write)
    {
        ctx->ns->i32 = SB_ERR_UNSUPPORTED;
        return;
    }
    ctx->ns->i32 = d->write(d->ctx, ctx->ns->point.point, ctx->ns->point.value);
}

// Read a contiguous span of points in one driver call.
static void read_block(struct SouthboundInternal *restrict ctx)
{
    if (!ctx->ns->block.out || ctx->ns->block.n == 0)
    {
        ctx->ns->i32 = SB_ERR_ARG;
        return;
    }
    const SouthboundDriver *d = lookup(ctx->store, ctx->ns->name);
    if (!d)
    {
        ctx->ns->i32 = SB_ERR_NOT_FOUND;
        return;
    }
    if (!d->read_block)
    {
        ctx->ns->i32 = SB_ERR_UNSUPPORTED;
        return;
    }
    ctx->ns->i32 = d->read_block(d->ctx, ctx->ns->block.first, ctx->ns->block.out, ctx->ns->block.n);
}

// Write a contiguous span of points in one driver call.
static void write_block(struct SouthboundInternal *restrict ctx)
{
    if (!ctx->ns->block.in || ctx->ns->block.n == 0)
    {
        ctx->ns->i32 = SB_ERR_ARG;
        return;
    }
    const SouthboundDriver *d = lookup(ctx->store, ctx->ns->name);
    if (!d)
    {
        ctx->ns->i32 = SB_ERR_NOT_FOUND;
        return;
    }
    if (!d->write_block)
    {
        ctx->ns->i32 = SB_ERR_UNSUPPORTED;
        return;
    }
    ctx->ns->i32 = d->write_block(d->ctx, ctx->ns->block.first, ctx->ns->block.in, ctx->ns->block.n);
}

// Designated, so a member's position in the struct does not decide what it binds to.
SouthboundNs Southbound = {.add = add,
                           .clear = clear,
                           .count = count,
                           .find = find,
                           .read = read,
                           .write = write,
                           .read_block = read_block,
                           .write_block = write_block,
                           .internal = &s_sb};

#endif // PROTOCORE_ENABLE_SOUTHBOUND
