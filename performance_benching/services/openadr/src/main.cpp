// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the OpenADR 3.0 JSON codec (services/energy/openadr): the two
// production builders that turn a demand-response signal / telemetry reading into spec-conformant
// JSON text in a caller buffer -
//   pc_openadr_event()  -> {"objectType":"EVENT",...} with programID + eventName + an intervals
//                           array (each interval a start/duration + a SIMPLE/PRICE payload point),
//   pc_openadr_report() -> {"objectType":"REPORT",...} carrying one VEN reading for one resource.
// Both are pure: string escaping + a no-stdlib 3-decimal double formatter, zero heap, no sockets.
// The OAuth2 token and HTTP transport that actually move these objects are separate services and are
// deliberately out of scope here (this rig moves no packets); every call below exercises the real
// production code path with the known-good, spec-conformant sample data lifted from
// test/test_openadr/test_openadr.cpp.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/openadr -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/energy/openadr/openadr.h"
#include <Arduino.h>

static void openadr_bench_task(void *)
{
    // Two-interval event (SIMPLE level + PRICE point) - straight from test_event().
    static const OpenAdrInterval iv2[2] = {
        {1720000000u, 3600, "SIMPLE", 1.0},
        {1720003600u, 3600, "PRICE", 0.125},
    };
    // Eight-interval price schedule to show how the builder scales with the intervals array.
    static const OpenAdrInterval iv8[8] = {
        {1720000000u, 3600, "PRICE", 0.125}, {1720003600u, 3600, "PRICE", 0.148}, {1720007200u, 3600, "PRICE", 0.201},
        {1720010800u, 3600, "PRICE", 0.312}, {1720014400u, 3600, "PRICE", 0.289}, {1720018000u, 3600, "PRICE", 0.175},
        {1720021600u, 3600, "PRICE", 0.133}, {1720025200u, 3600, "PRICE", 0.110},
    };
    static char out[1024];

    for (;;)
    {
        Serial.printf("DB ==== openadr device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        DBENCH_OP("pc_openadr_event x2 intervals", 20000,
                  sink += pc_openadr_event("program-1", "peak", iv2, 2, out, sizeof(out)));
        DBENCH_OP("pc_openadr_event x8 intervals", 10000,
                  sink += pc_openadr_event("program-1", "day-ahead", iv8, 8, out, sizeof(out)));
        DBENCH_OP("pc_openadr_report reading", 20000,
                  sink += pc_openadr_report("program-1", "event-9", "meter-A", -2.5, 1720000000u, out, sizeof(out)));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: openadr device microbench");
    xTaskCreatePinnedToCore(openadr_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
