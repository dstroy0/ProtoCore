// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the virtual filesystem (services/storage/vfs) over its RAM backend:
// write_file / read_file / exists. The RAM backend keeps everything in memory (no flash I/O), so this
// measures the pure VFS bookkeeping + copy cost; the LittleFS/SD backends carry real I/O latency.
//
// Build/flash:  pio run -d performance_benching/device/vfs -t upload --upload-port COM7
#include "device_bench.h"
#include "services/storage/vfs/vfs.h"
#include <Arduino.h>

static void vfs_bench_task(void *)
{
    static uint8_t data[256];
    for (int i = 0; i < 256; i++)
    {
        data[i] = (uint8_t)(i * 13 + 7);
    }

    for (;;)
    {
        Serial.printf("DB ==== vfs device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        pc_vfs_mount(pc_vfs_ram());
        pc_vfs_ram_format();
        pc_vfs_write_file("/cfg.bin", data, sizeof(data));
        volatile long sink = 0;
        DBENCH_OP("pc_vfs_write_file (256B)", 50000, sink += pc_vfs_write_file("/cfg.bin", data, sizeof(data)) ? 1 : 0);
        static uint8_t rd[256];
        DBENCH_OP("pc_vfs_read_file (256B)", 50000, sink += pc_vfs_read_file("/cfg.bin", rd, sizeof(rd)));
        DBENCH_OP("pc_vfs_exists", 200000, sink += pc_vfs_exists("/cfg.bin") ? 1 : 0);
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: vfs device microbench");
    xTaskCreatePinnedToCore(vfs_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
