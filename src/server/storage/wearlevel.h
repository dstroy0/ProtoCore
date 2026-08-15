// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wearlevel.h
 * @brief Flash wear-leveling slot selector (PROTOCORE_ENABLE_WEARLEVEL).
 *
 * Flash/NVS cells wear out after a bounded number of erase cycles, so a device that repeatedly writes a
 * record (a log line, a config snapshot, a counter) to the *same* location burns that block out early.
 * This is the pure core of wear leveling: given a per-slot erase/write count, `protocore_wearlevel_pick`
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

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_WEARLEVEL

/** @brief The slot table a call reads, and the slot it names. */
typedef struct
{
    const uint32_t *counts; ///< per-slot write/erase counts; the app persists these across boots
    uint32_t *counts_rw;    ///< the same table, where a mark writes to it
    size_t n;               ///< number of slots
    size_t idx;             ///< the slot a mark records a write to
} WearArgs;

/** @brief The wear policy's own calls, described only in wearlevel.c. */
struct WearlevelInternal;

/**
 * @brief The wear-levelling policy over a caller-owned count table.
 *
 * A caller sets the members a call takes, invokes it through ::Wearlevel, and reads the outcome off
 * the same handle. The table is the caller's; nothing is held here.
 *
 * @var WearlevelNs::args      the slot table a call reads, and the slot it names
 * @var WearlevelNs::n_out     the slot a pick chose
 * @var WearlevelNs::spread    the imbalance a spread reports
 * @var WearlevelNs::pick      the least-worn slot to write next
 * @var WearlevelNs::mark      record a write to args.idx (saturating, so a count never wraps to 0)
 * @var WearlevelNs::imbalance max count - min count across the slots (0 = perfectly level)
 * @var WearlevelNs::internal  the calls that read and bump the table
 *
 * pick round-robins naturally: after writing to the chosen slot the app bumps its count with mark,
 * so the next pick moves on and the region wears uniformly. Ties resolve to the lowest index, and a
 * null table or zero length picks 0. imbalance is a monotone health metric for a /health-style
 * endpoint: it stays small under pick+mark and grows if the app writes off-policy.
 *
 * No storage member: every call works in the caller's table.
 */
typedef struct
{
    WearArgs args;

    size_t n_out;
    uint32_t spread;

    void (*pick)(struct WearlevelInternal *ctx);
    void (*mark)(struct WearlevelInternal *ctx);
    void (*imbalance)(struct WearlevelInternal *ctx);

    struct WearlevelInternal *internal;
} WearlevelNs;

/** @brief The one symbol this module exports. */
extern WearlevelNs Wearlevel;

#endif // PROTOCORE_ENABLE_WEARLEVEL

PROTOCORE_END_DECLS

#endif // PROTOCORE_WEARLEVEL_H
