// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the pluggable time source (services/timing_position/time_source): the
// priority-resolved current-time read (protocore_time_now) and the RFC 7231 IMF-fixdate HTTP-date
// formatter (HttpClock.date). Pure; a fixed in-memory source stands in for a real clock so the
// figures are deterministic.
//
// Build/flash:  idf.py -C test/performance_benching/time_source -t upload --upload-port COM7
#include "device_bench.h"
#include "services/timing_position/time_source/time_source.h"

#include "server/io/http_clock/http_clock.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint32_t fixed_epoch(void)
{
    return 1720700000u; // 2024-07-11T12:13:20Z, a stable value for the formatter bench
}

void dbench_run(void)
{
    protocore_time_source_reset();
    protocore_time_source_add("bench", 10, fixed_epoch);

    for (;;)
    {
        DBENCH_BANNER("time_source");
        volatile size_t sink = 0;
        DBENCH_OP("protocore_time_now (resolve source)", 200000, sink += protocore_time_now());
        static char out[40];
        DBENCH_OP("HttpClock.date (IMF-fixdate)", 200000,
                  (HttpClock.date(protocore_http_clock_span()), sink += HttpClockV.n));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("time_source")
