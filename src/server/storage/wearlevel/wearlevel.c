// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wearlevel.c
 * @brief Flash wear-leveling slot selector core (see wearlevel.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_WEARLEVEL

#include "server/storage/wearlevel/wearlevel.h"

PROTOCORE_BEGIN_DECLS

void protocore_wearlevel_pick(uint8_t *restrict work)
{
    (void)work;
    const uint32_t *counts = WearlevelV.args.counts;
    const size_t n = WearlevelV.args.n;

    WearlevelV.n_out = 0;
    if (!counts || n == 0)
    {
        return;
    }
    size_t best = 0;
    uint32_t lowest = counts[0];
    for (size_t i = 1; i < n; i++)
    {
        if (counts[i] < lowest) // strict <, so ties keep the lowest index
        {
            lowest = counts[i];
            best = i;
        }
    }
    WearlevelV.n_out = best;
}

void protocore_wearlevel_mark(uint8_t *restrict work)
{
    (void)work;
    uint32_t *counts = WearlevelV.args.counts_rw;
    const size_t idx = WearlevelV.args.idx;

    if (!counts || idx >= WearlevelV.args.n)
    {
        return;
    }
    if (counts[idx] != 0xFFFFFFFFu) // saturate: never wrap a wear count back to 0
    {
        counts[idx]++;
    }
}

void protocore_wearlevel_imbalance(uint8_t *restrict work)
{
    (void)work;
    const uint32_t *counts = WearlevelV.args.counts;
    const size_t n = WearlevelV.args.n;

    WearlevelV.spread = 0;
    if (!counts || n == 0)
    {
        return;
    }
    uint32_t lo = counts[0];
    uint32_t hi = counts[0];
    for (size_t i = 1; i < n; i++)
    {
        if (counts[i] < lo)
        {
            lo = counts[i];
        }
        if (counts[i] > hi)
        {
            hi = counts[i];
        }
    }
    WearlevelV.spread = hi - lo;
}

/** @brief The operands and the outcome. */
WearlevelVars WearlevelV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WEARLEVEL
