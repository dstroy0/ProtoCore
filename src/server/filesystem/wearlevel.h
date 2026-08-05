// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wearlevel.h
 * @brief Flash wear-leveling slot selector (PC_ENABLE_WEARLEVEL).
 *
 * Flash/NVS cells wear out after a bounded number of erase cycles, so a device that repeatedly writes a
 * record (a log line, a config snapshot, a counter) to the *same* location burns that block out early.
 * This is the pure core of wear leveling: given a per-slot erase/write count, `pc_wearlevel_pick`
 * returns the least-worn slot to write next, so writes spread evenly and the whole region ages together.
 *
 * The app owns the actual slots (NVS keys, flash sectors, VFS files) and the persisted counts; this core
 * just decides *where* the next write goes and reports the wear imbalance. Pure, zero heap, no stdlib,
 * so it is fully host-testable. It composes with the mount (the storage medium) and server/logbuf
 * (whose sink can offload to a wear-leveled store).
 */

#ifndef PROTOCORE_WEARLEVEL_H
#define PROTOCORE_WEARLEVEL_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_WEARLEVEL

/**
 * @brief Pick the least-worn slot to write next.
 * @param counts per-slot write/erase counts (length @p n); the app persists these across boots.
 * @param n      number of slots.
 * @return the index of the slot with the lowest count (ties resolve to the lowest index), or 0 if
 *         @p counts is null or @p n is 0.
 *
 * Round-robins naturally: after writing to the chosen slot the app bumps its count (pc_wearlevel_mark),
 * so the next pick moves on, and the region wears uniformly.
 */
size_t pc_wearlevel_pick(const uint32_t *counts, size_t n);

/** @brief Record a write to slot @p idx (saturating increment, so a count never wraps to 0). */
void pc_wearlevel_mark(uint32_t *counts, size_t n, size_t idx);

/**
 * @brief Wear imbalance = max count - min count across the slots (0 = perfectly level).
 *
 * A monotone health metric for a /health-style endpoint: it stays small under `pick`+`mark` and grows
 * if the app writes off-policy.
 */
uint32_t pc_wearlevel_spread(const uint32_t *counts, size_t n);

#endif // PC_ENABLE_WEARLEVEL

PROTO_END_DECLS

#endif // PROTOCORE_WEARLEVEL_H
