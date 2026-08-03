// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wearlevel.c
 * @brief Flash wear-leveling slot selector core (see wearlevel.h).
 */

#include "server/filesystem/wearlevel.h"

#if PC_ENABLE_WEARLEVEL

size_t pc_wearlevel_pick(const uint32_t *counts, size_t n)
{
    if (!counts || n == 0)
    {
        return 0;
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
    return best;
}

void pc_wearlevel_mark(uint32_t *counts, size_t n, size_t idx)
{
    if (!counts || idx >= n)
    {
        return;
    }
    if (counts[idx] != 0xFFFFFFFFu) // saturate: never wrap a wear count back to 0
    {
        counts[idx]++;
    }
}

uint32_t pc_wearlevel_spread(const uint32_t *counts, size_t n)
{
    if (!counts || n == 0)
    {
        return 0;
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
    return hi - lo;
}

#endif // PC_ENABLE_WEARLEVEL
