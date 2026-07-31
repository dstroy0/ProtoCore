// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the preempting work queue (services/system/preempt_queue), the v5
// real-time ingest primitive: a producer posts a fixed-size item onto a fixed-capacity, zero-heap
// lane and a dedicated, core-pinned task drains it. This bench times the PRODUCER hot path - the
// three post entry points (back / urgent-to-front / ISR-style) plus the pure per-lane priority
// lookup the scheduler uses. Every op here is in-memory FreeRTOS queue work; there is NO hardware,
// bus, or socket involved, so nothing is stubbed - the real production code path runs.
//
// To keep the fixed PC_PQ_DEPTH=4 lane from saturating (which would collapse every post into the
// fail-closed early-return), a high-priority sink task drains the lane concurrently on the OTHER
// core (core 0) while this bench task posts from core 1 - the "hardware event -> process now" shape
// the service exists for. Draining on device is done by that task; pc_pq_drain_lane() is a no-op
// here (host-only), so we never call it. Items are 4-byte u32s, matching PC_PQ_ITEM_SIZE=4 and the
// known-good payloads in test/test_preempt_queue/test_preempt_queue.cpp.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/preempt_queue -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/system/preempt_queue/preempt_queue.h"
#include <Arduino.h>
#include <string.h>

// High-priority sink: pulls each posted item off the lane and touches it so the compiler cannot
// elide the drain. Runs on core 0 (see task setup) while the bench posts from core 1.
static volatile uint32_t g_drained = 0;
static void pq_drain_handler(const void *item, void *ctx)
{
    (void)ctx;
    uint32_t v;
    memcpy(&v, item, sizeof(v));
    g_drained += v;
}

static void preempt_queue_bench_task(void *)
{
    // Start the USER lane: creates its fixed-capacity queue and a dedicated draining task pinned to
    // core 0 (the opposite core from this bench task, which xTaskCreatePinnedToCore pinned to core 1),
    // at max priority so posts are consumed promptly instead of piling up against the depth-4 ring.
    pc_pq_config cfg = {};
    cfg.handler = pq_drain_handler;
    cfg.ctx = nullptr;
    cfg.priority = 24; // ceiling (configMAX_PRIORITIES-1); high enough to drain between posts
    cfg.core = 0;      // drain on the other core so this task's CCOUNT measures the post side
    cfg.name = "pq_sink";
    pc_pq_start(&cfg);

    uint32_t item = 0xDEADBEEF; // one 4-byte item == PC_PQ_ITEM_SIZE

    for (;;)
    {
        Serial.printf("DB ==== preempt_queue device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;  // count accepted posts; keeps the calls live
        volatile uint32_t psink = 0; // accumulate priorities; keeps the pure lookup live

        // Back-post (normal ingest): guard + xQueueSendToBack(copy 4B) + high-water note.
        DBENCH_OP("pc_pq_post back", 20000, sink += pc_pq_post(&item, 0) ? 1u : 0u);
        // Front-post (urgent-to-front ingest): xQueueSendToFront so it jumps ahead of queued work.
        DBENCH_OP("pc_pq_post_urgent front", 20000, sink += pc_pq_post_urgent(&item, 0) ? 1u : 0u);
        // ISR-style post: xQueueSendToBackFromISR + a (no-op cross-core) yield request.
        DBENCH_OP("pc_pq_post_from_isr", 20000, sink += pc_pq_post_from_isr(&item) ? 1u : 0u);
        // Pure scheduler math: the per-lane default priority switch (no queue touched).
        DBENCH_OP("pc_pq_lane_priority", 200000, psink += pc_pq_lane_priority(pc_pq_lane::PC_PQ_LANE_DMA));

        (void)sink;
        (void)psink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: preempt_queue device microbench");
    xTaskCreatePinnedToCore(preempt_queue_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
