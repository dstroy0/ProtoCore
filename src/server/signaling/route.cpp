// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file route.cpp
 * @brief The route table and its one owner. See route.h.
 */

#include "server/signaling/route.h"
#include <string.h>

namespace
{
// The table, owned by one instance (internal linkage). It was a member of the server class, which
// meant every translation unit that included the server header could index it directly - and four
// of them did. Nothing outside this file can name it now; callers take an entry or walk by index.
struct RouteCtx
{
    Route entry[MAX_ROUTES];
    uint8_t count;
};
static RouteCtx s_route;
} // namespace

Route *pc_route_add(void)
{
    if (s_route.count >= MAX_ROUTES)
    {
        return nullptr;
    }
    Route *r = &s_route.entry[s_route.count];
    s_route.count++;

    // Zeroed on hand-out, not on release. A registration fills the fields its route kind uses and
    // leaves the rest, so an entry carrying a previous tenant's handler or backend pointer would
    // dispatch to it. There is no release path - routes are registered at setup and live forever -
    // so hand-out is the only moment this can be done.
    memset(r, 0, sizeof(*r));
    return r;
}

uint8_t pc_route_count(void)
{
    return s_route.count;
}

Route *pc_route_at(uint8_t i)
{
    if (i >= s_route.count)
    {
        return nullptr;
    }
    return &s_route.entry[i];
}
