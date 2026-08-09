// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SEN0192 motion core (services/peripherals/sen0192): the pure
// hold/debounce state machine pc_sen0192_motion_update() that turns a raw PIR/microwave OUT level
// into a debounced presence verdict + events. The GPIO read is real-hardware and out of scope.
//
// Build/flash:  idf.py -C test/performance_benching/sen0192 -t upload --upload-port COM7
#include "device_bench.h"
#include "services/peripherals/sen0192/sen0192.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("sen0192");
        volatile uint32_t sink = 0;
        Sen0192Motion m;
        pc_sen0192_motion_init(&m, 2000, true);
        uint32_t t = 0;
        DBENCH_OP("pc_sen0192_motion_update", 200000, {
            sink += pc_sen0192_motion_update(&m, (t & 128) != 0, t);
            t += 5;
        });
        DBENCH_OP("pc_sen0192_motion_present", 200000, sink += pc_sen0192_motion_present(&m));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("sen0192")
