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

#include "server/core/worker/worker.h"

#include "config/platform/platform.h" // the target's queues and tasks, under our names

#include <stdatomic.h> // PROTO_ATOMIC_LOAD/STORE: the run flag crosses tasks

// ---------------------------------------------------------------------------
// Worker identity
// ---------------------------------------------------------------------------
//
// Per-task worker id, for a genuine multi-worker build. Default 0: the user loop(), the lwIP
// thread, and unit tests all read worker 0.
//
// The binding is a table keyed on the platform's own answer to "which execution context is running
// me", not a `_Thread_local`. Both reach the same place on an RTOS - the task's own storage - but a
// thread-local makes the compiler emit __emutls_get_address, and libgcc's emulated-TLS allocates
// each block with malloc and calls abort when it cannot. A build that owns no heap after boot
// cannot link that, so the identity is held here instead.
//
// One entry per worker plus the ghost slot, which is every context that can ever bind one; the
// count is fixed at build time, so this is BSS and the claim is a bounded walk. `s_bound` is the
// counter: a context that has never bound reads worker 0.
#define PROTOCORE_WORKER_BINDINGS (PROTOCORE_WORKER_COUNT + 1)

static uintptr_t s_ctx[PROTOCORE_WORKER_BINDINGS];
static int s_ctx_worker[PROTOCORE_WORKER_BINDINGS];
static int s_bound;

int protocore_worker_count(void)
{
    return PROTOCORE_WORKER_COUNT;
}

#if PROTOCORE_WORKER_COUNT != 1
int protocore_worker_self(void)
{
    const uintptr_t me = protocore_platform_context_id();
    for (int i = 0; i < s_bound; i++)
    {
        if (s_ctx[i] == me)
        {
            return s_ctx_worker[i];
        }
    }
    return 0;
}
#endif

void protocore_worker_set_self(int id)
{
    const uintptr_t me = protocore_platform_context_id();
    for (int i = 0; i < s_bound; i++)
    {
        if (s_ctx[i] == me)
        {
            s_ctx_worker[i] = id; // this context rebinding itself
            return;
        }
    }
    if (s_bound < PROTOCORE_WORKER_BINDINGS)
    {
        s_ctx[s_bound] = me;
        s_ctx_worker[s_bound] = id;
        s_bound++;
    }
}

// ---------------------------------------------------------------------------
// Cellblocks
// ---------------------------------------------------------------------------

// A cellblock is a power of two, and the arena sizes are sums of what the enabled modules declare,
// so the ghost's pools are each sum rounded up to the next power of two. Smeared on an enum name
// rather than on the sum itself, so the expression is 32 copies of one identifier.
enum
{
    GHOST_PLAIN_WANT = PROTOCORE_PLAINTEXT_ARENA_SIZE,
    GHOST_SECURE_WANT = PROTOCORE_SECURE_ARENA_SIZE,
};
#define WORKER_SMEAR1(x) ((x) | ((x) >> 1))
#define WORKER_SMEAR2(x) (WORKER_SMEAR1(x) | (WORKER_SMEAR1(x) >> 2))
#define WORKER_SMEAR4(x) (WORKER_SMEAR2(x) | (WORKER_SMEAR2(x) >> 4))
#define WORKER_SMEAR8(x) (WORKER_SMEAR4(x) | (WORKER_SMEAR4(x) >> 8))
#define WORKER_SMEAR16(x) (WORKER_SMEAR8(x) | (WORKER_SMEAR8(x) >> 16))
#define WORKER_POW2_UP(n) (WORKER_SMEAR16((unsigned long)(n) - 1u) + 1u)

// The ghost slot's pools: the library's own, borrowed from by every context no worker slot covers.
ParsMemoriaeInternae(ghost_plain, WORKER_POW2_UP(GHOST_PLAIN_WANT));
ParsMemoriaeInternae(ghost_secure, WORKER_POW2_UP(GHOST_SECURE_WANT));
LocusCarcerum(ghost_site, MMGR_MINIMUM_SECURITY(ghost_plain), MMGR_MAXIMUM_SECURITY(ghost_secure));

// What each worker slot borrows through. NULL is the ghost's.
static const MinimumSecurityGuard *s_plain[PROTOCORE_WORKER_COUNT];
static const MaximumSecurityGuard *s_secure[PROTOCORE_WORKER_COUNT];

void protocore_cellblocks_bind(int worker, const MinimumSecurityGuard *plain, const MaximumSecurityGuard *secure)
{
    if (worker < 0 || worker >= PROTOCORE_WORKER_COUNT)
    {
        return;
    }
    s_plain[worker] = plain;
    s_secure[worker] = secure;
}

const MinimumSecurityGuard *protocore_plain_guard(void)
{
    const int w = protocore_worker_self();
    if (w >= 0 && w < PROTOCORE_WORKER_COUNT && s_plain[w] != NULL)
    {
        return s_plain[w];
    }
    return &ghost_site.ghost_plain;
}

const MaximumSecurityGuard *protocore_secure_guard(void)
{
    const int w = protocore_worker_self();
    if (w >= 0 && w < PROTOCORE_WORKER_COUNT && s_secure[w] != NULL)
    {
        return s_secure[w];
    }
    return &ghost_site.ghost_secure;
}

void *protocore_plain_persist(size_t n)
{
    void *p = protocore_plain_guard()->persistent_buf_alloc(n);
    if (p != NULL)
    {
        mmgr_zero_buf(p, n);
    }
    return p;
}

void *protocore_secure_persist(size_t n)
{
    void *p = protocore_secure_guard()->persistent_buf_alloc(n);
    if (p != NULL)
    {
        mmgr_zero_buf(p, n);
    }
    return p;
}

// ---------------------------------------------------------------------------
// Worker tasks
// ---------------------------------------------------------------------------

// Called by defer above its definition.
void protocore_workers_wake(uint8_t *work);

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
    uint8_t *span; ///< PROTOCORE_WORKER_BORROW persistent bytes
} WorkersOwnCtx;
static WorkersOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_worker_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = (uint8_t *)protocore_plain_persist(PROTOCORE_WORKER_BORROW);
    }
    return s_own.span;
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

void protocore_workers_start(uint8_t *work)
{
    if (PROTO_ATOMIC_LOAD(&WORKER_CTX(work)->run))
    {
        return; // already running
    }
    WORKER_CTX(work)->pump = WorkersV.pump;
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

void protocore_workers_defer(uint8_t *work)
{
    WorkersV.ok = PROTO_FALSE;
    if (!WorkersV.defer_args.fn)
    {
        return;
    }
    if (WorkersV.worker_id < 0 || WorkersV.worker_id >= PROTOCORE_WORKER_COUNT ||
        !WORKER_CTX(work)->dq[WorkersV.worker_id])
    {
        return;
    }
    DeferCmd cmd = {WorkersV.defer_args.fn, WorkersV.defer_args.arg};
    if (protocore_platform_queue_send(WORKER_CTX(work)->dq[WorkersV.worker_id], &cmd, 0) != PROTOCORE_PLATFORM_OK)
    {
        return;
    }
    protocore_workers_wake(work); // run the callback now, not on the next idle sweep
    WorkersV.ok = PROTO_TRUE;
}

void protocore_workers_wake(uint8_t *work)
{
    if (WorkersV.worker_id < 0 || WorkersV.worker_id >= PROTOCORE_WORKER_COUNT)
    {
        return;
    }
    protocore_platform_task t = WORKER_CTX(work)->tasks[WorkersV.worker_id];
    if (t)
    {
        protocore_platform_task_notify(t);
    }
}

void protocore_workers_run_deferred(uint8_t *work)
{
    if (WorkersV.worker_id < 0 || WorkersV.worker_id >= PROTOCORE_WORKER_COUNT ||
        !WORKER_CTX(work)->dq[WorkersV.worker_id])
    {
        return;
    }
    DeferCmd cmd;
    while (protocore_platform_queue_recv(WORKER_CTX(work)->dq[WorkersV.worker_id], &cmd, 0) == PROTOCORE_PLATFORM_OK)
    {
        if (cmd.fn)
        {
            cmd.fn(cmd.arg);
        }
    }
}

void protocore_workers_stop(uint8_t *work)
{
    if (!PROTO_ATOMIC_LOAD(&WORKER_CTX(work)->run))
    {
        return;
    }
    PROTO_ATOMIC_STORE(&WORKER_CTX(work)->run, PROTO_FALSE);
    // Tasks self-delete on their next iteration; give them a few ticks to exit
    // before the caller tears down the slots they were servicing.
    protocore_platform_task_delay(3);
}

void protocore_workers_running(uint8_t *work)
{
    WorkersV.ok = PROTO_ATOMIC_LOAD(&WORKER_CTX(work)->run);
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
WorkersVars WorkersV = {
#if PROTOCORE_ENABLE_PREEMPT_QUEUE
    .queue = &PreemptQueue,
#endif
};
