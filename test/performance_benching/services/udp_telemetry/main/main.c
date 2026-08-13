// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the UDP telemetry line builder (services/iot/udp_telemetry): the
// InfluxDB line-protocol assembly (measurement + tags + int/uint/float fields) - the per-point hot op
// before the UDP send. Pure; no socket.
//
// Build/flash:  idf.py -C test/performance_benching/udp_telemetry -t upload --upload-port COM7
#include "device_bench.h"
#include "services/iot/udp_telemetry/udp_telemetry.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("udp_telemetry");
        volatile size_t sink = 0;
        static char buf[256];
        DBENCH_OP("protocore_line build (2 tags, 3 fields)", 200000, {
            protocore_line l;
            protocore_line_init(&l, buf, sizeof(buf), "env");
            protocore_line_add_tag(&l, "host", "rig-1");
            protocore_line_add_tag(&l, "room", "lab");
            protocore_line_add_float(&l, "temp", 21.5f, 1);
            protocore_line_add_int(&l, "rssi", -42);
            protocore_line_add_uint(&l, "uptime", 1234u);
            sink += protocore_line_len(&l);
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("udp_telemetry")
