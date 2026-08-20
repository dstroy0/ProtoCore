// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file telemetry.c
 * @brief The sample aggregators (PROTOCORE_ENABLE_TELEMETRY): the window sums, the first difference,
 *        and the trapezoidal integral.
 *
 * A push folds the new sample into the running sums and folds the evicted one back out, so the mean
 * and the variance read straight off them. An update and an add both take the unsigned difference
 * of the millisecond counts, which subtracts correctly across a rollover.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_TELEMETRY

#include "services/iot/telemetry/telemetry.h"

#include <math.h> // sqrtf: the standard deviation

// Population variance from the running sums, clamped at 0 where rounding drives the difference
// below it. The caller checks count first.
static double window_variance_of(const TelemetryWindow *w)
{
    double mean = w->sum / (double)w->count;
    double var = w->sum_sq / (double)w->count - mean * mean;
    return var < 0.0 ? 0.0 : var;
}

// Bind the caller's sample array to the window and empty it.
void protocore_telemetry_window_init(uint8_t *restrict work)
{
    (void)work;
    TelemetryWindow *w = TelemetryV.window.w;
    TelemetryV.ok = PROTO_FALSE;
    if (!w)
    {
        return;
    }
    w->buf = TelemetryV.window.buf;
    w->cap = TelemetryV.window.cap;
    w->count = 0;
    w->head = 0;
    w->sum = 0.0;
    w->sum_sq = 0.0;
    TelemetryV.ok = PROTO_TRUE;
}

// Store the sample at head and move the sums by it, dropping the sample it overwrites once the
// window is full.
void protocore_telemetry_window_push(uint8_t *restrict work)
{
    (void)work;
    TelemetryWindow *w = TelemetryV.window.w;
    TelemetryV.ok = PROTO_FALSE;
    if (!w || !w->buf || w->cap == 0)
    {
        return;
    }
    if (w->count == w->cap)
    {
        float old = w->buf[w->head];
        w->sum -= (double)old;
        w->sum_sq -= (double)old * (double)old;
    }
    else
    {
        w->count++;
    }
    float sample = TelemetryV.window.sample;
    w->buf[w->head] = sample;
    w->sum += (double)sample;
    w->sum_sq += (double)sample * (double)sample;
    // Advance the write cursor and wrap it at capacity: a compare, not a divide.
    w->head = (uint16_t)(w->head + 1);
    if (w->head >= w->cap)
    {
        w->head = 0;
    }
    TelemetryV.ok = PROTO_TRUE;
}

// The samples the window holds.
void protocore_telemetry_window_count(uint8_t *restrict work)
{
    (void)work;
    const TelemetryWindow *w = TelemetryV.window.w;
    TelemetryV.u16 = 0;
    TelemetryV.ok = PROTO_FALSE;
    if (!w)
    {
        return;
    }
    TelemetryV.u16 = w->count;
    TelemetryV.ok = PROTO_TRUE;
}

// The sum over the count.
void protocore_telemetry_window_mean(uint8_t *restrict work)
{
    (void)work;
    const TelemetryWindow *w = TelemetryV.window.w;
    TelemetryV.f32 = 0.0f;
    TelemetryV.ok = PROTO_FALSE;
    if (!w || w->count == 0)
    {
        return;
    }
    TelemetryV.f32 = (float)(w->sum / (double)w->count);
    TelemetryV.ok = PROTO_TRUE;
}

// The mean of the squares less the square of the mean.
void protocore_telemetry_window_variance(uint8_t *restrict work)
{
    (void)work;
    const TelemetryWindow *w = TelemetryV.window.w;
    TelemetryV.f32 = 0.0f;
    TelemetryV.ok = PROTO_FALSE;
    if (!w || w->count == 0)
    {
        return;
    }
    TelemetryV.f32 = (float)window_variance_of(w);
    TelemetryV.ok = PROTO_TRUE;
}

// The square root of the variance.
void protocore_telemetry_window_stddev(uint8_t *restrict work)
{
    (void)work;
    const TelemetryWindow *w = TelemetryV.window.w;
    TelemetryV.f32 = 0.0f;
    TelemetryV.ok = PROTO_FALSE;
    if (!w || w->count == 0)
    {
        return;
    }
    TelemetryV.f32 = sqrtf((float)window_variance_of(w));
    TelemetryV.ok = PROTO_TRUE;
}

// The smallest of the samples held.
void protocore_telemetry_window_min(uint8_t *restrict work)
{
    (void)work;
    const TelemetryWindow *w = TelemetryV.window.w;
    TelemetryV.f32 = 0.0f;
    TelemetryV.ok = PROTO_FALSE;
    if (!w || w->count == 0 || !w->buf)
    {
        return;
    }
    float m = w->buf[0];
    for (uint16_t i = 1; i < w->count; i++)
    {
        if (w->buf[i] < m)
        {
            m = w->buf[i];
        }
    }
    TelemetryV.f32 = m;
    TelemetryV.ok = PROTO_TRUE;
}

// The largest of the samples held.
void protocore_telemetry_window_max(uint8_t *restrict work)
{
    (void)work;
    const TelemetryWindow *w = TelemetryV.window.w;
    TelemetryV.f32 = 0.0f;
    TelemetryV.ok = PROTO_FALSE;
    if (!w || w->count == 0 || !w->buf)
    {
        return;
    }
    float m = w->buf[0];
    for (uint16_t i = 1; i < w->count; i++)
    {
        if (w->buf[i] > m)
        {
            m = w->buf[i];
        }
    }
    TelemetryV.f32 = m;
    TelemetryV.ok = PROTO_TRUE;
}

// Drop the prior sample, so the next update primes the tracker.
void protocore_telemetry_rate_init(uint8_t *restrict work)
{
    (void)work;
    TelemetryRate *r = TelemetryV.rate.r;
    TelemetryV.ok = PROTO_FALSE;
    if (!r)
    {
        return;
    }
    r->last_value = 0.0f;
    r->last_ms = 0;
    r->primed = PROTO_FALSE;
    TelemetryV.ok = PROTO_TRUE;
}

// The change in value over the elapsed seconds since the previous sample. The first sample and a
// zero elapsed time both report 0.
void protocore_telemetry_rate_update(uint8_t *restrict work)
{
    (void)work;
    TelemetryRate *r = TelemetryV.rate.r;
    TelemetryV.f32 = 0.0f;
    TelemetryV.ok = PROTO_FALSE;
    if (!r)
    {
        return;
    }
    TelemetryV.ok = PROTO_TRUE;
    float value = TelemetryV.rate.value;
    uint32_t now_ms = TelemetryV.rate.now_ms;
    if (!r->primed)
    {
        r->last_value = value;
        r->last_ms = now_ms;
        r->primed = PROTO_TRUE;
        return;
    }
    uint32_t dt_ms = (uint32_t)(now_ms - r->last_ms); // wraps correctly
    if (dt_ms != 0)
    {
        TelemetryV.f32 = (value - r->last_value) * 1000.0f / (float)dt_ms;
    }
    r->last_value = value;
    r->last_ms = now_ms;
}

// Zero the total and drop the prior rate sample.
void protocore_telemetry_totalizer_init(uint8_t *restrict work)
{
    (void)work;
    TelemetryTotalizer *t = TelemetryV.totalizer.t;
    TelemetryV.ok = PROTO_FALSE;
    if (!t)
    {
        return;
    }
    t->total = 0.0;
    t->last_rate = 0.0f;
    t->last_ms = 0;
    t->primed = PROTO_FALSE;
    TelemetryV.ok = PROTO_TRUE;
}

// Add the mean of the two rate endpoints multiplied by the elapsed seconds. The first sample only
// seeds the endpoint.
void protocore_telemetry_totalizer_add(uint8_t *restrict work)
{
    (void)work;
    TelemetryTotalizer *t = TelemetryV.totalizer.t;
    TelemetryV.f64 = 0.0;
    TelemetryV.ok = PROTO_FALSE;
    if (!t)
    {
        return;
    }
    TelemetryV.ok = PROTO_TRUE;
    float rate = TelemetryV.totalizer.rate;
    uint32_t now_ms = TelemetryV.totalizer.now_ms;
    if (!t->primed)
    {
        t->last_rate = rate;
        t->last_ms = now_ms;
        t->primed = PROTO_TRUE;
        TelemetryV.f64 = t->total;
        return;
    }
    uint32_t dt_ms = (uint32_t)(now_ms - t->last_ms); // wraps correctly
    double dt_s = (double)dt_ms / 1000.0;
    t->total += ((double)t->last_rate + (double)rate) * 0.5 * dt_s;
    t->last_rate = rate;
    t->last_ms = now_ms;
    TelemetryV.f64 = t->total;
}

// The running total, in rate units multiplied by seconds.
void protocore_telemetry_totalizer_total(uint8_t *restrict work)
{
    (void)work;
    const TelemetryTotalizer *t = TelemetryV.totalizer.t;
    TelemetryV.f64 = 0.0;
    TelemetryV.ok = PROTO_FALSE;
    if (!t)
    {
        return;
    }
    TelemetryV.f64 = t->total;
    TelemetryV.ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to. A reset zeroes
// the total and drops the prior rate sample, which is the whole of an init, so both names bind to
// the one function.
/** @brief The operands and the outcome. */
TelemetryVars TelemetryV;

#endif // PROTOCORE_ENABLE_TELEMETRY
