// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the StatsD metrics client (services/iot/statsd):
// protocore_statsd_format() builds one `name:value|type|@rate|#tags` line into a caller buffer - the
// per-metric hot op before the UDP send. Pure; no socket.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/statsd -t upload --upload-port COM7
#include "device_bench.h"
#include "services/iot/statsd/statsd.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("statsd");
        volatile size_t sink = 0;
        static char out[256];
        DBENCH_OP("protocore_statsd_format (counter+tags)", 200000,
                  sink += protocore_statsd_format(out, sizeof(out), "api.requests", "1", STATSD_COUNTER, 0.1f,
                                                  "env:prod,host:pc-rig"));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("statsd")
