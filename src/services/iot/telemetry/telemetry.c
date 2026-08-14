// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "services/iot/telemetry/telemetry.h"

#if PROTOCORE_ENABLE_TELEMETRY

#include <math.h> // sqrtf: the standard deviation

/**
 * @brief The aggregator's calls - what TelemetryNs points at.
 *
 * No storage member: every accumulator, and the window's sample array, belongs to the caller, so
 * the module holds nothing of its own.
 *
 * @var TelemetryInternal::ns  the handle a caller sets a call's members on
 */
struct TelemetryInternal
{
    TelemetryNs *ns;
};

static struct TelemetryInternal s_telemetry = {.ns = &Telemetry};

// Population variance from the running sums, clamped at 0 where rounding drives the difference
// below it. The caller checks count first.
static double window_variance_of(const TelemetryWindow *w)
{
    double mean = w->sum / (double)w->count;
    double var = w->sum_sq / (double)w->count - mean * mean;
    return var < 0.0 ? 0.0 : var;
}

// Bind the caller's sample array to the window and empty it.
static void telemetry_window_init(struct TelemetryInternal *restrict ctx)
{
    TelemetryWindow *w = ctx->ns->window.w;
    ctx->ns->ok = PROTO_FALSE;
    if (!w)
    {
        return;
    }
    w->buf = ctx->ns->window.buf;
    w->cap = ctx->ns->window.cap;
    w->count = 0;
    w->head = 0;
    w->sum = 0.0;
    w->sum_sq = 0.0;
    ctx->ns->ok = PROTO_TRUE;
}

// Store the sample at head and move the sums by it, dropping the sample it overwrites once the
// window is full.
static void telemetry_window_push(struct TelemetryInternal *restrict ctx)
{
    TelemetryWindow *w = ctx->ns->window.w;
    ctx->ns->ok = PROTO_FALSE;
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
    float sample = ctx->ns->window.sample;
    w->buf[w->head] = sample;
    w->sum += (double)sample;
    w->sum_sq += (double)sample * (double)sample;
    // Advance the write cursor and wrap it at capacity: a compare, not a divide.
    w->head = (uint16_t)(w->head + 1);
    if (w->head >= w->cap)
    {
        w->head = 0;
    }
    ctx->ns->ok = PROTO_TRUE;
}

// The samples the window holds.
static void telemetry_window_count(struct TelemetryInternal *restrict ctx)
{
    const TelemetryWindow *w = ctx->ns->window.w;
    ctx->ns->u16 = 0;
    ctx->ns->ok = PROTO_FALSE;
    if (!w)
    {
        return;
    }
    ctx->ns->u16 = w->count;
    ctx->ns->ok = PROTO_TRUE;
}

// The sum over the count.
static void telemetry_window_mean(struct TelemetryInternal *restrict ctx)
{
    const TelemetryWindow *w = ctx->ns->window.w;
    ctx->ns->f32 = 0.0f;
    ctx->ns->ok = PROTO_FALSE;
    if (!w || w->count == 0)
    {
        return;
    }
    ctx->ns->f32 = (float)(w->sum / (double)w->count);
    ctx->ns->ok = PROTO_TRUE;
}

// The mean of the squares less the square of the mean.
static void telemetry_window_variance(struct TelemetryInternal *restrict ctx)
{
    const TelemetryWindow *w = ctx->ns->window.w;
    ctx->ns->f32 = 0.0f;
    ctx->ns->ok = PROTO_FALSE;
    if (!w || w->count == 0)
    {
        return;
    }
    ctx->ns->f32 = (float)window_variance_of(w);
    ctx->ns->ok = PROTO_TRUE;
}

// The square root of the variance.
static void telemetry_window_stddev(struct TelemetryInternal *restrict ctx)
{
    const TelemetryWindow *w = ctx->ns->window.w;
    ctx->ns->f32 = 0.0f;
    ctx->ns->ok = PROTO_FALSE;
    if (!w || w->count == 0)
    {
        return;
    }
    ctx->ns->f32 = sqrtf((float)window_variance_of(w));
    ctx->ns->ok = PROTO_TRUE;
}

// The smallest of the samples held.
static void telemetry_window_min(struct TelemetryInternal *restrict ctx)
{
    const TelemetryWindow *w = ctx->ns->window.w;
    ctx->ns->f32 = 0.0f;
    ctx->ns->ok = PROTO_FALSE;
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
    ctx->ns->f32 = m;
    ctx->ns->ok = PROTO_TRUE;
}

// The largest of the samples held.
static void telemetry_window_max(struct TelemetryInternal *restrict ctx)
{
    const TelemetryWindow *w = ctx->ns->window.w;
    ctx->ns->f32 = 0.0f;
    ctx->ns->ok = PROTO_FALSE;
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
    ctx->ns->f32 = m;
    ctx->ns->ok = PROTO_TRUE;
}

// Drop the prior sample, so the next update primes the tracker.
static void telemetry_rate_init(struct TelemetryInternal *restrict ctx)
{
    TelemetryRate *r = ctx->ns->rate.r;
    ctx->ns->ok = PROTO_FALSE;
    if (!r)
    {
        return;
    }
    r->last_value = 0.0f;
    r->last_ms = 0;
    r->primed = PROTO_FALSE;
    ctx->ns->ok = PROTO_TRUE;
}

// The change in value over the elapsed seconds since the previous sample. The first sample and a
// zero elapsed time both report 0.
static void telemetry_rate_update(struct TelemetryInternal *restrict ctx)
{
    TelemetryRate *r = ctx->ns->rate.r;
    ctx->ns->f32 = 0.0f;
    ctx->ns->ok = PROTO_FALSE;
    if (!r)
    {
        return;
    }
    ctx->ns->ok = PROTO_TRUE;
    float value = ctx->ns->rate.value;
    uint32_t now_ms = ctx->ns->rate.now_ms;
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
        ctx->ns->f32 = (value - r->last_value) * 1000.0f / (float)dt_ms;
    }
    r->last_value = value;
    r->last_ms = now_ms;
}

// Zero the total and drop the prior rate sample.
static void telemetry_totalizer_init(struct TelemetryInternal *restrict ctx)
{
    TelemetryTotalizer *t = ctx->ns->totalizer.t;
    ctx->ns->ok = PROTO_FALSE;
    if (!t)
    {
        return;
    }
    t->total = 0.0;
    t->last_rate = 0.0f;
    t->last_ms = 0;
    t->primed = PROTO_FALSE;
    ctx->ns->ok = PROTO_TRUE;
}

// Add the mean of the two rate endpoints multiplied by the elapsed seconds. The first sample only
// seeds the endpoint.
static void telemetry_totalizer_add(struct TelemetryInternal *restrict ctx)
{
    TelemetryTotalizer *t = ctx->ns->totalizer.t;
    ctx->ns->f64 = 0.0;
    ctx->ns->ok = PROTO_FALSE;
    if (!t)
    {
        return;
    }
    ctx->ns->ok = PROTO_TRUE;
    float rate = ctx->ns->totalizer.rate;
    uint32_t now_ms = ctx->ns->totalizer.now_ms;
    if (!t->primed)
    {
        t->last_rate = rate;
        t->last_ms = now_ms;
        t->primed = PROTO_TRUE;
        ctx->ns->f64 = t->total;
        return;
    }
    uint32_t dt_ms = (uint32_t)(now_ms - t->last_ms); // wraps correctly
    double dt_s = (double)dt_ms / 1000.0;
    t->total += ((double)t->last_rate + (double)rate) * 0.5 * dt_s;
    t->last_rate = rate;
    t->last_ms = now_ms;
    ctx->ns->f64 = t->total;
}

// The running total, in rate units multiplied by seconds.
static void telemetry_totalizer_total(struct TelemetryInternal *restrict ctx)
{
    const TelemetryTotalizer *t = ctx->ns->totalizer.t;
    ctx->ns->f64 = 0.0;
    ctx->ns->ok = PROTO_FALSE;
    if (!t)
    {
        return;
    }
    ctx->ns->f64 = t->total;
    ctx->ns->ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to. A reset zeroes
// the total and drops the prior rate sample, which is the whole of an init, so both names bind to
// the one function.
TelemetryNs Telemetry = {.window_init = telemetry_window_init,
                         .window_push = telemetry_window_push,
                         .window_count = telemetry_window_count,
                         .window_mean = telemetry_window_mean,
                         .window_variance = telemetry_window_variance,
                         .window_stddev = telemetry_window_stddev,
                         .window_min = telemetry_window_min,
                         .window_max = telemetry_window_max,
                         .rate_init = telemetry_rate_init,
                         .rate_update = telemetry_rate_update,
                         .totalizer_init = telemetry_totalizer_init,
                         .totalizer_add = telemetry_totalizer_add,
                         .totalizer_total = telemetry_totalizer_total,
                         .totalizer_reset = telemetry_totalizer_init,
                         .internal = &s_telemetry};

#endif // PROTOCORE_ENABLE_TELEMETRY
