// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file trace_capture.c
 * @brief Pre/post-trigger sample-window assembler - implementation.
 *
 * All state is one static instance (internal linkage) - zero heap, fixed capacity
 * PROTOCORE_TC_MAX_WINDOW_SAMPLES. The pre-trigger ring is sized to the *configured*
 * pretrigger_samples (<= the compile-time max) and indexed with a running write cursor;
 * protocore_tc_trigger() reads it out oldest-first into the front of the window buffer. No
 * dynamic memory, no locks: feed() and trigger() are each a single bounded pass with no
 * blocking, so both are safe to call from an ISR (a DMA-complete callback and a GPIO
 * trigger ISR respectively).
 */

#include "server/signaling/trace_capture.h"
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_TRACE_CAPTURE

#include "server/clock/clock.h" // protocore_cycles()
                                // memset

/**
 * @brief The capture's compile-time storage: the pre-roll ring, the assembled window, and the
 *        cursors over both.
 */
struct TraceCaptureStorage
{
    uint16_t pre_ring[PROTOCORE_TC_MAX_WINDOW_SAMPLES];
    uint16_t window[PROTOCORE_TC_MAX_WINDOW_SAMPLES];
    protocore_tc_sink_fn sink;
    void *ctx;
    uint16_t pretrigger_samples;
    uint16_t posttrigger_samples;
    uint16_t pre_head;   // next pre_ring write index [0, pretrigger_samples)
    uint16_t post_count; // post-trigger samples collected since a trigger [0, posttrigger_samples]
    uint32_t trace_id;
    uint32_t trigger_cycles;
    protocore_tc_stats stats;
    proto_bool capturing;
    proto_bool configured;
};

/**
 * @brief The capture's state and the calls that reach it - what TraceCaptureNs points at.
 *
 * @var TraceCaptureInternal::store  the pre-roll ring, the window, and the cursors over both
 * @var TraceCaptureInternal::ns     the handle a caller sets a call's members on
 */
struct TraceCaptureInternal
{
    struct TraceCaptureStorage *store;
    TraceCaptureNs *ns;
};

static struct TraceCaptureStorage s_store;

static struct TraceCaptureInternal s_tc = {.store = &s_store, .ns = &TraceCapture};

static void ring_push(struct TraceCaptureInternal *restrict ctx, uint16_t sample)
{
    if (ctx->store->pretrigger_samples == 0)
    {
        return; // no pre-roll configured - nothing to keep
    }
    ctx->store->pre_ring[ctx->store->pre_head] = sample;
    ctx->store->pre_head++;
    if (ctx->store->pre_head >= ctx->store->pretrigger_samples)
    {
        ctx->store->pre_head = 0;
    }
}

static void tc_begin(struct TraceCaptureInternal *restrict ctx)
{
    const protocore_tc_config *cfg = ctx->ns->cfg;

    ctx->ns->ok = PROTO_FALSE;
    if (!cfg || !cfg->sink)
    {
        return;
    }
    if (cfg->pretrigger_samples == 0 && cfg->posttrigger_samples == 0)
    {
        return;
    }
    uint32_t total = (uint32_t)cfg->pretrigger_samples + (uint32_t)cfg->posttrigger_samples;
    if (total > PROTOCORE_TC_MAX_WINDOW_SAMPLES)
    {
        return;
    }

    mem.set(ctx->store, 0, sizeof(*ctx->store));
    ctx->store->sink = cfg->sink;
    ctx->store->ctx = cfg->ctx;
    ctx->store->pretrigger_samples = cfg->pretrigger_samples;
    ctx->store->posttrigger_samples = cfg->posttrigger_samples;
    ctx->store->configured = PROTO_TRUE;
    ctx->ns->ok = PROTO_TRUE;
}

static void tc_feed(struct TraceCaptureInternal *restrict ctx)
{
    const uint16_t *samples = ctx->ns->feed.samples;
    const uint16_t n = ctx->ns->feed.n;

    if (!ctx->store->configured || !samples)
    {
        ctx->store->stats.samples_dropped += n;
        ctx->ns->accepted = 0;
        return;
    }
    for (uint16_t i = 0; i < n; i++)
    {
        uint16_t s = samples[i];
        ring_push(ctx, s);
        if (ctx->store->capturing && ctx->store->post_count < ctx->store->posttrigger_samples)
        {
            ctx->store->window[ctx->store->pretrigger_samples + ctx->store->post_count] = s;
            ctx->store->post_count++;
            if (ctx->store->post_count == ctx->store->posttrigger_samples)
            {
                protocore_tc_window win;
                win.samples = ctx->store->window;
                win.n_samples = (uint16_t)(ctx->store->pretrigger_samples + ctx->store->posttrigger_samples);
                win.pretrigger_samples = ctx->store->pretrigger_samples;
                win.trace_id = ctx->store->trace_id++;
                Clock.cycles(Clock.internal);
                win.assembly_cycles = Clock.cyc - ctx->store->trigger_cycles; // wrap-safe unsigned delta
                ctx->store->capturing = PROTO_FALSE;
                ctx->store->stats.windows_completed++;
                ctx->store->sink(&win, ctx->store->ctx);
            }
        }
    }
    ctx->ns->accepted = n;
}

static void tc_trigger(struct TraceCaptureInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->store->configured)
    {
        return;
    }
    if (ctx->store->capturing)
    {
        ctx->store->stats.triggers_dropped++;
        return;
    }
    for (uint16_t i = 0; i < ctx->store->pretrigger_samples; i++)
    {
        ctx->store->window[i] = ctx->store->pre_ring[(ctx->store->pre_head + i) % ctx->store->pretrigger_samples];
    }
    ctx->store->post_count = 0;
    ctx->store->capturing = PROTO_TRUE;
    Clock.cycles(Clock.internal);
    ctx->store->trigger_cycles = Clock.cyc;
    ctx->ns->ok = PROTO_TRUE;
}

static void tc_get_stats(struct TraceCaptureInternal *restrict ctx)
{
    if (ctx->ns->feed.stats)
    {
        *ctx->ns->feed.stats = ctx->store->stats;
    }
}

static void tc_capturing(struct TraceCaptureInternal *restrict ctx)
{
    ctx->ns->ok = ctx->store->configured && ctx->store->capturing;
}

static void tc_end(struct TraceCaptureInternal *restrict ctx)
{
    ctx->store->configured = PROTO_FALSE;
    ctx->store->capturing = PROTO_FALSE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
TraceCaptureNs TraceCapture = {.begin = tc_begin,
                               .feed_in = tc_feed,
                               .trigger = tc_trigger,
                               .get_stats = tc_get_stats,
                               .capturing = tc_capturing,
                               .end = tc_end,
                               .internal = &s_tc};

#endif // PROTOCORE_ENABLE_TRACE_CAPTURE
