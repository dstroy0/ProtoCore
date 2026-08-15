// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "protocore_config.h" // CONN_POOL_SLOTS, proto_bool: the tables below
#include "network_drivers/transport/tcp/evt.h" // EvtType, TcpEvt: the events this layer drains

#include "server/core/proto_handler.h" // ProtoRegistryNs: carried below as Session.proto
#include "server/core/worker.h"        // WorkerNs: carried below as Session.workers

/**
 * @brief Per-connection state, keyed on the transport slot index.
 *
 * A connection is opened, closed and controlled here, so what a connection carries between its
 * requests is held here too rather than by whichever layer happens to read it. Sized
 * CONN_POOL_SLOTS, not MAX_CONNS: the HTTP/3 dispatch slot is a reserved index above the TCP range.
 * All BSS. Defined in session.c.
 *
 * @var http_req_start_ms  protocore_millis() at the first byte of the in-progress request (0 = none).
 *                         The request-header deadline (PROTOCORE_REQUEST_TIMEOUT_MS, slow-loris
 *                         defense) measures against this; unlike the transport's idle timer a
 *                         trickle byte cannot reset it.
 * @var http_resp_sink     where a response for the slot is written, per slot.
 */
typedef proto_bool (*protocore_resp_sink_fn)(uint8_t slot, int code, const char *content_type, const char *body,
                                             size_t len);

extern uint32_t http_req_start_ms[CONN_POOL_SLOTS];
extern protocore_resp_sink_fn http_resp_sink[CONN_POOL_SLOTS];

#if PROTOCORE_ENABLE_HTTP2
/**
 * @brief Whether the slot negotiated HTTP/2 (ALPN "h2"), whether that check has run, and the
 * stream it is serving.
 *
 * RFC 9113 sec 5: a stream is "an independent, bidirectional sequence of frames exchanged between
 * the client and server within an HTTP/2 connection", and "stream identifiers are assigned to
 * streams by the endpoint initiating the stream" - so which stream a slot is answering on is a
 * property of the connection, not of the request being parsed.
 */
extern uint8_t http_h2[CONN_POOL_SLOTS];
extern uint8_t http_h2_checked[CONN_POOL_SLOTS];
extern uint32_t http_h2_stream[CONN_POOL_SLOTS];
#endif

#if PROTOCORE_ENABLE_HTTP3
/** @brief The reserved HTTP/3 dispatch slot, and the QUIC connection and stream a response routes back on. */
extern uint8_t http_h3[CONN_POOL_SLOTS];
extern uint32_t http_h3_conn_id[CONN_POOL_SLOTS];
extern uint64_t http_h3_stream[CONN_POOL_SLOTS];
#endif

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
