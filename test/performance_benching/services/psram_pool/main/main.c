// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the PSRAM buffer-placement policy + DMA ping-pong index
// manager (services/storage/psram_pool). Two pure, heap-free decision paths are benched:
//   - protocore_psram_place(): given a request size, DMA requirement, and the current free-heap headroom of
//     both internal DRAM and external PSRAM, it returns DRAM / PSRAM / FAIL (large-cold -> PSRAM,
//     small-hot + DMA -> DRAM, always leaving a DRAM reserve). Benched across its large-cold, small-hot,
//     and DMA-forced branches since each takes a different arm of the policy.
//   - protocore_pingpong_swap()/fill/drain index: the classic double-buffer role flip the CPU does every DMA
//     swap. A single xor + read, so it is benched as a tight bookkeeping loop.
// Both are pure integer policy - no heap_caps_calloc, no PSRAM chip, no DMA engine is touched here
// (that allocation/transfer is the application's job and out of scope for this rig, which has no
// external PSRAM guaranteed and no peripheral attached). Every call exercises the real production code
// path, like the modbus worked example.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/psram_pool -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "mmgr/psram_pool.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // Realistic headroom figures lifted from test/test_psram_pool/test_psram_pool.cpp: ~120KB free
    // DRAM, ~2MB free PSRAM, a 4KB large/cold threshold, and a 32KB DRAM reserve.
    static const size_t free_dram = 120000;
    static const size_t free_psram = 2000000;
    static const size_t threshold = 4096;
    static const size_t reserve = 32768;

    PingPong pp;
    protocore_pingpong_init(&pp);

    for (;;)
    {
        DBENCH_BANNER("psram_pool");
        volatile int sink = 0;

        // Large 64KB cold asset, no DMA -> prefers PSRAM.
        DBENCH_OP("protocore_psram_place large-cold->PSRAM", 200000,
                  sink += (int)protocore_psram_place(65536, false, free_dram, free_psram, threshold, reserve));
        // Small 512B hot buffer, no DMA -> prefers DRAM.
        DBENCH_OP("protocore_psram_place small-hot->DRAM", 200000,
                  sink += (int)protocore_psram_place(512, false, free_dram, free_psram, threshold, reserve));
        // 8KB buffer that must be DMA-capable -> forced to DRAM (PSRAM is not DMA-capable).
        DBENCH_OP("protocore_psram_place dma->DRAM", 200000,
                  sink += (int)protocore_psram_place(8192, true, free_dram, free_psram, threshold, reserve));
        // Ping-pong role flip: swap + read the new fill/drain indices (one DMA swap's worth of work).
        DBENCH_OP("protocore_pingpong_swap+index", 200000,
                  sink += protocore_pingpong_swap(&pp) + protocore_pingpong_fill_index(&pp) +
                          protocore_pingpong_drain_index(&pp));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("psram_pool")
