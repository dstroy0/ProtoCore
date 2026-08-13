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
// To keep the fixed PROTOCORE_PQ_DEPTH=4 lane from saturating (which would collapse every post into the
// fail-closed early-return), a high-priority sink task drains the lane concurrently on the OTHER
// core (core 0) while this bench task posts from core 1 - the "hardware event -> process now" shape
// the service exists for. Draining on device is done by that task; PreemptQueue.drain() is a no-op
// here (host-only), so we never call it. Items are 4-byte u32s, matching PROTOCORE_PQ_ITEM_SIZE=4 and the
// known-good payloads in test/test_preempt_queue/test_preempt_queue.cpp.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/preempt_queue -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "network_drivers/session/preempt_queue.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

void dbench_run(void)
{
    // Start the USER lane: creates its fixed-capacity queue and a dedicated draining task pinned to
    // core 0 (the opposite core from this bench task, which xTaskCreatePinnedToCore pinned to core 1),
    // at max priority so posts are consumed promptly instead of piling up against the depth-4 ring.
    protocore_pq_config cfg = {};
    cfg.handler = pq_drain_handler;
    cfg.ctx = NULL;
    cfg.priority = 24; // ceiling (configMAX_PRIORITIES-1); high enough to drain between posts
    cfg.core = 0;      // drain on the other core so this task's CCOUNT measures the post side
    cfg.name = "pq_sink";
    protocore_pq_start(&cfg);

    uint32_t item = 0xDEADBEEF; // one 4-byte item == PROTOCORE_PQ_ITEM_SIZE

    for (;;)
    {
        DBENCH_BANNER("preempt_queue");
        volatile uint32_t sink = 0;  // count accepted posts; keeps the calls live
        volatile uint32_t psink = 0; // accumulate priorities; keeps the pure lookup live

        // Back-post (normal ingest): guard + xQueueSendToBack(copy 4B) + high-water note.
        DBENCH_OP("protocore_pq_post back", 20000, sink += protocore_pq_post(&item, 0) ? 1u : 0u);
        // Front-post (urgent-to-front ingest): xQueueSendToFront so it jumps ahead of queued work.
        DBENCH_OP("protocore_pq_post_urgent front", 20000, sink += protocore_pq_post_urgent(&item, 0) ? 1u : 0u);
        // ISR-style post: xQueueSendToBackFromISR + a (no-op cross-core) yield request.
        DBENCH_OP("protocore_pq_post_from_isr", 20000, sink += protocore_pq_post_from_isr(&item) ? 1u : 0u);
        // Pure scheduler math: the per-lane default priority switch (no queue touched).
        DBENCH_OP("protocore_pq_lane_priority", 200000, psink += PreemptQueue.priority(PROTOCORE_PQ_LANE_DMA));

        (void)sink;
        (void)psink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("preempt_queue")
