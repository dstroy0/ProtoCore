// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
#include "mmgr/plaintext/plaintext.h"
#include "network_drivers/presentation/presentation.h" // http_req_start_ms: the request deadline a first byte arms
#include "network_drivers/presentation/ssh/network/network.h" // SshNetwork.owns: which SSH slot this stream carries
#include "network_drivers/transport/tcp/protocol/protocol.h"  // ConnPool: the slot an event names
#include "network_drivers/transport/tcp/server/server.h"      // TcpListener: the queues this tick drains
#include "network_drivers/transport/udp/server/server.h"      // UdpListener: the datagram rings this tick drains
#include "server/clock/clock.h"                               // Clock.ms: the pass stamp an arm takes
#include "server/core/proto_handler.h"
#include "server/storage/filesystem/filesystem.h" // Fs: the source a released transfer still holds open

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
// The per-connection tables session.h declares. All BSS.
uint32_t http_req_start_ms[CONN_POOL_SLOTS];
protocore_resp_sink_fn http_resp_sink[CONN_POOL_SLOTS];
#if PROTOCORE_ENABLE_HTTP2
uint8_t http_h2[CONN_POOL_SLOTS];
uint8_t http_h2_checked[CONN_POOL_SLOTS];
uint32_t http_h2_stream[CONN_POOL_SLOTS];
#endif
#if PROTOCORE_ENABLE_FILE_SERVING
FileSend file_send[CONN_POOL_SLOTS];
#endif
#if PROTOCORE_ENABLE_SSH_SCP
ScpConn scp_conns[MAX_SSH_CONNS];
#endif
#if PROTOCORE_ENABLE_SSH_SFTP
SftpSession sftp_sess[MAX_SSH_CONNS];
#endif
#if PROTOCORE_ENABLE_HTTP3
uint8_t http_h3[CONN_POOL_SLOTS];
uint32_t http_h3_conn_id[CONN_POOL_SLOTS];
uint64_t http_h3_stream[CONN_POOL_SLOTS];
#endif

struct SessionStorage
{
    const ProtoHandler *proto_handlers[PROTO_MAX_HANDLERS];
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define SESSION_OFF_CTX 0u
static_assert(SESSION_OFF_CTX + sizeof(struct SessionStorage) <= PROTOCORE_SESSION_BORROW,
              "PROTOCORE_SESSION_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define SESSION_CTX(w) ((struct SessionStorage *)(void *)((w) + SESSION_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_SESSION_BORROW persistent bytes
} SessionOwnCtx;
static SessionOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_session_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_SESSION_BORROW).buf;
    }
    return s_own.span;
}

static void proto_builtins(uint8_t *restrict work)
{
    (void)work;
    protocore_register_builtins();
}

static void proto_register(uint8_t *restrict work)
{
    if ((unsigned)Protocols.proto < PROTO_MAX_HANDLERS)
    {
        SESSION_CTX(work)->proto_handlers[(unsigned)Protocols.proto] = Protocols.h;
    }
}

static void proto_get(uint8_t *restrict work)
{
    // The protocol asked for, read before the bootstrap below can touch it. Registering the
    // built-ins runs back through THIS namespace - protocore_builtins.c sets Protocols.proto once
    // per entry - so a lookup that read Protocols.proto afterwards would answer for whichever
    // protocol happened to register last instead of the one the caller named.
    const ProtoConn want = Protocols.proto;

    // Install the built-ins on first lookup so dispatch works before begin() (the native test
    // harness drives server_tick() directly). The list itself lives in protocore_builtins.c -
    // this dispatcher names no protocol; it just knows PROTO_HTTP is always registered, and
    // uses that as the "already bootstrapped" sentinel.
    if (!SESSION_CTX(work)->proto_handlers[(unsigned)PROTO_HTTP])
    {
        protocore_register_builtins();
        Protocols.proto = want; // the handle names what the caller asked for, not the last built-in
    }
    // No implicit fallback: a slot must carry an explicit, registered protocol.
    // PROTO_NONE and any unregistered protocol resolve to NULL (event dropped).
    Protocols.handler =
        ((unsigned)want < PROTO_MAX_HANDLERS) ? SESSION_CTX(work)->proto_handlers[(unsigned)want] : NULL;
}

// The connection is over, so everything it carried between its requests is released here. This
// layer opens and closes the connection, so it is the one that decides a slot's state is finished -
// an application that polled the transport to work that out for itself would be deciding the
// connection's life from above it.
static void conn_release(uint8_t slot)
{
    http_req_start_ms[slot] = 0;
#if PROTOCORE_ENABLE_FILE_SERVING
    if (file_send[slot].active)
    {
        Fs.io.handle = file_send[slot].fh; // the source outlived the connection reading it
        Fs.close(protocore_filesystem_span());
        file_send[slot].active = PROTO_FALSE;
    }
#endif
#if PROTOCORE_ENABLE_SSH_SCP || PROTOCORE_ENABLE_SSH_SFTP
    // An SSH slot is its own index, bound to a stream slot rather than equal to it, so the SSH
    // slot this connection carries is the one that answers owns() for it. MAX_SSH_CONNS is the
    // whole pool, so the walk is bounded by the pool and not by the arrival rate.
    for (uint8_t i = 0; i < MAX_SSH_CONNS; i++)
    {
        SshNetworkV.ssh_slot = i;
        SshNetworkV.conn_slot = slot;
        SshNetwork.owns(protocore_ssh_network_span());
        if (!SshNetworkV.ok)
        {
            continue;
        }
#if PROTOCORE_ENABLE_SSH_SCP
        if (scp_conns[i].active)
        {
            Fs.io.handle = scp_conns[i].fh; // the destination outlived the transfer writing it
            Fs.close(protocore_filesystem_span());
            scp_conns[i].fh = -1;
            scp_conns[i].active = PROTO_FALSE;
        }
#endif
#if PROTOCORE_ENABLE_SSH_SFTP
        while (sftp_sess[i].open_mask != 0)
        {
            const int h = __builtin_ctz(sftp_sess[i].open_mask);
            Fs.io.handle = sftp_sess[i].handles[h].fh;
            Fs.close(protocore_filesystem_span());
            sftp_sess[i].open_mask &= ~(1u << h);
        }
        sftp_sess[i].active = PROTO_FALSE;
#endif
    }
#endif
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
    ConnPoolV.slot = evt->slot_id;
    ConnPool.proto_of(protocore_conn_pool_span());
    Protocols.proto = ConnPoolV.proto;
    proto_get(protocore_session_span());
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
        conn_release(evt->slot_id);
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
        conn_release(evt->slot_id);
        break;
    }
}

void protocore_session_tick(uint8_t *restrict work)
{
    (void)work;
    /*
     * Check timeouts BEFORE draining events.  This ensures that a slot
     * freed by a timeout is already in the CONN_FREE state if a coincident
     * EVT_DISCONNECT or EVT_ERROR is dequeued in the same tick - the
     * http_reset() call for that event is then a clean no-op. Each worker
     * sweeps only the slots it owns.
     */
    ConnPoolV.life.worker_id = SessionV.worker_id;
    ConnPoolV.life.conn_timeout_ms = SessionV.conn_timeout_ms;
    ConnPool.check_timeouts(protocore_conn_pool_span());

#if PROTOCORE_NEED_UDP
    // One set of datagram rings serves the whole server rather than one per worker, so worker 0
    // drains them: the receive side runs each bound port's handler, the send side moves queued
    // frames to the wire.
    if (SessionV.worker_id == 0)
    {
        UdpListener.poll(protocore_udp_listener_span());
    }
#endif

#if PROTOCORE_WORKER_COUNT > 1
    // Drain only this worker's queue: it is the sole consumer of its slots.
    TcpListenerV.q.worker_id = SessionV.worker_id;
    TcpListener.worker_queue(protocore_tcp_listener_span());
    if (!TcpListenerV.queue)
    {
        return;
    }
    TcpEvt evt;
    while (protocore_platform_queue_recv(TcpListenerV.queue, &evt, 0) == PROTOCORE_PLATFORM_OK)
    {
        dispatch_event(&evt);
    }
#else
    // Single worker owns all slots; drain every listener queue.
    for (uint8_t li = 0; li < MAX_LISTENERS; li++)
    {
        TcpListenerV.idx = li;
        TcpListener.listener_queue(protocore_tcp_listener_span());
        if (!TcpListenerV.queue)
        {
            continue;
        }

        TcpEvt evt;
        while (protocore_platform_queue_recv(TcpListenerV.queue, &evt, 0) == PROTOCORE_PLATFORM_OK)
        {
            dispatch_event(&evt);
        }
    }
#endif
}

// Designated, so a member's position in the struct does not decide what it binds to.
ProtoRegistryNs Protocols = {.register_builtins = proto_builtins, .add = proto_register, .get = proto_get};

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
SessionVars SessionV = {
    .proto = &Protocols,
    .workers = &Workers,
};
