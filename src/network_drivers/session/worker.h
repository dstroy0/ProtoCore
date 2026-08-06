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

/** @brief Spawn the worker task(s) and start them running @p pump. No-op on host. */
void pc_workers_start(pc_worker_pump_fn pump);

/**
 * @brief Wake worker @p worker_id so it services a freshly-queued event now.
 *
 * Each worker blocks between service iterations (it no longer free-runs the poll),
 * so a producer that posts to a worker's event/defer queue must nudge it. This is
 * a FreeRTOS task notification (no allocation, task- and tcpip-thread safe); a
 * nudge that lands between the worker's pump and its block is latched in the
 * notification count, so the next block returns at once and no event is missed.
 * No-op on host (no worker task; the pipeline runs inline). Safe to call with an
 * out-of-range id or before the task exists.
 */
void pc_worker_wake(int worker_id);

/** @brief Signal the worker task(s) to exit and wait briefly for them. No-op on host. */
void pc_workers_stop(void);

/** @brief True while worker task(s) are running (always false on host). */
proto_bool pc_workers_running(void);

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

/** @brief Run @p fn(@p arg) on worker @p worker_id. Returns false if the queue is full. */
proto_bool pc_defer(int worker_id, pc_deferred_fn fn, void *arg);

/** @brief Drain and run worker @p worker_id's deferred callbacks (called by the worker). */
void pc_worker_run_deferred(int worker_id);

PROTO_END_DECLS

#endif // PROTOCORE_WORKER_H
