// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mnt.c
 * @brief The mount registry: which backend serves which subtree, and which one is active.
 *
 * A table of pointers and a count. The backend behind it is a separate module - the built-in one
 * is server/storage/mnt_ram, and a board layer supplies its own - because a backend is a
 * FOOTPRINT and this is a seam. The filesystem accessor calls active() on every operation, so
 * the seam is unconditional and no feature flag decides whether it resolves.
 */

#include "server/storage/mnt/mnt.h"

// --- the HAL: which store is mounted -------------------------------------------------------------
// One pointer. Which backend filled it is the mounting caller's business, and whether any backend
// was compiled at all is that backend module's flag, not this one's.
typedef struct
{
    const protocore_mnt_backend *backend;
} MntCtx;
static MntCtx s_hal;

// A mount point: which backend serves a subtree, and which subtree. Both registrars that offer a
// mount - static file serving and DAV - describe one with this pair, so the pair lives here with the
// rest of mounting rather than being copied into each of their route entries.
typedef struct
{
    const protocore_mnt_backend *backend; ///< null is legal: the accessor uses whatever is mounted
    const char *root;                     ///< the subtree, as a request-path piece
} MntPoint;

typedef struct
{
    MntPoint point[MAX_ROUTES];
    uint8_t count;
} MntPointCtx;
static MntPointCtx s_point;

void protocore_mnt_point_add(uint8_t *restrict work)
{
    (void)work;
    const protocore_mnt_backend *backend = MntV.args.backend;
    const char *root = MntV.args.root;

    if (s_point.count >= MAX_ROUTES)
    {
        MntV.u8 = PROTOCORE_MNT_NONE;
        return;
    }
    MntPoint *m = &s_point.point[s_point.count];
    m->backend = backend;
    m->root = root;
    MntV.u8 = s_point.count++;
}

void protocore_mnt_point_of(uint8_t *restrict work)
{
    (void)work;
    const uint8_t id = MntV.args.id;

    if (id >= s_point.count)
    {
        MntV.backend = NULL;
        return;
    }
    MntV.backend = s_point.point[id].backend;
}

void protocore_mnt_root_of(uint8_t *restrict work)
{
    (void)work;
    const uint8_t id = MntV.args.id;

    // Empty rather than null, because every caller wants the subtree as a path piece to compare or
    // append: handing back null would put the same null test at each of them.
    if (id >= s_point.count || s_point.point[id].root == NULL)
    {
        MntV.text = "";
        return;
    }
    MntV.text = s_point.point[id].root;
}

void protocore_mnt_reset(uint8_t *restrict work)
{
    (void)work;

    // The count is the table: a row above it is unreachable, and add() writes both fields before the
    // count reaches it, so there is nothing to clear here.
    s_point.count = 0;
}

void protocore_mnt_mount(uint8_t *restrict work)
{
    (void)work;
    const protocore_mnt_backend *backend = MntV.args.backend;

    s_hal.backend = backend;
}

void protocore_mnt_active(uint8_t *restrict work)
{
    (void)work;
    MntV.backend = s_hal.backend;
}

/** @brief The operands and the outcome. */
MntVars MntV;
