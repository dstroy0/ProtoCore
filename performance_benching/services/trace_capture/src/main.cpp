// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the pre/post-trigger capture assembler (services/system/trace_capture):
// pc_tc_feed() is the per-batch hot op (most naturally called from a DMA-complete handler) that fills
// the pre-trigger ring and, after a trigger, the post-trigger half. Pure ring bookkeeping over a
// caller sample batch; the DMA source and the completed-window sink are the application's.
//
// Build/flash:  pio run -d performance_benching/device/trace_capture -t upload --upload-port COM7
#include "device_bench.h"
#include "services/system/trace_capture/trace_capture.h"
#include <Arduino.h>

static volatile uint32_t g_windows;
static void sink_cb(const pc_tc_window *, void *)
{
    g_windows++; // count completed windows; do no work in the hot path
}

static void trace_capture_bench_task(void *)
{
    static uint16_t batch[64];
    for (int i = 0; i < 64; i++)
        batch[i] = (uint16_t)(i * 37 + 5);

    for (;;)
    {
        Serial.printf("DB ==== trace_capture device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        pc_tc_config cfg = {512, 512, sink_cb, nullptr};
        pc_tc_begin(&cfg);
        volatile uint32_t sink = 0;
        // Steady pre-trigger feeding: the continuous ring fill that runs every DMA-complete.
        DBENCH_OP("pc_tc_feed (64 samples)", 100000, sink += pc_tc_feed(batch, 64));
        (void)sink;
        pc_tc_end();
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: trace_capture device microbench");
    xTaskCreatePinnedToCore(trace_capture_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
