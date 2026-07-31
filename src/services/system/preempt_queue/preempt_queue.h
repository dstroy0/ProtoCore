// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file preempt_queue.h
 * @brief User-configurable preempting work queues + high-priority processing tasks
 *        (PC_ENABLE_PREEMPT_QUEUE) - the v5 real-time ingest primitive.
 *
 * Fixed-capacity queues, each feeding one dedicated, core-pinned task. A producer posts
 * a fixed-size item; the scheduler **preempts** the lower-priority producer the instant
 * the item lands so it is processed immediately instead of on the next tick. Producers
 * post from a task (back or front, with a wait timeout) or from an ISR (interrupt-safe,
 * with an immediate context-switch request). Each processing task pops items in order
 * and hands each to a user handler.
 *
 * **Named lanes.** There are several queues, addressed by @ref pc_pq_lane:
 *   - `pc_pq_lane::PC_PQ_LANE_USER` - the single lane exposed to the application. The no-lane
 *     `pc_pq_*` API drives it (so existing user code is unchanged). Lowest priority.
 *   - `pc_pq_lane::PC_PQ_LANE_DMA` / `_FORWARD` / `_DEVICE` - internal lanes for the library's own
 *     real-time work (DMA peripheral transfers, interface forwarding, device access).
 *     They run **above** the user lane (base `PC_PQ_INTERNAL_PRIORITY`, DMA highest),
 *     so internal ingest always preempts user work; and below the lwIP tcpip / WiFi tasks
 *     so networking is never starved.
 *
 * This is the single normalized pipe for "hardware event -> process now": a DMA-complete
 * / GPIO / bus ISR posts a descriptor onto its lane, the lane's task drains it. Zero-heap
 * queue storage (static, compile-time PC_PQ_DEPTH x PC_PQ_ITEM_SIZE per lane; a
 * task's stack is created only when its lane starts, so unused lanes cost only their queue
 * storage), fail-closed on a full queue, no hot-path locks - so latency stays bounded.
 *
 * Host builds have no FreeRTOS task: posts enqueue into the same fixed per-lane ring and
 * pc_pq_drain[_lane]() runs the handler over what is queued, so the logic is
 * host-testable and behaves identically to the device's draining task.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PREEMPT_QUEUE_H
#define PROTOCORE_PREEMPT_QUEUE_H

#include "protocore_config.h"

#if PC_ENABLE_PREEMPT_QUEUE

#include <stddef.h>
#include <stdint.h>

/**
 * @brief The preempting lanes, ordered by role. The USER lane is exposed to the
 *        application; the internal lanes run at a higher priority (DMA highest) so
 *        internal ingest preempts user work.
 */
enum class pc_pq_lane : uint8_t
{
    PC_PQ_LANE_USER = 0, ///< exposed to the app (no-lane API); lowest priority
    PC_PQ_LANE_DMA,      ///< internal: DMA peripheral transfers (highest)
    PC_PQ_LANE_FORWARD,  ///< internal: interface forwarding
    PC_PQ_LANE_DEVICE,   ///< internal: device access
    PC_PQ_LANE_COUNT
};

/**
 * @brief Handler a lane's processing task invokes for each dequeued item.
 * @param item pointer to PC_PQ_ITEM_SIZE bytes (the posted item).
 * @param ctx  the opaque pointer passed to pc_pq_start[_lane]().
 */
typedef void (*pc_pq_handler)(const void *item, void *ctx);

/** @brief Processing-task configuration set by the caller at start. */
struct pc_pq_config
{
    pc_pq_handler handler; ///< Called once per dequeued item (required).
    void *ctx;             ///< Opaque, forwarded to @ref handler.
    uint8_t priority;      ///< Task priority; 0 = use the lane's default (internal > user).
    uint8_t core;          ///< Core to pin the task to (ESP32; ignored on host).
    const char *name;      ///< Task name (debug); may be nullptr.
};

// --- Lane API -------------------------------------------------------------------------

/**
 * @brief Create a lane's queue and start its processing task.
 *
 * Idempotent per lane: a second call while that lane runs is a no-op returning false. On
 * host no task is started (drive with pc_pq_drain_lane()). If `cfg->priority` is 0 the
 * lane's default priority is used (internal lanes default above the user lane).
 * @return true if started; false on a bad lane / null handler / already running.
 */
bool pc_pq_start_lane(pc_pq_lane lane, const pc_pq_config *cfg);

/** @brief Post to the back of a lane from a task context (copies PC_PQ_ITEM_SIZE
 *         bytes, blocks up to @p timeout_ticks, then fails closed). */
bool pc_pq_post_lane(pc_pq_lane lane, const void *item, uint32_t timeout_ticks);

/** @brief Post to the FRONT of a lane (urgent) from a task context. */
bool pc_pq_post_lane_urgent(pc_pq_lane lane, const void *item, uint32_t timeout_ticks);

/** @brief Post to a lane from an ISR (interrupt-safe; requests an immediate switch). */
bool pc_pq_post_lane_from_isr(pc_pq_lane lane, const void *item);

/** @brief Run the handler over every currently-queued item on a lane (host / inline
 *         drive). No-op on ARDUINO, where the lane's task drains it. */
void pc_pq_drain_lane(pc_pq_lane lane);

/** @brief Stop a lane's processing task (no-op on host). */
void pc_pq_stop_lane(pc_pq_lane lane);

/** @brief True while a lane's processing task is running (always false on host). */
bool pc_pq_running_lane(pc_pq_lane lane);

/** @brief Peak items ever queued at once on a lane (for sizing PC_PQ_DEPTH). */
size_t pc_pq_high_water_lane(pc_pq_lane lane);

/** @brief The default task priority for a lane (internal lanes rank above the user lane;
 *         DMA highest). Used when a config passes priority 0. */
uint8_t pc_pq_lane_priority(pc_pq_lane lane);

// --- User-lane API (unchanged; drives pc_pq_lane::PC_PQ_LANE_USER) -----------------------------

/** @brief Start the USER lane. @see pc_pq_start_lane. */
inline bool pc_pq_start(const pc_pq_config *cfg)
{
    return pc_pq_start_lane(pc_pq_lane::PC_PQ_LANE_USER, cfg);
}
/** @brief Post to the back of the USER lane. */
inline bool pc_pq_post(const void *item, uint32_t timeout_ticks)
{
    return pc_pq_post_lane(pc_pq_lane::PC_PQ_LANE_USER, item, timeout_ticks);
}
/** @brief Post to the front of the USER lane (urgent). */
inline bool pc_pq_post_urgent(const void *item, uint32_t timeout_ticks)
{
    return pc_pq_post_lane_urgent(pc_pq_lane::PC_PQ_LANE_USER, item, timeout_ticks);
}
/** @brief Post to the USER lane from an ISR. */
inline bool pc_pq_post_from_isr(const void *item)
{
    return pc_pq_post_lane_from_isr(pc_pq_lane::PC_PQ_LANE_USER, item);
}
/** @brief Drain the USER lane (host / inline drive). */
inline void pc_pq_drain(void)
{
    pc_pq_drain_lane(pc_pq_lane::PC_PQ_LANE_USER);
}
/** @brief Stop the USER lane's task. */
inline void pc_pq_stop(void)
{
    pc_pq_stop_lane(pc_pq_lane::PC_PQ_LANE_USER);
}
/** @brief True while the USER lane's task is running. */
inline bool pc_pq_running(void)
{
    return pc_pq_running_lane(pc_pq_lane::PC_PQ_LANE_USER);
}
/** @brief Peak items ever queued on the USER lane. */
inline size_t pc_pq_high_water(void)
{
    return pc_pq_high_water_lane(pc_pq_lane::PC_PQ_LANE_USER);
}

#endif // PC_ENABLE_PREEMPT_QUEUE

#endif // PROTOCORE_PREEMPT_QUEUE_H
