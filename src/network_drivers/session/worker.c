// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file worker.c
 * @brief Server worker identity - implementation.
 *
 * The id lives in thread-local storage so each worker task resolves its own
 * per-worker state with no lock and no shared lookup. The block is part of task
 * creation (no heap after begin()); an unbound context reads the zero default.
 */

#include "network_drivers/session/worker.h"

#include "board_drivers/board_profiles/pc_platform.h" // the target's queues and tasks, under our names
// Worker identity lives in mmgr/arena.c, with the pools it indexes.

// ---------------------------------------------------------------------------
// Worker tasks
// ---------------------------------------------------------------------------

#if PROTOCORE_HOT

// All worker-task state, owned by one instance (internal linkage): the pump callback, the task
// handles, and the run flag. One named owner, unreachable from any other translation unit.
typedef struct
{
    pc_worker_pump_fn pump;
    pc_platform_task tasks[PC_WORKER_COUNT];
    _Atomic proto_bool run; // release on start publishes pump; acquire in the task
} WorkerCtx;
static WorkerCtx s_worker;

// Each worker binds its id, then pumps until asked to stop. Between iterations it
// blocks on its task notification instead of free-running the poll: a producer
// (listener_enqueue, pc_defer) nudges it the moment work arrives, so events are
// serviced immediately rather than on the next tick. The block still times out
// after PC_WORKER_POLL_TICKS so the idle timeout sweep (check_timeouts) keeps
// reaping stale connections with no events in flight; raising that knob now lowers
// idle wakeups without costing event latency. A nudge that races the pump is
// latched in the notify count, so the wait returns at once - no lost wake.
static void worker_task(void *arg)
{
    int id = (int)(intptr_t)arg;
    pc_worker_set_self(id);
    while (atomic_load_explicit(&s_worker.run, memory_order_acquire))
    {
        if (s_worker.pump)
        {
            s_worker.pump(id);
        }
        pc_platform_task_wait(PC_PLATFORM_OK, PC_WORKER_POLL_TICKS); // wake on event, else idle-sweep timeout
    }
    s_worker.tasks[id] = NULL;
    pc_platform_task_stop(NULL);
}

// Per-worker deferred-callback queues: app code on any task hands a {fn, arg} to
// the owning worker, which runs it in its own context (race-free push path).
typedef struct
{
    pc_deferred_fn fn;
    void *arg;
} DeferCmd;

// The per-worker deferred-callback queue HANDLES, owned by one instance. The hot path
// (pc_defer / run_deferred / wake) touches only these, so this stays small and live.
typedef struct
{
    pc_platform_queue dq[PC_WORKER_COUNT];
} DeferCtx;
static DeferCtx s_defer;

// The static-queue backing store (control blocks + byte storage), in its OWN owned instance. Only
// pc_workers_start() references it (to create the queues), so a firmware that never starts workers
// (e.g. a pure client sketch) garbage-collects this multi-hundred-byte store instead of anchoring
// it through the always-live handle path.
typedef struct
{
    pc_platform_queue_ctrl dq_struct[PC_WORKER_COUNT];
    uint8_t dq_storage[PC_WORKER_COUNT][PC_DEFER_QUEUE_DEPTH * sizeof(DeferCmd)];
} DeferStorageCtx;
static DeferStorageCtx s_defer_store;

void pc_workers_start(pc_worker_pump_fn pump)
{
    if (atomic_load_explicit(&s_worker.run, memory_order_acquire))
    {
        return; // already running
    }
    s_worker.pump = pump;
    for (int i = 0; i < PC_WORKER_COUNT; i++)
    {
        if (!s_defer.dq[i])
        {
            s_defer.dq[i] = pc_platform_queue_create(PC_DEFER_QUEUE_DEPTH, sizeof(DeferCmd),
                                                     s_defer_store.dq_storage[i], &s_defer_store.dq_struct[i]);
        }
    }
    atomic_store_explicit(&s_worker.run, PROTO_TRUE, memory_order_release);
    for (int i = 0; i < PC_WORKER_COUNT; i++)
    {
        int core = (PC_WORKER_CORE + i) % PC_PLATFORM_CORES;
        pc_platform_task_start(worker_task, "pc_worker", PC_WORKER_TASK_STACK, (void *)(intptr_t)i,
                               PC_WORKER_TASK_PRIORITY, &s_worker.tasks[i], core);
    }
}

proto_bool pc_defer(int worker_id, pc_deferred_fn fn, void *arg)
{
    if (!fn)
    {
        return PROTO_FALSE;
    }
    if (worker_id < 0 || worker_id >= PC_WORKER_COUNT || !s_defer.dq[worker_id])
    {
        return PROTO_FALSE;
    }
    DeferCmd cmd = {fn, arg};
    if (pc_platform_queue_send(s_defer.dq[worker_id], &cmd, 0) != PC_PLATFORM_OK)
    {
        return PROTO_FALSE;
    }
    pc_worker_wake(worker_id); // run the callback now, not on the next idle sweep
    return PROTO_TRUE;
}

void pc_worker_wake(int worker_id)
{
    if (worker_id < 0 || worker_id >= PC_WORKER_COUNT)
    {
        return;
    }
    pc_platform_task t = s_worker.tasks[worker_id];
    if (t)
    {
        pc_platform_task_notify(t);
    }
}

void pc_worker_run_deferred(int worker_id)
{
    if (worker_id < 0 || worker_id >= PC_WORKER_COUNT || !s_defer.dq[worker_id])
    {
        return;
    }
    DeferCmd cmd;
    while (pc_platform_queue_recv(s_defer.dq[worker_id], &cmd, 0) == PC_PLATFORM_OK)
    {
        if (cmd.fn)
        {
            cmd.fn(cmd.arg);
        }
    }
}

void pc_workers_stop(void)
{
    if (!atomic_load_explicit(&s_worker.run, memory_order_acquire))
    {
        return;
    }
    atomic_store_explicit(&s_worker.run, PROTO_FALSE, memory_order_release);
    // Tasks self-delete on their next iteration; give them a few ticks to exit
    // before the caller tears down the slots they were servicing.
    pc_platform_task_delay(3);
}

proto_bool pc_workers_running(void)
{
    return atomic_load_explicit(&s_worker.run, memory_order_acquire);
}

#else // host build - no tasks; handle()/tests drive the pipeline inline

void pc_workers_start(pc_worker_pump_fn pump)
{
    (void)pump;
}
void pc_workers_stop(void)
{
}
proto_bool pc_workers_running(void)
{
    return PROTO_FALSE;
}
void pc_worker_wake(int worker_id)
{
    (void)worker_id; // no worker task on host - nothing to wake
}

// No worker task on host: the caller and the pipeline are the same thread, so a
// deferred callback can run inline immediately (same observable effect, race-free).
proto_bool pc_defer(int worker_id, pc_deferred_fn fn, void *arg)
{
    (void)worker_id;
    if (!fn)
    {
        return PROTO_FALSE;
    }
    fn(arg);
    return PROTO_TRUE;
}
void pc_worker_run_deferred(int worker_id)
{
    (void)worker_id;
}

#endif // PROTOCORE_HOT
