// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the route regex matcher (server/regex): regex_match() tests a
// request path against a route pattern - the per-route hot op during dispatch. Pure.
// Build/flash: pio run -d performance_benching/server/regex -t upload
#include "device_bench.h"
#include "server/protocore_internal.h" // regex_match declaration
#include <Arduino.h>

static void regex_bench_task(void *)
{
    for (;;)
    {
        Serial.printf("DB ==== regex device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile int sink = 0;
        DBENCH_OP("regex_match (hit)", 200000, sink += regex_match("/api/v1/[0-9]+/status", "/api/v1/42/status"));
        DBENCH_OP("regex_match (miss)", 200000, sink += regex_match("/api/v1/[0-9]+/status", "/api/v1/xx/status"));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: regex device microbench");
    xTaskCreatePinnedToCore(regex_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}
void loop()
{
    delay(1000);
}
