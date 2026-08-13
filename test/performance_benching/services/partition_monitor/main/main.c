// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the flash partition-map monitor (server/storage/partition_monitor):
// pc_partition_kind() (the type/subtype -> human "kind" classifier) and pc_partition_json() (the
// {"partitions":[...]} serializer). Both are pure - no flash, no server - so every call here
// exercises the real production code path. Like performance_benching/device/ads1115 (a peripheral driver), only the
// deterministic CPU-side core is benched: pc_partition_collect() is the ESP32-only esp_partition /
// esp_ota_ops flash walk (a no-op on host builds) and is deliberately out of scope, so nothing here
// touches flash or any peripheral.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/partition_monitor -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/storage/partition_monitor/partition_monitor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // A realistic ESP32-S3 dual-OTA partition table (labels/types/subtypes/offsets/sizes straight
    // out of a factory csv layout) - the exact shape pc_partition_collect() would hand the
    // serializer at runtime. Field ordering matches the pc_partition_info test fixtures.
    static const pc_partition_info table[] = {
        {"nvs", 1, 0x02, 0x009000, 0x005000, false},      // data / nvs
        {"otadata", 1, 0x00, 0x00E000, 0x002000, false},  // data / otadata
        {"phy_init", 1, 0x01, 0x010000, 0x001000, false}, // data / phy
        {"app0", 0, 0x10, 0x020000, 0x180000, true},      // app / ota_0 (running)
        {"app1", 0, 0x11, 0x1A0000, 0x180000, false},     // app / ota_1
        {"spiffs", 1, 0x82, 0x320000, 0x0D0000, false},   // data / spiffs
        {"coredump", 1, 0x03, 0x3F0000, 0x010000, false}, // data / coredump
    };
    static const uint8_t count = (uint8_t)(sizeof(table) / sizeof(table[0]));
    static char buf[1024];

    // volatile inputs keep the classifier honest (the compiler can't fold a constant-argument call
    // to a constant string). Exercises the app if-chain (ota range) and the data switch (littlefs).
    volatile uint8_t app_type = 0, app_sub = 0x10;
    volatile uint8_t dat_type = 1, dat_sub = 0x83;

    for (;;)
    {
        DBENCH_BANNER("partition_monitor");
        volatile uint32_t sink = 0;
        volatile int jsink = 0;

        DBENCH_OP("pc_partition_kind app (ota)", 200000, sink += (uint32_t)pc_partition_kind(app_type, app_sub)[0]);
        DBENCH_OP("pc_partition_kind data (littlefs)", 200000,
                  sink += (uint32_t)pc_partition_kind(dat_type, dat_sub)[0]);

        // Serializer throughput: report MB/s over the JSON produced for the full 7-entry table.
        int jlen = pc_partition_json(table, count, buf, sizeof(buf));
        DBENCH_BULK("pc_partition_json (7 parts)", 20000, (jlen > 0 ? (size_t)jlen : 1),
                    jsink += pc_partition_json(table, count, buf, sizeof(buf)));

        (void)sink;
        (void)jsink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("partition_monitor")
