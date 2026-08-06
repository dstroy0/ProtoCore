// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file worker.h
 * @brief Layer 5 (Session) - server worker identity.
 *
 * The server pipeline runs in one or more dedicated worker tasks (see
 * PC_WORKER_COUNT). Each worker owns a disjoint partition of connection slots
 * (slot i -> worker i % count) and its own scratch arena, so per-worker state
 * (the arena, work buffers) is selected by the caller's worker id. This header is
 * the single source of that id.
 *
 * The id is per-task/per-thread: a worker binds itself once at task entry via
 * pc_worker_set_self(); any context that has not bound an id (the user's
 * loop(), a unit test, the lwIP thread) reads 0, which is also the only valid id
 * in the default single-worker build, so PC_WORKER_COUNT == 1 is byte-for-byte
 * the original single-pipeline behavior.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_WORKER_H
#define PROTOCORE_WORKER_H

#include "protocore_config.h"

#if PC_ENABLE_PREEMPT_QUEUE
#include "network_drivers/session/preempt_queue.h" // carried below as Session.workers->queue
#endif

PROTO_BEGIN_DECLS

// Worker identity (pc_worker_count / pc_worker_self / pc_worker_set_self) is declared in
// mmgr/arena.h, with the pools it indexes. This header is scheduling: starting, waking, stopping
// and deferring onto those workers.

// ---------------------------------------------------------------------------
// Worker tasks (ESP32)
// ---------------------------------------------------------------------------
//
// On ESP32 the server runs in dedicated FreeRTOS worker tasks instead of the
// user's loop(): pc_workers_start() spawns PC_WORKER_COUNT tasks, each
// pinned to a core, each binding its worker id and repeatedly invoking the
// app-supplied pump (so this layer stays free of any app dependency). On host
// builds there are no tasks - the pipeline is driven inline by handle() / tests -
// so these are no-ops and pc_workers_running() is false.

/** @brief Pump callback run by each worker task with its worker id. */
typedef void (*pc_worker_pump_fn)(int worker_id);

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
typedef void (*pc_deferred_fn)(void *arg);

/**
 * @brief The Workers module.
 *
 * @var WorkerNs::run_deferred
 * @var WorkerNs::running
 * @var WorkerNs::start
 * @var WorkerNs::stop
 * @var WorkerNs::wake
 * @var WorkerNs::defer
 */
typedef struct
{
    void (*run_deferred)(int worker_id);
    proto_bool (*running)(void);
    void (*start)(pc_worker_pump_fn pump);
    void (*stop)(void);
    void (*wake)(int worker_id);
    proto_bool (*defer)(int worker_id, pc_deferred_fn fn, void *arg);
#if PC_ENABLE_PREEMPT_QUEUE
    // The lane the workers jump. They run without it; it only changes what runs first.
    const PreemptQueueNs *queue;
#endif
} WorkerNs;

/** @brief The one symbol this module exports. */
extern const WorkerNs Workers;

PROTO_END_DECLS

#endif // PROTOCORE_WORKER_H
