// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file preempt_queue.c
 * @brief Preempting work queues + high-priority processing tasks - implementation.
 *
 * One queue + one task per lane (PC_PQ_LANE_COUNT lanes). The no-lane pc_pq_* API lives in the
 * header and forwards to the USER lane. Internal lanes default to a higher priority than the
 * user lane so internal ingest preempts user work.
 */

#include "network_drivers/session/preempt_queue.h"

#if PC_ENABLE_PREEMPT_QUEUE

#include "board_drivers/board_profiles/pc_platform.h"
#include <string.h>

// Common preempt-queue state (both host + device), owned by one instance (internal linkage):
// the per-lane handler, its context, and the high-water mark. The backend-specific state (the
// platform queue/task on device, the ring on host) lives in its own owner where its types are
// in scope. One named owner, unreachable from any other translation unit.
typedef struct
{
    pc_pq_handler handler[(size_t)PC_PQ_LANE_COUNT];
    void *ctx[(size_t)PC_PQ_LANE_COUNT];
    size_t high_water[(size_t)PC_PQ_LANE_COUNT]; // peak items queued at once (sizing aid)
} PqCtx;
static PqCtx s_pq;

// the coverage host never starts tasks, and this has internal linkage.
static const char *lane_name(pc_pq_lane lane)
{
    switch (lane)
    {
    case PC_PQ_LANE_DMA:
        return "pc_pq_dma";
    case PC_PQ_LANE_FORWARD:
        return "pc_pq_fwd";
    case PC_PQ_LANE_DEVICE:
        return "pc_pq_dev";
    default:
        return "pc_pq_user";
    }
}

static proto_bool lane_ok(pc_pq_lane lane)
{
    return (unsigned)lane < (unsigned)PC_PQ_LANE_COUNT;
}

// Default task priority per lane: internal lanes rank above the user lane (DMA highest),
// staying below the network stack's own tasks so networking is never starved.
uint8_t pc_pq_lane_priority(pc_pq_lane lane)
{
    switch (lane)
    {
    case PC_PQ_LANE_DMA:
        return (uint8_t)(PC_PQ_INTERNAL_PRIORITY + 2);
    case PC_PQ_LANE_FORWARD:
        return (uint8_t)(PC_PQ_INTERNAL_PRIORITY + 1);
    case PC_PQ_LANE_DEVICE:
        return (uint8_t)(PC_PQ_INTERNAL_PRIORITY);
    case PC_PQ_LANE_USER:
    default:
        return 5; // used only when a config passes priority 0; kept below the internal lanes
    }
}

#if PROTOCORE_HOT

// All queue-backend state, owned by one instance (internal linkage): the static queue
// storage/control blocks, the queue + task handles, and the per-lane run flag. One named
// owner, unreachable from any other translation unit.
typedef struct
{
    pc_platform_queue_ctrl q_struct[(size_t)PC_PQ_LANE_COUNT];
    uint8_t q_storage[(size_t)PC_PQ_LANE_COUNT][PC_PQ_DEPTH * PC_PQ_ITEM_SIZE];
    pc_platform_queue q[(size_t)PC_PQ_LANE_COUNT];
    pc_platform_task task[(size_t)PC_PQ_LANE_COUNT];
    volatile proto_bool run[(size_t)PC_PQ_LANE_COUNT];
} PqQueueCtx;
static PqQueueCtx s_pqq;

static void note_depth(pc_pq_lane lane, size_t waiting)
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
    pc_pq_lane lane = (pc_pq_lane)((uintptr_t)arg);
    // The task's own frame rather than a request-path allocation: pq_task is a task entry point whose
    // stack depth is stated at creation, and it never returns, so this buffer is permanent and counted.
    uint8_t item[PC_PQ_ITEM_SIZE]; // PC_ALLOW_STACK_ARRAY: task entry frame, sized at task creation
    for (;;)
    {
        if (pc_platform_queue_recv(s_pqq.q[(size_t)lane], item, PC_PLATFORM_WAIT_FOREVER) == PC_PLATFORM_OK &&
            s_pq.handler[(size_t)lane])
        {
            s_pq.handler[(size_t)lane](item, s_pq.ctx[(size_t)lane]);
        }
    }
}

proto_bool pc_pq_start_lane(pc_pq_lane lane, const pc_pq_config *cfg)
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
        s_pqq.q[(size_t)lane] = pc_platform_queue_create(PC_PQ_DEPTH, PC_PQ_ITEM_SIZE, s_pqq.q_storage[(size_t)lane],
                                                         &s_pqq.q_struct[(size_t)lane]);
    }
    if (!s_pqq.q[(size_t)lane])
    {
        return PROTO_FALSE;
    }
    s_pqq.run[(size_t)lane] = PROTO_TRUE;
    uint8_t prio = cfg->priority ? cfg->priority : pc_pq_lane_priority(lane);
    int core = cfg->core % PC_PLATFORM_CORES;
    if (pc_platform_task_start(pq_task, cfg->name ? cfg->name : lane_name(lane), PC_PQ_STACK, (void *)(uintptr_t)lane,
                               prio, &s_pqq.task[(size_t)lane], core) != PC_PLATFORM_PASS)
    {
        s_pqq.run[(size_t)lane] = PROTO_FALSE;
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

proto_bool pc_pq_post_lane(pc_pq_lane lane, const void *item, uint32_t timeout_ticks)
{
    if (!lane_ok(lane) || !s_pqq.q[(size_t)lane] || !item)
    {
        return PROTO_FALSE;
    }
    if (pc_platform_queue_send(s_pqq.q[(size_t)lane], item, (pc_platform_ticks)timeout_ticks) != PC_PLATFORM_OK)
    {
        return PROTO_FALSE;
    }
    note_depth(lane, pc_platform_queue_waiting(s_pqq.q[(size_t)lane]));
    return PROTO_TRUE;
}

proto_bool pc_pq_post_lane_urgent(pc_pq_lane lane, const void *item, uint32_t timeout_ticks)
{
    if (!lane_ok(lane) || !s_pqq.q[(size_t)lane] || !item)
    {
        return PROTO_FALSE;
    }
    if (pc_platform_queue_send_front(s_pqq.q[(size_t)lane], item, (pc_platform_ticks)timeout_ticks) != PC_PLATFORM_OK)
    {
        return PROTO_FALSE;
    }
    note_depth(lane, pc_platform_queue_waiting(s_pqq.q[(size_t)lane]));
    return PROTO_TRUE;
}

proto_bool pc_pq_post_lane_from_isr(pc_pq_lane lane, const void *item)
{
    if (!lane_ok(lane) || !s_pqq.q[(size_t)lane] || !item)
    {
        return PROTO_FALSE;
    }
    pc_platform_status woke = PC_PLATFORM_FALSE;
    if (pc_platform_queue_send_isr(s_pqq.q[(size_t)lane], item, &woke) != PC_PLATFORM_OK)
    {
        return PROTO_FALSE;
    }
    note_depth(lane, pc_platform_queue_waiting_isr(s_pqq.q[(size_t)lane]));
    pc_platform_task_yield_from_isr(woke); // switch to the processing task now if it outranks us
    return PROTO_TRUE;
}

void pc_pq_drain_lane(pc_pq_lane lane)
{
    (void)lane;
    // The lane's task drains on device; nothing to do here.
}

void pc_pq_stop_lane(pc_pq_lane lane)
{
    if (!lane_ok(lane))
    {
        return;
    }
    s_pqq.run[(size_t)lane] = PROTO_FALSE;
    if (s_pqq.task[(size_t)lane]) // the task blocks on the queue forever, so stop it directly
    {
        pc_platform_task_stop(s_pqq.task[(size_t)lane]);
        s_pqq.task[(size_t)lane] = NULL;
    }
}

proto_bool pc_pq_running_lane(pc_pq_lane lane)
{
    return lane_ok(lane) && s_pqq.run[(size_t)lane];
}

size_t pc_pq_high_water_lane(pc_pq_lane lane)
{
    return lane_ok(lane) ? s_pq.high_water[(size_t)lane] : 0;
}

#else // host build - fixed per-lane rings, no tasks; pc_pq_drain_lane() runs the handler

// All host-backend state, owned by one instance (internal linkage): the per-lane ring buffer,
// its head/tail/count cursors, and the started flag. One named owner, unreachable cross-TU.
typedef struct
{
    uint8_t buf[(size_t)PC_PQ_LANE_COUNT][PC_PQ_DEPTH * PC_PQ_ITEM_SIZE];
    size_t head[(size_t)PC_PQ_LANE_COUNT]; // next write slot
    size_t tail[(size_t)PC_PQ_LANE_COUNT]; // next read slot
    size_t count[(size_t)PC_PQ_LANE_COUNT];
    proto_bool started[(size_t)PC_PQ_LANE_COUNT];
} PqRingCtx;
static PqRingCtx s_pqr;

static void note_count(pc_pq_lane lane)
{
    if (s_pqr.count[(size_t)lane] > s_pq.high_water[(size_t)lane])
    {
        s_pq.high_water[(size_t)lane] = s_pqr.count[(size_t)lane];
    }
}

proto_bool pc_pq_start_lane(pc_pq_lane lane, const pc_pq_config *cfg)
{
    if (!lane_ok(lane) || s_pqr.started[(size_t)lane] || !cfg || !cfg->handler)
    {
        return PROTO_FALSE;
    }
    s_pq.handler[(size_t)lane] = cfg->handler;
    s_pq.ctx[(size_t)lane] = cfg->ctx;
    s_pqr.head[(size_t)lane] = 0;
    s_pqr.tail[(size_t)lane] = 0;
    s_pqr.count[(size_t)lane] = 0;
    s_pq.high_water[(size_t)lane] = 0;
    s_pqr.started[(size_t)lane] = PROTO_TRUE;
    return PROTO_TRUE;
}

proto_bool pc_pq_post_lane(pc_pq_lane lane, const void *item, uint32_t timeout_ticks)
{
    (void)timeout_ticks;
    if (!lane_ok(lane) || !item || s_pqr.count[(size_t)lane] >= PC_PQ_DEPTH)
    {
        return PROTO_FALSE; // fail closed when full
    }
    memcpy(s_pqr.buf[(size_t)lane] + s_pqr.head[(size_t)lane] * PC_PQ_ITEM_SIZE, item, PC_PQ_ITEM_SIZE);
    s_pqr.head[(size_t)lane] = (s_pqr.head[(size_t)lane] + 1) % PC_PQ_DEPTH;
    s_pqr.count[(size_t)lane]++;
    note_count(lane);
    return PROTO_TRUE;
}

proto_bool pc_pq_post_lane_urgent(pc_pq_lane lane, const void *item, uint32_t timeout_ticks)
{
    (void)timeout_ticks;
    if (!lane_ok(lane) || !item || s_pqr.count[(size_t)lane] >= PC_PQ_DEPTH)
    {
        return PROTO_FALSE;
    }
    s_pqr.tail[(size_t)lane] =
        (s_pqr.tail[(size_t)lane] + PC_PQ_DEPTH - 1) % PC_PQ_DEPTH; // step the read cursor back, write there
    memcpy(s_pqr.buf[(size_t)lane] + s_pqr.tail[(size_t)lane] * PC_PQ_ITEM_SIZE, item, PC_PQ_ITEM_SIZE);
    s_pqr.count[(size_t)lane]++;
    note_count(lane);
    return PROTO_TRUE;
}

proto_bool pc_pq_post_lane_from_isr(pc_pq_lane lane, const void *item)
{
    return pc_pq_post_lane(lane, item, 0); // no ISRs on host
}

void pc_pq_drain_lane(pc_pq_lane lane)
{
    if (!lane_ok(lane))
    {
        return;
    }
    while (s_pqr.count[(size_t)lane] > 0)
    {
        if (s_pq.handler[(size_t)lane])
        {
            s_pq.handler[(size_t)lane](s_pqr.buf[(size_t)lane] + s_pqr.tail[(size_t)lane] * PC_PQ_ITEM_SIZE,
                                       s_pq.ctx[(size_t)lane]);
        }
        s_pqr.tail[(size_t)lane] = (s_pqr.tail[(size_t)lane] + 1) % PC_PQ_DEPTH;
        s_pqr.count[(size_t)lane]--;
    }
}

void pc_pq_stop_lane(pc_pq_lane lane)
{
    if (lane_ok(lane))
    {
        s_pqr.started[(size_t)lane] = PROTO_FALSE;
    }
}

proto_bool pc_pq_running_lane(pc_pq_lane lane)
{
    return lane_ok(lane) && s_pqr.started[(size_t)lane];
}

size_t pc_pq_high_water_lane(pc_pq_lane lane)
{
    return lane_ok(lane) ? s_pq.high_water[(size_t)lane] : 0;
}

#endif // PROTOCORE_HOT

#endif // PC_ENABLE_PREEMPT_QUEUE
