// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SQLite file-format reader primitives (services/storage/sqlite):
// the record varint decode/encode and the serial-type size lookup - the per-cell hot ops when
// walking a b-tree page. Pure byte math; no filesystem (the full page-reader is exercised in the
// host bench with a real DB fixture that is not synced to this build).
//
// Build/flash:  pio run -d performance_benching/device/sqlite -t upload --upload-port COM7
#include "device_bench.h"
#include "services/storage/sqlite/sqlite_format.h"
#include <Arduino.h>

static void sqlite_bench_task(void *)
{
    static const uint8_t vi1[] = {0x7F};                                                 // 1-byte varint (127)
    static const uint8_t vi2[] = {0x83, 0x5E};                                           // 2-byte varint (0x35E)
    static const uint8_t vi9[] = {0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7F}; // 9-byte varint

    for (;;)
    {
        Serial.printf("DB ==== sqlite device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint64_t sink = 0;
        uint64_t v;
        DBENCH_OP("pc_sqlite_varint_decode (1B)", 200000, sink += pc_sqlite_varint_decode(vi1, 1, &v));
        DBENCH_OP("pc_sqlite_varint_decode (2B)", 200000, sink += pc_sqlite_varint_decode(vi2, 2, &v));
        DBENCH_OP("pc_sqlite_varint_decode (9B)", 200000, sink += pc_sqlite_varint_decode(vi9, 9, &v));
        static uint8_t out[9];
        DBENCH_OP("pc_sqlite_varint_encode", 200000, sink += pc_sqlite_varint_encode(0x123456789ull, out, sizeof(out)));
        DBENCH_OP("pc_sqlite_serial_type_size", 200000, sink += pc_sqlite_serial_type_size(6));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: sqlite device microbench");
    xTaskCreatePinnedToCore(sqlite_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
