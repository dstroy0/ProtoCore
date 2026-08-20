// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sleep_sched.c
 * @brief Dynamic sleep-cycle scheduler decision core (see sleep_sched.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SLEEP_SCHED

#include "server/core/sleep_sched/sleep_sched.h"

void protocore_sleep_sched_next(uint8_t *restrict work)
{
    (void)work;
    const protocore_sleep_cfg *cfg = SleepSchedV.ask.cfg;

    SleepSchedV.ms = 0;
    if (!cfg)
    {
        return;
    }

    uint32_t idle = (uint32_t)(SleepSchedV.ask.now - SleepSchedV.ask.last_active_ms); // wrap-safe unsigned delta
    if (idle < cfg->idle_ms)
    {
        return; // active recently: stay awake
    }

    uint32_t ceil_ms = cfg->max_ms < cfg->min_ms ? cfg->min_ms : cfg->max_ms;
    if (cfg->ramp_ms == 0)
    {
        SleepSchedV.ms = ceil_ms; // no ramp: go straight to the deepest window
        return;
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
    if (window < cfg->min_ms)
    {
        window = cfg->min_ms;
    }
    SleepSchedV.ms = window;
}

/** @brief The operands and the outcome. */
SleepSchedVars SleepSchedV;

#endif // PROTOCORE_ENABLE_SLEEP_SCHED
