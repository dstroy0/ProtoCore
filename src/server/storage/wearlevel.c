// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wearlevel.c
 * @brief Flash wear-leveling slot selector core (see wearlevel.h).
 */

#include "server/storage/wearlevel.h"

#if PROTOCORE_ENABLE_WEARLEVEL

/**
 * @brief The wear policy's calls - what WearlevelNs points at.
 *
 * @var WearlevelInternal::ns  the handle a caller sets a call's members on
 */
struct WearlevelInternal
{
    WearlevelNs *ns;
};

static struct WearlevelInternal s_wear = {.ns = &Wearlevel};

static void wear_pick(struct WearlevelInternal *restrict ctx)
{
    const uint32_t *counts = ctx->ns->args.counts;
    const size_t n = ctx->ns->args.n;

    ctx->ns->n_out = 0;
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
    ctx->ns->n_out = best;
}

static void wear_mark(struct WearlevelInternal *restrict ctx)
{
    uint32_t *counts = ctx->ns->args.counts_rw;
    const size_t idx = ctx->ns->args.idx;

    if (!counts || idx >= ctx->ns->args.n)
    {
        return;
    }
    if (counts[idx] != 0xFFFFFFFFu) // saturate: never wrap a wear count back to 0
    {
        counts[idx]++;
    }
}

static void wear_imbalance(struct WearlevelInternal *restrict ctx)
{
    const uint32_t *counts = ctx->ns->args.counts;
    const size_t n = ctx->ns->args.n;

    ctx->ns->spread = 0;
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
    ctx->ns->spread = hi - lo;
}

WearlevelNs Wearlevel = {.pick = wear_pick, .mark = wear_mark, .imbalance = wear_imbalance, .internal = &s_wear};

#endif // PROTOCORE_ENABLE_WEARLEVEL
