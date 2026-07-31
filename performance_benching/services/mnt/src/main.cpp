// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the mounted storage (server/filesystem/mnt) over its RAM backend:
// write_file / read_file / exists. The RAM backend keeps everything in memory (no flash I/O), so this
// measures the pure VFS bookkeeping + copy cost; the LittleFS/SD backends carry real I/O latency.
//
// Build/flash:  pio run -d performance_benching/services/mnt -t upload --upload-port COM7
#include "device_bench.h"
#include "server/filesystem/filesystem.h"
#include <Arduino.h>

static void mnt_bench_task(void *)
{
    static uint8_t data[256];
    for (int i = 0; i < 256; i++)
    {
        data[i] = (uint8_t)(i * 13 + 7);
    }

    for (;;)
    {
        Serial.printf("DB ==== mnt device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        pc_mnt_mount(pc_mnt_ram());
        pc_mnt_ram_format();
        pc_fs_write_file("/cfg.bin", data, sizeof(data));
        volatile long sink = 0;
        DBENCH_OP("pc_fs_write_file (256B)", 50000, sink += pc_fs_write_file("/cfg.bin", data, sizeof(data)) ? 1 : 0);
        static uint8_t rd[256];
        DBENCH_OP("pc_fs_read_file (256B)", 50000, sink += pc_fs_read_file("/cfg.bin", rd, sizeof(rd)));
        DBENCH_OP("pc_fs_exists", 200000, sink += pc_fs_exists("/cfg.bin") ? 1 : 0);
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: mnt device microbench");
    xTaskCreatePinnedToCore(mnt_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
