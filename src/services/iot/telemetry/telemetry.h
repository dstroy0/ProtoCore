// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file telemetry.h
 * @brief Zero-heap telemetry math helpers (PROTOCORE_ENABLE_TELEMETRY).
 *
 * Pure-computation building blocks for turning a stream of sensor samples into
 * dashboard figures, alert triggers, and odometer-style totals - no heap, no
 * Arduino dependency, all state in caller-supplied storage or small POD structs,
 * so the whole cluster unit-tests on the host:
 *
 *   - protocore_window    moving-window statistics (mean / variance / stddev / min /
 *                    max) over a caller-provided ring buffer, O(1) mean/variance
 *                    via running sums.
 *   - protocore_rate      derivative / rate-of-change between successive samples
 *                    (units per second), for slope alerts.
 *   - protocore_totalizer trapezoidal integration of a rate over time (a running
 *                    total / odometer).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TELEMETRY_H
#define PROTOCORE_TELEMETRY_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_TELEMETRY

// ---------------------------------------------------------------------------
// Moving-window statistics
// ---------------------------------------------------------------------------

/**
 * @brief Moving-window stats accumulator over a caller-provided ring buffer.
 *
 * The caller owns the `float` storage (no heap). Mean and variance are kept O(1)
 * via running sums; min/max are an O(window) scan.
 */
typedef struct
{
    float *buf;     ///< caller-provided sample storage (>= cap floats).
    uint16_t cap;   ///< window capacity (samples).
    uint16_t count; ///< samples currently held (<= cap).
    uint16_t head;  ///< next write index (oldest sample when full).
    double sum;     ///< running sum of held samples.
    double sum_sq;  ///< running sum of squares of held samples.
} protocore_window;

/** @brief Bind @p w to @p buf (capacity @p cap samples) and reset it to empty. */
void protocore_window_init(protocore_window *w, float *buf, uint16_t cap);

/** @brief Add @p sample, evicting the oldest once the window is full. */
void protocore_window_push(protocore_window *w, float sample);

/** @brief Number of samples currently in the window. */
uint16_t protocore_window_count(const protocore_window *w);

/** @brief Arithmetic mean of the window (0 when empty). */
float protocore_window_mean(const protocore_window *w);

/** @brief Population variance of the window (0 when empty). */
float protocore_window_variance(const protocore_window *w);

/** @brief Population standard deviation of the window (0 when empty). */
float protocore_window_stddev(const protocore_window *w);

/** @brief Smallest sample in the window (0 when empty). */
float protocore_window_min(const protocore_window *w);

/** @brief Largest sample in the window (0 when empty). */
float protocore_window_max(const protocore_window *w);

// ---------------------------------------------------------------------------
// Rate of change (first derivative)
// ---------------------------------------------------------------------------

/** @brief Derivative / rate-of-change tracker between successive samples. */
typedef struct
{
    float last_value;  ///< previous sample value.
    uint32_t last_ms;  ///< millis() of the previous sample.
    proto_bool primed; ///< false until the first sample is seen.
} protocore_rate;

/** @brief Reset @p r so the next sample is treated as the first. */
void protocore_rate_init(protocore_rate *r);

/**
 * @brief Feed a sample; returns the rate of change in units per second since the
 *        previous sample.
 *
 * Returns 0 on the first sample (nothing to differentiate) and when the elapsed
 * time is 0. The elapsed-time math is unsigned, so it survives a millis()
 * rollover.
 */
float protocore_rate_update(protocore_rate *r, float value, uint32_t now_ms);

// ---------------------------------------------------------------------------
// Totalizer (run-time integral / odometer)
// ---------------------------------------------------------------------------

/** @brief Running total from trapezoidal integration of a rate over time. */
typedef struct
{
    double total;      ///< accumulated total (in rate-units * seconds).
    float last_rate;   ///< previous rate sample.
    uint32_t last_ms;  ///< millis() of the previous rate sample.
    proto_bool primed; ///< false until the first rate sample is seen.
} protocore_totalizer;

/** @brief Reset @p t to a zero total with no prior sample. */
void protocore_totalizer_init(protocore_totalizer *t);

/**
 * @brief Integrate @p rate (units per second) over the time since the last call
 *        (trapezoidal rule) and return the running total.
 *
 * The first call only seeds the baseline (total stays 0). Unsigned elapsed-time
 * math survives a millis() rollover.
 */
double protocore_totalizer_add(protocore_totalizer *t, float rate, uint32_t now_ms);

/** @brief Current running total. */
double protocore_totalizer_total(const protocore_totalizer *t);

/** @brief Reset the running total to 0 and drop the prior sample. */
void protocore_totalizer_reset(protocore_totalizer *t);

#endif // PROTOCORE_ENABLE_TELEMETRY

PROTOCORE_END_DECLS

#endif // PROTOCORE_TELEMETRY_H
