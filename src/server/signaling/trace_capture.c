// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file trace_capture.c
 * @brief Pre/post-trigger sample-window assembler - implementation.
 *
 * All state is one static instance (internal linkage) - zero heap, fixed capacity
 * PC_TC_MAX_WINDOW_SAMPLES. The pre-trigger ring is sized to the *configured*
 * pretrigger_samples (<= the compile-time max) and indexed with a running write cursor;
 * pc_tc_trigger() reads it out oldest-first into the front of the window buffer. No
 * dynamic memory, no locks: feed() and trigger() are each a single bounded pass with no
 * blocking, so both are safe to call from an ISR (a DMA-complete callback and a GPIO
 * trigger ISR respectively).
 */

#include "server/signaling/trace_capture.h"

#if PC_ENABLE_TRACE_CAPTURE

#include "server/clock/clock.h" // pc_cycles()
                                // memset

typedef struct
{
    uint16_t pre_ring[PC_TC_MAX_WINDOW_SAMPLES];
    uint16_t window[PC_TC_MAX_WINDOW_SAMPLES];
    pc_tc_sink_fn sink;
    void *ctx;
    uint16_t pretrigger_samples;
    uint16_t posttrigger_samples;
    uint16_t pre_head;   // next pre_ring write index [0, pretrigger_samples)
    uint16_t post_count; // post-trigger samples collected since trigger() [0, posttrigger_samples]
    uint32_t trace_id;
    uint32_t trigger_cycles;
    pc_tc_stats stats;
    proto_bool capturing;
    proto_bool configured;
} TcCtx;
static TcCtx s_tc;

static void ring_push(uint16_t sample)
{
    if (s_tc.pretrigger_samples == 0)
    {
        return; // no pre-roll configured - nothing to keep
    }
    s_tc.pre_ring[s_tc.pre_head] = sample;
    s_tc.pre_head++;
    if (s_tc.pre_head >= s_tc.pretrigger_samples)
    {
        s_tc.pre_head = 0;
    }
}

proto_bool pc_tc_begin(const pc_tc_config *cfg)
{
    if (!cfg || !cfg->sink)
    {
        return PROTO_FALSE;
    }
    if (cfg->pretrigger_samples == 0 && cfg->posttrigger_samples == 0)
    {
        return PROTO_FALSE;
    }
    uint32_t total = (uint32_t)cfg->pretrigger_samples + (uint32_t)cfg->posttrigger_samples;
    if (total > PC_TC_MAX_WINDOW_SAMPLES)
    {
        return PROTO_FALSE;
    }

    memset(&s_tc, 0, sizeof(s_tc));
    s_tc.sink = cfg->sink;
    s_tc.ctx = cfg->ctx;
    s_tc.pretrigger_samples = cfg->pretrigger_samples;
    s_tc.posttrigger_samples = cfg->posttrigger_samples;
    s_tc.configured = PROTO_TRUE;
    return PROTO_TRUE;
}

uint16_t pc_tc_feed(const uint16_t *samples, uint16_t n)
{
    if (!s_tc.configured || !samples)
    {
        s_tc.stats.samples_dropped += n;
        return 0;
    }
    for (uint16_t i = 0; i < n; i++)
    {
        uint16_t s = samples[i];
        ring_push(s);
        if (s_tc.capturing && s_tc.post_count < s_tc.posttrigger_samples)
        {
            s_tc.window[s_tc.pretrigger_samples + s_tc.post_count] = s;
            s_tc.post_count++;
            if (s_tc.post_count == s_tc.posttrigger_samples)
            {
                pc_tc_window win;
                win.samples = s_tc.window;
                win.n_samples = (uint16_t)(s_tc.pretrigger_samples + s_tc.posttrigger_samples);
                win.pretrigger_samples = s_tc.pretrigger_samples;
                win.trace_id = s_tc.trace_id++;
                win.assembly_cycles = pc_cycles() - s_tc.trigger_cycles; // wrap-safe unsigned delta
                s_tc.capturing = PROTO_FALSE;
                s_tc.stats.windows_completed++;
                s_tc.sink(&win, s_tc.ctx);
            }
        }
    }
    return n;
}

proto_bool pc_tc_trigger(void)
{
    if (!s_tc.configured)
    {
        return PROTO_FALSE;
    }
    if (s_tc.capturing)
    {
        s_tc.stats.triggers_dropped++;
        return PROTO_FALSE;
    }
    for (uint16_t i = 0; i < s_tc.pretrigger_samples; i++)
    {
        s_tc.window[i] = s_tc.pre_ring[(s_tc.pre_head + i) % s_tc.pretrigger_samples];
    }
    s_tc.post_count = 0;
    s_tc.capturing = PROTO_TRUE;
    s_tc.trigger_cycles = pc_cycles();
    return PROTO_TRUE;
}

void pc_tc_get_stats(pc_tc_stats *out)
{
    if (out)
    {
        *out = s_tc.stats;
    }
}

proto_bool pc_tc_capturing(void)
{
    return s_tc.configured && s_tc.capturing;
}

void pc_tc_end(void)
{
    s_tc.configured = PROTO_FALSE;
    s_tc.capturing = PROTO_FALSE;
}

#endif // PC_ENABLE_TRACE_CAPTURE
