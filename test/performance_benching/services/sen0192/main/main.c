// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SEN0192 motion core (server/peripherals/sen0192): the pure
// hold/debounce state machine protocore_sen0192_motion_update() that turns a raw PIR/microwave OUT level
// into a debounced presence verdict + events. The GPIO read is real-hardware and out of scope.
//
// Build/flash:  idf.py -C test/performance_benching/sen0192 -t upload --upload-port COM7
#include "device_bench.h"
#include "server/peripherals/sen0192/sen0192.h"

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
        Sen0192V.motion_init_args.m = &m;
        Sen0192V.motion_init_args.hold_ms = 2000;
        Sen0192V.motion_init_args.active_high = true;
        Sen0192.motion_init(protocore_sen0192_span());
        uint32_t t = 0;
        DBENCH_OP("protocore_sen0192_motion_update", 200000, {
            Sen0192V.motion_update_args.m = &m;
            Sen0192V.motion_update_args.level_high = (t & 128) != 0;
            Sen0192V.motion_update_args.now_ms = t;
            Sen0192.motion_update(protocore_sen0192_span());
            sink += Sen0192V.ok;
            t += 5;
        });
        DBENCH_OP("Sen0192.motion_present", 200000, {
            Sen0192V.motion_present_args.m = &m;
            Sen0192.motion_present(protocore_sen0192_span());
            sink += Sen0192V.ok;
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("sen0192")
