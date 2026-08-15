// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file failsafe.h
 * @brief Software watchdog: deadlock detection + fail-safe safe-state (PROTOCORE_ENABLE_FAILSAFE).
 *
 * A fixed registry of "lifelines" - a task, worker, or control loop that must check in
 * (`protocore_failsafe_feed`) at least every `deadline_ms`. If one stops checking in (a hang, a
 * deadlock, a wedged loop), `protocore_failsafe_check()` detects it and fires a breach callback exactly
 * once per stuck episode, so the app can drive its outputs to a known-safe state (motors off, valves
 * closed), log, and optionally reset. It complements the hardware task watchdog: this one is
 * app-defined, per-lifeline, and knows *which* subsystem wedged.
 *
 * Zero heap (a static registry), no stdlib. The overdue test is a wrap-safe unsigned time delta, so it
 * is correct across a `millis()` rollover. The evaluation core takes an explicit `now`, so it is fully
 * host-testable with a synthetic clock; the no-`now` wrappers read the pluggable `protocore_millis()`.
 */

#ifndef PROTOCORE_FAILSAFE_H
#define PROTOCORE_FAILSAFE_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_FAILSAFE

/** @brief One monitored lifeline. */
typedef struct
{
    const char *name;      ///< label (for the breach callback + JSON); not copied.
    uint32_t deadline_ms;  ///< max interval between feeds before it is considered stuck.
    uint32_t last_feed_ms; ///< time of the last check-in (protocore_millis units).
    proto_bool armed;      ///< slot in use.
    proto_bool breached;   ///< currently in breach (so the callback fires once per episode).
} protocore_lifeline;

/** @brief Breach callback: invoked once when @p id (named @p name) misses its deadline. */
typedef void (*protocore_failsafe_cb)(int id, const char *name, void *arg);

// ---------------------------------------------------------------------------
// Host-testable core
// ---------------------------------------------------------------------------

/**
 * @brief Is a lifeline overdue at @p now?
 *
 * Wrap-safe: the unsigned delta `now - last_feed` is correct across a millis() rollover as long as the
 * true gap is under 2^32 ms (~49 days), which any real deadline is.
 */
static inline proto_bool protocore_lifeline_overdue(uint32_t now, uint32_t last_feed_ms, uint32_t deadline_ms)
{
    return (uint32_t)(now - last_feed_ms) > deadline_ms;
}

// ---------------------------------------------------------------------------
// Registry API
// ---------------------------------------------------------------------------

/** @brief The lifeline a call names, and what arming it takes. */
typedef struct
{
    const char *name;     ///< the lifeline's label; a persistent string
    uint32_t deadline_ms; ///< how long it may go unfed before it is overdue
    int id;               ///< the lifeline a feed names
    uint32_t now;         ///< the caller's clock, so the module stays testable against a synthetic one
} FailsafeArgs;

/** @brief What a breach fires, and where a report is written. */
typedef struct
{
    protocore_failsafe_cb cb; ///< what a breach fires; NULL leaves the callback off
    void *arg;                ///< the opaque pointer it is given back
    char *out;                ///< where the JSON lands
    size_t cap;               ///< how much room it has
} FailsafeOutArgs;

/** @brief The lifelines' own state and the calls that reach them, described only in failsafe.c. */
struct FailsafeInternal;

/**
 * @brief The lifeline watchdog.
 *
 * A caller sets the members a call takes, invokes it through ::Failsafe, and reads the outcome off
 * the same handle. The lifeline table is behind @ref internal.
 *
 * @var FailsafeNs::args       the lifeline a call names, and what arming it takes
 * @var FailsafeNs::out_args   what a breach fires, and where a report is written
 * @var FailsafeNs::ok         a call's true/false outcome
 * @var FailsafeNs::i32        the lifeline a register took, or < 0 when the table is full
 * @var FailsafeNs::breached   one bit per lifeline that went overdue on this check
 * @var FailsafeNs::n          characters a report wrote
 * @var FailsafeNs::reset      clear every lifeline and drop the breach callback
 * @var FailsafeNs::add        arm a lifeline against args.now; it starts fed
 * @var FailsafeNs::feed       check one in against args.now
 * @var FailsafeNs::on_breach  install what a breach fires
 * @var FailsafeNs::check      judge every armed lifeline against args.now
 * @var FailsafeNs::json       report every armed lifeline
 * @var FailsafeNs::internal   the lifeline table and the calls that reach it
 *
 * Every call takes the clock as @c args.now rather than reading one, so the whole module runs
 * against a synthetic clock on the host. A feed clears the breach so the lifeline can fire again.
 */
typedef struct
{
    FailsafeArgs args;
    FailsafeOutArgs out_args;

    proto_bool ok;
    int i32;
    uint32_t breached;
    int n;

    void (*reset)(struct FailsafeInternal *ctx);
    void (*add)(struct FailsafeInternal *ctx);
    void (*feed)(struct FailsafeInternal *ctx);
    void (*on_breach)(struct FailsafeInternal *ctx);
    void (*check)(struct FailsafeInternal *ctx);
    void (*json)(struct FailsafeInternal *ctx);

    struct FailsafeInternal *internal;
} FailsafeNs;

/** @brief The one symbol this module exports. */
extern FailsafeNs Failsafe;

#endif // PROTOCORE_ENABLE_FAILSAFE

PROTOCORE_END_DECLS

#endif // PROTOCORE_FAILSAFE_H
