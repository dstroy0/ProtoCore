// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the pre/post-trigger capture assembler (server/signaling/trace_capture):
// TraceCapture.feed_in is the per-batch hot op (most naturally called from a DMA-complete handler) that fills
// the pre-trigger ring and, after a trigger, the post-trigger half. Pure ring bookkeeping over a
// caller sample batch; the DMA source and the completed-window sink are the application's.
//
// Build/flash:  idf.py -C test/performance_benching/trace_capture -t upload --upload-port COM7
#include "device_bench.h"
#include "server/signaling/trace_capture/trace_capture.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static volatile uint32_t g_windows;
static void sink_cb(const protocore_tc_window *, void *)
{
    g_windows++; // count completed windows; do no work in the hot path
}

/** @brief Open a capture with @p cfg: the pre-roll ring, the post-trigger half, and the sink. */
static proto_bool tc_begin(const protocore_tc_config *cfg)
{
    TraceCaptureV.cfg = cfg;
    TraceCapture.begin(protocore_trace_capture_span());
    return TraceCaptureV.ok;
}

/** @brief Feed @p n samples at @p samples into the capture; how many it took. */
static uint16_t tc_feed(const uint16_t *samples, uint16_t n)
{
    TraceCaptureV.feed.samples = samples;
    TraceCaptureV.feed.n = n;
    TraceCapture.feed_in(protocore_trace_capture_span());
    return TraceCaptureV.accepted;
}

void dbench_run(void)
{
    static uint16_t batch[64];
    for (int i = 0; i < 64; i++)
    {
        batch[i] = (uint16_t)(i * 37 + 5);
    }

    for (;;)
    {
        DBENCH_BANNER("trace_capture");
        protocore_tc_config cfg = {512, 512, sink_cb, NULL};
        tc_begin(&cfg);
        volatile uint32_t sink = 0;
        // Steady pre-trigger feeding: the continuous ring fill that runs every DMA-complete.
        DBENCH_OP("TraceCapture.feed_in (64 samples)", 100000, sink += tc_feed(batch, 64));
        (void)sink;
        TraceCapture.end(protocore_trace_capture_span());
        DBENCH_DONE();
    }
}

DBENCH_MAIN("trace_capture")
