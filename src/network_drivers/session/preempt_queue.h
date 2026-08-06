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
 *   - `PC_PQ_LANE_USER` - the single lane exposed to the application. The no-lane
 *     `pc_pq_*` API drives it. Lowest priority.
 *   - `PC_PQ_LANE_DMA` / `_FORWARD` / `_DEVICE` - internal lanes for the library's own
 *     real-time work (DMA peripheral transfers, interface forwarding, device access).
 *     They run **above** the user lane (base `PC_PQ_INTERNAL_PRIORITY`, DMA highest),
 *     so internal ingest always preempts user work; and below the network stack's own
 *     tasks so networking is never starved.
 *
 * This is the single normalized pipe for "hardware event -> process now": a DMA-complete
 * / GPIO / bus ISR posts a descriptor onto its lane, the lane's task drains it. Zero-heap
 * queue storage (static, compile-time PC_PQ_DEPTH x PC_PQ_ITEM_SIZE per lane; a
 * task's stack is created only when its lane starts, so unused lanes cost only their queue
 * storage), fail-closed on a full queue, no hot-path locks - so latency stays bounded.
 *
 * A build with no task backend posts into the same fixed per-lane ring, and
 * pc_pq_drain[_lane]() runs the handler over what is queued, so the logic is
 * host-testable and behaves identically to the device's draining task.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PREEMPT_QUEUE_H
#define PROTOCORE_PREEMPT_QUEUE_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_PREEMPT_QUEUE

/**
 * @brief The preempting lanes, ordered by role. The USER lane is exposed to the
 *        application; the internal lanes run at a higher priority (DMA highest) so
 *        internal ingest preempts user work.
 */
typedef enum PROTO_ENUM_PACKED
{
    PC_PQ_LANE_USER = 0, ///< exposed to the app (no-lane API); lowest priority
    PC_PQ_LANE_DMA,      ///< internal: DMA peripheral transfers (highest)
    PC_PQ_LANE_FORWARD,  ///< internal: interface forwarding
    PC_PQ_LANE_DEVICE,   ///< internal: device access
    PC_PQ_LANE_COUNT
} pc_pq_lane;

/**
 * @brief Handler a lane's processing task invokes for each dequeued item.
 * @param item pointer to PC_PQ_ITEM_SIZE bytes (the posted item).
 * @param ctx  the opaque pointer passed to pc_pq_start[_lane]().
 */
typedef void (*pc_pq_handler)(const void *item, void *ctx);

/**
 * @brief What a lane does with an item, and where it ranks.
 *
 * The priority is the QoS: it is what makes a post preempt lower-ranked work instead of waiting
 * for it, so it belongs to the lane rather than to whichever task happens to drain it.
 */
typedef struct
{
    pc_pq_handler handler; ///< Called once per dequeued item (required).
    void *ctx;             ///< Opaque, forwarded to @ref handler.
    uint8_t priority;      ///< Lane priority; 0 = the lane's default (internal ranks above user).
    uint8_t core;          ///< Core to pin the lane's worker to; ignored where there is one.
    const char *name;      ///< Lane name (debug); may be NULL.
} pc_pq_config;

/**
 * @brief The PreemptQueue module.
 *
 * @var PreemptQueueNs::post_from_isr
 * @var PreemptQueueNs::post_urgent
 * @var PreemptQueueNs::high_water
 * @var PreemptQueueNs::priority
 * @var PreemptQueueNs::running
 * @var PreemptQueueNs::start
 * @var PreemptQueueNs::post
 * @var PreemptQueueNs::drain
 * @var PreemptQueueNs::stop
 */
typedef struct
{
    proto_bool (*post_from_isr)(pc_pq_lane lane, const void *item);
    proto_bool (*post_urgent)(pc_pq_lane lane, const void *item, uint32_t timeout_ticks);
    size_t (*high_water)(pc_pq_lane lane);
    uint8_t (*priority)(pc_pq_lane lane);
    proto_bool (*running)(pc_pq_lane lane);
    proto_bool (*start)(pc_pq_lane lane, const pc_pq_config *cfg);
    proto_bool (*post)(pc_pq_lane lane, const void *item, uint32_t timeout_ticks);
    void (*drain)(pc_pq_lane lane);
    void (*stop)(pc_pq_lane lane);
} PreemptQueueNs;

/** @brief The one symbol this module exports. */
extern const PreemptQueueNs PreemptQueue;

// --- User-lane API (drives PC_PQ_LANE_USER) --------------------------------------------

/** @brief Start the USER lane. @see pc_pq_start_lane. */
PC_INLINE proto_bool pc_pq_start(const pc_pq_config *cfg)
{
    return PreemptQueue.start(PC_PQ_LANE_USER, cfg);
}
/** @brief Post to the back of the USER lane. */
PC_INLINE proto_bool pc_pq_post(const void *item, uint32_t timeout_ticks)
{
    return PreemptQueue.post(PC_PQ_LANE_USER, item, timeout_ticks);
}
/** @brief Post to the front of the USER lane (urgent). */
PC_INLINE proto_bool pc_pq_post_urgent(const void *item, uint32_t timeout_ticks)
{
    return PreemptQueue.post_urgent(PC_PQ_LANE_USER, item, timeout_ticks);
}
/** @brief Post to the USER lane from an ISR. */
PC_INLINE proto_bool pc_pq_post_from_isr(const void *item)
{
    return PreemptQueue.post_from_isr(PC_PQ_LANE_USER, item);
}
/** @brief Drain the USER lane (host / inline drive). */
PC_INLINE void pc_pq_drain(void)
{
    PreemptQueue.drain(PC_PQ_LANE_USER);
}
/** @brief Stop the USER lane's task. */
PC_INLINE void pc_pq_stop(void)
{
    PreemptQueue.stop(PC_PQ_LANE_USER);
}
/** @brief True while the USER lane's task is running. */
PC_INLINE proto_bool pc_pq_running(void)
{
    return PreemptQueue.running(PC_PQ_LANE_USER);
}
/** @brief Peak items ever queued on the USER lane. */
PC_INLINE size_t pc_pq_high_water(void)
{
    return PreemptQueue.high_water(PC_PQ_LANE_USER);
}

#endif // PC_ENABLE_PREEMPT_QUEUE

PROTO_END_DECLS

#endif // PROTOCORE_PREEMPT_QUEUE_H
