// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the RCWL-0516 presence core (services/peripherals/rcwl0516): the pure
// debounce/hold state machine protocore_presence_core_update() that turns a raw doppler-radar OUT-pin
// level into a debounced presence verdict + edge events. The GPIO read (protocore_rcwl0516_poll) is
// real-hardware and out of scope; only the deterministic per-sample state machine is benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/rcwl0516 -t upload --upload-port COM7
#include "device_bench.h"
#include "services/peripherals/rcwl0516/rcwl0516.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("rcwl0516");
        volatile uint32_t sink = 0;
        PresenceCore c;
        protocore_presence_core_init(&c, 50, 2000, 0);
        uint32_t t = 0;
        // Alternate the pin each call so both edges + debounce/hold paths are exercised.
        DBENCH_OP("protocore_presence_core_update", 200000, {
            sink += protocore_presence_core_update(&c, (t & 64) != 0, t);
            t += 3;
        });
        DBENCH_OP("protocore_presence_core_get", 200000, sink += protocore_presence_core_get(&c));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("rcwl0516")
