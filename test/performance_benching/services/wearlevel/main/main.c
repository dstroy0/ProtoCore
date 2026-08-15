// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the wear-leveling slot picker (services/storage/wearlevel): pick the
// least-worn slot, mark a write, and report the spread (max-min erase count). Pure array scans over
// a fixed erase-count table; no flash I/O.
//
// Build/flash:  idf.py -C test/performance_benching/wearlevel -t upload --upload-port COM7
#include "device_bench.h"
#include "server/storage/wearlevel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SLOTS 16

/** @brief The least-worn slot of the @p n counts at @p counts. */
static size_t wear_pick(const uint32_t *counts, size_t n)
{
    Wearlevel.args.counts = counts;
    Wearlevel.args.n = n;
    Wearlevel.pick(Wearlevel.internal);
    return Wearlevel.n_out;
}

/** @brief Record a write to slot @p idx of the @p n counts at @p counts. */
static void wear_mark(uint32_t *counts, size_t n, size_t idx)
{
    Wearlevel.args.counts_rw = counts;
    Wearlevel.args.n = n;
    Wearlevel.args.idx = idx;
    Wearlevel.mark(Wearlevel.internal);
}

/** @brief Max count - min count across the @p n counts at @p counts. */
static uint32_t wear_spread(const uint32_t *counts, size_t n)
{
    Wearlevel.args.counts = counts;
    Wearlevel.args.n = n;
    Wearlevel.imbalance(Wearlevel.internal);
    return Wearlevel.spread;
}

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
        DBENCH_OP("Wearlevel.pick (16 slots)", 200000, sink += (uint32_t)wear_pick(counts, SLOTS));
        DBENCH_OP("Wearlevel.mark", 200000, {
            wear_mark(counts, SLOTS, sink % SLOTS);
            sink += 1;
        });
        DBENCH_OP("Wearlevel.imbalance", 200000, sink += wear_spread(counts, SLOTS));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("wearlevel")
