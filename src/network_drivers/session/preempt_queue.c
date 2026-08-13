// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file preempt_queue.c
 * @brief Preempting work queues + high-priority processing tasks - implementation.
 *
 * One queue + one task per lane (PROTOCORE_PQ_LANE_COUNT lanes). The no-lane protocore_pq_* API lives in the
 * header and forwards to the USER lane. Internal lanes default to a higher priority than the
 * user lane so internal ingest preempts user work.
 */

#include "network_drivers/session/preempt_queue.h"

#if PROTOCORE_ENABLE_PREEMPT_QUEUE

#include "core_setup/board_profiles/protocore_platform.h"
#include "mmgr/secure.h" // protocore_secure_persist_span: the item a lane's task receives into

// Common preempt-queue state (both host + device), owned by one instance (internal linkage):
// the per-lane handler, its context, and the high-water mark. The backend-specific state (the
// platform queue/task on device, the ring on host) lives in its own owner where its types are
// in scope. One named owner, unreachable from any other translation unit.
typedef struct
{
    protocore_pq_handler handler[(size_t)PROTOCORE_PQ_LANE_COUNT];
    void *ctx[(size_t)PROTOCORE_PQ_LANE_COUNT];
    size_t high_water[(size_t)PROTOCORE_PQ_LANE_COUNT]; // peak items queued at once (sizing aid)
} PqCtx;
static PqCtx s_pq;

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
static uint8_t protocore_pq_lane_priority(protocore_pq_lane lane)
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

// All queue-backend state, owned by one instance (internal linkage): the static queue
// storage/control blocks, the queue + task handles, and the per-lane run flag. One named
// owner, unreachable from any other translation unit.
typedef struct
{
    protocore_platform_queue_ctrl q_struct[(size_t)PROTOCORE_PQ_LANE_COUNT];
    uint8_t q_storage[(size_t)PROTOCORE_PQ_LANE_COUNT][PROTOCORE_PQ_DEPTH * PROTOCORE_PQ_ITEM_SIZE];
    protocore_platform_queue q[(size_t)PROTOCORE_PQ_LANE_COUNT];
    protocore_platform_task task[(size_t)PROTOCORE_PQ_LANE_COUNT];
    volatile proto_bool run[(size_t)PROTOCORE_PQ_LANE_COUNT];
} PqQueueCtx;
static PqQueueCtx s_pqq;

static void note_depth(protocore_pq_lane lane, size_t waiting)
{
    if (waiting > s_pq.high_water[(size_t)lane])
    {
        s_pq.high_water[(size_t)lane] = waiting;
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
        if (protocore_platform_queue_recv(s_pqq.q[(size_t)lane], item.buf, PROTOCORE_PLATFORM_WAIT_FOREVER) == PROTOCORE_PLATFORM_OK &&
            s_pq.handler[(size_t)lane])
        {
            s_pq.handler[(size_t)lane](item.buf, s_pq.ctx[(size_t)lane]);
        }
    }
}

static proto_bool protocore_pq_start_lane(protocore_pq_lane lane, const protocore_pq_config *cfg)
{
    if (!lane_ok(lane) || s_pqq.run[(size_t)lane] || !cfg || !cfg->handler)
    {
        return PROTO_FALSE;
    }
    s_pq.handler[(size_t)lane] = cfg->handler;
    s_pq.ctx[(size_t)lane] = cfg->ctx;
    s_pq.high_water[(size_t)lane] = 0;
    if (!s_pqq.q[(size_t)lane])
    {
        s_pqq.q[(size_t)lane] = protocore_platform_queue_create(PROTOCORE_PQ_DEPTH, PROTOCORE_PQ_ITEM_SIZE, s_pqq.q_storage[(size_t)lane],
                                                         &s_pqq.q_struct[(size_t)lane]);
    }
    if (!s_pqq.q[(size_t)lane])
    {
        return PROTO_FALSE;
    }
    s_pqq.run[(size_t)lane] = PROTO_TRUE;
    uint8_t prio = cfg->priority ? cfg->priority : protocore_pq_lane_priority(lane);
    int core = cfg->core % PROTOCORE_PLATFORM_CORES;
    if (protocore_platform_task_start(pq_task, cfg->name ? cfg->name : lane_name(lane), PROTOCORE_PQ_STACK, (void *)(uintptr_t)lane,
                               prio, &s_pqq.task[(size_t)lane], core) != PROTOCORE_PLATFORM_PASS)
    {
        s_pqq.run[(size_t)lane] = PROTO_FALSE;
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

static proto_bool protocore_pq_post_lane(protocore_pq_lane lane, const void *item, uint32_t timeout_ticks)
{
    if (!lane_ok(lane) || !s_pqq.q[(size_t)lane] || !item)
    {
        return PROTO_FALSE;
    }
    if (protocore_platform_queue_send(s_pqq.q[(size_t)lane], item, (protocore_platform_ticks)timeout_ticks) != PROTOCORE_PLATFORM_OK)
    {
        return PROTO_FALSE;
    }
    note_depth(lane, protocore_platform_queue_waiting(s_pqq.q[(size_t)lane]));
    return PROTO_TRUE;
}

static proto_bool protocore_pq_post_lane_urgent(protocore_pq_lane lane, const void *item, uint32_t timeout_ticks)
{
    if (!lane_ok(lane) || !s_pqq.q[(size_t)lane] || !item)
    {
        return PROTO_FALSE;
    }
    if (protocore_platform_queue_send_front(s_pqq.q[(size_t)lane], item, (protocore_platform_ticks)timeout_ticks) != PROTOCORE_PLATFORM_OK)
    {
        return PROTO_FALSE;
    }
    note_depth(lane, protocore_platform_queue_waiting(s_pqq.q[(size_t)lane]));
    return PROTO_TRUE;
}

static proto_bool protocore_pq_post_lane_from_isr(protocore_pq_lane lane, const void *item)
{
    if (!lane_ok(lane) || !s_pqq.q[(size_t)lane] || !item)
    {
        return PROTO_FALSE;
    }
    protocore_platform_status woke = PROTOCORE_PLATFORM_FALSE;
    if (protocore_platform_queue_send_isr(s_pqq.q[(size_t)lane], item, &woke) != PROTOCORE_PLATFORM_OK)
    {
        return PROTO_FALSE;
    }
    note_depth(lane, protocore_platform_queue_waiting_isr(s_pqq.q[(size_t)lane]));
    protocore_platform_task_yield_from_isr(woke); // switch to the processing task now if it outranks us
    return PROTO_TRUE;
}

static void protocore_pq_drain_lane(protocore_pq_lane lane)
{
    (void)lane;
    // The lane's task drains it; a build whose task backend does not run the entry function
    // drains nothing, so a caller that relies on this must pump the lane itself.
}

static void protocore_pq_stop_lane(protocore_pq_lane lane)
{
    if (!lane_ok(lane))
    {
        return;
    }
    s_pqq.run[(size_t)lane] = PROTO_FALSE;
    if (s_pqq.task[(size_t)lane]) // the task blocks on the queue forever, so stop it directly
    {
        protocore_platform_task_stop(s_pqq.task[(size_t)lane]);
        s_pqq.task[(size_t)lane] = NULL;
    }
}

static proto_bool protocore_pq_running_lane(protocore_pq_lane lane)
{
    return lane_ok(lane) && s_pqq.run[(size_t)lane];
}

static size_t protocore_pq_high_water_lane(protocore_pq_lane lane)
{
    return lane_ok(lane) ? s_pq.high_water[(size_t)lane] : 0;
}

// Designated, so a member's position in the struct does not decide what it binds to.
const PreemptQueueNs PreemptQueue = {.post_from_isr = protocore_pq_post_lane_from_isr,
                                     .post_urgent = protocore_pq_post_lane_urgent,
                                     .high_water = protocore_pq_high_water_lane,
                                     .priority = protocore_pq_lane_priority,
                                     .running = protocore_pq_running_lane,
                                     .start = protocore_pq_start_lane,
                                     .post = protocore_pq_post_lane,
                                     .drain = protocore_pq_drain_lane,
                                     .stop = protocore_pq_stop_lane};

#endif // PROTOCORE_ENABLE_PREEMPT_QUEUE
