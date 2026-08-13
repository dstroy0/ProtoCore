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

void dbench_run(void)
{
    static float wbuf[32];
    static protocore_window win;
    static protocore_rate rate;
    static protocore_totalizer tot;

    for (;;)
    {
        DBENCH_BANNER("telemetry");
        volatile float sink = 0;
        protocore_window_init(&win, wbuf, 32);
        for (int i = 0; i < 32; i++)
        {
            protocore_window_push(&win, (float)(i % 7) + 0.5f);
        }
        DBENCH_OP("protocore_window_push", 200000, protocore_window_push(&win, (float)(sink)));
        DBENCH_OP("protocore_window_mean", 200000, sink += protocore_window_mean(&win));
        DBENCH_OP("protocore_window_variance", 200000, sink += protocore_window_variance(&win));
        protocore_rate_init(&rate);
        uint32_t t = 0;
        DBENCH_OP("protocore_rate_update", 200000, {
            sink += protocore_rate_update(&rate, sink + 1.0f, t);
            t += 10;
        });
        protocore_totalizer_init(&tot);
        t = 0;
        DBENCH_OP("protocore_totalizer_add", 200000, {
            sink += (float)protocore_totalizer_add(&tot, 2.5f, t);
            t += 10;
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("telemetry")
