// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sleep_sched.h
 * @brief Dynamic sleep-cycle scheduler (PC_ENABLE_SLEEP_SCHED).
 *
 * Decides, from the time since the last activity, whether a low-power device should sleep between
 * requests and for how long - so a battery / solar node idles most of the time yet still serves. It is
 * a pure decision core (`pc_sleep_next`): given `now`, the last-activity timestamp, and a config, it
 * returns the number of milliseconds to sleep (0 = stay awake). The device stays awake until it has
 * been idle for `idle_ms`, then sleeps in windows that ramp from `min_ms` up to `max_ms` the longer the
 * idle streak runs (so a briefly-idle device wakes often and responsively, a long-idle one sleeps deep).
 *
 * Pure, zero heap, no stdlib, and takes an explicit `now`, so it is fully host-testable with a synthetic
 * clock. The scheduler only decides the window; the app applies it per its own policy (an
 * `esp_light_sleep_start()` with a timer wakeup, modem sleep, or deep sleep). It sits beside
 * services/system/radio_power (modem sleep): that trims radio power while awake, this schedules the sleep.
 */

#ifndef PROTOCORE_SLEEP_SCHED_H
#define PROTOCORE_SLEEP_SCHED_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_SLEEP_SCHED

/** @brief Scheduler configuration (all times in ms). */
struct pc_sleep_cfg
{
    uint32_t idle_ms; ///< stay fully awake until idle at least this long.
    uint32_t min_ms;  ///< first sleep window once idle (also the floor).
    uint32_t max_ms;  ///< longest single sleep window (the ceiling as the idle streak grows).
    uint32_t ramp_ms; ///< every additional `ramp_ms` of idle doubles the window (0 => jump to max_ms).
};

/**
 * @brief Milliseconds to sleep given the idle duration, or 0 to stay awake.
 *
 * @param now             current time (pc_millis units).
 * @param last_active_ms  timestamp of the last activity (a request, a send, app work).
 * @param cfg             thresholds.
 *
 * Wrap-safe: uses the unsigned delta `now - last_active_ms`, correct across a millis() rollover. Returns
 * 0 while idle < `idle_ms`; otherwise a window clamped to [min_ms, max_ms] that grows with the idle
 * streak (doubling every `cfg.ramp_ms`, or straight to `max_ms` when `ramp_ms` is 0). If `max_ms` <
 * `min_ms` the result is clamped to `min_ms`.
 */
uint32_t pc_sleep_next(uint32_t now, uint32_t last_active_ms, const pc_sleep_cfg *cfg);

#endif // PC_ENABLE_SLEEP_SCHED
#endif // PROTOCORE_SLEEP_SCHED_H
