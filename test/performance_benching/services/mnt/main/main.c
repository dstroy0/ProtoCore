// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the mounted storage (server/storage/mnt) over the RAM backend
// (server/storage/mnt_ram):
// write_file / read_file / exists. The RAM backend keeps everything in memory (no flash I/O), so this
// measures the pure VFS bookkeeping + copy cost; the LittleFS/SD backends carry real I/O latency.
//
// Build/flash:  pio run -d performance_benching/services/mnt -t upload --upload-port COM7
#include "device_bench.h"
#include "server/storage/filesystem/filesystem.h"
#include "server/storage/mnt_ram/mnt_ram.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    static uint8_t mnt_work[16]; // the borrow an entry takes; Mnt and MntRam never read it
    static uint8_t data[256];
    for (int i = 0; i < 256; i++)
    {
        data[i] = (uint8_t)(i * 13 + 7);
    }

    for (;;)
    {
        DBENCH_BANNER("mnt");
        MntRam.backend(mnt_work);
        MntV.args.backend = MntRamV.backend;
        Mnt.mount(mnt_work);
        MntRam.format(mnt_work);
        Fs.mount = "";
        Fs.begin(protocore_filesystem_span());
        const int root = Fs.i32;

        // The operands a write takes do not change between iterations, so they are set once and
        // the timed region is the call - which is what the number is supposed to be about.
        Fs.path.root = root;
        Fs.path.dir = "";
        Fs.path.name = "cfg.bin";
        Fs.io.wbuf = data;
        Fs.io.n = sizeof(data);
        Fs.write_file(protocore_filesystem_span());
        volatile long sink = 0;
        DBENCH_OP("Fs.write_file (256B)", 50000, Fs.write_file(protocore_filesystem_span()); sink += Fs.ok ? 1 : 0);

        static uint8_t rd[256];
        Fs.io.buf = rd;
        Fs.io.n = sizeof(rd);
        DBENCH_OP("Fs.read_file (256B)", 50000, Fs.read_file(protocore_filesystem_span()); sink += Fs.len);

        DBENCH_OP("Fs.exists", 200000, Fs.exists(protocore_filesystem_span()); sink += Fs.ok ? 1 : 0);
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("mnt")
