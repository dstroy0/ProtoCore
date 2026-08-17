// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

static uint8_t sockpool_work[16]; // the borrow an entry takes; Sockpool never reads it

#define POOL_N 8

void dbench_run(void)
{
    static SockSlot slots[POOL_N];
    static SockPool pool;

    for (;;)
    {
        DBENCH_BANNER("sockpool");
        volatile uint32_t sink = 0;
        Sockpool.init_args.p = &pool;
        Sockpool.init_args.slots = slots;
        Sockpool.init_args.n = POOL_N;
        Sockpool.init(sockpool_work);
        uint32_t id = 1, now = 0;
        // A churn of acquires: fills, then steadily recycles the LRU slot.
        DBENCH_OP("Sockpool.acquire (LRU)", 200000, {
            size_t idx = 0;
            uint32_t evicted = 0;
            Sockpool.acquire_args.p = &pool;
            Sockpool.acquire_args.id = id++;
            Sockpool.acquire_args.now = now++;
            Sockpool.acquire_args.idx = &idx;
            Sockpool.acquire_args.evicted_id = &evicted;
            Sockpool.acquire(sockpool_work);
            sink += (uint32_t)Sockpool.acq;
        });
        size_t fidx = 0;
        Sockpool.find_args.p = &pool;
        Sockpool.find_args.id = id - 4;
        Sockpool.find_args.idx = &fidx;
        // The entry call stays inside DBENCH_OP so the timed loop measures the lookup itself.
        DBENCH_OP("Sockpool.find", 200000, (Sockpool.find(sockpool_work), sink += Sockpool.ok));
        DBENCH_OP("Sockpool.touch", 200000, {
            Sockpool.touch_args.p = &pool;
            Sockpool.touch_args.idx = 0;
            Sockpool.touch_args.now = now++;
            Sockpool.touch(sockpool_work);
            sink += now;
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("sockpool")
