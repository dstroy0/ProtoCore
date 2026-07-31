// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the write-ahead log (services/storage/wal): the CRC-32 and the record
// encode - the pure per-record CPU ops. The store append/checkpoint path is deliberately not benched
// here: it is I/O-bound (its real cost is the flash/SD write, not CPU) and needs a large backing
// device, so it is covered by the host bench (performance_benching/bench_datastore.cpp) over a RAM disk instead.
//
// Build/flash:  pio run -d performance_benching/device/wal -t upload --upload-port COM7
#include "device_bench.h"
#include "services/storage/wal/wal.h"
#include <Arduino.h>

static void wal_bench_task(void *)
{
    static uint8_t src[1024];
    for (size_t i = 0; i < sizeof(src); i++)
        src[i] = (uint8_t)(i * 31 + 7);

    for (;;)
    {
        Serial.printf("DB ==== wal device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;
        DBENCH_BULK("pc_wal_crc32 (1 KiB)", 100000, 1024, sink += pc_wal_crc32(src, 1024));
        DBENCH_BULK("pc_wal_crc32 (128 B)", 200000, 128, sink += pc_wal_crc32(src, 128));
        static uint8_t rec[256];
        DBENCH_OP("pc_wal_record_encode (128B)", 200000,
                  sink += (uint32_t)pc_wal_record_encode(rec, sizeof(rec), 1, src, 128));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: wal device microbench");
    xTaskCreatePinnedToCore(wal_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
