// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wearlevel.c
 * @brief Flash wear-leveling slot selector core (see wearlevel.h).
 */

#include "server/storage/wearlevel.h"

#if PROTOCORE_ENABLE_WEARLEVEL

static void wear_pick(uint8_t *restrict work)
{
    (void)work;
    const uint32_t *counts = Wearlevel.args.counts;
    const size_t n = Wearlevel.args.n;

    Wearlevel.n_out = 0;
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
    Wearlevel.n_out = best;
}

static void wear_mark(uint8_t *restrict work)
{
    (void)work;
    uint32_t *counts = Wearlevel.args.counts_rw;
    const size_t idx = Wearlevel.args.idx;

    if (!counts || idx >= Wearlevel.args.n)
    {
        return;
    }
    if (counts[idx] != 0xFFFFFFFFu) // saturate: never wrap a wear count back to 0
    {
        counts[idx]++;
    }
}

static void wear_imbalance(uint8_t *restrict work)
{
    (void)work;
    const uint32_t *counts = Wearlevel.args.counts;
    const size_t n = Wearlevel.args.n;

    Wearlevel.spread = 0;
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
    Wearlevel.spread = hi - lo;
}

WearlevelNs Wearlevel = {.pick = wear_pick, .mark = wear_mark, .imbalance = wear_imbalance};

#endif // PROTOCORE_ENABLE_WEARLEVEL
