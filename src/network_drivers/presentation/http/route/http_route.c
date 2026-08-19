// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HTTP_ROUTE

#include "mmgr/protomem/protomem.h" // mem.zero: the hand-out wipe
#include "mmgr/secure/secure.h"     // where the table lives
#include "network_drivers/presentation/http/route/http_route.h"
#include "protocore.h" // completes HttpRoute; route.h names it only as an opaque tag

PROTOCORE_BEGIN_DECLS

// The table's layout, known only here. The handle is the module's one file-scope mutable; the
// storage behind it belongs to the secure pool.
struct HttpRouteCtx
{
    HttpRoute entry[MAX_ROUTES];
    uint8_t count;
};
static struct HttpRouteCtx *s_route;

_Static_assert(sizeof(struct HttpRouteCtx) <= PROTOCORE_WORK_ROUTE_TABLE,
               "route table outgrew PROTOCORE_WORK_ROUTE_TABLE");

// Bound on first use rather than at an init the caller has to remember: a registration is the first
// thing that touches the table, and every reader runs after one. The borrow is from the persistent
// end, which no mark walks and no release reclaims, and it comes back zeroed.
static struct HttpRouteCtx *bind_route(void)
{
    if (s_route == NULL)
    {
        protocore_span s = protocore_secure_persist_span(sizeof(struct HttpRouteCtx));
        if (span.ok(s))
        {
            s_route = (struct HttpRouteCtx *)s.buf;
        }
    }
    return s_route;
}

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void http_routes_add(uint8_t *restrict work)
{
    (void)work;

    struct HttpRouteCtx *t = bind_route();
    if (t == NULL || t->count >= MAX_ROUTES)
    {
        HttpRoutes.ptr = NULL;
        return;
    }
    HttpRoute *r = &t->entry[t->count];
    t->count++;

    // Zeroed on hand-out, not on release. A registration fills the fields its route kind uses and
    // leaves the rest, so an entry carrying a previous tenant's handler or backend pointer would
    // dispatch to it. There is no release path - routes are registered at setup and live forever -
    // so hand-out is the only moment this can be done.
    mem.zero(r, sizeof(*r));
    HttpRoutes.ptr = r;
}

static void http_routes_count(uint8_t *restrict work)
{
    (void)work;

    HttpRoutes.value = s_route == NULL ? 0u : s_route->count;
}

static void http_routes_at(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = HttpRoutes.at_args.i;

    if (s_route == NULL || i >= s_route->count)
    {
        HttpRoutes.ptr = NULL;
        return;
    }
    HttpRoutes.ptr = &s_route->entry[i];
}

static void http_routes_reset(uint8_t *restrict work)
{
    (void)work;

    // The count is the table: add() zeroes an entry on hand-out, so nothing below the count can carry
    // a previous tenant's fields and there is nothing to wipe here.
    if (s_route != NULL)
    {
        s_route->count = 0;
    }
}

HttpRouteNs HttpRoutes = {
    .add = http_routes_add,
    .count = http_routes_count,
    .at = http_routes_at,
    .reset = http_routes_reset,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP_ROUTE
