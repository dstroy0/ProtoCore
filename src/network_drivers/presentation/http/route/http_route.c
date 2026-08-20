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

// The table's layout, known only here. The storage behind it belongs to the secure pool.
struct HttpRouteCtx
{
    HttpRoute entry[MAX_ROUTES];
    uint8_t count;
};

// The caller's borrow, split: the table at its offset. One pointer arrives and every region is that
// pointer plus a compile-time offset, so the assert below proves the span covers it before anything
// runs.
#define ROUTE_OFF_CTX 0u
static_assert(ROUTE_OFF_CTX + sizeof(struct HttpRouteCtx) <= PROTOCORE_HTTP_ROUTE_BORROW,
              "PROTOCORE_HTTP_ROUTE_BORROW is short of the route table - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define ROUTE_CTX(w) ((struct HttpRouteCtx *)(void *)((w) + ROUTE_OFF_CTX))

// The one owned instance, private to this TU: the pointer to the bytes taken for the table.
static uint8_t *s_span;

// Not an entry: an entry takes a borrow and this is where that borrow comes from. Every registrar
// and every reader drives the same table, so the bytes are the module's rather than any one
// caller's. Taken from the persistent end, which no mark walks and no release reclaims, and it comes
// back zeroed.
uint8_t *protocore_http_route_span(void)
{
    if (s_span == NULL)
    {
        s_span = protocore_secure_persist_span(PROTOCORE_HTTP_ROUTE_BORROW).buf;
    }
    return s_span;
}

// --- the entries -----------------------------------------------------------

// The table is the borrow: every entry reads it through ROUTE_CTX.

static void http_routes_add(uint8_t *restrict work)
{
    struct HttpRouteCtx *t = ROUTE_CTX(work);
    if (t->count >= MAX_ROUTES)
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
    HttpRoutes.value = ROUTE_CTX(work)->count;
}

static void http_routes_at(uint8_t *restrict work)
{
    uint8_t i = HttpRoutes.at_args.i;

    if (i >= ROUTE_CTX(work)->count)
    {
        HttpRoutes.ptr = NULL;
        return;
    }
    HttpRoutes.ptr = &ROUTE_CTX(work)->entry[i];
}

static void http_routes_reset(uint8_t *restrict work)
{
    // The count is the table: add() zeroes an entry on hand-out, so nothing below the count can carry
    // a previous tenant's fields and there is nothing to wipe here.
    ROUTE_CTX(work)->count = 0;
}

HttpRouteNs HttpRoutes = {
    .add = http_routes_add,
    .count = http_routes_count,
    .at = http_routes_at,
    .reset = http_routes_reset,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP_ROUTE
