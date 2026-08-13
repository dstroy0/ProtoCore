// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the socket pool (server/net/sockpool): the LRU acquire (free
// slot, else recycle the least-recently-used), the id->slot lookup, and touch. Pure fixed-size
// bookkeeping; no real sockets.
//
// Build/flash:  idf.py -C test/performance_benching/sockpool -t upload --upload-port COM7
#include "device_bench.h"
#include "server/net/sockpool/sockpool.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POOL_N 8

void dbench_run(void)
{
    static SockSlot slots[POOL_N];
    static SockPool pool;

    for (;;)
    {
        DBENCH_BANNER("sockpool");
        volatile uint32_t sink = 0;
        protocore_sockpool_init(&pool, slots, POOL_N);
        uint32_t id = 1, now = 0;
        // A churn of acquires: fills, then steadily recycles the LRU slot.
        DBENCH_OP("protocore_sockpool_acquire (LRU)", 200000, {
            size_t idx = 0;
            uint32_t evicted = 0;
            sink += (uint32_t)protocore_sockpool_acquire(&pool, id++, now++, &idx, &evicted);
        });
        size_t fidx = 0;
        DBENCH_OP("protocore_sockpool_find", 200000, sink += protocore_sockpool_find(&pool, id - 4, &fidx));
        DBENCH_OP("protocore_sockpool_touch", 200000, {
            protocore_sockpool_touch(&pool, 0, now++);
            sink += now;
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("sockpool")
