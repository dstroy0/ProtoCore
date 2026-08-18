// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the RCWL-0516 presence core (server/peripherals/rcwl0516): the pure
// debounce/hold state machine protocore_presence_core_update() that turns a raw doppler-radar OUT-pin
// level into a debounced presence verdict + edge events. The GPIO read (protocore_rcwl0516_poll) is
// real-hardware and out of scope; only the deterministic per-sample state machine is benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/rcwl0516 -t upload --upload-port COM7
#include "device_bench.h"
#include "server/peripherals/rcwl0516/rcwl0516.h"

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
        Rcwl0516.presence_init_args.c = &c;
        Rcwl0516.presence_init_args.debounce_ms = 50;
        Rcwl0516.presence_init_args.hold_ms = 2000;
        Rcwl0516.presence_init_args.now = 0;
        Rcwl0516.presence_init(protocore_rcwl0516_span());
        uint32_t t = 0;
        // Alternate the pin each call so both edges + debounce/hold paths are exercised.
        DBENCH_OP("protocore_presence_core_update", 200000, {
            Rcwl0516.presence_update_args.c = &c;
            Rcwl0516.presence_update_args.pin_high = (t & 64) != 0;
            Rcwl0516.presence_update_args.now = t;
            Rcwl0516.presence_update(protocore_rcwl0516_span());
            sink += Rcwl0516.ok;
            t += 3;
        });
        DBENCH_OP("Rcwl0516.presence_get", 200000, {
            Rcwl0516.presence_get_args.c = &c;
            Rcwl0516.presence_get(protocore_rcwl0516_span());
            sink += Rcwl0516.ok;
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("rcwl0516")
