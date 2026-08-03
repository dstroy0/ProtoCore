// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file trace_capture.h
 * @brief Pre/post-trigger sample-window assembler (PC_ENABLE_TRACE_CAPTURE) - the v5
 *        high-rate acquisition primitive.
 *
 * The consumer sitting downstream of mmgr/dma on a sampling front end (an external
 * ADC front-end - e.g. an AD9226/AD9238 - draining into the device over SPI or UART DMA,
 * a benchtop scope's digitizer output, or any other high-rate source): pc_tc_feed()
 * is called with every batch of samples as they arrive (from a DMA-complete handler, most
 * naturally), and a continuously-running **pre-trigger ring** always holds the most recent
 * @ref pc_tc_config::pretrigger_samples samples. When pc_tc_trigger() fires - a GPIO ISR,
 * a software threshold detector, or an external front-end's own trigger line - the ring's
 * current content becomes the pre-trigger half of the window and subsequent feeds fill the
 * post-trigger half, so the emitted window straddles the trigger instant exactly like a
 * benchtop oscilloscope's pretrigger/posttrigger split, even though the trigger is detected
 * only after the pre-trigger samples already went by.
 *
 * One capture in flight at a time, fail-closed: a trigger while a window is still filling is
 * rejected and counted (@ref pc_tc_stats::triggers_dropped), never queued or stomped - the
 * same determinism contract as mmgr/dma's one-TX-in-flight rule. Storage is static
 * (PC_TC_MAX_WINDOW_SAMPLES samples, zero heap); feed() and trigger() are ISR-safe (no
 * blocking, no allocation) so the natural wiring is a DMA-complete callback calling feed()
 * and a GPIO ISR calling trigger(), both posting nothing further themselves - the window
 * callback fires inline the instant the post-trigger half completes.
 *
 * Window-assembly latency (trigger() to the completed callback) is timestamped with
 * pc_cycles() (server/clock/clock.h) rather than pc_micros(): at the sample rates this feeds
 * from (an SPI-drained ADC burst can complete several feed() calls within a single
 * microsecond tick) a 1 us clock under-resolves the jitter that matters for sizing
 * PC_DMA_BUF_SIZE / the post-trigger sample count; pc_cycles_to_ns() converts the
 * measured cycle delta to nanoseconds once the caller supplies the running CPU frequency.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TRACE_CAPTURE_H
#define PROTOCORE_TRACE_CAPTURE_H

#include "protocore_config.h"

#if PC_ENABLE_TRACE_CAPTURE

/** @brief One completed pre/post-trigger sample window, handed to the sink inline. */
typedef struct
{
    const uint16_t *samples;     ///< pretrigger_samples + posttrigger_samples contiguous codes
    uint16_t n_samples;          ///< total samples in the window (== the configured split's sum)
    uint16_t pretrigger_samples; ///< how many of @ref samples precede the trigger instant
    uint32_t trace_id;           ///< monotonic capture sequence (wraps), one per completed window
    uint32_t assembly_cycles;    ///< pc_cycles() delta from trigger() to this callback
} pc_tc_window;

/** @brief Sink for one completed window. Called inline from pc_tc_feed() / pc_tc_trigger(). */
typedef void (*pc_tc_sink_fn)(const pc_tc_window *win, void *ctx);

/** @brief Capture configuration passed to pc_tc_begin(). */
typedef struct
{
    uint16_t pretrigger_samples;  ///< samples of history kept before the trigger instant
    uint16_t posttrigger_samples; ///< samples collected after the trigger before the window fires
    pc_tc_sink_fn sink;           ///< completed-window callback (required)
    void *ctx;                    ///< opaque, forwarded to @ref sink
} pc_tc_config;

/** @brief Rolling telemetry: never inferred from state, always the ground truth counters. */
typedef struct
{
    uint32_t windows_completed; ///< total windows handed to the sink
    uint32_t triggers_dropped;  ///< trigger() calls rejected because a window was already filling
    uint32_t samples_dropped;   ///< feed() samples rejected because the window buffer was full
} pc_tc_stats;

/**
 * @brief Configure the assembler and reset all state.
 * @return true if configured; false on a null config/sink, a zero split, or a split that
 *         exceeds PC_TC_MAX_WINDOW_SAMPLES.
 */
proto_bool pc_tc_begin(const pc_tc_config *cfg);

/**
 * @brief Feed @p n newly-arrived samples (oldest first). Always refreshes the pre-trigger
 *        ring; while a window is filling, also appends into it - completing and firing the
 *        sink the instant @ref pc_tc_config::posttrigger_samples have arrived since
 *        trigger(). ISR-safe: no blocking, no allocation, bounded work proportional to @p n.
 * @return @p n once configured; 0 (all @p n counted into @ref pc_tc_stats::samples_dropped,
 *         fail-closed) if called before pc_tc_begin() / after pc_tc_end().
 */
uint16_t pc_tc_feed(const uint16_t *samples, uint16_t n);

/**
 * @brief Declare the trigger instant now: the pre-trigger ring's current content becomes the
 *        window's pre-trigger half, and subsequent pc_tc_feed() calls fill the post-trigger
 *        half. ISR-safe.
 * @return true if armed; false (fail-closed, counted) if a window was already filling.
 */
proto_bool pc_tc_trigger(void);

/** @brief Rolling telemetry snapshot (see @ref pc_tc_stats). */
void pc_tc_get_stats(pc_tc_stats *out);

/** @brief True while a window is between trigger() and the post-trigger count completing. */
proto_bool pc_tc_capturing(void);

/** @brief Tear down: no further sink calls until pc_tc_begin() again. */
void pc_tc_end(void);

#endif // PC_ENABLE_TRACE_CAPTURE

#endif // PROTOCORE_TRACE_CAPTURE_H
