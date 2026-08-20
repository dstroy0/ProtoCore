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

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_TRACE_CAPTURE

#include "server/signaling/trace_capture/trace_capture.h"
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protomem/protomem.h"

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

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define TRACE_CAPTURE_OFF_CTX 0u
static_assert(TRACE_CAPTURE_OFF_CTX + sizeof(struct TraceCaptureStorage) <= PROTOCORE_TRACE_CAPTURE_BORROW,
              "PROTOCORE_TRACE_CAPTURE_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define TRACE_CAPTURE_CTX(w) ((struct TraceCaptureStorage *)(void *)((w) + TRACE_CAPTURE_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_TRACE_CAPTURE_BORROW persistent bytes
} TraceCaptureOwnCtx;
static TraceCaptureOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_trace_capture_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_TRACE_CAPTURE_BORROW).buf;
    }
    return s_own.span;
}

static void ring_push(uint8_t *restrict work, uint16_t sample)
{
    if (TRACE_CAPTURE_CTX(work)->pretrigger_samples == 0)
    {
        return; // no pre-roll configured - nothing to keep
    }
    TRACE_CAPTURE_CTX(work)->pre_ring[TRACE_CAPTURE_CTX(work)->pre_head] = sample;
    TRACE_CAPTURE_CTX(work)->pre_head++;
    if (TRACE_CAPTURE_CTX(work)->pre_head >= TRACE_CAPTURE_CTX(work)->pretrigger_samples)
    {
        TRACE_CAPTURE_CTX(work)->pre_head = 0;
    }
}

void protocore_trace_capture_begin(uint8_t *restrict work)
{
    const protocore_tc_config *cfg = TraceCaptureV.cfg;

    TraceCaptureV.ok = PROTO_FALSE;
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

    mem.set(TRACE_CAPTURE_CTX(work), 0, sizeof(*TRACE_CAPTURE_CTX(work)));
    TRACE_CAPTURE_CTX(work)->sink = cfg->sink;
    TRACE_CAPTURE_CTX(work)->ctx = cfg->ctx;
    TRACE_CAPTURE_CTX(work)->pretrigger_samples = cfg->pretrigger_samples;
    TRACE_CAPTURE_CTX(work)->posttrigger_samples = cfg->posttrigger_samples;
    TRACE_CAPTURE_CTX(work)->configured = PROTO_TRUE;
    TraceCaptureV.ok = PROTO_TRUE;
}

void protocore_trace_capture_feed_in(uint8_t *restrict work)
{
    const uint16_t *samples = TraceCaptureV.feed.samples;
    const uint16_t n = TraceCaptureV.feed.n;

    if (!TRACE_CAPTURE_CTX(work)->configured || !samples)
    {
        TRACE_CAPTURE_CTX(work)->stats.samples_dropped += n;
        TraceCaptureV.accepted = 0;
        return;
    }
    for (uint16_t i = 0; i < n; i++)
    {
        uint16_t s = samples[i];
        ring_push(work, s);
        if (TRACE_CAPTURE_CTX(work)->capturing &&
            TRACE_CAPTURE_CTX(work)->post_count < TRACE_CAPTURE_CTX(work)->posttrigger_samples)
        {
            TRACE_CAPTURE_CTX(work)
                ->window[TRACE_CAPTURE_CTX(work)->pretrigger_samples + TRACE_CAPTURE_CTX(work)->post_count] = s;
            TRACE_CAPTURE_CTX(work)->post_count++;
            if (TRACE_CAPTURE_CTX(work)->post_count == TRACE_CAPTURE_CTX(work)->posttrigger_samples)
            {
                protocore_tc_window win;
                win.samples = TRACE_CAPTURE_CTX(work)->window;
                win.n_samples = (uint16_t)(TRACE_CAPTURE_CTX(work)->pretrigger_samples +
                                           TRACE_CAPTURE_CTX(work)->posttrigger_samples);
                win.pretrigger_samples = TRACE_CAPTURE_CTX(work)->pretrigger_samples;
                win.trace_id = TRACE_CAPTURE_CTX(work)->trace_id++;
                Clock.cycles(Clock.internal);
                win.assembly_cycles = Clock.cyc - TRACE_CAPTURE_CTX(work)->trigger_cycles; // wrap-safe unsigned delta
                TRACE_CAPTURE_CTX(work)->capturing = PROTO_FALSE;
                TRACE_CAPTURE_CTX(work)->stats.windows_completed++;
                TRACE_CAPTURE_CTX(work)->sink(&win, TRACE_CAPTURE_CTX(work)->ctx);
            }
        }
    }
    TraceCaptureV.accepted = n;
}

void protocore_trace_capture_trigger(uint8_t *restrict work)
{
    TraceCaptureV.ok = PROTO_FALSE;
    if (!TRACE_CAPTURE_CTX(work)->configured)
    {
        return;
    }
    if (TRACE_CAPTURE_CTX(work)->capturing)
    {
        TRACE_CAPTURE_CTX(work)->stats.triggers_dropped++;
        return;
    }
    for (uint16_t i = 0; i < TRACE_CAPTURE_CTX(work)->pretrigger_samples; i++)
    {
        TRACE_CAPTURE_CTX(work)->window[i] =
            TRACE_CAPTURE_CTX(work)
                ->pre_ring[(TRACE_CAPTURE_CTX(work)->pre_head + i) % TRACE_CAPTURE_CTX(work)->pretrigger_samples];
    }
    TRACE_CAPTURE_CTX(work)->post_count = 0;
    TRACE_CAPTURE_CTX(work)->capturing = PROTO_TRUE;
    Clock.cycles(Clock.internal);
    TRACE_CAPTURE_CTX(work)->trigger_cycles = Clock.cyc;
    TraceCaptureV.ok = PROTO_TRUE;
}

void protocore_trace_capture_get_stats(uint8_t *restrict work)
{
    if (TraceCaptureV.feed.stats)
    {
        *TraceCaptureV.feed.stats = TRACE_CAPTURE_CTX(work)->stats;
    }
}

void protocore_trace_capture_capturing(uint8_t *restrict work)
{
    TraceCaptureV.ok = TRACE_CAPTURE_CTX(work)->configured && TRACE_CAPTURE_CTX(work)->capturing;
}

void protocore_trace_capture_end(uint8_t *restrict work)
{
    TRACE_CAPTURE_CTX(work)->configured = PROTO_FALSE;
    TRACE_CAPTURE_CTX(work)->capturing = PROTO_FALSE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
TraceCaptureVars TraceCaptureV;

#endif // PROTOCORE_ENABLE_TRACE_CAPTURE
