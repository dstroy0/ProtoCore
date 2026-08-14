// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file session.c
 * @brief Layer 5 (Session) - event queue processor: opens, closes and controls connections.
 *
 * server_tick() is the only function here.  Its bounded loop drains every
 * active listener's platform queue in one call so that the application layer
 * always sees the most up-to-date state before checking http_pool[].
 *
 * Events are routed to the correct protocol handler via the slot's proto field.
 * A slot must carry an explicit protocol (assigned from its listener on
 * accept); PROTO_NONE and any unregistered protocol resolve to no handler
 * and the event is dropped.
 */

#include "network_drivers/session/session.h"
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: the slot an event names
#include "network_drivers/presentation/presentation.h" // http_req_start_ms: the request deadline a first byte arms
#include "server/clock/clock.h"                        // Clock.ms: the pass stamp an arm takes
#include "network_drivers/transport/tcp/server/server.h" // TcpListener: the queues this tick drains
#include "network_drivers/transport/udp/server/server.h" // UdpListener: the datagram rings this tick drains
#include "mmgr/plaintext.h"
#include "server/core/proto_handler.h"

// This layer is protocol-agnostic: it owns the dispatch mechanism only (register / look up /
// route / drain) and names no protocol. Each protocol's handler lives in its own module and is
// installed through protocore_register_builtins() (protocore_builtins.c, the policy list).

// ---------------------------------------------------------------------------
// Protocol-handler dispatch table (see proto_handler.h)
// ---------------------------------------------------------------------------
/**
 * @brief The layer's compile-time storage: the per-protocol handler table.
 *
 * All of it BSS. One named owner, unreachable from any other translation unit.
 */
struct SessionStorage
{
    const ProtoHandler *proto_handlers[PROTO_MAX_HANDLERS];
};

/**
 * @brief The layer's state and the calls that reach it - what SessionNs points at.
 *
 * @var SessionInternal::store  the per-protocol handler table
 * @var SessionInternal::ns     the handle a caller sets a call's members on
 */
struct SessionInternal
{
    struct SessionStorage *store;
    SessionNs *ns;
};

static struct SessionStorage s_store;

static struct SessionInternal s_session = {.store = &s_store, .ns = &Session};

/**
 * @brief The registry's table and the calls that reach it - what ProtoRegistryNs points at.
 *
 * @var ProtoRegistryInternal::ns  the handle a caller sets a call's members on
 */
struct ProtoRegistryInternal
{
    ProtoRegistryNs *ns;
};

static struct ProtoRegistryInternal s_registry = {.ns = &Protocols};

static void proto_builtins(struct ProtoRegistryInternal *restrict ctx)
{
    (void)ctx;
    protocore_register_builtins();
}

static void proto_register(struct ProtoRegistryInternal *restrict ctx)
{
    if ((unsigned)ctx->ns->proto < PROTO_MAX_HANDLERS)
    {
        s_store.proto_handlers[(unsigned)ctx->ns->proto] = ctx->ns->h;
    }
}

static void proto_get(struct ProtoRegistryInternal *restrict ctx)
{
    // Install the built-ins on first lookup so dispatch works before begin() (the native test
    // harness drives server_tick() directly). The list itself lives in protocore_builtins.c -
    // this dispatcher names no protocol; it just knows PROTO_HTTP is always registered, and
    // uses that as the "already bootstrapped" sentinel.
    if (!s_store.proto_handlers[(unsigned)PROTO_HTTP])
    {
        protocore_register_builtins();
    }
    // No implicit fallback: a slot must carry an explicit, registered protocol.
    // PROTO_NONE and any unregistered protocol resolve to NULL (event dropped).
    ctx->ns->handler = ((unsigned)ctx->ns->proto < PROTO_MAX_HANDLERS)
                           ? s_store.proto_handlers[(unsigned)ctx->ns->proto]
                           : NULL;
}

// Dispatch one drained event to its slot's protocol handler. Shared by the
// single-queue (N=1) and per-worker-queue (N>1) drain paths below.
static inline void dispatch_event(const TcpEvt *evt)
{
    // Per-dispatch reset of the calling worker's scratch arena: every handler
    // runs with the whole arena available, and any scratch it borrows is
    // reclaimed before the next event - the backstop that stops a forgotten
    // release from accumulating across events.
    protocore_plaintext_reset();

    // HttpRoute to the slot's protocol handler. PROTO_NONE and any unregistered
    // protocol have no handler, so the event is dropped.
    ConnPool.slot = evt->slot_id;
    ConnPool.proto_of(ConnPool.internal);
    Protocols.proto = ConnPool.proto;
    proto_get(Protocols.internal);
    const ProtoHandler *h = Protocols.handler;
    if (!h)
    {
        return;
    }

    switch (evt->type)
    {
    case EVT_CONNECT:
        if (h->on_accept)
        {
            h->on_accept(evt->slot_id);
        }
        break;
    case EVT_DATA:
        // First byte of a request arms the completion deadline; a request already under way keeps
        // the arm it has. Zero means unarmed, so a stamp of zero is carried as one.
        if (http_req_start_ms[evt->slot_id] == 0)
        {
            http_req_start_ms[evt->slot_id] = Clock.ms ? Clock.ms : 1;
        }
        if (h->on_data)
        {
            h->on_data(evt->slot_id);
        }
        break;
    case EVT_DISCONNECT:
        if (h->on_close)
        {
            h->on_close(evt->slot_id);
        }
        break;
    case EVT_ERROR:
        // RFC 9293 sec 3.6 MUST-12: the application is told whether the connection closed normally
        // or was aborted, so an abort routes to its own handler. A protocol that has not installed
        // one falls back to on_close and keeps the pre-existing behaviour.
        if (h->on_abort)
        {
            h->on_abort(evt->slot_id);
        }
        else if (h->on_close)
        {
            h->on_close(evt->slot_id);
        }
        break;
    }
}

static void server_tick(struct SessionInternal *restrict ctx)
{
    /*
     * Check timeouts BEFORE draining events.  This ensures that a slot
     * freed by a timeout is already in the CONN_FREE state if a coincident
     * EVT_DISCONNECT or EVT_ERROR is dequeued in the same tick - the
     * http_reset() call for that event is then a clean no-op. Each worker
     * sweeps only the slots it owns.
     */
    ConnPool.life.worker_id = ctx->ns->worker_id;
    ConnPool.check_timeouts(ConnPool.internal);

#if PROTOCORE_NEED_UDP
    // One set of datagram rings serves the whole server rather than one per worker, so worker 0
    // drains them: the receive side runs each bound port's handler, the send side moves queued
    // frames to the wire.
    if (ctx->ns->worker_id == 0)
    {
        UdpListener.poll(UdpListener.internal);
    }
#endif

#if PROTOCORE_WORKER_COUNT > 1
    // Drain only this worker's queue: it is the sole consumer of its slots.
    TcpListener.q.worker_id = ctx->ns->worker_id;
    TcpListener.worker_queue(TcpListener.internal);
    if (!TcpListener.queue)
    {
        return;
    }
    TcpEvt evt;
    while (protocore_platform_queue_recv(TcpListener.queue, &evt, 0) == PROTOCORE_PLATFORM_OK)
    {
        dispatch_event(&evt);
    }
#else
    // Single worker owns all slots; drain every listener queue.
    for (uint8_t li = 0; li < MAX_LISTENERS; li++)
    {
        TcpListener.idx = li;
        TcpListener.listener_queue(TcpListener.internal);
        if (!TcpListener.queue)
        {
            continue;
        }

        TcpEvt evt;
        while (protocore_platform_queue_recv(TcpListener.queue, &evt, 0) == PROTOCORE_PLATFORM_OK)
        {
            dispatch_event(&evt);
        }
    }
#endif
}

// Designated, so a member's position in the struct does not decide what it binds to.
ProtoRegistryNs Protocols = {.register_builtins = proto_builtins,
                             .add = proto_register,
                             .get = proto_get,
                             .internal = &s_registry};

// Designated, so a member's position in the struct does not decide what it binds to.
SessionNs Session = {.tick = server_tick, .proto = &Protocols, .workers = &Workers, .internal = &s_session};
