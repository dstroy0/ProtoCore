// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file session.h
 * @brief Core server - event queue dispatcher and connection lifecycle.
 *
 * This is the bridge between the interrupt-driven transport
 * layer and the application-layer HTTP handler.  It processes all pending
 * events from the platform queue in a single bounded loop, ensuring that
 * `server_tick()` has a deterministic worst-case execution time of
 * O(queue_depth + MAX_CONNS).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SESSION_H
#define PROTOCORE_SESSION_H

#include "network_drivers/transport/tcp/evt.h" // EvtType, TcpEvt: the events this layer drains

#include "server/system/proto_handler.h" // ProtoRegistryNs: carried below as Session.proto
#include "server/system/worker.h"        // WorkerNs: carried below as Session.workers

/** @brief The layer's own state and the calls that reach it, described only in session.c. */
struct SessionInternal;

/**
 * @brief The server tick, and the modules it carries.
 *
 * A caller sets the members a call takes and invokes it through ::Session.
 *
 * @var SessionNs::worker_id  which worker is turning: whose slots it sweeps and whose queue it drains
 * @var SessionNs::tick       drive the layer for one loop iteration: sweep, drain, dispatch
 * @var SessionNs::proto      the protocol registry a connection is dispatched through
 * @var SessionNs::workers    the worker tasks that turn the pipeline, their deferred-callback
 *                            path, and the queue they jump when one is compiled in
 * @var SessionNs::internal   the layer's state and the calls that reach it
 *
 * A child is a pointer: a table in one translation unit is not a constant expression in another.
 * A child behind a feature flag is declared under it, so the layer names only what the image
 * contains.
 */
typedef struct
{
    int worker_id;

    void (*tick)(struct SessionInternal *ctx);
    ProtoRegistryNs *proto;
    WorkerNs *workers;

    struct SessionInternal *internal;
} SessionNs;

/** @brief The one symbol this module exports. */
extern SessionNs Session;

#endif
