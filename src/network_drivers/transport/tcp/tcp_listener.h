// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file listener.h
 * @brief Layer 4 (Listener) - per-port TCP listener abstraction.
 *
 * Each active listener owns one listening control block and one event queue.
 * When a new client connects, `listener_accept_cb` claims a slot from the shared
 * `conn_pool`, wires the standard per-connection callbacks, and posts
 * `EVT_CONNECT` to the owning listener's queue.
 *
 * The session layer drains all active listener queues each `server_tick()`,
 * routing events to the correct protocol handler via `TcpConn::proto`.
 *
 * **Single accept callback**
 * `proto_pcb_set_arg(listen_pcb, (void*)(uintptr_t)idx)` embeds the listener index
 * in the control block's user data so a single static `listener_accept_cb`
 * handles all ports.
 *
 * **Event posting**
 * The queue belongs to the listener, so the connection callbacks in tcp.c post to it through
 * `listener_enqueue()`, declared here and defined in listener.c, rather than writing a queue they
 * do not own.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TCP_LISTENER_H
#define PROTOCORE_TCP_LISTENER_H

#include "../tcp_evt.h" // TcpEvt: what a listener's queue holds. The slots themselves are tcp.h's.
#include "board_drivers/board_profiles/pc_platform.h" // the target's queues and TCP, under our names
#include "protocore_config.h"
#include "shared_primitives/ip.h" // pc_ip: the peer address an allowlist matches

PROTO_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Listener pool entry
// ---------------------------------------------------------------------------

/**
 * @brief State for one TCP listening port.
 *
 * All queue storage is embedded in this struct so the entire listener pool
 * lives in BSS - no heap allocation anywhere in the listener layer.
 *
 * A single `Listener` instance consumes:
 *   sizeof(tcp_pcb*) + sizeof(pc_platform_queue_ctrl) + EVT_QUEUE_DEPTH*sizeof(TcpEvt)
 *   + sizeof(pc_platform_queue) + 3 bytes overhead (port, proto, active).
 */
typedef struct
{
    uint16_t port;                        ///< TCP port this listener binds.
    ConnProto proto;                      ///< Application protocol for all connections accepted here.
    pc_pcb *listen_pcb;                   ///< lwIP listen PCB; NULL when inactive.
    pc_platform_queue_ctrl _queue_struct; ///< Static queue descriptor.
    uint8_t _queue_storage[EVT_QUEUE_DEPTH * sizeof(TcpEvt)]; ///< Queue backing store.
    pc_platform_queue queue;                                  ///< Handle returned by pc_platform_queue_create().
    proto_bool active; ///< True after listener_add(), false after listener_stop().
    proto_bool tls;    ///< True when connections accepted here begin a TLS handshake.
#if PC_ENABLE_DIFFSERV
    uint8_t dscp; ///< Per-listener DiffServ DSCP for accepted connections; PC_DSCP_UNSET = use the default.
#endif
} Listener;

/** @brief Static pool of listener contexts.  Defined in listener.c. */
extern Listener listener_pool[MAX_LISTENERS];

/**
 * @brief lwIP accept callback - single handler for all listener ports (defined in listener.c).
 *
 * Non-static so the host unit tests can call it directly with a fabricated newpcb, the same
 * convention tcp.c uses for lowlevel_recv_cb / lowlevel_sent_cb / lowlevel_err_cb - production
 * code never calls this directly, it is wired in via tcp_arg()+tcp_accept() in listener_add().
 */
pc_net_err listener_accept_cb(void *arg, pc_pcb *newpcb, pc_net_err err);

// ---------------------------------------------------------------------------
// Listener management API
// ---------------------------------------------------------------------------

#if PC_WORKER_COUNT > 1

#endif

// ---------------------------------------------------------------------------
// Source-IP allowlist (accept-time firewall)
// ---------------------------------------------------------------------------

/**
 * @brief The accepting side of TCP: bound ports, their worker queues, and the accept-time gates.
 *
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
    void (*stop)(uint8_t idx);
    void (*stop_all)(void);
    void (*stop_dynamic)(uint8_t idx);
    int32_t (*add)(uint8_t idx, uint16_t port, ConnProto proto, proto_bool tls);
    int32_t (*add_dynamic)(uint8_t idx, uint16_t port, ConnProto proto);
    proto_bool (*enqueue)(uint8_t listener_id, const TcpEvt *evt);
#if PC_ENABLE_DIFFSERV
    proto_bool (*set_dscp)(uint16_t port, uint8_t dscp);
#endif
#if PC_WORKER_COUNT > 1
    // One worker owns every slot at N=1, so there are no per-worker queues to name.
    void (*worker_queues_init)(void);
    pc_platform_queue (*worker_queue)(int worker_id);
#endif
    proto_bool (*accept_allowed)(uint32_t now_ms);
    void (*accept_throttle_reset)(void);
    proto_bool (*accept_allowed_ip)(const pc_ip *ip, uint32_t now_ms);
    void (*per_ip_throttle_reset)(void);
    proto_bool (*ip_allow_add)(const pc_ip *network, uint8_t prefix_len);
    proto_bool (*ip_allow_add_cidr)(const char *cidr);
    proto_bool (*ip_allowed)(const pc_ip *ip);
    void (*ip_allowlist_reset)(void);
} TcpListenerNs;

/** @brief The one symbol this module exports. */
extern const TcpListenerNs TcpListener;

PROTO_END_DECLS

#endif
