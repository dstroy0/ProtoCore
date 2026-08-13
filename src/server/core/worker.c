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

#include "server/core/worker.h"

#include "core_setup/board_profiles/protocore_platform.h" // the target's queues and tasks, under our names
#include "mmgr/arena.h"                            // protocore_worker_set_self: identity lives with the pools it indexes
#include "mmgr/ring.h"                             // PROTO_ATOMIC_LOAD/STORE: the run flag crosses tasks

// ---------------------------------------------------------------------------
// Worker tasks
// ---------------------------------------------------------------------------

// Called by defer above its definition.
static void wake(struct WorkerInternal *restrict ctx);

// Per-worker deferred-callback queues: app code on any task hands a {fn, arg} to
// the owning worker, which runs it in its own context (race-free push path).
typedef struct
{
    protocore_deferred_fn fn;
    void *arg;
} DeferCmd;

/**
 * @brief The workers' compile-time storage: the task handles and the deferred-callback queues.
 *
 * All of it BSS, so a worker costs no heap and nothing lands on a task stack.
 */
struct WorkerStorage
{
    protocore_platform_task tasks[PROTOCORE_WORKER_COUNT]; ///< one task handle per worker
    protocore_platform_queue dq[PROTOCORE_WORKER_COUNT];   ///< the deferred-callback queue handles
    protocore_platform_queue_ctrl dq_struct[PROTOCORE_WORKER_COUNT];               ///< their descriptors
    uint8_t dq_storage[PROTOCORE_WORKER_COUNT][PROTOCORE_DEFER_QUEUE_DEPTH * sizeof(DeferCmd)]; ///< their backing store
};

/**
 * @brief The workers' state and the calls that reach it - what WorkerNs points at.
 *
 * @var WorkerInternal::store  the task handles and the queues
 * @var WorkerInternal::ns     the handle a caller sets a call's members on
 * @var WorkerInternal::pump   what each task runs each iteration
 * @var WorkerInternal::run    release on start publishes pump; acquire in the task
 */
struct WorkerInternal
{
    struct WorkerStorage *store;
    WorkerNs *ns;
    protocore_worker_pump_fn pump;
    _Atomic proto_bool run;
};

static struct WorkerStorage s_store;

static struct WorkerInternal s_worker = {.store = &s_store, .ns = &Workers};

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
    s_worker.store->tasks[id] = NULL;
    protocore_platform_task_stop(NULL);
}

static void start(struct WorkerInternal *restrict ctx)
{
    if (PROTO_ATOMIC_LOAD(&ctx->run))
    {
        return; // already running
    }
    ctx->pump = ctx->ns->pump;
    for (int i = 0; i < PROTOCORE_WORKER_COUNT; i++)
    {
        if (!ctx->store->dq[i])
        {
            ctx->store->dq[i] = protocore_platform_queue_create(PROTOCORE_DEFER_QUEUE_DEPTH, sizeof(DeferCmd),
                                                               ctx->store->dq_storage[i], &ctx->store->dq_struct[i]);
        }
    }
    PROTO_ATOMIC_STORE(&ctx->run, PROTO_TRUE);
    for (int i = 0; i < PROTOCORE_WORKER_COUNT; i++)
    {
        int core = (PROTOCORE_WORKER_CORE + i) % PROTOCORE_PLATFORM_CORES;
        protocore_platform_task_start(worker_task, "protocore_worker", PROTOCORE_WORKER_TASK_STACK, (void *)(intptr_t)i,
                                      PROTOCORE_WORKER_TASK_PRIORITY, &ctx->store->tasks[i], core);
    }
}

static void defer(struct WorkerInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->defer_args.fn)
    {
        return;
    }
    if (ctx->ns->worker_id < 0 || ctx->ns->worker_id >= PROTOCORE_WORKER_COUNT || !ctx->store->dq[ctx->ns->worker_id])
    {
        return;
    }
    DeferCmd cmd = {ctx->ns->defer_args.fn, ctx->ns->defer_args.arg};
    if (protocore_platform_queue_send(ctx->store->dq[ctx->ns->worker_id], &cmd, 0) != PROTOCORE_PLATFORM_OK)
    {
        return;
    }
    wake(ctx); // run the callback now, not on the next idle sweep
    ctx->ns->ok = PROTO_TRUE;
}

static void wake(struct WorkerInternal *restrict ctx)
{
    if (ctx->ns->worker_id < 0 || ctx->ns->worker_id >= PROTOCORE_WORKER_COUNT)
    {
        return;
    }
    protocore_platform_task t = ctx->store->tasks[ctx->ns->worker_id];
    if (t)
    {
        protocore_platform_task_notify(t);
    }
}

static void run_deferred(struct WorkerInternal *restrict ctx)
{
    if (ctx->ns->worker_id < 0 || ctx->ns->worker_id >= PROTOCORE_WORKER_COUNT ||
        !ctx->store->dq[ctx->ns->worker_id])
    {
        return;
    }
    DeferCmd cmd;
    while (protocore_platform_queue_recv(ctx->store->dq[ctx->ns->worker_id], &cmd, 0) == PROTOCORE_PLATFORM_OK)
    {
        if (cmd.fn)
        {
            cmd.fn(cmd.arg);
        }
    }
}

static void stop(struct WorkerInternal *restrict ctx)
{
    if (!PROTO_ATOMIC_LOAD(&ctx->run))
    {
        return;
    }
    PROTO_ATOMIC_STORE(&ctx->run, PROTO_FALSE);
    // Tasks self-delete on their next iteration; give them a few ticks to exit
    // before the caller tears down the slots they were servicing.
    protocore_platform_task_delay(3);
}

static void running(struct WorkerInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_ATOMIC_LOAD(&ctx->run);
}

// Designated, so a member's position in the struct does not decide what it binds to.
WorkerNs Workers = {
    .run_deferred = run_deferred,
    .running = running,
    .start = start,
    .stop = stop,
    .wake = wake,
    .defer = defer,
#if PROTOCORE_ENABLE_PREEMPT_QUEUE
    .queue = &PreemptQueue,
#endif
    .internal = &s_worker,
};
