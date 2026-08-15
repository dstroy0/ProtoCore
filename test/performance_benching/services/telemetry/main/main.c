// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the telemetry primitives (services/iot/telemetry): the rolling
// window statistics (push + mean/variance), the exponential rate estimator, and the trapezoidal
// totalizer. Pure float math; these run per sample on an ingest path.
//
// Build/flash:  idf.py -C test/performance_benching/telemetry -t upload --upload-port COM7
#include "device_bench.h"
#include "services/iot/telemetry/telemetry.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Bind @p buf as @p w's sample storage and empty it. */
static void window_init(TelemetryWindow *w, float *buf, uint16_t cap)
{
    Telemetry.window.w = w;
    Telemetry.window.buf = buf;
    Telemetry.window.cap = cap;
    Telemetry.window_init(Telemetry.internal);
}

/** @brief Add @p s to @p w, evicting the oldest sample once full. */
static void window_push(TelemetryWindow *w, float s)
{
    Telemetry.window.w = w;
    Telemetry.window.sample = s;
    Telemetry.window_push(Telemetry.internal);
}

/** @brief The arithmetic mean of the samples @p w holds. */
static float window_mean(TelemetryWindow *w)
{
    Telemetry.window.w = w;
    Telemetry.window_mean(Telemetry.internal);
    return Telemetry.f32;
}

/** @brief The population variance of the samples @p w holds. */
static float window_variance(TelemetryWindow *w)
{
    Telemetry.window.w = w;
    Telemetry.window_variance(Telemetry.internal);
    return Telemetry.f32;
}

/** @brief Drop @p r's prior sample, so the next update primes it. */
static void rate_init(TelemetryRate *r)
{
    Telemetry.rate.r = r;
    Telemetry.rate_init(Telemetry.internal);
}

/** @brief Feed @p v at @p now_ms; the change per second since the previous sample. */
static float rate_update(TelemetryRate *r, float v, uint32_t now_ms)
{
    Telemetry.rate.r = r;
    Telemetry.rate.value = v;
    Telemetry.rate.now_ms = now_ms;
    Telemetry.rate_update(Telemetry.internal);
    return Telemetry.f32;
}

/** @brief Zero @p t and drop its prior rate sample. */
static void totalizer_init(TelemetryTotalizer *t)
{
    Telemetry.totalizer.t = t;
    Telemetry.totalizer_init(Telemetry.internal);
}

/** @brief Integrate @p rate up to @p now_ms by the trapezoidal rule; the running total. */
static double totalizer_add(TelemetryTotalizer *t, float rate, uint32_t now_ms)
{
    Telemetry.totalizer.t = t;
    Telemetry.totalizer.rate = rate;
    Telemetry.totalizer.now_ms = now_ms;
    Telemetry.totalizer_add(Telemetry.internal);
    return Telemetry.f64;
}

void dbench_run(void)
{
    static float wbuf[32];
    static TelemetryWindow win;
    static TelemetryRate rate;
    static TelemetryTotalizer tot;

    for (;;)
    {
        DBENCH_BANNER("telemetry");
        volatile float sink = 0;
        window_init(&win, wbuf, 32);
        for (int i = 0; i < 32; i++)
        {
            window_push(&win, (float)(i % 7) + 0.5f);
        }
        DBENCH_OP("Telemetry.window_push", 200000, window_push(&win, (float)(sink)));
        DBENCH_OP("Telemetry.window_mean", 200000, sink += window_mean(&win));
        DBENCH_OP("Telemetry.window_variance", 200000, sink += window_variance(&win));
        rate_init(&rate);
        uint32_t t = 0;
        DBENCH_OP("Telemetry.rate_update", 200000, {
            sink += rate_update(&rate, sink + 1.0f, t);
            t += 10;
        });
        totalizer_init(&tot);
        t = 0;
        DBENCH_OP("Telemetry.totalizer_add", 200000, {
            sink += (float)totalizer_add(&tot, 2.5f, t);
            t += 10;
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("telemetry")
