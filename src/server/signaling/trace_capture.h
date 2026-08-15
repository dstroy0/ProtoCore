// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file trace_capture.h
 * @brief Pre/post-trigger sample-window assembler (PROTOCORE_ENABLE_TRACE_CAPTURE) - the v5
 *        high-rate acquisition primitive.
 *
 * The consumer sitting downstream of mmgr/dma on a sampling front end (an external
 * ADC front-end - e.g. an AD9226/AD9238 - draining into the device over SPI or UART DMA,
 * a benchtop scope's digitizer output, or any other high-rate source): protocore_tc_feed()
 * is called with every batch of samples as they arrive (from a DMA-complete handler, most
 * naturally), and a continuously-running **pre-trigger ring** always holds the most recent
 * @ref protocore_tc_config::pretrigger_samples samples. When protocore_tc_trigger() fires - a GPIO ISR,
 * a software threshold detector, or an external front-end's own trigger line - the ring's
 * current content becomes the pre-trigger half of the window and subsequent feeds fill the
 * post-trigger half, so the emitted window straddles the trigger instant exactly like a
 * benchtop oscilloscope's pretrigger/posttrigger split, even though the trigger is detected
 * only after the pre-trigger samples already went by.
 *
 * One capture in flight at a time, fail-closed: a trigger while a window is still filling is
 * rejected and counted (@ref protocore_tc_stats::triggers_dropped), never queued or stomped - the
 * same determinism contract as mmgr/dma's one-TX-in-flight rule. Storage is static
 * (PROTOCORE_TC_MAX_WINDOW_SAMPLES samples, zero heap); feed() and trigger() are ISR-safe (no
 * blocking, no allocation) so the natural wiring is a DMA-complete callback calling feed()
 * and a GPIO ISR calling trigger(), both posting nothing further themselves - the window
 * callback fires inline the instant the post-trigger half completes.
 *
 * Window-assembly latency (trigger() to the completed callback) is timestamped with
 * protocore_cycles() (server/clock/clock.h) rather than protocore_micros(): at the sample rates this feeds
 * from (an SPI-drained ADC burst can complete several feed() calls within a single
 * microsecond tick) a 1 us clock under-resolves the jitter that matters for sizing
 * PROTOCORE_DMA_BUF_SIZE / the post-trigger sample count; protocore_cycles_to_ns() converts the
 * measured cycle delta to nanoseconds once the caller supplies the running CPU frequency.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TRACE_CAPTURE_H
#define PROTOCORE_TRACE_CAPTURE_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_TRACE_CAPTURE

PROTOCORE_BEGIN_DECLS

/** @brief One completed pre/post-trigger sample window, handed to the sink inline. */
typedef struct
{
    const uint16_t *samples;     ///< pretrigger_samples + posttrigger_samples contiguous codes
    uint16_t n_samples;          ///< total samples in the window (== the configured split's sum)
    uint16_t pretrigger_samples; ///< how many of @ref samples precede the trigger instant
    uint32_t trace_id;           ///< monotonic capture sequence (wraps), one per completed window
    uint32_t assembly_cycles;    ///< protocore_cycles() delta from trigger() to this callback
} protocore_tc_window;

/** @brief Sink for one completed window. Called inline from protocore_tc_feed() / protocore_tc_trigger(). */
typedef void (*protocore_tc_sink_fn)(const protocore_tc_window *win, void *ctx);

/** @brief Capture configuration passed to protocore_tc_begin(). */
typedef struct
{
    uint16_t pretrigger_samples;  ///< samples of history kept before the trigger instant
    uint16_t posttrigger_samples; ///< samples collected after the trigger before the window fires
    protocore_tc_sink_fn sink;    ///< completed-window callback (required)
    void *ctx;                    ///< opaque, forwarded to @ref sink
} protocore_tc_config;

/** @brief Rolling telemetry: never inferred from state, always the ground truth counters. */
typedef struct
{
    uint32_t windows_completed; ///< total windows handed to the sink
    uint32_t triggers_dropped;  ///< trigger() calls rejected because a window was already filling
    uint32_t samples_dropped;   ///< feed() samples rejected because the window buffer was full
} protocore_tc_stats;

/** @brief The samples one feed carries, and where a stats read lands. */
typedef struct
{
    const uint16_t *samples;   ///< the block just sampled
    uint16_t n;                ///< how many
    protocore_tc_stats *stats; ///< where a stats read copies the tallies
} TcFeedArgs;

/** @brief The capture's own state and the calls that reach it, described only in trace_capture.c. */
struct TraceCaptureInternal;

/**
 * @brief The pre/post-trigger sample capture.
 *
 * A caller sets the members a call takes, invokes it through ::TraceCapture, and reads the outcome
 * off the same handle. The pre-roll ring and the assembled window are behind @ref internal.
 *
 * @var TraceCaptureNs::cfg        what arming the capture takes
 * @var TraceCaptureNs::feed       the samples one feed carries, and where a stats read lands
 * @var TraceCaptureNs::ok         a call's true/false outcome
 * @var TraceCaptureNs::accepted   samples the feed took
 * @var TraceCaptureNs::begin      arm the capture at its pre/post sample counts
 * @var TraceCaptureNs::feed_in    push samples through the pre-roll ring and the window
 * @var TraceCaptureNs::trigger    latch the pre-roll and start collecting post-trigger samples
 * @var TraceCaptureNs::get_stats  copy the tallies out
 * @var TraceCaptureNs::capturing  a window is being assembled right now
 * @var TraceCaptureNs::end        disarm
 * @var TraceCaptureNs::internal   the ring, the window and the calls that fill them
 *
 * The window completes inside a feed: when the last post-trigger sample lands, the sink is called
 * with the assembled window before the feed returns.
 */
typedef struct
{
    const protocore_tc_config *cfg;
    TcFeedArgs feed;

    proto_bool ok;
    uint16_t accepted;

    void (*begin)(struct TraceCaptureInternal *ctx);
    void (*feed_in)(struct TraceCaptureInternal *ctx);
    void (*trigger)(struct TraceCaptureInternal *ctx);
    void (*get_stats)(struct TraceCaptureInternal *ctx);
    void (*capturing)(struct TraceCaptureInternal *ctx);
    void (*end)(struct TraceCaptureInternal *ctx);

    struct TraceCaptureInternal *internal;
} TraceCaptureNs;

/** @brief The one symbol this module exports. */
extern TraceCaptureNs TraceCapture;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_TRACE_CAPTURE

#endif // PROTOCORE_TRACE_CAPTURE_H
