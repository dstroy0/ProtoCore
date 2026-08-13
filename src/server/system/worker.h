// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#if PROTOCORE_ENABLE_PREEMPT_QUEUE
#include "server/system/preempt_queue.h" // carried below as Session.workers->queue
#endif

PROTOCORE_BEGIN_DECLS

// Worker identity (protocore_worker_count / protocore_worker_self / protocore_worker_set_self) is declared in
// mmgr/arena.h, with the pools it indexes. This header is scheduling: starting, waking, stopping
// and deferring onto those workers.

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

/** @brief The workers' own state and the calls that reach it, described only in worker.c. */
struct WorkerInternal;

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
 * @var WorkerNs::internal      the workers' state and the calls that reach it
 */
typedef struct
{
    int worker_id;                 ///< the worker every call names
    protocore_worker_pump_fn pump; ///< what a started worker runs each time it wakes

    WorkerDeferArgs defer_args;    ///< the call handed to a worker to run in its own context

    proto_bool ok;

    void (*run_deferred)(struct WorkerInternal *ctx);
    void (*running)(struct WorkerInternal *ctx);
    void (*start)(struct WorkerInternal *ctx);
    void (*stop)(struct WorkerInternal *ctx);
    void (*wake)(struct WorkerInternal *ctx);
    void (*defer)(struct WorkerInternal *ctx);
#if PROTOCORE_ENABLE_PREEMPT_QUEUE
    // The lane the workers jump. They run without it; it only changes what runs first.
    PreemptQueueNs *queue;
#endif

    struct WorkerInternal *internal;
} WorkerNs;

/** @brief The one symbol this module exports. */
extern WorkerNs Workers;

PROTOCORE_END_DECLS

#endif // PROTOCORE_WORKER_H
