// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file route.c
 * @brief The route table and its one owner. See route.h.
 *
 * The one symbol this file exports is @ref RouteTable.
 */

#include "network_drivers/network/route.h"
#include "mmgr/protomem.h" // mem.zero: the hand-out wipe
#include "protocore.h"     // completes Route; route.h names it only as an opaque tag

// The table, owned by one instance with internal linkage. Nothing outside this file can name it;
// callers take an entry or walk by index.
struct RouteCtx
{
    Route entry[MAX_ROUTES];
    uint8_t count;
};
static struct RouteCtx s_route;

static Route *add(void)
{
    if (s_route.count >= MAX_ROUTES)
    {
        return NULL;
    }
    Route *r = &s_route.entry[s_route.count];
    s_route.count++;

    // Zeroed on hand-out, not on release. A registration fills the fields its route kind uses and
    // leaves the rest, so an entry carrying a previous tenant's handler or backend pointer would
    // dispatch to it. There is no release path - routes are registered at setup and live forever -
    // so hand-out is the only moment this can be done.
    mem.zero(r, sizeof(*r));
    return r;
}

static uint8_t count(void)
{
    return s_route.count;
}

static Route *at(uint8_t i)
{
    if (i >= s_route.count)
    {
        return NULL;
    }
    return &s_route.entry[i];
}

static void reset(void)
{
    // The count is the table: add() zeroes an entry on hand-out, so nothing below the count can carry
    // a previous tenant's fields and there is nothing to wipe here.
    s_route.count = 0;
}

const RouteNs RouteTable = {&s_route, add, count, at, reset};
