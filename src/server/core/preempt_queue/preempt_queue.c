// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file preempt_queue.c
 * @brief Preempting work queues + high-priority processing tasks - implementation.
 *
 * One queue + one task per lane (PROTOCORE_PQ_LANE_COUNT lanes). The no-lane protocore_pq_* API lives in the
 * header and drives the USER lane. Internal lanes default to a higher priority than the user lane
 * so internal ingest preempts user work.
 */

#include "server/core/preempt_queue/preempt_queue.h"
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from

#if PROTOCORE_ENABLE_PREEMPT_QUEUE

#include "config/platform/platform.h"
#include "mmgr/secure/secure.h" // protocore_secure_persist_span: the item a lane's task receives into

/**
 * @brief The lanes' compile-time storage: what each does with an item, and the queue behind it.
 *
 * Two owners rather than one: the handler, its context and the high-water mark are the same on
 * every target, while the queue control block, its bytes, the handles and the run flag are the
 * backend's and are only nameable where its types are in scope.
 */
typedef struct
{
    protocore_pq_handler handler[(size_t)PROTOCORE_PQ_LANE_COUNT];
    void *ctx[(size_t)PROTOCORE_PQ_LANE_COUNT];
    size_t high_water[(size_t)PROTOCORE_PQ_LANE_COUNT]; // peak items queued at once (sizing aid)
} PqCtx;

typedef struct
{
    protocore_platform_queue_ctrl q_struct[(size_t)PROTOCORE_PQ_LANE_COUNT];
    uint8_t q_storage[(size_t)PROTOCORE_PQ_LANE_COUNT][PROTOCORE_PQ_DEPTH * PROTOCORE_PQ_ITEM_SIZE];
    protocore_platform_queue q[(size_t)PROTOCORE_PQ_LANE_COUNT];
    protocore_platform_task task[(size_t)PROTOCORE_PQ_LANE_COUNT];
    volatile proto_bool run[(size_t)PROTOCORE_PQ_LANE_COUNT];
} PqQueueCtx;

struct PreemptQueueStorage
{
    PqCtx pq;      ///< what each lane does with an item, and how deep it has ever been
    PqQueueCtx qq; ///< the platform queue and task backing each lane
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define PREEMPT_QUEUE_OFF_CTX 0u
static_assert(PREEMPT_QUEUE_OFF_CTX + sizeof(struct PreemptQueueStorage) <= PROTOCORE_PREEMPT_QUEUE_BORROW,
              "PROTOCORE_PREEMPT_QUEUE_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define PREEMPT_QUEUE_CTX(w) ((struct PreemptQueueStorage *)(void *)((w) + PREEMPT_QUEUE_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_PREEMPT_QUEUE_BORROW persistent bytes, or null while the pool was short
} PreemptQueueOwnCtx;
static PreemptQueueOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_preempt_queue_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_plaintext_persist_span(PROTOCORE_PREEMPT_QUEUE_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

static const char *lane_name(protocore_pq_lane lane)
{
    switch (lane)
    {
    case PROTOCORE_PQ_LANE_DMA:
        return "protocore_pq_dma";
    case PROTOCORE_PQ_LANE_FORWARD:
        return "protocore_pq_fwd";
    case PROTOCORE_PQ_LANE_DEVICE:
        return "protocore_pq_dev";
    default:
        return "protocore_pq_user";
    }
}

static proto_bool lane_ok(protocore_pq_lane lane)
{
    return (unsigned)lane < (unsigned)PROTOCORE_PQ_LANE_COUNT;
}

// Default task priority per lane: internal lanes rank above the user lane (DMA highest),
// staying below the network stack's own tasks so networking is never starved.
static uint8_t lane_priority(protocore_pq_lane lane)
{
    switch (lane)
    {
    case PROTOCORE_PQ_LANE_DMA:
        return (uint8_t)(PROTOCORE_PQ_INTERNAL_PRIORITY + 2);
    case PROTOCORE_PQ_LANE_FORWARD:
        return (uint8_t)(PROTOCORE_PQ_INTERNAL_PRIORITY + 1);
    case PROTOCORE_PQ_LANE_DEVICE:
        return (uint8_t)(PROTOCORE_PQ_INTERNAL_PRIORITY);
    case PROTOCORE_PQ_LANE_USER:
    default:
        return 5; // used only when a config passes priority 0; kept below the internal lanes
    }
}

static void note_depth(uint8_t *restrict work, protocore_pq_lane lane, size_t waiting)
{
    if (waiting > PREEMPT_QUEUE_CTX(work)->pq.high_water[(size_t)lane])
    {
        PREEMPT_QUEUE_CTX(work)->pq.high_water[(size_t)lane] = waiting;
    }
}

// The dedicated processing task for one lane (its id is the task parameter): block until
// an item lands (so a post preempts straight into here), then run the handler for each
// item in order. It blocks forever between items (zero idle wakeups).
static void pq_task(void *arg)
{
    protocore_pq_lane lane = (protocore_pq_lane)((uintptr_t)arg);
    // The entry never returns, so this item is permanent: it comes from the persistent end, which no
    // reset or release walks. A lane that cannot get one has nowhere to receive into and stops.
    protocore_span item = protocore_secure_persist_span(PROTOCORE_PQ_ITEM_SIZE);
    if (!span.has_storage(item))
    {
        return;
    }
    for (;;)
    {
        if (protocore_platform_queue_recv(PREEMPT_QUEUE_CTX(protocore_preempt_queue_span())->qq.q[(size_t)lane],
                                          item.buf, PROTOCORE_PLATFORM_WAIT_FOREVER) == PROTOCORE_PLATFORM_OK &&
            PREEMPT_QUEUE_CTX(protocore_preempt_queue_span())->pq.handler[(size_t)lane])
        {
            PREEMPT_QUEUE_CTX(protocore_preempt_queue_span())
                ->pq.handler[(size_t)lane](item.buf,
                                           PREEMPT_QUEUE_CTX(protocore_preempt_queue_span())->pq.ctx[(size_t)lane]);
        }
    }
}

static void pq_start(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    const protocore_pq_lane lane = PreemptQueue.lane;
    const protocore_pq_config *cfg = PreemptQueue.cfg;

    PreemptQueue.ok = PROTO_FALSE;
    if (!lane_ok(lane) || PREEMPT_QUEUE_CTX(work)->qq.run[(size_t)lane] || !cfg || !cfg->handler)
    {
        return;
    }
    PREEMPT_QUEUE_CTX(work)->pq.handler[(size_t)lane] = cfg->handler;
    PREEMPT_QUEUE_CTX(work)->pq.ctx[(size_t)lane] = cfg->ctx;
    PREEMPT_QUEUE_CTX(work)->pq.high_water[(size_t)lane] = 0;
    if (!PREEMPT_QUEUE_CTX(work)->qq.q[(size_t)lane])
    {
        PREEMPT_QUEUE_CTX(work)->qq.q[(size_t)lane] = protocore_platform_queue_create(
            PROTOCORE_PQ_DEPTH, PROTOCORE_PQ_ITEM_SIZE, PREEMPT_QUEUE_CTX(work)->qq.q_storage[(size_t)lane],
            &PREEMPT_QUEUE_CTX(work)->qq.q_struct[(size_t)lane]);
    }
    if (!PREEMPT_QUEUE_CTX(work)->qq.q[(size_t)lane])
    {
        return;
    }
    PREEMPT_QUEUE_CTX(work)->qq.run[(size_t)lane] = PROTO_TRUE;
    uint8_t prio = cfg->priority ? cfg->priority : lane_priority(lane);
    int core = cfg->core % PROTOCORE_PLATFORM_CORES;
    if (protocore_platform_task_start(pq_task, cfg->name ? cfg->name : lane_name(lane), PROTOCORE_PQ_STACK,
                                      (void *)(uintptr_t)lane, prio, &PREEMPT_QUEUE_CTX(work)->qq.task[(size_t)lane],
                                      core) != PROTOCORE_PLATFORM_PASS)
    {
        PREEMPT_QUEUE_CTX(work)->qq.run[(size_t)lane] = PROTO_FALSE;
        return;
    }
    PreemptQueue.ok = PROTO_TRUE;
}

static void pq_post(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    const protocore_pq_lane lane = PreemptQueue.lane;

    PreemptQueue.ok = PROTO_FALSE;
    if (!lane_ok(lane) || !PREEMPT_QUEUE_CTX(work)->qq.q[(size_t)lane] || !PreemptQueue.post_args.item)
    {
        return;
    }
    if (protocore_platform_queue_send(PREEMPT_QUEUE_CTX(work)->qq.q[(size_t)lane], PreemptQueue.post_args.item,
                                      (protocore_platform_ticks)PreemptQueue.post_args.timeout_ticks) !=
        PROTOCORE_PLATFORM_OK)
    {
        return;
    }
    note_depth(work, lane, protocore_platform_queue_waiting(PREEMPT_QUEUE_CTX(work)->qq.q[(size_t)lane]));
    PreemptQueue.ok = PROTO_TRUE;
}

static void pq_post_urgent(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    const protocore_pq_lane lane = PreemptQueue.lane;

    PreemptQueue.ok = PROTO_FALSE;
    if (!lane_ok(lane) || !PREEMPT_QUEUE_CTX(work)->qq.q[(size_t)lane] || !PreemptQueue.post_args.item)
    {
        return;
    }
    if (protocore_platform_queue_send_front(PREEMPT_QUEUE_CTX(work)->qq.q[(size_t)lane], PreemptQueue.post_args.item,
                                            (protocore_platform_ticks)PreemptQueue.post_args.timeout_ticks) !=
        PROTOCORE_PLATFORM_OK)
    {
        return;
    }
    note_depth(work, lane, protocore_platform_queue_waiting(PREEMPT_QUEUE_CTX(work)->qq.q[(size_t)lane]));
    PreemptQueue.ok = PROTO_TRUE;
}

static void pq_post_from_isr(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    const protocore_pq_lane lane = PreemptQueue.lane;

    PreemptQueue.ok = PROTO_FALSE;
    if (!lane_ok(lane) || !PREEMPT_QUEUE_CTX(work)->qq.q[(size_t)lane] || !PreemptQueue.post_args.item)
    {
        return;
    }
    protocore_platform_status woke = PROTOCORE_PLATFORM_FALSE;
    if (protocore_platform_queue_send_isr(PREEMPT_QUEUE_CTX(work)->qq.q[(size_t)lane], PreemptQueue.post_args.item,
                                          &woke) != PROTOCORE_PLATFORM_OK)
    {
        return;
    }
    note_depth(work, lane, protocore_platform_queue_waiting_isr(PREEMPT_QUEUE_CTX(work)->qq.q[(size_t)lane]));
    protocore_platform_task_yield_from_isr(woke); // switch to the processing task now if it outranks us
    PreemptQueue.ok = PROTO_TRUE;
}

static void pq_drain(uint8_t *restrict work)
{
    (void)work;
    // The lane's task drains it; a build whose task backend does not run the entry function
    // drains nothing, so a caller that relies on this must pump the lane itself.
}

static void pq_stop(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    const protocore_pq_lane lane = PreemptQueue.lane;

    if (!lane_ok(lane))
    {
        return;
    }
    PREEMPT_QUEUE_CTX(work)->qq.run[(size_t)lane] = PROTO_FALSE;
    if (PREEMPT_QUEUE_CTX(work)->qq.task[(size_t)lane]) // the task blocks on the queue forever, so stop it directly
    {
        protocore_platform_task_stop(PREEMPT_QUEUE_CTX(work)->qq.task[(size_t)lane]);
        PREEMPT_QUEUE_CTX(work)->qq.task[(size_t)lane] = NULL;
    }
}

static void pq_running(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    PreemptQueue.ok = lane_ok(PreemptQueue.lane) && PREEMPT_QUEUE_CTX(work)->qq.run[(size_t)PreemptQueue.lane];
}

static void pq_high_water(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    PreemptQueue.n = lane_ok(PreemptQueue.lane) ? PREEMPT_QUEUE_CTX(work)->pq.high_water[(size_t)PreemptQueue.lane] : 0;
}

static void pq_priority(uint8_t *restrict work)
{
    (void)work;
    PreemptQueue.u8 = lane_priority(PreemptQueue.lane);
}

// Designated, so a member's position in the struct does not decide what it binds to.
PreemptQueueNs PreemptQueue = {.post_from_isr = pq_post_from_isr,
                               .post_urgent = pq_post_urgent,
                               .high_water = pq_high_water,
                               .priority = pq_priority,
                               .running = pq_running,
                               .start = pq_start,
                               .post = pq_post,
                               .drain = pq_drain,
                               .stop = pq_stop};

#endif // PROTOCORE_ENABLE_PREEMPT_QUEUE
