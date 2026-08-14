// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sleep_sched.h
 * @brief Dynamic sleep-cycle scheduler (PROTOCORE_ENABLE_SLEEP_SCHED).
 *
 * Decides, from the time since the last activity, whether a low-power device should sleep between
 * requests and for how long - so a battery / solar node idles most of the time yet still serves. It is
 * a pure decision core (`protocore_sleep_next`): given `now`, the last-activity timestamp, and a config, it
 * returns the number of milliseconds to sleep (0 = stay awake). The device stays awake until it has
 * been idle for `idle_ms`, then sleeps in windows that ramp from `min_ms` up to `max_ms` the longer the
 * idle streak runs (so a briefly-idle device wakes often and responsively, a long-idle one sleeps deep).
 *
 * Pure, zero heap, no stdlib, and takes an explicit `now`, so it is fully host-testable with a synthetic
 * clock. The scheduler only decides the window; the app applies it per its own policy (a light
 * sleep with a timer wakeup, modem sleep, or deep sleep). It sits beside
 * network_drivers/physical/radio_power (modem sleep): that trims radio power while awake, this schedules the sleep.
 */

#ifndef PROTOCORE_SLEEP_SCHED_H
#define PROTOCORE_SLEEP_SCHED_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_SLEEP_SCHED

/** @brief Scheduler configuration (all times in ms). */
typedef struct
{
    uint32_t idle_ms; ///< stay fully awake until idle at least this long.
    uint32_t min_ms;  ///< first sleep window once idle (also the floor).
    uint32_t max_ms;  ///< longest single sleep window (the ceiling as the idle streak grows).
    uint32_t ramp_ms; ///< every additional `ramp_ms` of idle doubles the window (0 => jump to max_ms).
} protocore_sleep_cfg;

/** @brief What the decision reads: where the clock stands against the last activity. */
typedef struct
{
    uint32_t now;                   ///< current time (protocore_millis units)
    uint32_t last_active_ms;        ///< timestamp of the last activity (a request, a send, app work)
    const protocore_sleep_cfg *cfg; ///< the thresholds
} SleepAskArgs;

/** @brief The scheduler's own call, described only in sleep_sched.c. */
struct SleepSchedInternal;

/**
 * @brief The dynamic sleep-cycle scheduler.
 *
 * A caller sets the members the call takes, invokes it through ::SleepSched, and reads the window
 * off the same handle.
 *
 * @var SleepSchedNs::ask       where the clock stands against the last activity
 * @var SleepSchedNs::ms        milliseconds to sleep, or 0 to stay awake
 * @var SleepSchedNs::next      decide the window
 * @var SleepSchedNs::internal  the call that decides it
 *
 * Wrap-safe: uses the unsigned delta `now - last_active_ms`, correct across a millis() rollover.
 * Reports 0 while idle < `idle_ms`; otherwise a window clamped to [min_ms, max_ms] that grows with
 * the idle streak (doubling every `cfg.ramp_ms`, or straight to `max_ms` when `ramp_ms` is 0). If
 * `max_ms` < `min_ms` the result is clamped to `min_ms`.
 *
 * No storage member: the decision reads its operands and holds nothing.
 */
typedef struct
{
    SleepAskArgs ask;

    uint32_t ms;

    void (*next)(struct SleepSchedInternal *ctx);

    struct SleepSchedInternal *internal;
} SleepSchedNs;

/** @brief The one symbol this module exports. */
extern SleepSchedNs SleepSched;

#endif // PROTOCORE_ENABLE_SLEEP_SCHED

PROTOCORE_END_DECLS

#endif // PROTOCORE_SLEEP_SCHED_H
