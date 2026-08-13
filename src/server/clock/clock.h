// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file clock.h
 * @brief Pluggable monotonic clock for all library timing.
 *
 * The library's internal timing runs at **1000 Hz** - one tick is one millisecond,
 * the cadence the test suite asserts and every timeout / poll is expressed in.
 * `protocore_millis()` is that single time source; by default it is the platform
 * `millis()`.
 *
 * To drive the library from your own clock (a hardware timer, an external RTC, a
 * simulation clock), call:
 *
 *     protocore_set_clock(my_clock_fn, my_ticks_per_second);
 *
 * Your clock reports a free-running tick count at `ticks_per_second`. The library
 * **divides it down to its internal 1000 Hz**, so timeouts and polling keep the
 * exact 1 ms granularity the tests verify regardless of how fast your clock runs.
 * Pass a rate >= 1000, ideally a multiple of 1000 for exact division (e.g. a
 * 1 MHz timer -> ticks_per_second = 1000000, divided by 1000). Pass `NULL` to
 * revert to the platform default. One source covers everything - swap it once and
 * every subsystem follows.
 *
 * The worker poll cadence is fixed at 1000 Hz (the tested default); a build can
 * trade latency for idle power with PROTOCORE_WORKER_POLL_TICKS - see protocore_config.h.
 *
 * The installed clocks live in clock.c, one instance for the whole program, so a
 * build that reads the clock links that translation unit.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CLOCK_H
#define PROTOCORE_CLOCK_H

#include "protocore_config.h" // the entry point: PROTOCORE_INLINE, protocore_types.h, and the platform time base

PROTOCORE_BEGIN_DECLS

/** @brief User clock: returns a free-running monotonic tick count. */
typedef uint32_t (*protocore_clock_fn)(void);

/**
 * @brief Install a custom clock running at @p ticks_per_second; the library divides
 *        it down to its internal 1000 Hz. Pass (NULL, 0) to revert to the
 *        platform default.
 */
/** @brief A time source and the rate it counts at. */
typedef struct
{
    protocore_clock_fn fn;     ///< the source; NULL falls back to the platform's own
    uint32_t ticks_per_second; ///< its rate, divided down to the library's
} ClockSrcArgs;

/** @brief The installed clocks and the calls that read them, described only in clock.c. */
struct ClockInternal;

/**
 * @brief The pluggable time source.
 *
 * A caller sets the members a call takes, invokes it through ::Clock, and reads the outcome off the
 * same handle. The installed sources are behind @ref internal.
 *
 * @var ClockNs::src       a time source and the rate it counts at
 * @var ClockNs::ms        milliseconds since boot
 * @var ClockNs::us        microseconds since boot
 * @var ClockNs::cyc       the free-running CPU cycle count
 * @var ClockNs::set_ms    install the millisecond source
 * @var ClockNs::millis    read it, or the platform's when none is installed
 * @var ClockNs::set_us    install the microsecond source
 * @var ClockNs::micros    read it, or the platform's when none is installed
 * @var ClockNs::cycles    read the cycle counter
 * @var ClockNs::internal  the installed sources and the calls that read them
 *
 * The sources live behind the handle rather than in this header because a caller installing a clock
 * and the library reading it are different translation units and have to see the same value.
 */
typedef struct
{
    ClockSrcArgs src;

    uint32_t ms;
    uint32_t us;
    uint32_t cyc;

    void (*set_ms)(struct ClockInternal *ctx);
    void (*millis)(struct ClockInternal *ctx);
    void (*set_us)(struct ClockInternal *ctx);
    void (*micros)(struct ClockInternal *ctx);
    void (*cycles)(struct ClockInternal *ctx);

    struct ClockInternal *internal;
} ClockNs;

/** @brief The one symbol this module exports. */
extern ClockNs Clock;

/** @brief The library's monotonic time at 1000 Hz (milliseconds). */

/**
 * @brief Block for at least @p ms milliseconds - the library's single delay primitive.
 *
 * `src/` never calls the platform `delay()` directly; every wait goes through this so timing stays
 * centralized and portable. On device it yields to the RTOS one tick at a time until @p ms has elapsed on
 * the monotonic clock (so it sleeps the task and feeds the watchdog, never starving the scheduler); on host
 * it spins on the same clock. Measured against ::protocore_millis, so a custom clock governs it too.
 */
PROTOCORE_INLINE void pcdelay(uint32_t ms)
{
#if PROTOCORE_HAS_SCHEDULER
    if (ms == 0)
    {
        protocore_platform_task_delay(0); // a bare cooperative yield
        return;
    }
    Clock.millis(Clock.internal);
    const uint32_t start = Clock.ms;
    for (Clock.millis(Clock.internal); Clock.ms - start < ms; Clock.millis(Clock.internal))
    {
        protocore_platform_task_delay(1); // one tick: sleeps the task (the core can idle) and feeds the watchdog
    }
#else
    Clock.millis(Clock.internal);
    const uint32_t start = Clock.ms;
    for (Clock.millis(Clock.internal); Clock.ms - start < ms; Clock.millis(Clock.internal))
    {
        // Same one-tick hand-off the RTOS arm makes. A host has no tick timer, so its clock only
        // moves when something moves it, and this is what gives the platform that hook: without a
        // call in the loop a driver's wait would spin on a value that never changes.
        protocore_platform_task_delay(1);
    }
#endif
}

// ---------------------------------------------------------------------------
// Microsecond time base (v5 clock-awareness): ISR timestamps + sub-ms latency
// ---------------------------------------------------------------------------
//
// A second, higher-resolution source for real-time work: timestamping a hardware
// event in an ISR and budgeting how long a piece of work takes. Pluggable like the
// millisecond clock; the default is the platform micros() on device, or
// protocore_millis() * 1000 on host (override for sub-ms precision in tests).

/**
 * @brief Install a custom microsecond clock running at @p ticks_per_second; the
 *        library divides it down to 1 MHz. Pass (NULL, 0) for the platform
 *        default.
 */

/**
 * @brief Monotonic microseconds - the high-resolution time base for ISR
 *        timestamps and sub-millisecond latency. Safe to call from an ISR. Wraps
 *        roughly every 71 minutes, so use it only for short deltas (unsigned
 *        subtraction is wrap-safe).
 */

/**
 * @brief Block for at least @p us microseconds of REAL time - a hardware settle.
 *
 * ::pcdelay sleeps the task one RTOS tick at a time and a tick is a millisecond, so it cannot
 * express a shorter wait: asking it for 500 us waits 1 ms.
 *
 * This reads ::protocore_platform_micros, the raw counter, and NOT ::protocore_micros. The library clock is
 * pluggable: an application can install one that runs at its own rate, and a test can install one
 * it steps by hand. A part that needs 500 us to settle needs 500 us of real time, so a clock the
 * application controls cannot be what decides when the wait ends - against a stepped clock this
 * returns at once or never returns. The subtraction is unsigned, so the counter's wrap is safe.
 *
 * This SPINS: it does not yield, and nothing else on the core runs while it does. That holds only
 * where the wait is part of bringing a device up. On the request path it stalls handle() for its
 * whole duration, which the pump's latency budget then records.
 */
PROTOCORE_INLINE void protocore_delay_us(uint32_t us)
{
    uint32_t start = protocore_platform_micros();
    while (protocore_platform_micros() - start < us)
    {
    }
}

// ---------------------------------------------------------------------------
// Latency budgeting: measure an operation against a microsecond budget
// ---------------------------------------------------------------------------

/**
 * @brief Rolling latency statistics in microseconds: sample count, min / max /
 *        mean, and how many samples blew a budget. Fixed size, no heap; a
 *        subsystem (the preempting queue, a DMA path, a forwarding rule) keeps one
 *        and reports it for real-time visibility.
 */
typedef struct
{
    uint32_t count;
    uint32_t over_budget; ///< samples whose latency exceeded the budget
    uint32_t min_us;
    uint32_t max_us;
    uint64_t sum_us;
} protocore_latency_stat;

/** @brief Zero a stat (min seeded high so the first sample sets it). */
PROTOCORE_INLINE void protocore_lat_reset(protocore_latency_stat *s)
{
    s->count = 0;
    s->over_budget = 0;
    s->min_us = 0xFFFFFFFFu;
    s->max_us = 0;
    s->sum_us = 0;
}

/** @brief Start of a measured span: capture the current microsecond time. */
PROTOCORE_INLINE uint32_t protocore_lat_begin(void)
{
    Clock.micros(Clock.internal);
    return Clock.us;
}

/**
 * @brief End of a span started at @p start_us: record its latency, counting it as
 *        over-budget when @p budget_us is non-zero and exceeded. Wrap-safe.
 */
PROTOCORE_INLINE void protocore_lat_end(protocore_latency_stat *s, uint32_t start_us, uint32_t budget_us)
{
    Clock.micros(Clock.internal);
    uint32_t lat = Clock.us - start_us; // wrap-safe unsigned delta
    s->count++;
    s->sum_us += lat;
    if (lat < s->min_us)
    {
        s->min_us = lat;
    }
    if (lat > s->max_us)
    {
        s->max_us = lat;
    }
    if (budget_us && lat > budget_us)
    {
        s->over_budget++;
    }
}

/** @brief Mean latency (us) over the recorded samples, 0 if none. */
PROTOCORE_INLINE uint32_t protocore_lat_avg_us(const protocore_latency_stat *s)
{
    return s->count ? (uint32_t)(s->sum_us / s->count) : 0u;
}

// ---------------------------------------------------------------------------
// CPU cycle counter (v5 clock-awareness): sub-microsecond jitter measurement
// ---------------------------------------------------------------------------
//
// protocore_micros() wraps the platform micros(), which is itself commonly derived from
// a 1 MHz-divided cycle counter - roughly 1 us of quantization noise. That is too
// coarse to characterize a single SPI-DMA transaction: at a 20 MHz SPI clock one
// byte is 400 ns, and a fast external DAQ/scope can complete several DMA transfers
// within one microsecond tick. protocore_cycles() reads the CPU cycle counter directly
// for nanosecond-grade deltas - trigger-to-first-sample latency, inter-transfer jitter -
// the same primitive mmgr/dma and the pentesting rig's cryptobench already reach for ad
// hoc; this gives every subsystem one named, documented entry point instead. Like
// protocore_micros, it wraps (roughly every 18 s at 240 MHz) - use it only for short
// deltas via wrap-safe unsigned subtraction.

/**
 * @brief Free-running CPU cycle count. ISR-safe. On a part that has one this is the
 *        hardware cycle counter; on host it falls back to protocore_micros()
 *        scaled by @p host_fallback_mhz (a coarse stand-in - override with a real
 *        cycle source in a host test that needs nanosecond precision).
 */

/**
 * @brief Convert a cycle-count delta to nanoseconds at @p cpu_mhz (the running CPU
 *        frequency, as the platform reports it). @p delta_cycles must come
 *        from a wrap-safe unsigned subtraction of two protocore_cycles() reads.
 */
PROTOCORE_INLINE uint32_t protocore_cycles_to_ns(uint32_t delta_cycles, uint32_t cpu_mhz)
{
    return cpu_mhz ? (uint32_t)(((uint64_t)delta_cycles * 1000u) / cpu_mhz) : 0u;
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_CLOCK_H
