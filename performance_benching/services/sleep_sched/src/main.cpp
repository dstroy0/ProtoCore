// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the sleep scheduler (server/sleep_sched): pc_sleep_next()
// computes the next light-sleep window from the idle streak (0 while busy, then a window ramped
// between min and max). Pure wrap-safe integer math; the actual esp_light_sleep call is elsewhere.
//
// Build/flash:  pio run -d performance_benching/device/sleep_sched -t upload --upload-port COM7
#include "device_bench.h"
#include "server/sleep_sched.h"
#include <Arduino.h>

static void sleep_sched_bench_task(void *)
{
    static const pc_sleep_cfg cfg = {30000, 100, 8000, 2000}; // idle_ms, min_ms, max_ms, ramp_ms

    for (;;)
    {
        Serial.printf("DB ==== sleep_sched device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;
        uint32_t now = 100000;
        // Sweep the idle streak so the ramp/clamp branches are all exercised.
        DBENCH_OP("pc_sleep_next", 200000, {
            sink += pc_sleep_next(now, now - (sink & 0xFFFF), &cfg);
            now += 7;
        });
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: sleep_sched device microbench");
    xTaskCreatePinnedToCore(sleep_sched_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
