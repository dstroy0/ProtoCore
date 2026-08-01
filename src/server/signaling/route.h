// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file route.h
 * @brief The route table: where a request goes.
 *
 * Under signaling because that is what decides whether a request reaches a route at all. Routing
 * answers one question - which entry matches - and holds nothing else.
 *
 * The table had no owner. Six registration sites and four walks reached into the same array from
 * three translation units, each repeating the same bounds test and the same post-increment, so
 * "is there room" was answered in six places that could disagree. Two calls replace all ten.
 */

#ifndef PROTOCORE_ROUTE_H
#define PROTOCORE_ROUTE_H

#include "protocore.h"

/**
 * @brief Take the next free entry.
 *
 * The bounds test and the take are one call because they were never separable: every caller wrote
 * the same guard immediately before the same increment, and a caller that got the order wrong would
 * write past the table.
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
 * Firmware never calls this - routes are registered at setup and live for the run, which is why
 * there is no per-entry release. A test registers a fresh set per case, and without this each case
 * would inherit every route the previous ones left behind and match against them.
 */
void pc_route_reset(void);

#endif // PROTOCORE_ROUTE_H
