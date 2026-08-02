// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file route.h
 * @brief The route table: where a request goes.
 *
 * Under signaling because that is what decides whether a request reaches a route at all.
 */

#ifndef PROTOCORE_ROUTE_H
#define PROTOCORE_ROUTE_H

#include <stdint.h>

struct Route;

/**
 * @brief Take the next free entry.
 *
 * @return the entry, zeroed and ready to fill, or nullptr when the table is full.
 */
Route *pc_route_add(void);

/** @brief Entries currently registered. */
uint8_t pc_route_count(void);

/** @brief Entry @p i, or nullptr if @p i is past the end. */
Route *pc_route_at(uint8_t i);

/**
 * @brief Empty the table.
 *
 * For tests. A case that does not reset matches against every route the previous cases registered.
 */
void pc_route_reset(void);

#endif // PROTOCORE_ROUTE_H
