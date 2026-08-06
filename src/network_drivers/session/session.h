// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file session.h
 * @brief Layer 5 (Session) - event queue dispatcher and session lifecycle.
 *
 * The session layer is the bridge between the interrupt-driven transport
 * layer and the application-layer HTTP handler.  It processes all pending
 * events from the FreeRTOS queue in a single bounded loop, ensuring that
 * `server_tick()` has a deterministic worst-case execution time of
 * O(queue_depth + MAX_CONNS).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SESSION_H
#define PROTOCORE_SESSION_H

#include "../transport/tcp_evt.h" // EvtType, TcpEvt: the events this layer drains

#include "network_drivers/session/proto_handler.h" // ProtoRegistryNs: carried below as Session.proto
#include "network_drivers/session/worker.h"        // WorkerNs: carried below as Session.workers

/**
 * @brief Layer 5, and the modules it carries.
 *
 * @var SessionNs::tick    drive the layer for one loop iteration: sweep, drain, dispatch
 * @var SessionNs::proto   the protocol registry a connection is dispatched through
 * @var SessionNs::workers the worker tasks that turn the pipeline, their deferred-callback
 *                         path, and the queue they jump when one is compiled in
 *
 * A child is a pointer: a table in one translation unit is not a constant expression in another.
 * A child behind a feature flag is declared under it, so the layer names only what the image
 * contains.
 */
typedef struct
{
    void (*tick)(int worker_id);
    const ProtoRegistryNs *proto;
    const WorkerNs *workers;
} SessionNs;

/** @brief The one symbol this module exports. */
extern const SessionNs Session;

#endif
