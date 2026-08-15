// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file preempt_queue.h
 * @brief User-configurable preempting work queues + high-priority processing tasks
 *        (PROTOCORE_ENABLE_PREEMPT_QUEUE) - the v5 real-time ingest primitive.
 *
 * Fixed-capacity queues, each feeding one dedicated, core-pinned task. A producer posts
 * a fixed-size item; the scheduler **preempts** the lower-priority producer the instant
 * the item lands so it is processed immediately instead of on the next tick. Producers
 * post from a task (back or front, with a wait timeout) or from an ISR (interrupt-safe,
 * with an immediate context-switch request). Each processing task pops items in order
 * and hands each to a user handler.
 *
 * **Named lanes.** There are several queues, addressed by @ref protocore_pq_lane:
 *   - `PROTOCORE_PQ_LANE_USER` - the single lane exposed to the application. The no-lane
 *     `protocore_pq_*` API drives it. Lowest priority.
 *   - `PROTOCORE_PQ_LANE_DMA` / `_FORWARD` / `_DEVICE` - internal lanes for the library's own
 *     real-time work (DMA peripheral transfers, interface forwarding, device access).
 *     They run **above** the user lane (base `PROTOCORE_PQ_INTERNAL_PRIORITY`, DMA highest),
 *     so internal ingest always preempts user work; and below the network stack's own
 *     tasks so networking is never starved.
 *
 * This is the single normalized pipe for "hardware event -> process now": a DMA-complete
 * / GPIO / bus ISR posts a descriptor onto its lane, the lane's task drains it. Zero-heap
 * queue storage (static, compile-time PROTOCORE_PQ_DEPTH x PROTOCORE_PQ_ITEM_SIZE per lane; a
 * task's stack is created only when its lane starts, so unused lanes cost only their queue
 * storage), fail-closed on a full queue, no hot-path locks - so latency stays bounded.
 *
 * A build with no task backend posts into the same fixed per-lane ring, and
 * protocore_pq_drain[_lane]() runs the handler over what is queued, so the logic is
 * host-testable and behaves identically to the device's draining task.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PREEMPT_QUEUE_H
#define PROTOCORE_PREEMPT_QUEUE_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_PREEMPT_QUEUE

/**
 * @brief The preempting lanes, ordered by role. The USER lane is exposed to the
 *        application; the internal lanes run at a higher priority (DMA highest) so
 *        internal ingest preempts user work.
 */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_PQ_LANE_USER = 0, ///< exposed to the app (no-lane API); lowest priority
    PROTOCORE_PQ_LANE_DMA,      ///< internal: DMA peripheral transfers (highest)
    PROTOCORE_PQ_LANE_FORWARD,  ///< internal: interface forwarding
    PROTOCORE_PQ_LANE_DEVICE,   ///< internal: device access
    PROTOCORE_PQ_LANE_COUNT
} protocore_pq_lane;

/**
 * @brief Handler a lane's processing task invokes for each dequeued item.
 * @param item pointer to PROTOCORE_PQ_ITEM_SIZE bytes (the posted item).
 * @param ctx  the opaque pointer passed to protocore_pq_start[_lane]().
 */
typedef void (*protocore_pq_handler)(const void *item, void *ctx);

/**
 * @brief What a lane does with an item, and where it ranks.
 *
 * The priority is the QoS: it is what makes a post preempt lower-ranked work instead of waiting
 * for it, so it belongs to the lane rather than to whichever task happens to drain it.
 */
typedef struct
{
    protocore_pq_handler handler; ///< Called once per dequeued item (required).
    void *ctx;                    ///< Opaque, forwarded to @ref handler.
    uint8_t priority;             ///< Lane priority; 0 = the lane's default (internal ranks above user).
    uint8_t core;                 ///< Core to pin the lane's worker to; ignored where there is one.
    const char *name;             ///< Lane name (debug); may be NULL.
} protocore_pq_config;

/** @brief What one post carries, and how long it may wait for room. */
typedef struct
{
    const void *item;       ///< PROTOCORE_PQ_ITEM_SIZE bytes to copy onto the lane
    uint32_t timeout_ticks; ///< how long a post may block; 0 returns at once when the lane is full
} PqPostArgs;

/** @brief The lanes' own state and the calls that reach them, described only in preempt_queue.c. */
struct PreemptQueueInternal;

/**
 * @brief The preempting work queues.
 *
 * A caller names the lane, sets the members a call takes, invokes it through ::PreemptQueue, and
 * reads the outcome off the same handle. The queue storage and the tasks are behind @ref internal.
 *
 * @var PreemptQueueNs::lane           the lane every call names
 * @var PreemptQueueNs::cfg            what starting a lane installs
 * @var PreemptQueueNs::post_args      what a post carries
 * @var PreemptQueueNs::ok             a call's true/false outcome
 * @var PreemptQueueNs::n              the peak depth a high-water read reports
 * @var PreemptQueueNs::u8             the priority a lookup reports
 * @var PreemptQueueNs::post_from_isr  post from an ISR, then hand the CPU to the lane if it outranks us
 * @var PreemptQueueNs::post_urgent    post to the front of the lane, ahead of what is already queued
 * @var PreemptQueueNs::high_water     the most items ever queued at once, as a sizing aid
 * @var PreemptQueueNs::priority       the lane's default task priority
 * @var PreemptQueueNs::running        the lane's task is up
 * @var PreemptQueueNs::start          create the queue and spawn the lane's task
 * @var PreemptQueueNs::post           post to the back of the lane
 * @var PreemptQueueNs::drain          run the handler over what is queued, where no task does it
 * @var PreemptQueueNs::stop           stop the lane's task
 * @var PreemptQueueNs::internal       the lanes' state and the calls that reach them
 */
typedef struct
{
    protocore_pq_lane lane;

    const protocore_pq_config *cfg;
    PqPostArgs post_args;

    proto_bool ok;
    size_t n;
    uint8_t u8;

    void (*post_from_isr)(struct PreemptQueueInternal *ctx);
    void (*post_urgent)(struct PreemptQueueInternal *ctx);
    void (*high_water)(struct PreemptQueueInternal *ctx);
    void (*priority)(struct PreemptQueueInternal *ctx);
    void (*running)(struct PreemptQueueInternal *ctx);
    void (*start)(struct PreemptQueueInternal *ctx);
    void (*post)(struct PreemptQueueInternal *ctx);
    void (*drain)(struct PreemptQueueInternal *ctx);
    void (*stop)(struct PreemptQueueInternal *ctx);

    struct PreemptQueueInternal *internal;
} PreemptQueueNs;

/** @brief The one symbol this module exports. */
extern PreemptQueueNs PreemptQueue;

// --- User-lane API (drives PROTOCORE_PQ_LANE_USER) --------------------------------------------

/** @brief Start the USER lane. */
PROTOCORE_INLINE proto_bool protocore_pq_start(const protocore_pq_config *cfg)
{
    PreemptQueue.lane = PROTOCORE_PQ_LANE_USER;
    PreemptQueue.cfg = cfg;
    PreemptQueue.start(PreemptQueue.internal);
    return PreemptQueue.ok;
}
/** @brief Post to the back of the USER lane. */
PROTOCORE_INLINE proto_bool protocore_pq_post(const void *item, uint32_t timeout_ticks)
{
    PreemptQueue.lane = PROTOCORE_PQ_LANE_USER;
    PreemptQueue.post_args.item = item;
    PreemptQueue.post_args.timeout_ticks = timeout_ticks;
    PreemptQueue.post(PreemptQueue.internal);
    return PreemptQueue.ok;
}
/** @brief Post to the front of the USER lane (urgent). */
PROTOCORE_INLINE proto_bool protocore_pq_post_urgent(const void *item, uint32_t timeout_ticks)
{
    PreemptQueue.lane = PROTOCORE_PQ_LANE_USER;
    PreemptQueue.post_args.item = item;
    PreemptQueue.post_args.timeout_ticks = timeout_ticks;
    PreemptQueue.post_urgent(PreemptQueue.internal);
    return PreemptQueue.ok;
}
/** @brief Post to the USER lane from an ISR. */
PROTOCORE_INLINE proto_bool protocore_pq_post_from_isr(const void *item)
{
    PreemptQueue.lane = PROTOCORE_PQ_LANE_USER;
    PreemptQueue.post_args.item = item;
    PreemptQueue.post_from_isr(PreemptQueue.internal);
    return PreemptQueue.ok;
}
/** @brief Drain the USER lane (host / inline drive). */
PROTOCORE_INLINE void protocore_pq_drain(void)
{
    PreemptQueue.lane = PROTOCORE_PQ_LANE_USER;
    PreemptQueue.drain(PreemptQueue.internal);
}
/** @brief Stop the USER lane's task. */
PROTOCORE_INLINE void protocore_pq_stop(void)
{
    PreemptQueue.lane = PROTOCORE_PQ_LANE_USER;
    PreemptQueue.stop(PreemptQueue.internal);
}
/** @brief True while the USER lane's task is running. */
PROTOCORE_INLINE proto_bool protocore_pq_running(void)
{
    PreemptQueue.lane = PROTOCORE_PQ_LANE_USER;
    PreemptQueue.running(PreemptQueue.internal);
    return PreemptQueue.ok;
}
/** @brief Peak items ever queued on the USER lane. */
PROTOCORE_INLINE size_t protocore_pq_high_water(void)
{
    PreemptQueue.lane = PROTOCORE_PQ_LANE_USER;
    PreemptQueue.high_water(PreemptQueue.internal);
    return PreemptQueue.n;
}

#endif // PROTOCORE_ENABLE_PREEMPT_QUEUE

PROTOCORE_END_DECLS

#endif // PROTOCORE_PREEMPT_QUEUE_H
