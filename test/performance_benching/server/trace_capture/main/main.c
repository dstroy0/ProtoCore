// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the pre/post-trigger capture assembler (server/signaling/trace_capture):
// pc_tc_feed() is the per-batch hot op (most naturally called from a DMA-complete handler) that fills
// the pre-trigger ring and, after a trigger, the post-trigger half. Pure ring bookkeeping over a
// caller sample batch; the DMA source and the completed-window sink are the application's.
//
// Build/flash:  idf.py -C test/performance_benching/trace_capture -t upload --upload-port COM7
#include "device_bench.h"
#include "server/signaling/trace_capture.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static volatile uint32_t g_windows;
static void sink_cb(const pc_tc_window *, void *)
{
    g_windows++; // count completed windows; do no work in the hot path
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
        pc_tc_config cfg = {512, 512, sink_cb, NULL};
        pc_tc_begin(&cfg);
        volatile uint32_t sink = 0;
        // Steady pre-trigger feeding: the continuous ring fill that runs every DMA-complete.
        DBENCH_OP("pc_tc_feed (64 samples)", 100000, sink += pc_tc_feed(batch, 64));
        (void)sink;
        pc_tc_end();
        DBENCH_DONE();
    }
}

DBENCH_MAIN("trace_capture")
