// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the write-ahead log (services/storage/wal): the CRC-32 and the record
// encode - the pure per-record CPU ops. The store append/checkpoint path is deliberately not benched
// here: it is I/O-bound (its real cost is the flash/SD write, not CPU) and needs a large backing
// device, so it is covered by the host bench (performance_benching/bench_datastore.cpp) over a RAM disk instead.
//
// Build/flash:  idf.py -C test/performance_benching/wal -t upload --upload-port COM7
#include "device_bench.h"
#include "services/storage/wal/wal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    static uint8_t src[1024];
    for (size_t i = 0; i < sizeof(src); i++)
    {
        src[i] = (uint8_t)(i * 31 + 7);
    }

    for (;;)
    {
        DBENCH_BANNER("wal");
        volatile uint32_t sink = 0;
        DBENCH_BULK("pc_wal_crc32 (1 KiB)", 100000, 1024, sink += pc_wal_crc32(src, 1024));
        DBENCH_BULK("pc_wal_crc32 (128 B)", 200000, 128, sink += pc_wal_crc32(src, 128));
        static uint8_t rec[256];
        DBENCH_OP("pc_wal_record_encode (128B)", 200000,
                  sink += (uint32_t)pc_wal_record_encode(rec, sizeof(rec), 1, src, 128));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("wal")
