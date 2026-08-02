// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file session.cpp
 * @brief Layer 5 (Session) - event queue processor implementation.
 *
 * server_tick() is the only function here.  Its bounded loop drains every
 * active listener's FreeRTOS queue in one call so that the application layer
 * always sees the most up-to-date state before checking http_pool[].
 *
 * Events are routed to the correct protocol handler via TcpConn::proto.
 * A slot must carry an explicit protocol (assigned from its listener on
 * accept); ConnProto::PROTO_NONE and any unregistered protocol resolve to no handler
 * and the event is dropped.
 */

#include "session.h"
#include "../transport/listener.h"
#include "mmgr/plaintext.h"
#include "proto_handler.h"

// This layer is protocol-agnostic: it owns the dispatch mechanism only (register / look up /
// route / drain) and names no protocol. Each protocol's handler lives in its own module and is
// installed through proto_register_builtins() (proto_builtins.cpp, the policy list).

// ---------------------------------------------------------------------------
// Protocol-handler dispatch table (see proto_handler.h)
// ---------------------------------------------------------------------------
// Protocol-handler dispatch table, owned by one instance (internal linkage): the per-protocol
// ProtoHandler pointers. One named owner, unreachable from any other translation unit.
typedef struct
{
    const ProtoHandler *proto_handlers[PROTO_MAX_HANDLERS];
} SessionCtx;
static SessionCtx s_session;

void proto_register(ConnProto proto, const ProtoHandler *h)
{
    if ((unsigned)proto < PROTO_MAX_HANDLERS)
    {
        s_session.proto_handlers[(unsigned)proto] = h;
    }
}

const ProtoHandler *proto_get(ConnProto proto)
{
    // Install the built-ins on first lookup so dispatch works before begin() (the native test
    // harness drives server_tick() directly). The list itself lives in proto_builtins.cpp -
    // this dispatcher names no protocol; it just knows ConnProto::PROTO_HTTP is always registered, and
    // uses that as the "already bootstrapped" sentinel.
    if (!s_session.proto_handlers[(unsigned)ConnProto::PROTO_HTTP])
    {
        proto_register_builtins();
    }
    // No implicit fallback: a slot must carry an explicit, registered protocol.
    // ConnProto::PROTO_NONE and any unregistered protocol resolve to nullptr (event dropped).
    return ((unsigned)proto < PROTO_MAX_HANDLERS) ? s_session.proto_handlers[(unsigned)proto] : NULL;
}

// Dispatch one drained event to its slot's protocol handler. Shared by the
// single-queue (N=1) and per-worker-queue (N>1) drain paths below.
static inline void dispatch_event(const TcpEvt &evt)
{
    // Per-dispatch reset of the calling worker's scratch arena: every handler
    // runs with the whole arena available, and any scratch it borrows is
    // reclaimed before the next event - the backstop that stops a forgotten
    // release from accumulating across events.
    pc_plaintext_reset();

    // Route to the slot's protocol handler. ConnProto::PROTO_NONE and any unregistered
    // protocol have no handler, so the event is dropped.
    const ProtoHandler *h = proto_get(conn_pool[evt.slot_id].proto);
    if (!h)
    {
        return;
    }

    switch (evt.type)
    {
    case EvtType::EVT_CONNECT:
        if (h->on_accept)
        {
            h->on_accept(evt.slot_id);
        }
        break;
    case EvtType::EVT_DATA:
        if (h->on_data)
        {
            h->on_data(evt.slot_id);
        }
        break;
    case EvtType::EVT_DISCONNECT:
    case EvtType::EVT_ERROR:
        if (h->on_close)
        {
            h->on_close(evt.slot_id);
        }
        break;
    }
}

void server_tick(int worker_id)
{
    /*
     * Check timeouts BEFORE draining events.  This ensures that a slot
     * freed by a timeout is already in the CONN_FREE state if a coincident
     * EVT_DISCONNECT or EVT_ERROR is dequeued in the same tick - the
     * http_reset() call for that event is then a clean no-op. Each worker
     * sweeps only the slots it owns.
     */
    proto_tcp_check_timeouts(worker_id);

#if PC_WORKER_COUNT > 1
    // Drain only this worker's queue: it is the sole consumer of its slots.
    QueueHandle_t q = listener_worker_queue(worker_id);
    if (!q)
    {
        return;
    }
    TcpEvt evt;
    while (xQueueReceive(q, &evt, 0) == pdTRUE)
    {
        dispatch_event(evt);
    }
#else
    (void)worker_id; // single worker owns all slots; drain every listener queue
    for (uint8_t li = 0; li < MAX_LISTENERS; li++)
    {
        Listener *lst = &listener_pool[li];
        if (!lst->active || !lst->queue)
        {
            continue;
        }

        TcpEvt evt;
        while (xQueueReceive(lst->queue, &evt, 0) == pdTRUE)
        {
            dispatch_event(evt);
        }
    }
#endif
}
