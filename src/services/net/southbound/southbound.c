// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file southbound.c
 * @brief Southbound protocol-driver framework registry + dispatch (see southbound.h).
 */

#include "services/net/southbound/southbound.h"

#if PC_ENABLE_SOUTHBOUND

#include <string.h>

#ifndef PC_SOUTHBOUND_MAX_DRIVERS
#define PC_SOUTHBOUND_MAX_DRIVERS 8 ///< bounded registry; no heap.
#endif

// All southbound-registry state, owned by one instance (internal linkage): the bounded
// driver table and its count, grouped so it is one named owner, unreachable cross-TU.
typedef struct
{
    const SouthboundDriver *drivers[PC_SOUTHBOUND_MAX_DRIVERS];
    size_t count;
} SouthboundCtx;
static SouthboundCtx s_sb;

int pc_southbound_register(const SouthboundDriver *drv)
{
    if (!drv || !drv->name)
    {
        return SB_ERR_ARG;
    }
    if (pc_southbound_find(drv->name))
    {
        return SB_ERR_DUP;
    }
    if (s_sb.count >= PC_SOUTHBOUND_MAX_DRIVERS)
    {
        return SB_ERR_FULL;
    }
    s_sb.drivers[s_sb.count++] = drv;
    return SB_OK;
}

void pc_southbound_clear(void)
{
    for (size_t i = 0; i < PC_SOUTHBOUND_MAX_DRIVERS; i++)
    {
        s_sb.drivers[i] = NULL;
    }
    s_sb.count = 0;
}

size_t pc_southbound_count(void)
{
    return s_sb.count;
}

const SouthboundDriver *pc_southbound_find(const char *name)
{
    if (!name)
    {
        return NULL;
    }
    // Only the first && arm's false side is structurally unreachable: s_sb.drivers[i] can never be
    // null for i < count, because register() only ever stores a drv already proven non-null (its own
    // !drv check above) in the same statement that increments count, and clear() resets the whole
    // array and count together - no public-API path leaves a live index holding a null pointer. The
    // second arm (drivers[i]->name) is NOT similarly guaranteed: the registry stores a *borrowed*
    // pointer, so a caller can null out a registered driver's name field after registration (see
    // test_find_skips_driver_mutated_name_null) - that guard is live, tested defensive code, not dead
    // code; the exclusion below covers only the first arm's genuinely dead branch.
    for (size_t i = 0; i < s_sb.count; i++)
    {
        if (s_sb.drivers[i] && s_sb.drivers[i]->name && strcmp(s_sb.drivers[i]->name, name) == 0) // GCOVR_EXCL_BR_LINE
        {
            return s_sb.drivers[i];
        }
    }
    return NULL;
}

int pc_southbound_read(const char *name, uint32_t point, int32_t *value_out)
{
    if (!value_out)
    {
        return SB_ERR_ARG;
    }
    const SouthboundDriver *d = pc_southbound_find(name);
    if (!d)
    {
        return SB_ERR_NOT_FOUND;
    }
    if (!d->read)
    {
        return SB_ERR_UNSUPPORTED;
    }
    return d->read(d->ctx, point, value_out);
}

int pc_southbound_write(const char *name, uint32_t point, int32_t value)
{
    const SouthboundDriver *d = pc_southbound_find(name);
    if (!d)
    {
        return SB_ERR_NOT_FOUND;
    }
    if (!d->write)
    {
        return SB_ERR_UNSUPPORTED;
    }
    return d->write(d->ctx, point, value);
}

int pc_southbound_read_block(const char *name, uint32_t first, int32_t *out, size_t n)
{
    if (!out || n == 0)
    {
        return SB_ERR_ARG;
    }
    const SouthboundDriver *d = pc_southbound_find(name);
    if (!d)
    {
        return SB_ERR_NOT_FOUND;
    }
    if (!d->read_block)
    {
        return SB_ERR_UNSUPPORTED;
    }
    return d->read_block(d->ctx, first, out, n);
}

int pc_southbound_write_block(const char *name, uint32_t first, const int32_t *in, size_t n)
{
    if (!in || n == 0)
    {
        return SB_ERR_ARG;
    }
    const SouthboundDriver *d = pc_southbound_find(name);
    if (!d)
    {
        return SB_ERR_NOT_FOUND;
    }
    if (!d->write_block)
    {
        return SB_ERR_UNSUPPORTED;
    }
    return d->write_block(d->ctx, first, in, n);
}

#endif // PC_ENABLE_SOUTHBOUND
