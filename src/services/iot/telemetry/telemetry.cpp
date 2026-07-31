// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file telemetry.cpp
 * @brief Telemetry math helpers implementation (PC_ENABLE_TELEMETRY).
 */

#include "telemetry.h"

#if PC_ENABLE_TELEMETRY

#include <math.h>

void pc_window_init(pc_window *w, float *buf, uint16_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->count = 0;
    w->head = 0;
    w->sum = 0.0;
    w->sum_sq = 0.0;
}

void pc_window_push(pc_window *w, float sample)
{
    if (!w->buf || w->cap == 0)
    {
        return;
    }
    if (w->count == w->cap)
    {
        // Full: evict the oldest sample (at head) from the running sums.
        float old = w->buf[w->head];
        w->sum -= (double)old;
        w->sum_sq -= (double)old * (double)old;
    }
    else
    {
        w->count++;
    }
    w->buf[w->head] = sample;
    w->sum += (double)sample;
    w->sum_sq += (double)sample * (double)sample;
    w->head = (uint16_t)((w->head + 1) % w->cap);
}

uint16_t pc_window_count(const pc_window *w)
{
    return w->count;
}

float pc_window_mean(const pc_window *w)
{
    if (w->count == 0)
    {
        return 0.0f;
    }
    return (float)(w->sum / (double)w->count);
}

float pc_window_variance(const pc_window *w)
{
    if (w->count == 0)
    {
        return 0.0f;
    }
    double mean = w->sum / (double)w->count;
    double var = w->sum_sq / (double)w->count - mean * mean;
    return var < 0.0 ? 0.0f : (float)var; // clamp tiny negatives from rounding
}

float pc_window_stddev(const pc_window *w)
{
    return sqrtf(pc_window_variance(w));
}

float pc_window_min(const pc_window *w)
{
    if (w->count == 0)
    {
        return 0.0f;
    }
    float m = w->buf[0];
    for (uint16_t i = 1; i < w->count; i++)
    {
        if (w->buf[i] < m)
        {
            m = w->buf[i];
        }
    }
    return m;
}

float pc_window_max(const pc_window *w)
{
    if (w->count == 0)
    {
        return 0.0f;
    }
    float m = w->buf[0];
    for (uint16_t i = 1; i < w->count; i++)
    {
        if (w->buf[i] > m)
        {
            m = w->buf[i];
        }
    }
    return m;
}

void pc_rate_init(pc_rate *r)
{
    r->last_value = 0.0f;
    r->last_ms = 0;
    r->primed = false;
}

float pc_rate_update(pc_rate *r, float value, uint32_t now_ms)
{
    if (!r->primed)
    {
        r->last_value = value;
        r->last_ms = now_ms;
        r->primed = true;
        return 0.0f;
    }
    uint32_t dt_ms = (uint32_t)(now_ms - r->last_ms); // wraps correctly
    float rate = 0.0f;
    if (dt_ms != 0)
    {
        rate = (value - r->last_value) * 1000.0f / (float)dt_ms;
    }
    r->last_value = value;
    r->last_ms = now_ms;
    return rate;
}

void pc_totalizer_init(pc_totalizer *t)
{
    t->total = 0.0;
    t->last_rate = 0.0f;
    t->last_ms = 0;
    t->primed = false;
}

double pc_totalizer_add(pc_totalizer *t, float rate, uint32_t now_ms)
{
    if (!t->primed)
    {
        t->last_rate = rate;
        t->last_ms = now_ms;
        t->primed = true;
        return t->total;
    }
    uint32_t dt_ms = (uint32_t)(now_ms - t->last_ms); // wraps correctly
    double dt_s = (double)dt_ms / 1000.0;
    // Trapezoidal: average of the two rate endpoints over the interval.
    t->total += ((double)t->last_rate + (double)rate) * 0.5 * dt_s;
    t->last_rate = rate;
    t->last_ms = now_ms;
    return t->total;
}

double pc_totalizer_total(const pc_totalizer *t)
{
    return t->total;
}

void pc_totalizer_reset(pc_totalizer *t)
{
    t->total = 0.0;
    t->last_rate = 0.0f;
    t->last_ms = 0;
    t->primed = false;
}

#endif // PC_ENABLE_TELEMETRY
