// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the wear-leveling slot picker (services/storage/wearlevel): pick the
// least-worn slot, mark a write, and report the spread (max-min erase count). Pure array scans over
// a fixed erase-count table; no flash I/O.
//
// Build/flash:  pio run -d performance_benching/device/wearlevel -t upload --upload-port COM7
#include "device_bench.h"
#include "services/storage/wearlevel/wearlevel.h"
#include <Arduino.h>

#define SLOTS 16

static void wearlevel_bench_task(void *)
{
    static uint32_t counts[SLOTS];

    for (;;)
    {
        Serial.printf("DB ==== wearlevel device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        for (int i = 0; i < SLOTS; i++)
            counts[i] = (uint32_t)(i * 3 + 1);
        volatile uint32_t sink = 0;
        DBENCH_OP("pc_wearlevel_pick (16 slots)", 200000, sink += pc_wearlevel_pick(counts, SLOTS));
        DBENCH_OP("pc_wearlevel_mark", 200000, {
            pc_wearlevel_mark(counts, SLOTS, sink % SLOTS);
            sink += 1;
        });
        DBENCH_OP("pc_wearlevel_spread", 200000, sink += pc_wearlevel_spread(counts, SLOTS));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: wearlevel device microbench");
    xTaskCreatePinnedToCore(wearlevel_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
