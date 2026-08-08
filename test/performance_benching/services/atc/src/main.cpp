// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the ATC field-I/O interop snapshot codec (services/machine_tool/atc):
// serializing this device's field-I/O map as `{"inputs":[...],"outputs":[...]}` JSON for an ATC
// engine over HTTP, plus the output setter and value getter it exposes alongside the snapshot -
// all pure (no heap, no stdlib, no sockets). Worked example for performance_benching/device/<service>/: a pure
// protocol codec with no hardware involved, so every call here exercises the real production code
// path (contrast with performance_benching/device/ads1115, a peripheral driver where the bus transaction itself is
// stubbed). There is nothing to stub here: the ATC service reads/writes an in-memory AtcFieldIo
// table the caller owns - no transport or bus dependency exists to fake out.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/atc -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/machine_tool/atc/atc.h"
#include <Arduino.h>

static void atc_bench_task(void *)
{
    // A small, realistic field-I/O table (detectors as inputs, phase drivers as outputs) - the
    // same shape used in test/test_atc/test_atc.cpp's test_snapshot_json().
    static AtcPoint pts[] = {
        {"det.1", false, 1},         // input
        {"det.2", false, 0},         // input
        {"phase.1.green", true, 0},  // output
        {"phase.1.yellow", true, 0}, // output
        {"phase.2.green", true, 0},  // output
        {"phase.2.red", true, 1},    // output
    };
    AtcFieldIo io = {pts, sizeof(pts) / sizeof(pts[0])};

    static char buf[256];
    // Prime once so we know the serialized length to report bulk throughput against.
    size_t snap_len = pc_atc_snapshot_json(&io, buf, sizeof(buf));
    if (snap_len == 0)
    {
        snap_len = 1; // guard against div-by-zero in DBENCH_BULK if the table/buffer ever mismatch
    }

    for (;;)
    {
        Serial.printf("DB ==== atc device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sinkz = 0;
        volatile bool sinkb = false;
        volatile uint8_t sink8 = 0;

        DBENCH_BULK("pc_atc_snapshot_json", 20000, snap_len, sinkz += pc_atc_snapshot_json(&io, buf, sizeof(buf)));
        DBENCH_OP("pc_atc_set_output", 100000, sinkb |= pc_atc_set_output(&io, "phase.1.green", 1));
        DBENCH_OP("pc_atc_get", 100000, sink8 += pc_atc_get(&io, "det.1", nullptr));

        (void)sinkz;
        (void)sinkb;
        (void)sink8;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: atc device microbench");
    xTaskCreatePinnedToCore(atc_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
