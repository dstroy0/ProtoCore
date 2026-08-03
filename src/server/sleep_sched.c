// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sleep_sched.c
 * @brief Dynamic sleep-cycle scheduler decision core (see sleep_sched.h).
 */

#include "server/sleep_sched.h"

#if PC_ENABLE_SLEEP_SCHED

uint32_t pc_sleep_next(uint32_t now, uint32_t last_active_ms, const pc_sleep_cfg *cfg)
{
    if (!cfg)
    {
        return 0;
    }

    uint32_t idle = (uint32_t)(now - last_active_ms); // wrap-safe unsigned delta
    if (idle < cfg->idle_ms)
    {
        return 0; // active recently: stay awake
    }

    uint32_t ceil_ms = cfg->max_ms < cfg->min_ms ? cfg->min_ms : cfg->max_ms;
    if (cfg->ramp_ms == 0)
    {
        return ceil_ms; // no ramp: go straight to the deepest window
    }

    // Grow the window by doubling for every ramp_ms of idle past the threshold, clamped to the ceiling.
    // The pre-shift ceiling check keeps the doubling from overflowing.
    uint32_t doublings = (idle - cfg->idle_ms) / cfg->ramp_ms;
    uint32_t window = cfg->min_ms ? cfg->min_ms : 1;
    for (uint32_t i = 0; i < doublings; i++)
    {
        if (window >= ceil_ms || window > ceil_ms / 2)
        {
            window = ceil_ms;
            break;
        }
        window <<= 1;
    }
    if (window > ceil_ms)
    {
        window = ceil_ms;
    }
    // GCOVR_EXCL_START  unreachable: ceil_ms = max(min_ms, max_ms) (see above), so ceil_ms >= min_ms
    // always. window starts at min_ms (or 1 when min_ms is 0, itself >= min_ms since min_ms is then
    // 0), and every later change to it either doubles it (strictly increasing) or clamps it to
    // exactly ceil_ms (the loop-break above, or the post-loop check on the previous line) - never to
    // anything below ceil_ms >= min_ms. So window can never fall below min_ms. Kept as a defensive
    // floor.
    if (window < cfg->min_ms)
    {
        window = cfg->min_ms;
    }
    // GCOVR_EXCL_STOP
    return window;
}

#endif // PC_ENABLE_SLEEP_SCHED
