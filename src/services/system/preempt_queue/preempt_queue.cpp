// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file preempt_queue.cpp
 * @brief Preempting work queues + high-priority processing tasks - implementation.
 *
 * One queue + one task per lane (pc_pq_lane::PC_PQ_LANE_COUNT lanes). The no-lane pc_pq_* API
 * lives in the header and forwards to the USER lane. Internal lanes default to a higher
 * priority than the user lane so internal ingest preempts user work.
 */

#include "services/system/preempt_queue/preempt_queue.h"

#if PC_ENABLE_PREEMPT_QUEUE

#include <string.h>

#if defined(ARDUINO)
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#endif
namespace
{
// Common preempt-queue state (both host + device), owned by one instance (internal linkage):
// the per-lane handler, its context, and the high-water mark. The backend-specific state (the
// FreeRTOS queue/task on device, the ring on host) lives in its own owner where its types are
// in scope. One named owner, unreachable from any other translation unit.
struct PqCtx
{
    pc_pq_handler handler[(size_t)pc_pq_lane::PC_PQ_LANE_COUNT] = {};
    void *ctx[(size_t)pc_pq_lane::PC_PQ_LANE_COUNT] = {};
    size_t high_water[(size_t)pc_pq_lane::PC_PQ_LANE_COUNT] = {}; // peak items queued at once (sizing aid)
};
PqCtx s_pq;

// GCOVR_EXCL_START  ESP32-only: only used to name the FreeRTOS task in the ARDUINO pc_pq_start_lane;
// the coverage host never starts tasks, and this is internal-linkage (anon namespace) so not callable.
const char *lane_name(pc_pq_lane lane)
{
    switch (lane)
    {
    case pc_pq_lane::PC_PQ_LANE_DMA:
        return "pc_pq_dma";
    case pc_pq_lane::PC_PQ_LANE_FORWARD:
        return "pc_pq_fwd";
    case pc_pq_lane::PC_PQ_LANE_DEVICE:
        return "pc_pq_dev";
    default:
        return "pc_pq_user";
    }
}
// GCOVR_EXCL_STOP

bool lane_ok(pc_pq_lane lane)
{
    return (unsigned)lane < (unsigned)pc_pq_lane::PC_PQ_LANE_COUNT;
}
} // namespace

// Default task priority per lane: internal lanes rank above the user lane (DMA highest),
// staying below the lwIP tcpip (18) / WiFi tasks so networking is never starved.
uint8_t pc_pq_lane_priority(pc_pq_lane lane)
{
    switch (lane)
    {
    case pc_pq_lane::PC_PQ_LANE_DMA:
        return (uint8_t)(PC_PQ_INTERNAL_PRIORITY + 2);
    case pc_pq_lane::PC_PQ_LANE_FORWARD:
        return (uint8_t)(PC_PQ_INTERNAL_PRIORITY + 1);
    case pc_pq_lane::PC_PQ_LANE_DEVICE:
        return (uint8_t)(PC_PQ_INTERNAL_PRIORITY);
    case pc_pq_lane::PC_PQ_LANE_USER:
    default:
        return 5; // used only when a config passes priority 0; kept below the internal lanes
    }
}

#ifdef ARDUINO

namespace
{
// All FreeRTOS-backend state, owned by one instance (internal linkage): the static queue
// storage/control blocks, the queue + task handles, and the per-lane run flag. One named
// owner, unreachable from any other translation unit.
struct PqQueueCtx
{
    StaticQueue_t q_struct[(size_t)pc_pq_lane::PC_PQ_LANE_COUNT];
    uint8_t q_storage[(size_t)pc_pq_lane::PC_PQ_LANE_COUNT][PC_PQ_DEPTH * PC_PQ_ITEM_SIZE];
    QueueHandle_t q[(size_t)pc_pq_lane::PC_PQ_LANE_COUNT] = {};
    TaskHandle_t task[(size_t)pc_pq_lane::PC_PQ_LANE_COUNT] = {};
    volatile bool run[(size_t)pc_pq_lane::PC_PQ_LANE_COUNT] = {};
};
PqQueueCtx s_pqq;

void note_depth(pc_pq_lane lane, UBaseType_t waiting)
{
    if ((size_t)waiting > s_pq.high_water[(size_t)lane])
    {
        s_pq.high_water[(size_t)lane] = (size_t)waiting;
    }
}

// The dedicated processing task for one lane (its id is the task parameter): block until
// an item lands (so a post preempts straight into here), then run the handler for each
// item in order. It blocks forever between items (zero idle wakeups).
void pq_task(void *arg)
{
    pc_pq_lane lane = static_cast<pc_pq_lane>(reinterpret_cast<uintptr_t>(arg));
    uint8_t item[PC_PQ_ITEM_SIZE];
    for (;;)
    {
        if (xQueueReceive(s_pqq.q[(size_t)lane], item, portMAX_DELAY) == pdTRUE && s_pq.handler[(size_t)lane])
        {
            s_pq.handler[(size_t)lane](item, s_pq.ctx[(size_t)lane]);
        }
    }
}
} // namespace

bool pc_pq_start_lane(pc_pq_lane lane, const pc_pq_config *cfg)
{
    if (!lane_ok(lane) || s_pqq.run[(size_t)lane] || !cfg || !cfg->handler)
    {
        return false;
    }
    s_pq.handler[(size_t)lane] = cfg->handler;
    s_pq.ctx[(size_t)lane] = cfg->ctx;
    s_pq.high_water[(size_t)lane] = 0;
    if (!s_pqq.q[(size_t)lane])
    {
        s_pqq.q[(size_t)lane] = xQueueCreateStatic(PC_PQ_DEPTH, PC_PQ_ITEM_SIZE, s_pqq.q_storage[(size_t)lane],
                                                   &s_pqq.q_struct[(size_t)lane]);
    }
    if (!s_pqq.q[(size_t)lane])
    {
        return false;
    }
    s_pqq.run[(size_t)lane] = true;
    uint8_t prio = cfg->priority ? cfg->priority : pc_pq_lane_priority(lane);
    int core = cfg->core % portNUM_PROCESSORS;
    if (xTaskCreatePinnedToCore(pq_task, cfg->name ? cfg->name : lane_name(lane), PC_PQ_STACK, (void *)(uintptr_t)lane,
                                prio, &s_pqq.task[(size_t)lane], core) != pdPASS)
    {
        s_pqq.run[(size_t)lane] = false;
        return false;
    }
    return true;
}

bool pc_pq_post_lane(pc_pq_lane lane, const void *item, uint32_t timeout_ticks)
{
    if (!lane_ok(lane) || !s_pqq.q[(size_t)lane] || !item)
    {
        return false;
    }
    if (xQueueSendToBack(s_pqq.q[(size_t)lane], item, (TickType_t)timeout_ticks) != pdTRUE)
    {
        return false;
    }
    note_depth(lane, uxQueueMessagesWaiting(s_pqq.q[(size_t)lane]));
    return true;
}

bool pc_pq_post_lane_urgent(pc_pq_lane lane, const void *item, uint32_t timeout_ticks)
{
    if (!lane_ok(lane) || !s_pqq.q[(size_t)lane] || !item)
    {
        return false;
    }
    if (xQueueSendToFront(s_pqq.q[(size_t)lane], item, (TickType_t)timeout_ticks) != pdTRUE)
    {
        return false;
    }
    note_depth(lane, uxQueueMessagesWaiting(s_pqq.q[(size_t)lane]));
    return true;
}

bool pc_pq_post_lane_from_isr(pc_pq_lane lane, const void *item)
{
    if (!lane_ok(lane) || !s_pqq.q[(size_t)lane] || !item)
    {
        return false;
    }
    BaseType_t woke = pdFALSE;
    if (xQueueSendToBackFromISR(s_pqq.q[(size_t)lane], item, &woke) != pdTRUE)
    {
        return false;
    }
    note_depth(lane, uxQueueMessagesWaitingFromISR(s_pqq.q[(size_t)lane]));
    portYIELD_FROM_ISR(woke); // switch to the processing task now if it outranks us
    return true;
}

void pc_pq_drain_lane(pc_pq_lane)
{
    // The lane's task drains on device; nothing to do here.
}

void pc_pq_stop_lane(pc_pq_lane lane)
{
    if (!lane_ok(lane))
    {
        return;
    }
    s_pqq.run[(size_t)lane] = false;
    if (s_pqq.task[(size_t)lane]) // the task blocks on the queue (portMAX_DELAY), so delete it directly
    {
        vTaskDelete(s_pqq.task[(size_t)lane]);
        s_pqq.task[(size_t)lane] = nullptr;
    }
}

bool pc_pq_running_lane(pc_pq_lane lane)
{
    return lane_ok(lane) && s_pqq.run[(size_t)lane];
}

size_t pc_pq_high_water_lane(pc_pq_lane lane)
{
    return lane_ok(lane) ? s_pq.high_water[(size_t)lane] : 0;
}

#else // host build - fixed per-lane rings, no tasks; pc_pq_drain_lane() runs the handler

namespace
{
// All host-backend state, owned by one instance (internal linkage): the per-lane ring buffer,
// its head/tail/count cursors, and the started flag. One named owner, unreachable cross-TU.
struct PqRingCtx
{
    uint8_t buf[(size_t)pc_pq_lane::PC_PQ_LANE_COUNT][PC_PQ_DEPTH * PC_PQ_ITEM_SIZE];
    size_t head[(size_t)pc_pq_lane::PC_PQ_LANE_COUNT] = {}; // next write slot
    size_t tail[(size_t)pc_pq_lane::PC_PQ_LANE_COUNT] = {}; // next read slot
    size_t count[(size_t)pc_pq_lane::PC_PQ_LANE_COUNT] = {};
    bool started[(size_t)pc_pq_lane::PC_PQ_LANE_COUNT] = {};
};
PqRingCtx s_pqr;

void note_count(pc_pq_lane lane)
{
    if (s_pqr.count[(size_t)lane] > s_pq.high_water[(size_t)lane])
    {
        s_pq.high_water[(size_t)lane] = s_pqr.count[(size_t)lane];
    }
}
} // namespace

bool pc_pq_start_lane(pc_pq_lane lane, const pc_pq_config *cfg)
{
    if (!lane_ok(lane) || s_pqr.started[(size_t)lane] || !cfg || !cfg->handler)
    {
        return false;
    }
    s_pq.handler[(size_t)lane] = cfg->handler;
    s_pq.ctx[(size_t)lane] = cfg->ctx;
    s_pqr.head[(size_t)lane] = 0;
    s_pqr.tail[(size_t)lane] = 0;
    s_pqr.count[(size_t)lane] = 0;
    s_pq.high_water[(size_t)lane] = 0;
    s_pqr.started[(size_t)lane] = true;
    return true;
}

bool pc_pq_post_lane(pc_pq_lane lane, const void *item, uint32_t)
{
    if (!lane_ok(lane) || !item || s_pqr.count[(size_t)lane] >= PC_PQ_DEPTH)
    {
        return false; // fail closed when full
    }
    memcpy(s_pqr.buf[(size_t)lane] + s_pqr.head[(size_t)lane] * PC_PQ_ITEM_SIZE, item, PC_PQ_ITEM_SIZE);
    s_pqr.head[(size_t)lane] = (s_pqr.head[(size_t)lane] + 1) % PC_PQ_DEPTH;
    s_pqr.count[(size_t)lane]++;
    note_count(lane);
    return true;
}

bool pc_pq_post_lane_urgent(pc_pq_lane lane, const void *item, uint32_t)
{
    if (!lane_ok(lane) || !item || s_pqr.count[(size_t)lane] >= PC_PQ_DEPTH)
    {
        return false;
    }
    s_pqr.tail[(size_t)lane] =
        (s_pqr.tail[(size_t)lane] + PC_PQ_DEPTH - 1) % PC_PQ_DEPTH; // step the read cursor back, write there
    memcpy(s_pqr.buf[(size_t)lane] + s_pqr.tail[(size_t)lane] * PC_PQ_ITEM_SIZE, item, PC_PQ_ITEM_SIZE);
    s_pqr.count[(size_t)lane]++;
    note_count(lane);
    return true;
}

bool pc_pq_post_lane_from_isr(pc_pq_lane lane, const void *item)
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
        s_pqr.started[(size_t)lane] = false;
    }
}

bool pc_pq_running_lane(pc_pq_lane lane)
{
    return lane_ok(lane) && s_pqr.started[(size_t)lane];
}

size_t pc_pq_high_water_lane(pc_pq_lane lane)
{
    return lane_ok(lane) ? s_pq.high_water[(size_t)lane] : 0;
}

#endif // ARDUINO

#endif // PC_ENABLE_PREEMPT_QUEUE
