// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the pluggable time source (services/timing_position/time_source): the
// priority-resolved current-time read (pc_time_now) and the RFC 7231 IMF-fixdate HTTP-date
// formatter (pc_time_http_date). Pure; a fixed in-memory source stands in for a real clock so the
// figures are deterministic.
//
// Build/flash:  pio run -d performance_benching/device/time_source -t upload --upload-port COM7
#include "device_bench.h"
#include "services/timing_position/time_source/time_source.h"
#include <Arduino.h>

static uint32_t fixed_epoch(void)
{
    return 1720700000u; // 2024-07-11T12:13:20Z, a stable value for the formatter bench
}

static void time_source_bench_task(void *)
{
    pc_time_source_reset();
    pc_time_source_add("bench", 10, fixed_epoch);

    for (;;)
    {
        Serial.printf("DB ==== time_source device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        DBENCH_OP("pc_time_now (resolve source)", 200000, sink += pc_time_now());
        static char out[40];
        DBENCH_OP("pc_time_http_date (IMF-fixdate)", 200000, sink += pc_time_http_date(out, sizeof(out)));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: time_source device microbench");
    xTaskCreatePinnedToCore(time_source_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
