// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file preempt_queue.c
 * @brief Preempting work queues + high-priority processing tasks - implementation.
 *
 * One queue + one task per lane (PROTOCORE_PQ_LANE_COUNT lanes). The no-lane protocore_pq_* API lives in the
 * header and drives the USER lane. Internal lanes default to a higher priority than the user lane
 * so internal ingest preempts user work.
 */

#include "server/system/preempt_queue.h"

#if PROTOCORE_ENABLE_PREEMPT_QUEUE

#include "core_setup/board_profiles/protocore_platform.h"
#include "mmgr/secure.h" // protocore_secure_persist_span: the item a lane's task receives into

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

/**
 * @brief The lanes' state and the calls that reach them - what PreemptQueueNs points at.
 *
 * @var PreemptQueueInternal::store  the lane pool and its queue storage
 * @var PreemptQueueInternal::ns     the handle a caller sets a call's members on
 */
struct PreemptQueueInternal
{
    struct PreemptQueueStorage *store;
    PreemptQueueNs *ns;
};

static struct PreemptQueueStorage s_store;

static struct PreemptQueueInternal s_pq = {.store = &s_store, .ns = &PreemptQueue};

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

static void note_depth(struct PreemptQueueInternal *restrict ctx, protocore_pq_lane lane, size_t waiting)
{
    if (waiting > ctx->store->pq.high_water[(size_t)lane])
    {
        ctx->store->pq.high_water[(size_t)lane] = waiting;
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
    if (!protocore_span_has_storage(item))
    {
        return;
    }
    for (;;)
    {
        if (protocore_platform_queue_recv(s_store.qq.q[(size_t)lane], item.buf, PROTOCORE_PLATFORM_WAIT_FOREVER) ==
                PROTOCORE_PLATFORM_OK &&
            s_store.pq.handler[(size_t)lane])
        {
            s_store.pq.handler[(size_t)lane](item.buf, s_store.pq.ctx[(size_t)lane]);
        }
    }
}

static void pq_start(struct PreemptQueueInternal *restrict ctx)
{
    const protocore_pq_lane lane = ctx->ns->lane;
    const protocore_pq_config *cfg = ctx->ns->cfg;

    ctx->ns->ok = PROTO_FALSE;
    if (!lane_ok(lane) || ctx->store->qq.run[(size_t)lane] || !cfg || !cfg->handler)
    {
        return;
    }
    ctx->store->pq.handler[(size_t)lane] = cfg->handler;
    ctx->store->pq.ctx[(size_t)lane] = cfg->ctx;
    ctx->store->pq.high_water[(size_t)lane] = 0;
    if (!ctx->store->qq.q[(size_t)lane])
    {
        ctx->store->qq.q[(size_t)lane] =
            protocore_platform_queue_create(PROTOCORE_PQ_DEPTH, PROTOCORE_PQ_ITEM_SIZE,
                                            ctx->store->qq.q_storage[(size_t)lane], &ctx->store->qq.q_struct[(size_t)lane]);
    }
    if (!ctx->store->qq.q[(size_t)lane])
    {
        return;
    }
    ctx->store->qq.run[(size_t)lane] = PROTO_TRUE;
    uint8_t prio = cfg->priority ? cfg->priority : lane_priority(lane);
    int core = cfg->core % PROTOCORE_PLATFORM_CORES;
    if (protocore_platform_task_start(pq_task, cfg->name ? cfg->name : lane_name(lane), PROTOCORE_PQ_STACK,
                                      (void *)(uintptr_t)lane, prio, &ctx->store->qq.task[(size_t)lane],
                                      core) != PROTOCORE_PLATFORM_PASS)
    {
        ctx->store->qq.run[(size_t)lane] = PROTO_FALSE;
        return;
    }
    ctx->ns->ok = PROTO_TRUE;
}

static void pq_post(struct PreemptQueueInternal *restrict ctx)
{
    const protocore_pq_lane lane = ctx->ns->lane;

    ctx->ns->ok = PROTO_FALSE;
    if (!lane_ok(lane) || !ctx->store->qq.q[(size_t)lane] || !ctx->ns->post_args.item)
    {
        return;
    }
    if (protocore_platform_queue_send(ctx->store->qq.q[(size_t)lane], ctx->ns->post_args.item,
                                      (protocore_platform_ticks)ctx->ns->post_args.timeout_ticks) != PROTOCORE_PLATFORM_OK)
    {
        return;
    }
    note_depth(ctx, lane, protocore_platform_queue_waiting(ctx->store->qq.q[(size_t)lane]));
    ctx->ns->ok = PROTO_TRUE;
}

static void pq_post_urgent(struct PreemptQueueInternal *restrict ctx)
{
    const protocore_pq_lane lane = ctx->ns->lane;

    ctx->ns->ok = PROTO_FALSE;
    if (!lane_ok(lane) || !ctx->store->qq.q[(size_t)lane] || !ctx->ns->post_args.item)
    {
        return;
    }
    if (protocore_platform_queue_send_front(ctx->store->qq.q[(size_t)lane], ctx->ns->post_args.item,
                                            (protocore_platform_ticks)ctx->ns->post_args.timeout_ticks) !=
        PROTOCORE_PLATFORM_OK)
    {
        return;
    }
    note_depth(ctx, lane, protocore_platform_queue_waiting(ctx->store->qq.q[(size_t)lane]));
    ctx->ns->ok = PROTO_TRUE;
}

static void pq_post_from_isr(struct PreemptQueueInternal *restrict ctx)
{
    const protocore_pq_lane lane = ctx->ns->lane;

    ctx->ns->ok = PROTO_FALSE;
    if (!lane_ok(lane) || !ctx->store->qq.q[(size_t)lane] || !ctx->ns->post_args.item)
    {
        return;
    }
    protocore_platform_status woke = PROTOCORE_PLATFORM_FALSE;
    if (protocore_platform_queue_send_isr(ctx->store->qq.q[(size_t)lane], ctx->ns->post_args.item, &woke) !=
        PROTOCORE_PLATFORM_OK)
    {
        return;
    }
    note_depth(ctx, lane, protocore_platform_queue_waiting_isr(ctx->store->qq.q[(size_t)lane]));
    protocore_platform_task_yield_from_isr(woke); // switch to the processing task now if it outranks us
    ctx->ns->ok = PROTO_TRUE;
}

static void pq_drain(struct PreemptQueueInternal *restrict ctx)
{
    (void)ctx;
    // The lane's task drains it; a build whose task backend does not run the entry function
    // drains nothing, so a caller that relies on this must pump the lane itself.
}

static void pq_stop(struct PreemptQueueInternal *restrict ctx)
{
    const protocore_pq_lane lane = ctx->ns->lane;

    if (!lane_ok(lane))
    {
        return;
    }
    ctx->store->qq.run[(size_t)lane] = PROTO_FALSE;
    if (ctx->store->qq.task[(size_t)lane]) // the task blocks on the queue forever, so stop it directly
    {
        protocore_platform_task_stop(ctx->store->qq.task[(size_t)lane]);
        ctx->store->qq.task[(size_t)lane] = NULL;
    }
}

static void pq_running(struct PreemptQueueInternal *restrict ctx)
{
    ctx->ns->ok = lane_ok(ctx->ns->lane) && ctx->store->qq.run[(size_t)ctx->ns->lane];
}

static void pq_high_water(struct PreemptQueueInternal *restrict ctx)
{
    ctx->ns->n = lane_ok(ctx->ns->lane) ? ctx->store->pq.high_water[(size_t)ctx->ns->lane] : 0;
}

static void pq_priority(struct PreemptQueueInternal *restrict ctx)
{
    ctx->ns->u8 = lane_priority(ctx->ns->lane);
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
                               .stop = pq_stop,
                               .internal = &s_pq};

#endif // PROTOCORE_ENABLE_PREEMPT_QUEUE
