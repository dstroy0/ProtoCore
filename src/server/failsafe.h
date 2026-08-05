// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file failsafe.h
 * @brief Software watchdog: deadlock detection + fail-safe safe-state (PC_ENABLE_FAILSAFE).
 *
 * A fixed registry of "lifelines" - a task, worker, or control loop that must check in
 * (`pc_failsafe_feed`) at least every `deadline_ms`. If one stops checking in (a hang, a
 * deadlock, a wedged loop), `pc_failsafe_check()` detects it and fires a breach callback exactly
 * once per stuck episode, so the app can drive its outputs to a known-safe state (motors off, valves
 * closed), log, and optionally reset. It complements the hardware task watchdog: this one is
 * app-defined, per-lifeline, and knows *which* subsystem wedged.
 *
 * Zero heap (a static registry), no stdlib. The overdue test is a wrap-safe unsigned time delta, so it
 * is correct across a `millis()` rollover. The evaluation core takes an explicit `now`, so it is fully
 * host-testable with a synthetic clock; the no-`now` wrappers read the pluggable `pc_millis()`.
 */

#ifndef PROTOCORE_FAILSAFE_H
#define PROTOCORE_FAILSAFE_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_FAILSAFE

/** @brief One monitored lifeline. */
typedef struct
{
    const char *name;      ///< label (for the breach callback + JSON); not copied.
    uint32_t deadline_ms;  ///< max interval between feeds before it is considered stuck.
    uint32_t last_feed_ms; ///< time of the last check-in (pc_millis units).
    proto_bool armed;      ///< slot in use.
    proto_bool breached;   ///< currently in breach (so the callback fires once per episode).
} pc_lifeline;

/** @brief Breach callback: invoked once when @p id (named @p name) misses its deadline. */
typedef void (*pc_failsafe_cb)(int id, const char *name, void *arg);

// ---------------------------------------------------------------------------
// Host-testable core
// ---------------------------------------------------------------------------

/**
 * @brief Is a lifeline overdue at @p now?
 *
 * Wrap-safe: the unsigned delta `now - last_feed` is correct across a millis() rollover as long as the
 * true gap is under 2^32 ms (~49 days), which any real deadline is.
 */
static inline proto_bool pc_lifeline_overdue(uint32_t now, uint32_t last_feed_ms, uint32_t deadline_ms)
{
    return (uint32_t)(now - last_feed_ms) > deadline_ms;
}

// ---------------------------------------------------------------------------
// Registry API
// ---------------------------------------------------------------------------

/** @brief Clear the whole registry and the breach callback (mainly for tests / re-init). */
void pc_failsafe_reset(void);

/**
 * @brief Register a lifeline that must check in at least every @p deadline_ms.
 * @param name        label used in the breach callback + JSON (borrowed, not copied).
 * @param deadline_ms max allowed interval between feeds.
 * @return the lifeline id (>= 0), or -1 if the registry (PC_FAILSAFE_MAX_LIFELINES) is full.
 *
 * The lifeline starts fed at the registration time, so it is not instantly overdue.
 */
int pc_failsafe_register(const char *name, uint32_t deadline_ms);
int pc_failsafe_register_at(const char *name, uint32_t deadline_ms, uint32_t now);

/** @brief Check in lifeline @p id (clears any breach). Reads pc_millis(). */
proto_bool pc_failsafe_feed(int id);
proto_bool pc_failsafe_feed_at(int id, uint32_t now);

/** @brief Install the breach callback (fired once per stuck episode). */
void pc_failsafe_on_breach(pc_failsafe_cb cb, void *arg);

/**
 * @brief Evaluate every armed lifeline; fire the callback for each newly-overdue one.
 * @return a bitmask of the lifeline ids currently in breach (bit i set = id i breached).
 *
 * A lifeline that is overdue and not already flagged fires the callback once and is marked breached
 * until it is fed again; already-breached lifelines stay in the returned mask but do not re-fire.
 */
uint32_t pc_failsafe_check_at(uint32_t now);
uint32_t pc_failsafe_check(void);

/** @brief Serialize the registry as JSON for a /health-style endpoint. @return bytes written (excl NUL). */
int pc_failsafe_json_at(uint32_t now, char *out, size_t cap);

#endif // PC_ENABLE_FAILSAFE

PROTO_END_DECLS

#endif // PROTOCORE_FAILSAFE_H
