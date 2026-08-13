// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the mounted storage (server/storage/mnt) over its RAM backend:
// write_file / read_file / exists. The RAM backend keeps everything in memory (no flash I/O), so this
// measures the pure VFS bookkeeping + copy cost; the LittleFS/SD backends carry real I/O latency.
//
// Build/flash:  pio run -d performance_benching/services/mnt -t upload --upload-port COM7
#include "device_bench.h"
#include "server/storage/filesystem.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    static uint8_t data[256];
    for (int i = 0; i < 256; i++)
    {
        data[i] = (uint8_t)(i * 13 + 7);
    }

    for (;;)
    {
        DBENCH_BANNER("mnt");
        protocore_mnt_mount(protocore_mnt_ram());
        protocore_mnt_ram_format();
        int root = protocore_fs_begin("");
        protocore_fs_write_file(root, "", "cfg.bin", data, sizeof(data));
        volatile long sink = 0;
        DBENCH_OP("protocore_fs_write_file (256B)", 50000,
                  sink += protocore_fs_write_file(root, "", "cfg.bin", data, sizeof(data)) ? 1 : 0);
        static uint8_t rd[256];
        DBENCH_OP("protocore_fs_read_file (256B)", 50000, sink += protocore_fs_read_file(root, "", "cfg.bin", rd, sizeof(rd)));
        DBENCH_OP("protocore_fs_exists", 200000, sink += protocore_fs_exists(root, "", "cfg.bin") ? 1 : 0);
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("mnt")
