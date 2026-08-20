// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file telemetry.h
 * @brief Zero-heap sample aggregation (PROTOCORE_ENABLE_TELEMETRY): a moving window, a rate, a totalizer.
 *
 * No external specification governs this module. The three accumulators are ordinary descriptive
 * statistics and numeric integration over caller-owned records, and every name here is this
 * library's own.
 *
 *   - ::TelemetryWindow     the arithmetic mean, the population variance, the standard deviation,
 *                           the minimum and the maximum of the last @c cap samples. The mean and
 *                           the variance come off running sums, so a push and either read are each
 *                           a fixed number of operations; the minimum and the maximum scan the
 *                           samples held.
 *   - ::TelemetryRate       the first difference between successive samples, in units per second.
 *   - ::TelemetryTotalizer  the trapezoidal integral of a rate over time. Its value is the quantity
 *                           SenML names Sum (RFC 8428 sec 4.2, label "s" in sec 4.3 Table 1): the
 *                           integrated sum of the values over time, in the Unit multiplied by
 *                           seconds.
 *
 * The caller owns every byte: the window's sample array is a float array it passes in, and the
 * three accumulator records are its own. Nothing here allocates, and the module keeps no file-scope
 * state.
 *
 * Elapsed time is the unsigned difference of a monotonic millisecond count, so a counter rollover
 * subtracts correctly.
 *
 * The module exports one symbol, @ref Telemetry. Everything in telemetry.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TELEMETRY_H
#define PROTOCORE_TELEMETRY_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_TELEMETRY

PROTOCORE_BEGIN_DECLS

/**
 * @brief A moving window over the last @c cap samples, in storage the caller provides.
 *
 * The running sums carry the mean and the variance; the sample array carries the minimum and the
 * maximum. The write cursor wraps at @c cap, so the array holds the newest @c count samples.
 */
typedef struct
{
    float *buf;     ///< the sample storage the caller bound, cap floats or more
    uint16_t cap;   ///< how many samples the window holds
    uint16_t count; ///< how many it holds now, at most cap
    uint16_t head;  ///< the next write index, and the oldest sample once full
    double sum;     ///< the running sum of the samples held
    double sum_sq;  ///< the running sum of their squares
} TelemetryWindow;

/** @brief The first difference between successive samples, in units per second. */
typedef struct
{
    float last_value;  ///< the previous sample
    uint32_t last_ms;  ///< the monotonic millisecond count it arrived at
    proto_bool primed; ///< a first sample has been seen
} TelemetryRate;

/** @brief The trapezoidal integral of a rate over time: SenML's Sum (RFC 8428 sec 4.2). */
typedef struct
{
    double total;      ///< the accumulated total, in rate units multiplied by seconds
    float last_rate;   ///< the previous rate sample
    uint32_t last_ms;  ///< the monotonic millisecond count it arrived at
    proto_bool primed; ///< a first rate sample has been seen
} TelemetryTotalizer;

/** @brief The window a call acts on, the storage an init binds to it, and the sample a push adds. */
typedef struct
{
    TelemetryWindow *w; ///< the accumulator every window call names
    float *buf;         ///< the sample storage an init binds
    uint16_t cap;       ///< how many samples that storage holds
    float sample;       ///< the sample a push adds
} TelemetryWindowArgs;

/** @brief The rate tracker a call acts on, and the timed sample an update differentiates. */
typedef struct
{
    TelemetryRate *r; ///< the accumulator every rate call names
    float value;      ///< the sample an update differentiates
    uint32_t now_ms;  ///< the monotonic millisecond count it arrived at
} TelemetryRateArgs;

/** @brief The totalizer a call acts on, and the timed rate an add integrates. */
typedef struct
{
    TelemetryTotalizer *t; ///< the accumulator every totalizer call names
    float rate;            ///< the rate an add integrates, in units per second
    uint32_t now_ms;       ///< the monotonic millisecond count it arrived at
} TelemetryTotalizerArgs;

/**
 * @brief The sample aggregators: a moving window, a rate of change, and a totalizer.
 *
 * A caller sets the members a call takes, invokes it through ::Telemetry, and reads the outcome off
 * the same handle.
 *
 * No slot member: each call names its own caller-owned accumulator through the sub-struct of its
 * concern, so no call names a row.
 *
 * @var TelemetryNs::window            the window a call acts on, and what an init or a push feeds it
 * @var TelemetryNs::rate              the rate tracker a call acts on, and what an update feeds it
 * @var TelemetryNs::totalizer         the totalizer a call acts on, and what an add feeds it
 * @var TelemetryNs::ok                true when the call reached a bound accumulator; a window
 *                                     statistic also needs one sample held
 * @var TelemetryNs::u16               a call's unsigned count outcome
 * @var TelemetryNs::f32               a call's single-precision outcome
 * @var TelemetryNs::f64               a call's double-precision outcome
 * @var TelemetryNs::window_init       bind @c window.buf and @c window.cap to @c window.w and empty it
 * @var TelemetryNs::window_push       add @c window.sample, evicting the oldest sample once full
 * @var TelemetryNs::window_count      the samples held, into @c u16
 * @var TelemetryNs::window_mean       their arithmetic mean, into @c f32
 * @var TelemetryNs::window_variance   their population variance, into @c f32
 * @var TelemetryNs::window_stddev     the square root of that variance, into @c f32
 * @var TelemetryNs::window_min        the smallest sample held, into @c f32
 * @var TelemetryNs::window_max        the largest sample held, into @c f32
 * @var TelemetryNs::rate_init         drop the prior sample, so the next update primes @c rate.r
 * @var TelemetryNs::rate_update       feed @c rate.value at @c rate.now_ms and report the change per
 *                                     second since the previous sample, into @c f32
 * @var TelemetryNs::totalizer_init    zero @c totalizer.t and drop its prior rate sample
 * @var TelemetryNs::totalizer_add     integrate @c totalizer.rate from the previous sample to
 *                                     @c totalizer.now_ms by the trapezoidal rule, into @c f64
 * @var TelemetryNs::totalizer_total   the running total, into @c f64 (SenML Sum, RFC 8428 sec 4.2)
 * @var TelemetryNs::totalizer_reset   zero the running total and drop the prior rate sample
 */
typedef struct
{
    TelemetryWindowArgs window;       ///< what a window call acts on and reads
    TelemetryRateArgs rate;           ///< what a rate call acts on and reads
    TelemetryTotalizerArgs totalizer; ///< what a totalizer call acts on and reads

    proto_bool ok;
    uint16_t u16;
    float f32;
    double f64;

    void (*const window_init)(uint8_t *restrict work);
    void (*const window_push)(uint8_t *restrict work);
    void (*const window_count)(uint8_t *restrict work);
    void (*const window_mean)(uint8_t *restrict work);
    void (*const window_variance)(uint8_t *restrict work);
    void (*const window_stddev)(uint8_t *restrict work);
    void (*const window_min)(uint8_t *restrict work);
    void (*const window_max)(uint8_t *restrict work);
    void (*const rate_init)(uint8_t *restrict work);
    void (*const rate_update)(uint8_t *restrict work);
    void (*const totalizer_init)(uint8_t *restrict work);
    void (*const totalizer_add)(uint8_t *restrict work);
    void (*const totalizer_total)(uint8_t *restrict work);
    void (*const totalizer_reset)(uint8_t *restrict work);
} TelemetryNs;

/** @brief The one symbol this module exports. */
extern TelemetryNs Telemetry;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_TELEMETRY

#endif // PROTOCORE_TELEMETRY_H
