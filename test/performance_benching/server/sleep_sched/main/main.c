// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the sleep scheduler (server/sleep_sched): protocore_sleep_next()
// computes the next light-sleep window from the idle streak (0 while busy, then a window ramped
// between min and max). Pure wrap-safe integer math; the actual esp_light_sleep call is elsewhere.
//
// Build/flash:  idf.py -C test/performance_benching/sleep_sched -t upload --upload-port COM7
#include "device_bench.h"
#include "server/core/sleep_sched.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    static const protocore_sleep_cfg cfg = {30000, 100, 8000, 2000}; // idle_ms, min_ms, max_ms, ramp_ms

    for (;;)
    {
        DBENCH_BANNER("sleep_sched");
        volatile uint32_t sink = 0;
        uint32_t now = 100000;
        // Sweep the idle streak so the ramp/clamp branches are all exercised.
        DBENCH_OP("protocore_sleep_next", 200000, {
            sink += protocore_sleep_next(now, now - (sink & 0xFFFF), &cfg);
            now += 7;
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("sleep_sched")
