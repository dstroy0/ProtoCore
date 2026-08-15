// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the StatsD metrics client (services/iot/statsd):
// Statsd.format builds one `name:value|type|@rate|#tags` line into a caller buffer - the
// per-metric hot op before the UDP send. Pure; no socket.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/statsd -t upload --upload-port COM7
#include "device_bench.h"
#include "services/iot/statsd/statsd.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Format one sampled, tagged counter line into @p out; the octets written. */
static size_t statsd_counter_line(char *out, size_t cap)
{
    Statsd.line.out = out;
    Statsd.line.cap = cap;
    Statsd.metric.name = "api.requests";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 0.1f;
    Statsd.value.text = "1";
    Statsd.tags.metric = "env:prod,host:pc-rig";
    Statsd.format(Statsd.internal);
    return Statsd.n;
}

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("statsd");
        volatile size_t sink = 0;
        static char out[256];
        DBENCH_OP("Statsd.format (counter+tags)", 200000, sink += statsd_counter_line(out, sizeof(out)));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("statsd")
