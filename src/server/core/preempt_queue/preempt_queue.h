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

#if PROTOCORE_ENABLE_PREEMPT_QUEUE

PROTOCORE_BEGIN_DECLS

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
 */
typedef struct
{
    protocore_pq_lane lane;
    const protocore_pq_config *cfg;
    PqPostArgs post_args;
    proto_bool ok;
    size_t n;
    uint8_t u8;
} PreemptQueueVars;

/** @brief The operands and the outcome. */
extern PreemptQueueVars PreemptQueueV;

/** @brief The entries. */
typedef struct
{
    void (*const post_from_isr)(uint8_t *restrict work);
    void (*const post_urgent)(uint8_t *restrict work);
    void (*const high_water)(uint8_t *restrict work);
    void (*const priority)(uint8_t *restrict work);
    void (*const running)(uint8_t *restrict work);
    void (*const start)(uint8_t *restrict work);
    void (*const post)(uint8_t *restrict work);
    void (*const drain)(uint8_t *restrict work);
    void (*const stop)(uint8_t *restrict work);
} PreemptQueueNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in PreemptQueueV or a region of the borrow at a fixed offset.
void protocore_preempt_queue_post_from_isr(uint8_t *restrict work);
void protocore_preempt_queue_post_urgent(uint8_t *restrict work);
void protocore_preempt_queue_high_water(uint8_t *restrict work);
void protocore_preempt_queue_priority(uint8_t *restrict work);
void protocore_preempt_queue_running(uint8_t *restrict work);
void protocore_preempt_queue_start(uint8_t *restrict work);
void protocore_preempt_queue_post(uint8_t *restrict work);
void protocore_preempt_queue_drain(uint8_t *restrict work);
void protocore_preempt_queue_stop(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `PreemptQueue.post_from_isr(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const PreemptQueueNs PreemptQueue __attribute__((unused)) = {
    .post_from_isr = protocore_preempt_queue_post_from_isr,
    .post_urgent = protocore_preempt_queue_post_urgent,
    .high_water = protocore_preempt_queue_high_water,
    .priority = protocore_preempt_queue_priority,
    .running = protocore_preempt_queue_running,
    .start = protocore_preempt_queue_start,
    .post = protocore_preempt_queue_post,
    .drain = protocore_preempt_queue_drain,
    .stop = protocore_preempt_queue_stop,
};

/**
 * @brief The PROTOCORE_PREEMPT_QUEUE_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_preempt_queue_span(void);

// --- User-lane API (drives PROTOCORE_PQ_LANE_USER) --------------------------------------------

/** @brief Start the USER lane. */
PROTOCORE_INLINE proto_bool protocore_pq_start(const protocore_pq_config *cfg)
{
    PreemptQueue.lane = PROTOCORE_PQ_LANE_USER;
    PreemptQueue.cfg = cfg;
    PreemptQueue.start(protocore_preempt_queue_span());
    return PreemptQueue.ok;
}
/** @brief Post to the back of the USER lane. */
PROTOCORE_INLINE proto_bool protocore_pq_post(const void *item, uint32_t timeout_ticks)
{
    PreemptQueue.lane = PROTOCORE_PQ_LANE_USER;
    PreemptQueue.post_args.item = item;
    PreemptQueue.post_args.timeout_ticks = timeout_ticks;
    PreemptQueue.post(protocore_preempt_queue_span());
    return PreemptQueue.ok;
}
/** @brief Post to the front of the USER lane (urgent). */
PROTOCORE_INLINE proto_bool protocore_pq_post_urgent(const void *item, uint32_t timeout_ticks)
{
    PreemptQueue.lane = PROTOCORE_PQ_LANE_USER;
    PreemptQueue.post_args.item = item;
    PreemptQueue.post_args.timeout_ticks = timeout_ticks;
    PreemptQueue.post_urgent(protocore_preempt_queue_span());
    return PreemptQueue.ok;
}
/** @brief Post to the USER lane from an ISR. */
PROTOCORE_INLINE proto_bool protocore_pq_post_from_isr(const void *item)
{
    PreemptQueue.lane = PROTOCORE_PQ_LANE_USER;
    PreemptQueue.post_args.item = item;
    PreemptQueue.post_from_isr(protocore_preempt_queue_span());
    return PreemptQueue.ok;
}
/** @brief Drain the USER lane (host / inline drive). */
PROTOCORE_INLINE void protocore_pq_drain(void)
{
    PreemptQueue.lane = PROTOCORE_PQ_LANE_USER;
    PreemptQueue.drain(protocore_preempt_queue_span());
}
/** @brief Stop the USER lane's task. */
PROTOCORE_INLINE void protocore_pq_stop(void)
{
    PreemptQueue.lane = PROTOCORE_PQ_LANE_USER;
    PreemptQueue.stop(protocore_preempt_queue_span());
}
/** @brief True while the USER lane's task is running. */
PROTOCORE_INLINE proto_bool protocore_pq_running(void)
{
    PreemptQueue.lane = PROTOCORE_PQ_LANE_USER;
    PreemptQueue.running(protocore_preempt_queue_span());
    return PreemptQueue.ok;
}
/** @brief Peak items ever queued on the USER lane. */
PROTOCORE_INLINE size_t protocore_pq_high_water(void)
{
    PreemptQueue.lane = PROTOCORE_PQ_LANE_USER;
    PreemptQueue.high_water(protocore_preempt_queue_span());
    return PreemptQueue.n;
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_PREEMPT_QUEUE

#endif // PROTOCORE_PREEMPT_QUEUE_H
