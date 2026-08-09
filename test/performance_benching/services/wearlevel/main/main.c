// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the wear-leveling slot picker (services/storage/wearlevel): pick the
// least-worn slot, mark a write, and report the spread (max-min erase count). Pure array scans over
// a fixed erase-count table; no flash I/O.
//
// Build/flash:  idf.py -C test/performance_benching/wearlevel -t upload --upload-port COM7
#include "device_bench.h"
#include "server/filesystem/wearlevel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SLOTS 16

void dbench_run(void)
{
    static uint32_t counts[SLOTS];

    for (;;)
    {
        DBENCH_BANNER("wearlevel");
        for (int i = 0; i < SLOTS; i++)
        {
            counts[i] = (uint32_t)(i * 3 + 1);
        }
        volatile uint32_t sink = 0;
        DBENCH_OP("pc_wearlevel_pick (16 slots)", 200000, sink += pc_wearlevel_pick(counts, SLOTS));
        DBENCH_OP("pc_wearlevel_mark", 200000, {
            pc_wearlevel_mark(counts, SLOTS, sink % SLOTS);
            sink += 1;
        });
        DBENCH_OP("pc_wearlevel_spread", 200000, sink += pc_wearlevel_spread(counts, SLOTS));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("wearlevel")
