// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file session.h
 * @brief Layer 5 (Session) - where a connection is opened, closed and controlled.
 *
 * A connection's life is decided here: the transport signals that one arrived, ended or faulted,
 * and this layer turns that into an open, a close, or a dispatch to whichever protocol owns the
 * slot. It sits above the server core and uses it rather than being part of it - the worker pool
 * turns the crank (server/core/worker.h) and the protocol registry says who receives the event
 * (server/core/proto_handler.h).
 *
 * One tick drains every pending event in a single bounded loop, so the worst case is
 * O(queue_depth + MAX_CONNS) rather than unbounded in the arrival rate.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SESSION_H
#define PROTOCORE_SESSION_H

#include "network_drivers/transport/tcp/evt.h" // EvtType, TcpEvt: the events this layer drains

#include "server/core/proto_handler.h" // ProtoRegistryNs: carried below as Session.proto
#include "server/core/worker.h"        // WorkerNs: carried below as Session.workers

/** @brief The layer's own state and the calls that reach it, described only in session.c. */
struct SessionInternal;

/**
 * @brief The session tick, and the core modules it drives.
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
