// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file worker.h
 * @brief Core server - server worker identity.
 *
 * The server pipeline runs in one or more dedicated worker tasks (see
 * PROTOCORE_WORKER_COUNT). Each worker owns a disjoint partition of connection slots
 * (slot i -> worker i % count) and its own scratch arena, so per-worker state
 * (the arena, work buffers) is selected by the caller's worker id. This header is
 * the single source of that id.
 *
 * The id is per-task/per-thread: a worker binds itself once at task entry via
 * protocore_worker_set_self(); any context that has not bound an id (the user's
 * loop(), a unit test, the network stack's own thread) reads 0, which is also the only valid id
 * in the default single-worker build, so PROTOCORE_WORKER_COUNT == 1 is byte-for-byte
 * the original single-pipeline behavior.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_WORKER_H
#define PROTOCORE_WORKER_H

#include "protocore_config.h"

#include "locus_carcerum/locus_carcerum.h" // the cellblock guards each worker slot borrows through

#if PROTOCORE_ENABLE_PREEMPT_QUEUE
#include "server/core/preempt_queue/preempt_queue.h" // carried below as Session.workers->queue
#endif

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Worker identity
// ---------------------------------------------------------------------------

/** @brief Number of server worker tasks (PROTOCORE_WORKER_COUNT). */
int protocore_worker_count(void);

/**
 * @brief Worker id [0, count) of the calling task; 0 by default / single-worker.
 *
 * With PROTOCORE_WORKER_COUNT == 1 (the default) there is exactly one worker, so the answer is 0 by
 * construction and this is an inline constant - no lookup, no call. Every borrow asks, so the
 * multi-worker lookup is paid only where there is more than one worker to tell apart.
 */
#if PROTOCORE_WORKER_COUNT == 1
PROTOCORE_INLINE int protocore_worker_self(void)
{
    return 0;
}
#else
int protocore_worker_self(void);
#endif

/** @brief Bind the calling task/thread to worker id @p id (worker entry / tests). */
void protocore_worker_set_self(int id);

// ---------------------------------------------------------------------------
// Cellblocks - the memory each worker slot borrows from
// ---------------------------------------------------------------------------
//
// Each slot borrows through two MMgr cellblock guards: a minimum-security one for plaintext, whose
// bytes are left as they are on release, and a maximum-security one for key material, whose bytes
// are zeroed on release. A slot has exactly one accessor, the worker that owns it, so a borrow is a
// plain bump with no lock.
//
// The worker slots are the application's: it declares the pools and the LocusCarcerum over them in
// its own translation unit and hands the guards in through protocore_cellblocks_bind(). The ghost
// slot (PROTOCORE_GHOST_WORKER_SLOT) is the library's own, declared in worker.c. A slot nothing was
// bound to, and any context outside [0, PROTOCORE_WORKER_COUNT), borrows from the ghost.

/**
 * @brief Hand worker slot @p worker the guards it borrows through.
 *
 * @param worker a worker id in [0, PROTOCORE_WORKER_COUNT); anything else is ignored.
 * @param plain  the minimum-security guard for plaintext, or NULL to leave the slot on the ghost's.
 * @param secure the maximum-security guard for key material, or NULL to leave it on the ghost's.
 */
void protocore_cellblocks_bind(int worker, const MinimumSecurityGuard *plain, const MaximumSecurityGuard *secure);

/** @brief The plaintext guard the calling worker borrows through. Never NULL. */
const MinimumSecurityGuard *protocore_plain_guard(void);

/** @brief The key-material guard the calling worker borrows through. Never NULL. */
const MaximumSecurityGuard *protocore_secure_guard(void);

/**
 * @brief @p n persistent plaintext bytes, zeroed, or NULL if the calling worker's cellblock is full.
 *
 * State that lasts across dispatches starts from zero, and a cellblock hands its cells back as
 * they were, so this is the persistent borrow followed by the zeroing.
 */
void *protocore_plain_persist(size_t n);

/** @brief @p n persistent key-material bytes, zeroed, or NULL if the calling worker's cellblock is full. */
void *protocore_secure_persist(size_t n);

// This header is also scheduling: starting, waking, stopping and deferring onto those workers.

// ---------------------------------------------------------------------------
// Worker tasks
// ---------------------------------------------------------------------------
//
// Where the platform has a scheduler the server runs in dedicated worker tasks instead of the
// user's loop(): protocore_workers_start() spawns PROTOCORE_WORKER_COUNT tasks, each
// pinned to a core, each binding its worker id and repeatedly invoking the
// app-supplied pump (so this layer stays free of any app dependency). On host
// builds there are no tasks - the pipeline is driven inline by handle() / tests -
// so these are no-ops and protocore_workers_running() is false.

/** @brief Pump callback run by each worker task with its worker id. */
typedef void (*protocore_worker_pump_fn)(int worker_id);

// ---------------------------------------------------------------------------
// Deferred work (thread-safe app -> worker submission)
// ---------------------------------------------------------------------------
//
// HttpRoute a callback to a worker so it runs in that worker's single-thread context.
// This is how application code on loop() (or any other task) safely pushes to a
// connection - e.g. an SSE broadcast on a timer, or ws_send from a sensor task:
// instead of calling the send API directly (which would race the worker that owns
// the slot), wrap it in a small function and hand it to the owning worker. The
// worker drains and runs deferred callbacks each service iteration.
// (defer(slot, fn, arg) is the app-facing wrapper that resolves the
// slot's owner; this layer stays free of the transport/conn_pool dependency.)
//
// @p arg must remain valid until the callback runs (point it at static/global
// state, or data you keep alive). On host builds (no worker task) the callback
// runs inline immediately, so tests and loop()-driven code behave identically.

/** @brief Deferred callback signature. */
typedef void (*protocore_deferred_fn)(void *arg);

/** @brief One deferred call: what runs, and what it is given. */
typedef struct
{
    protocore_deferred_fn fn; ///< what the worker runs
    void *arg;                ///< the opaque context it is given
} WorkerDeferArgs;

/**
 * @brief The Workers module.
 *
 * A caller sets the members a call takes, invokes it through ::Workers, and reads the outcome off
 * the same handle.
 *
 * @var WorkerNs::worker_id     whose queue or task a call names
 * @var WorkerNs::pump          what each worker task runs each iteration
 * @var WorkerNs::defer_args   the call a defer hands to a worker; the arg must outlive it
 * @var WorkerNs::ok            a call's true/false outcome
 * @var WorkerNs::run_deferred  run every callback queued for worker_id, in its own context
 * @var WorkerNs::running       whether the worker tasks are up
 * @var WorkerNs::start         spawn the tasks and bind the pump
 * @var WorkerNs::stop          ask them to exit
 * @var WorkerNs::wake          nudge one so it services now rather than on the idle timeout
 * @var WorkerNs::defer         hand fn+arg to worker_id's queue and wake it
 */
typedef struct
{
    int worker_id;                 ///< the worker every call names
    protocore_worker_pump_fn pump; ///< what a started worker runs each time it wakes
    WorkerDeferArgs defer_args;    ///< the call handed to a worker to run in its own context
    proto_bool ok;
#if PROTOCORE_ENABLE_PREEMPT_QUEUE
    // The lane the workers jump. They run without it; it only changes what runs first.
    PreemptQueueNs *queue;
#endif
} WorkersVars;

/** @brief The operands and the outcome. */
extern WorkersVars WorkersV;

/** @brief The entries. */
typedef struct
{
    void (*const run_deferred)(uint8_t *work);
    void (*const running)(uint8_t *work);
    void (*const start)(uint8_t *work);
    void (*const stop)(uint8_t *work);
    void (*const wake)(uint8_t *work);
    void (*const defer)(uint8_t *work);
} WorkerNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in WorkersV or a region of the borrow at a fixed offset.
void protocore_workers_run_deferred(uint8_t *work);
void protocore_workers_running(uint8_t *work);
void protocore_workers_start(uint8_t *work);
void protocore_workers_stop(uint8_t *work);
void protocore_workers_wake(uint8_t *work);
void protocore_workers_defer(uint8_t *work);
#if PROTOCORE_ENABLE_PREEMPT_QUEUE
#endif

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Workers.run_deferred(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const WorkerNs Workers __attribute__((unused)) = {
    .run_deferred = protocore_workers_run_deferred,
    .running = protocore_workers_running,
    .start = protocore_workers_start,
    .stop = protocore_workers_stop,
    .wake = protocore_workers_wake,
    .defer = protocore_workers_defer,
#if PROTOCORE_ENABLE_PREEMPT_QUEUE
#endif
};

/**
 * @brief The PROTOCORE_WORKER_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_worker_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_WORKER_H
