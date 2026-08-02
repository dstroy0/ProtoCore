// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file worker.cpp
 * @brief Server worker identity - implementation.
 *
 * The id lives in thread-local storage so each worker task resolves its own
 * per-worker state with no lock and no shared lookup. The block is part of task
 * creation (no heap after begin()); an unbound context reads the zero default.
 */

#include "network_drivers/session/worker.h"
#include <atomic>

#if defined(ARDUINO)
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#endif
// Worker identity lives in mmgr/arena.cpp, with the pools it indexes.

// ---------------------------------------------------------------------------
// Worker tasks
// ---------------------------------------------------------------------------

#ifdef ARDUINO

namespace
{
// All worker-task state, owned by one instance (internal linkage): the pump callback, the task
// handles, and the run flag. One named owner, unreachable from any other translation unit.
struct WorkerCtx
{
    pc_worker_pump_fn pump = nullptr;
    TaskHandle_t tasks[PC_WORKER_COUNT] = {nullptr};
    std::atomic<bool> run{false}; // release on start publishes pump; acquire in the task
};
WorkerCtx s_worker;

// Each worker binds its id, then pumps until asked to stop. Between iterations it
// blocks on its task notification instead of free-running the poll: a producer
// (listener_enqueue, pc_defer) nudges it the moment work arrives, so events are
// serviced immediately rather than on the next tick. The block still times out
// after PC_WORKER_POLL_TICKS so the idle timeout sweep (check_timeouts) keeps
// reaping stale connections with no events in flight; raising that knob now lowers
// idle wakeups without costing event latency. A nudge that races the pump is
// latched in the notify count, so ulTaskNotifyTake returns at once - no lost wake.
void worker_task(void *arg)
{
    int id = (int)(intptr_t)arg;
    pc_worker_set_self(id);
    while (s_worker.run.load(std::memory_order_acquire))
    {
        if (s_worker.pump)
        {
            s_worker.pump(id);
        }
        ulTaskNotifyTake(pdTRUE, PC_WORKER_POLL_TICKS); // wake on event, else idle-sweep timeout
    }
    s_worker.tasks[id] = nullptr;
    vTaskDelete(nullptr);
}
} // namespace

// Per-worker deferred-callback queues: app code on any task hands a {fn, arg} to
// the owning worker, which runs it in its own context (race-free push path).
namespace
{
struct DeferCmd
{
    pc_deferred_fn fn;
    void *arg;
};
// The per-worker deferred-callback queue HANDLES, owned by one instance. The hot path
// (pc_defer / run_deferred / wake) touches only these, so this stays small and live.
struct DeferCtx
{
    QueueHandle_t dq[PC_WORKER_COUNT] = {nullptr};
};
DeferCtx s_defer;

// The FreeRTOS static-queue backing store (control blocks + byte storage), in its OWN
// owned instance. Only pc_workers_start() references it (to create the queues), so a
// firmware that never starts workers (e.g. a pure client sketch) garbage-collects this
// multi-hundred-byte store instead of anchoring it through the always-live handle path.
struct DeferStorageCtx
{
    StaticQueue_t dq_struct[PC_WORKER_COUNT];
    uint8_t dq_storage[PC_WORKER_COUNT][PC_DEFER_QUEUE_DEPTH * sizeof(DeferCmd)];
};
DeferStorageCtx s_defer_store;
} // namespace

void pc_workers_start(pc_worker_pump_fn pump)
{
    if (s_worker.run.load(std::memory_order_acquire))
    {
        return; // already running
    }
    s_worker.pump = pump;
    for (int i = 0; i < PC_WORKER_COUNT; i++)
    {
        if (!s_defer.dq[i])
        {
            s_defer.dq[i] = xQueueCreateStatic(PC_DEFER_QUEUE_DEPTH, sizeof(DeferCmd), s_defer_store.dq_storage[i],
                                               &s_defer_store.dq_struct[i]);
        }
    }
    s_worker.run.store(true, std::memory_order_release);
    for (int i = 0; i < PC_WORKER_COUNT; i++)
    {
        int core = (PC_WORKER_CORE + i) % portNUM_PROCESSORS;
        xTaskCreatePinnedToCore(worker_task, "pc_worker", PC_WORKER_TASK_STACK, (void *)(intptr_t)i,
                                PC_WORKER_TASK_PRIORITY, &s_worker.tasks[i], core);
    }
}

bool pc_defer(int worker_id, pc_deferred_fn fn, void *arg)
{
    if (!fn)
    {
        return false;
    }
    if (worker_id < 0 || worker_id >= PC_WORKER_COUNT || !s_defer.dq[worker_id])
    {
        return false;
    }
    DeferCmd cmd = {fn, arg};
    if (xQueueSend(s_defer.dq[worker_id], &cmd, 0) != pdTRUE)
    {
        return false;
    }
    pc_worker_wake(worker_id); // run the callback now, not on the next idle sweep
    return true;
}

void pc_worker_wake(int worker_id)
{
    if (worker_id < 0 || worker_id >= PC_WORKER_COUNT)
    {
        return;
    }
    TaskHandle_t t = s_worker.tasks[worker_id];
    if (t)
    {
        xTaskNotifyGive(t);
    }
}

void pc_worker_run_deferred(int worker_id)
{
    if (worker_id < 0 || worker_id >= PC_WORKER_COUNT || !s_defer.dq[worker_id])
    {
        return;
    }
    DeferCmd cmd;
    while (xQueueReceive(s_defer.dq[worker_id], &cmd, 0) == pdTRUE)
    {
        if (cmd.fn)
        {
            cmd.fn(cmd.arg);
        }
    }
}

void pc_workers_stop(void)
{
    if (!s_worker.run.load(std::memory_order_acquire))
    {
        return;
    }
    s_worker.run.store(false, std::memory_order_release);
    // Tasks self-delete on their next iteration; give them a few ticks to exit
    // before the caller tears down the slots they were servicing.
    vTaskDelay(3);
}

bool pc_workers_running(void)
{
    return s_worker.run.load(std::memory_order_acquire);
}

#else // host build - no tasks; handle()/tests drive the pipeline inline

void pc_workers_start(pc_worker_pump_fn)
{
}
void pc_workers_stop(void)
{
}
bool pc_workers_running(void)
{
    return false;
}
void pc_worker_wake(int)
{
} // no worker task on host - nothing to wake

// No worker task on host: the caller and the pipeline are the same thread, so a
// deferred callback can run inline immediately (same observable effect, race-free).
bool pc_defer(int, pc_deferred_fn fn, void *arg)
{
    if (!fn)
    {
        return false;
    }
    fn(arg);
    return true;
}
void pc_worker_run_deferred(int)
{
}

#endif // ARDUINO
