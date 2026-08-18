// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
#include "mmgr/plaintext.h" // the persistent end this module's state is taken from

#include "core_setup/board_profiles/protocore_platform.h" // the target's queues and tasks, under our names
#include "mmgr/arena.h" // protocore_worker_set_self: identity lives with the pools it indexes
#include "mmgr/ring.h"  // PROTO_ATOMIC_LOAD/STORE: the run flag crosses tasks

// ---------------------------------------------------------------------------
// Worker tasks
// ---------------------------------------------------------------------------

// Called by defer above its definition.
static void wake(uint8_t *restrict work);

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
    protocore_platform_task tasks[PROTOCORE_WORKER_COUNT];           ///< one task handle per worker
    protocore_platform_queue dq[PROTOCORE_WORKER_COUNT];             ///< the deferred-callback queue handles
    protocore_platform_queue_ctrl dq_struct[PROTOCORE_WORKER_COUNT]; ///< their descriptors
    uint8_t dq_storage[PROTOCORE_WORKER_COUNT][PROTOCORE_DEFER_QUEUE_DEPTH * sizeof(DeferCmd)]; ///< their backing store
    protocore_worker_pump_fn pump; ///< what each worker runs every iteration
    _Atomic proto_bool run;        ///< cleared from another task to stop the loops
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define WORKER_OFF_CTX 0u
static_assert(WORKER_OFF_CTX + sizeof(struct WorkerStorage) <= PROTOCORE_WORKER_BORROW,
              "PROTOCORE_WORKER_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define WORKER_CTX(w) ((struct WorkerStorage *)(void *)((w) + WORKER_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_WORKER_BORROW persistent bytes, or null while the pool was short
} WorkersOwnCtx;
static WorkersOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_worker_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_plaintext_persist_span(PROTOCORE_WORKER_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

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
    while (PROTO_ATOMIC_LOAD(&WORKER_CTX(protocore_worker_span())->run))
    {
        if (WORKER_CTX(protocore_worker_span())->pump)
        {
            WORKER_CTX(protocore_worker_span())->pump(id);
        }
        protocore_platform_task_wait(PROTOCORE_PLATFORM_OK,
                                     PROTOCORE_WORKER_POLL_TICKS); // wake on event, else idle-sweep timeout
    }
    WORKER_CTX(protocore_worker_span())->tasks[id] = NULL;
    protocore_platform_task_stop(NULL);
}

static void start(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    if (PROTO_ATOMIC_LOAD(&WORKER_CTX(work)->run))
    {
        return; // already running
    }
    WORKER_CTX(work)->pump = Workers.pump;
    for (int i = 0; i < PROTOCORE_WORKER_COUNT; i++)
    {
        if (!WORKER_CTX(work)->dq[i])
        {
            WORKER_CTX(work)->dq[i] =
                protocore_platform_queue_create(PROTOCORE_DEFER_QUEUE_DEPTH, sizeof(DeferCmd),
                                                WORKER_CTX(work)->dq_storage[i], &WORKER_CTX(work)->dq_struct[i]);
        }
    }
    PROTO_ATOMIC_STORE(&WORKER_CTX(work)->run, PROTO_TRUE);
    for (int i = 0; i < PROTOCORE_WORKER_COUNT; i++)
    {
        int core = (PROTOCORE_WORKER_CORE + i) % PROTOCORE_PLATFORM_CORES;
        protocore_platform_task_start(worker_task, "protocore_worker", PROTOCORE_WORKER_TASK_STACK, (void *)(intptr_t)i,
                                      PROTOCORE_WORKER_TASK_PRIORITY, &WORKER_CTX(work)->tasks[i], core);
    }
}

static void defer(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    Workers.ok = PROTO_FALSE;
    if (!Workers.defer_args.fn)
    {
        return;
    }
    if (Workers.worker_id < 0 || Workers.worker_id >= PROTOCORE_WORKER_COUNT ||
        !WORKER_CTX(work)->dq[Workers.worker_id])
    {
        return;
    }
    DeferCmd cmd = {Workers.defer_args.fn, Workers.defer_args.arg};
    if (protocore_platform_queue_send(WORKER_CTX(work)->dq[Workers.worker_id], &cmd, 0) != PROTOCORE_PLATFORM_OK)
    {
        return;
    }
    wake(work); // run the callback now, not on the next idle sweep
    Workers.ok = PROTO_TRUE;
}

static void wake(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    if (Workers.worker_id < 0 || Workers.worker_id >= PROTOCORE_WORKER_COUNT)
    {
        return;
    }
    protocore_platform_task t = WORKER_CTX(work)->tasks[Workers.worker_id];
    if (t)
    {
        protocore_platform_task_notify(t);
    }
}

static void run_deferred(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    if (Workers.worker_id < 0 || Workers.worker_id >= PROTOCORE_WORKER_COUNT ||
        !WORKER_CTX(work)->dq[Workers.worker_id])
    {
        return;
    }
    DeferCmd cmd;
    while (protocore_platform_queue_recv(WORKER_CTX(work)->dq[Workers.worker_id], &cmd, 0) == PROTOCORE_PLATFORM_OK)
    {
        if (cmd.fn)
        {
            cmd.fn(cmd.arg);
        }
    }
}

static void stop(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    if (!PROTO_ATOMIC_LOAD(&WORKER_CTX(work)->run))
    {
        return;
    }
    PROTO_ATOMIC_STORE(&WORKER_CTX(work)->run, PROTO_FALSE);
    // Tasks self-delete on their next iteration; give them a few ticks to exit
    // before the caller tears down the slots they were servicing.
    protocore_platform_task_delay(3);
}

static void running(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    Workers.ok = PROTO_ATOMIC_LOAD(&WORKER_CTX(work)->run);
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
};
