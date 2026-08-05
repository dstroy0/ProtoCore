// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file route.h
 * @brief The route table: where a request goes.
 *
 * The module exports one symbol, @ref RouteTable, which the network layer carries as
 * @c network.route. Everything in route.c has internal linkage.
 */

#ifndef PROTOCORE_ROUTE_H
#define PROTOCORE_ROUTE_H

#include "protocore_config.h" // the entry point: MAX_ROUTES, and types.h for the widths

PROTO_BEGIN_DECLS

typedef struct Route Route;

/** @brief The table's storage. Declared, never defined here: the layout stays in route.c. */
typedef struct RouteCtx RouteCtx;

/**
 * @brief The route-table module.
 *
 * @var RouteNs::ctx    the table, opaque to every caller.
 * @var RouteNs::add    take the next free entry, zeroed and ready to fill, or NULL when full.
 * @var RouteNs::count  entries currently registered.
 * @var RouteNs::at     entry @c i, or NULL if @c i is past the end.
 * @var RouteNs::reset  empty the table. For tests: a case that does not reset matches against every
 *                      route the previous cases registered.
 */
typedef struct
{
    RouteCtx *ctx;
    Route *(*add)(void);
    uint8_t (*count)(void);
    Route *(*at)(uint8_t i);
    void (*reset)(void);
} RouteNs;

/** @brief The one symbol this module exports. */
extern const RouteNs RouteTable;

PROTO_END_DECLS

#endif // PROTOCORE_ROUTE_H
