// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file server.h
 * @brief Layer 4 (Transport) - the passive OPEN: bound ports and their accept-time gates.
 *
 * RFC 9293 sec 3.9.1.1: "If the active/passive flag is set to passive, then this is a call to
 * LISTEN for an incoming connection." This is that side of OPEN. The dialing side is client.h.
 *
 * Each active listener owns one listening control block and one event queue.
 * When a new client connects, `listener_accept_cb` claims a slot from the shared
 * `conn_pool`, wires the standard per-connection callbacks, and posts
 * `EVT_CONNECT` to the owning listener's queue.
 *
 * The session layer drains all active listener queues each `Session.tick()`,
 * routing events to the correct protocol handler via `TcpConn::proto`.
 *
 * **Single accept callback**
 * `protocore_net_arg(listen_pcb, (void*)(uintptr_t)idx)` embeds the listener index
 * in the control block's user data so a single static `listener_accept_cb`
 * handles all ports.
 *
 * **Event posting**
 * The queue belongs to the listener, so the connection callbacks in protocol/protocol.c post to it
 * through ::TcpListener's enqueue rather than writing a queue they do not own.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TCP_SERVER_H
#define PROTOCORE_TCP_SERVER_H

#include "../evt.h" // TcpEvt: the event an enqueue posts. The listener rows themselves are common.h's.
#include "config/platform/platform.h" // the target's queues and TCP, under our names
#include "shared/ip/ip.h" // protocore_ip: the peer address an allowlist matches

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

/**
 * @brief Accept callback from the lower-level module - single handler for all listener ports.
 *
 * Non-static so the host unit tests can call it directly with a fabricated control block, the same
 * convention protocol.c uses for lowlevel_recv_cb / lowlevel_sent_cb / lowlevel_err_cb - production
 * code never calls this directly, the add binds it to the listening block.
 */
protocore_net_err listener_accept_cb(void *arg, protocore_pcb *newpcb, protocore_net_err err);

/** @brief RFC 9293 sec 3.9.1.1 passive OPEN: what a bound port is. */
typedef struct
{
    uint16_t port;   ///< the port it binds, or the one a lookup names
    ProtoConn proto; ///< the application protocol its connections take
    proto_bool tls;  ///< connections accepted here begin a handshake
    uint8_t dscp;    ///< the code point a port marks with
} TcpBindArgs;

/** @brief The accept-time gates: what a throttle or an allowlist judges. Nothing a bind reads. */
typedef struct
{
    uint32_t now_ms;          ///< the clock a throttle window measures against
    const protocore_ip *addr; ///< the source address a gate judges
    uint8_t prefix_len;       ///< its CIDR prefix length (RFC 4632)
    const char *cidr;         ///< the same rule as text
} TcpGateArgs;

/** @brief Event routing: whose queue an event goes to, and what it names. */
typedef struct
{
    int worker_id;     ///< whose queue is being named
    const TcpEvt *evt; ///< the event an enqueue posts
    uint8_t conn_slot; ///< the slot that event names
} TcpQueueArgs;

/**
 * @brief The accepting side of TCP: bound ports, their worker queues, and the accept-time gates.
 *
 * RFC 9293 sec 3.9.1.1: a passive OPEN is a call to LISTEN for an incoming connection. A caller
 * sets the members a call takes, invokes it through ::TcpListener, and reads the outcome off the
 * same handle.
 *
 * @var TcpListenerNs::idx         the listener row a call acts on
 * @var TcpListenerNs::port        the port it binds, or the one a lookup names
 * @var TcpListenerNs::proto       the application protocol its connections take
 * @var TcpListenerNs::tls         connections accepted here begin a handshake
 * @var TcpListenerNs::dscp        the code point a port marks with
 * @var TcpListenerNs::now_ms      the clock a throttle window measures against
 * @var TcpListenerNs::addr        the source address a gate judges
 * @var TcpListenerNs::prefix_len  its CIDR prefix length (RFC 4632)
 * @var TcpListenerNs::cidr        the same rule as text
 * @var TcpListenerNs::worker_id   whose queue is being named
 * @var TcpListenerNs::evt         the event an enqueue posts
 * @var TcpListenerNs::conn_slot   the slot that event names
 * @var TcpListenerNs::ok          a call's true/false outcome
 * @var TcpListenerNs::i32         a call's signed outcome
 * @var TcpListenerNs::queue       the queue a lookup reports
 * @var TcpListenerNs::add                  bind a port and start accepting on it
 * @var TcpListenerNs::add_dynamic          the same from a running task, for ssh -R
 * @var TcpListenerNs::stop                 tear one listener down
 * @var TcpListenerNs::stop_all             tear every listener down
 * @var TcpListenerNs::stop_dynamic         tear down only the dynamically started listeners
 * @var TcpListenerNs::enqueue              post an event to the owning worker's queue
 * @var TcpListenerNs::set_dscp             the mark every connection accepted on a port takes
 * @var TcpListenerNs::worker_queues_init   create the per-worker queues
 * @var TcpListenerNs::worker_queue         the queue a worker drains
 * @var TcpListenerNs::accept_allowed       the global fixed-window accept throttle
 * @var TcpListenerNs::accept_throttle_reset  clear the global window
 * @var TcpListenerNs::accept_allowed_ip    the per-source-address throttle bucket
 * @var TcpListenerNs::per_ip_throttle_reset  clear every per-address bucket
 * @var TcpListenerNs::ip_allow_add         add one address to the allowlist
 * @var TcpListenerNs::ip_allow_add_cidr    add one CIDR rule to the allowlist
 * @var TcpListenerNs::ip_allowed           the allowlist verdict for an address
 * @var TcpListenerNs::ip_allowlist_reset   clear every allowlist rule
 */
typedef struct
{
    uint8_t idx; ///< the listener row every call names

    TcpBindArgs bind; ///< what a passive OPEN binds (RFC 9293 sec 3.9.1.1)
    TcpGateArgs gate; ///< what an accept-time gate judges
    TcpQueueArgs q;   ///< where an event goes

    proto_bool ok;
    int32_t i32;
    protocore_platform_queue queue;

    void (*const stop)(uint8_t *restrict work);
    void (*const stop_all)(uint8_t *restrict work);
    void (*const stop_dynamic)(uint8_t *restrict work);
    void (*const add)(uint8_t *restrict work);
    void (*const add_dynamic)(uint8_t *restrict work);
    void (*const enqueue)(uint8_t *restrict work);
    void (*const set_dscp)(uint8_t *restrict work);
#if PROTOCORE_WORKER_COUNT > 1
    // One worker owns every slot at N=1, so there are no per-worker queues to name.
    void (*const worker_queues_init)(uint8_t *restrict work);
    void (*const worker_queue)(uint8_t *restrict work);
#else
    // Above one worker an event routes to its slot owner's queue, so a listener row has none.
    void (*const listener_queue)(uint8_t *restrict work);
#endif
    void (*const accept_allowed)(uint8_t *restrict work);
    void (*const accept_throttle_reset)(uint8_t *restrict work);
    void (*const accept_allowed_ip)(uint8_t *restrict work);
    void (*const per_ip_throttle_reset)(uint8_t *restrict work);
    void (*const ip_allow_add)(uint8_t *restrict work);
    void (*const ip_allow_add_cidr)(uint8_t *restrict work);
    void (*const ip_allowed)(uint8_t *restrict work);
    void (*const ip_allowlist_reset)(uint8_t *restrict work);
} TcpListenerNs;

/** @brief The one symbol this module exports. */
extern TcpListenerNs TcpListener;

/**
 * @brief The PROTOCORE_TCP_LISTENER_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_tcp_listener_span(void);

PROTOCORE_END_DECLS

#endif
