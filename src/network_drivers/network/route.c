// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file route.c
 * @brief The route table and its one owner. See route.h.
 *
 * The table is internal server state, so it is borrowed from the secure pool: that borrow is
 * aligned and padded like every other, and the pool wipes it on release rather than leaving a
 * previous tenant's handler and backend pointers readable.
 *
 * The one symbol this file exports is @ref RouteTable.
 */

#include "network_drivers/network/route.h"
#include "mmgr/protomem.h" // mem.zero: the hand-out wipe
#include "mmgr/secure.h"   // where the table lives
#include "protocore.h"     // completes Route; route.h names it only as an opaque tag

// The table's layout, known only here. The handle is the module's one file-scope mutable; the
// storage behind it belongs to the secure pool.
struct RouteCtx
{
    Route entry[MAX_ROUTES];
    uint8_t count;
};
static struct RouteCtx *s_route;

static void init(void)
{
    pc_span s = pc_secure_span(sizeof(struct RouteCtx), 8);
    s_route = pc_span_ok(s) ? (struct RouteCtx *)s.buf : NULL;
    if (s_route != NULL)
    {
        mem.zero(s_route, sizeof(*s_route)); // the pool hands back uninitialized bytes
    }
}

static Route *add(void)
{
    if (s_route == NULL || s_route->count >= MAX_ROUTES)
    {
        return NULL;
    }
    Route *r = &s_route->entry[s_route->count];
    s_route->count++;

    // Zeroed on hand-out, not on release. A registration fills the fields its route kind uses and
    // leaves the rest, so an entry carrying a previous tenant's handler or backend pointer would
    // dispatch to it. There is no release path - routes are registered at setup and live forever -
    // so hand-out is the only moment this can be done.
    mem.zero(r, sizeof(*r));
    return r;
}

static uint8_t count(void)
{
    return s_route == NULL ? 0u : s_route->count;
}

static Route *at(uint8_t i)
{
    if (s_route == NULL || i >= s_route->count)
    {
        return NULL;
    }
    return &s_route->entry[i];
}

static void reset(void)
{
    // The count is the table: add() zeroes an entry on hand-out, so nothing below the count can carry
    // a previous tenant's fields and there is nothing to wipe here.
    if (s_route != NULL)
    {
        s_route->count = 0;
    }
}

const RouteNs RouteTable = {init, add, count, at, reset};
