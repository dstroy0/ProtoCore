// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the IEC 61784-3 black-channel Safety Communication Layer
// (services/machine_tool/safety_scl): the per-frame safety verdict protocore_scl_on_frame() (monitoring-counter
// sequence check + watchdog state machine, given the CRC-signature verdict as input) and the
// protocore_scl_next_counter() modulus math. Pure (the CRC signature itself is computed elsewhere and
// passed in) - this is the deterministic SCL consequence logic run per received safety frame.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/safety_scl -t upload --upload-port COM7
#include "device_bench.h"
#include "services/machine_tool/safety_scl/safety_scl.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("safety_scl");
        volatile uint32_t sink = 0;
        SclConn c;
        protocore_scl_init(&c, 1, 0, 100, 0);
        uint32_t counter = 1, t = 0;
        // A stream of valid, in-sequence frames: exercises the accept + watchdog-refresh hot path.
        DBENCH_OP("protocore_scl_on_frame (valid seq)", 200000, {
            sink += protocore_scl_on_frame(&c, true, counter, t);
            counter = protocore_scl_next_counter(counter, 0);
            t += 1;
        });
        DBENCH_OP("protocore_scl_next_counter", 200000, sink += protocore_scl_next_counter(counter, 65535));
        DBENCH_OP("protocore_scl_poll", 200000, sink += protocore_scl_poll(&c, t));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("safety_scl")
