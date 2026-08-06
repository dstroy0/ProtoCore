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
 * The one symbol this file exports is @ref HttpRoutes.
 */

#include "network_drivers/presentation/http/route/http_route.h"
#include "mmgr/protomem.h" // mem.zero: the hand-out wipe
#include "mmgr/secure.h"   // where the table lives
#include "protocore.h"     // completes HttpRoute; route.h names it only as an opaque tag

// The table's layout, known only here. The handle is the module's one file-scope mutable; the
// storage behind it belongs to the secure pool.
struct HttpRouteCtx
{
    HttpRoute entry[MAX_ROUTES];
    uint8_t count;
};
static struct HttpRouteCtx *s_route;

_Static_assert(sizeof(struct HttpRouteCtx) <= PC_WORK_ROUTE_TABLE, "route table outgrew PC_WORK_ROUTE_TABLE");

// Bound on first use rather than at an init the caller has to remember: a registration is the first
// thing that touches the table, and every reader runs after one. The borrow is from the persistent
// end, which no mark walks and no release reclaims, and it comes back zeroed.
static struct HttpRouteCtx *bind_route(void)
{
    if (s_route == NULL)
    {
        pc_span s = pc_secure_persist_span(sizeof(struct HttpRouteCtx));
        if (pc_span_ok(s))
        {
            s_route = (struct HttpRouteCtx *)s.buf;
        }
    }
    return s_route;
}

static HttpRoute *add(void)
{
    struct HttpRouteCtx *t = bind_route();
    if (t == NULL || t->count >= MAX_ROUTES)
    {
        return NULL;
    }
    HttpRoute *r = &t->entry[t->count];
    t->count++;

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

static HttpRoute *at(uint8_t i)
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

const HttpRouteNs HttpRoutes = {add, count, at, reset};
