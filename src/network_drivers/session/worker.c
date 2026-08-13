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

#include "core_setup/board_profiles/protocore_platform.h" // the target's queues and tasks, under our names
#include "mmgr/arena.h"                            // protocore_worker_set_self: identity lives with the pools it indexes
#include "mmgr/ring.h"                             // PROTO_ATOMIC_LOAD/STORE: the run flag crosses tasks

// ---------------------------------------------------------------------------
// Worker tasks
// ---------------------------------------------------------------------------

// Called by protocore_defer above its definition.
static void protocore_worker_wake(int worker_id);

// All worker-task state, owned by one instance (internal linkage): the pump callback, the task
// handles, and the run flag. One named owner, unreachable from any other translation unit.
typedef struct
{
    protocore_worker_pump_fn pump;
    protocore_platform_task tasks[PROTOCORE_WORKER_COUNT];
    _Atomic proto_bool run; // release on start publishes pump; acquire in the task
} WorkerCtx;
static WorkerCtx s_worker;

// Each worker binds its id, then pumps until asked to stop. Between iterations it
// blocks on its task notification instead of free-running the poll: a producer
// (Tcp.listener->enqueue, protocore_defer) nudges it the moment work arrives, so events are
// serviced immediately rather than on the next tick. The block still times out
// after PROTOCORE_WORKER_POLL_TICKS so the idle timeout sweep (check_timeouts) keeps
// reaping stale connections with no events in flight; raising that knob now lowers
// idle wakeups without costing event latency. A nudge that races the pump is
// latched in the notify count, so the wait returns at once - no lost wake.
static void worker_task(void *arg)
{
    int id = (int)(intptr_t)arg;
    protocore_worker_set_self(id);
    while (PROTO_ATOMIC_LOAD(&s_worker.run))
    {
        if (s_worker.pump)
        {
            s_worker.pump(id);
        }
        protocore_platform_task_wait(PROTOCORE_PLATFORM_OK, PROTOCORE_WORKER_POLL_TICKS); // wake on event, else idle-sweep timeout
    }
    s_worker.tasks[id] = NULL;
    protocore_platform_task_stop(NULL);
}

// Per-worker deferred-callback queues: app code on any task hands a {fn, arg} to
// the owning worker, which runs it in its own context (race-free push path).
typedef struct
{
    protocore_deferred_fn fn;
    void *arg;
} DeferCmd;

// The per-worker deferred-callback queue HANDLES, owned by one instance. The hot path
// (protocore_defer / run_deferred / wake) touches only these, so this stays small and live.
typedef struct
{
    protocore_platform_queue dq[PROTOCORE_WORKER_COUNT];
} DeferCtx;
static DeferCtx s_defer;

// The static-queue backing store (control blocks + byte storage), in its OWN owned instance. Only
// protocore_workers_start() references it (to create the queues), so a firmware that never starts workers
// (e.g. a pure client sketch) garbage-collects this multi-hundred-byte store instead of anchoring
// it through the always-live handle path.
typedef struct
{
    protocore_platform_queue_ctrl dq_struct[PROTOCORE_WORKER_COUNT];
    uint8_t dq_storage[PROTOCORE_WORKER_COUNT][PROTOCORE_DEFER_QUEUE_DEPTH * sizeof(DeferCmd)];
} DeferStorageCtx;
static DeferStorageCtx s_defer_store;

static void protocore_workers_start(protocore_worker_pump_fn pump)
{
    if (PROTO_ATOMIC_LOAD(&s_worker.run))
    {
        return; // already running
    }
    s_worker.pump = pump;
    for (int i = 0; i < PROTOCORE_WORKER_COUNT; i++)
    {
        if (!s_defer.dq[i])
        {
            s_defer.dq[i] = protocore_platform_queue_create(PROTOCORE_DEFER_QUEUE_DEPTH, sizeof(DeferCmd),
                                                     s_defer_store.dq_storage[i], &s_defer_store.dq_struct[i]);
        }
    }
    PROTO_ATOMIC_STORE(&s_worker.run, PROTO_TRUE);
    for (int i = 0; i < PROTOCORE_WORKER_COUNT; i++)
    {
        int core = (PROTOCORE_WORKER_CORE + i) % PROTOCORE_PLATFORM_CORES;
        protocore_platform_task_start(worker_task, "protocore_worker", PROTOCORE_WORKER_TASK_STACK, (void *)(intptr_t)i,
                               PROTOCORE_WORKER_TASK_PRIORITY, &s_worker.tasks[i], core);
    }
}

static proto_bool protocore_defer(int worker_id, protocore_deferred_fn fn, void *arg)
{
    if (!fn)
    {
        return PROTO_FALSE;
    }
    if (worker_id < 0 || worker_id >= PROTOCORE_WORKER_COUNT || !s_defer.dq[worker_id])
    {
        return PROTO_FALSE;
    }
    DeferCmd cmd = {fn, arg};
    if (protocore_platform_queue_send(s_defer.dq[worker_id], &cmd, 0) != PROTOCORE_PLATFORM_OK)
    {
        return PROTO_FALSE;
    }
    protocore_worker_wake(worker_id); // run the callback now, not on the next idle sweep
    return PROTO_TRUE;
}

static void protocore_worker_wake(int worker_id)
{
    if (worker_id < 0 || worker_id >= PROTOCORE_WORKER_COUNT)
    {
        return;
    }
    protocore_platform_task t = s_worker.tasks[worker_id];
    if (t)
    {
        protocore_platform_task_notify(t);
    }
}

static void protocore_worker_run_deferred(int worker_id)
{
    if (worker_id < 0 || worker_id >= PROTOCORE_WORKER_COUNT || !s_defer.dq[worker_id])
    {
        return;
    }
    DeferCmd cmd;
    while (protocore_platform_queue_recv(s_defer.dq[worker_id], &cmd, 0) == PROTOCORE_PLATFORM_OK)
    {
        if (cmd.fn)
        {
            cmd.fn(cmd.arg);
        }
    }
}

static void protocore_workers_stop(void)
{
    if (!PROTO_ATOMIC_LOAD(&s_worker.run))
    {
        return;
    }
    PROTO_ATOMIC_STORE(&s_worker.run, PROTO_FALSE);
    // Tasks self-delete on their next iteration; give them a few ticks to exit
    // before the caller tears down the slots they were servicing.
    protocore_platform_task_delay(3);
}

static proto_bool protocore_workers_running(void)
{
    return PROTO_ATOMIC_LOAD(&s_worker.run);
}

// Designated, so a member's position in the struct does not decide what it binds to.
const WorkerNs Workers = {
    .run_deferred = protocore_worker_run_deferred,
    .running = protocore_workers_running,
    .start = protocore_workers_start,
    .stop = protocore_workers_stop,
    .wake = protocore_worker_wake,
    .defer = protocore_defer,
#if PROTOCORE_ENABLE_PREEMPT_QUEUE
    .queue = &PreemptQueue,
#endif
};
