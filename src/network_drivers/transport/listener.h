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
 * **Circular-dependency resolution**
 * tcp.c needs to post events to listener queues but cannot include
 * this header (listener.h already includes tcp.h).  The symbol
 * `listener_enqueue()` is exported from listener.c; tcp.c calls it
 * via a forward declaration added to tcp.h so no circular include
 * is introduced.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_LISTENER_H
#define PROTOCORE_LISTENER_H

#include "board_drivers/board_profiles/pc_platform.h" // the target's queues and TCP, under our names
#include "network_drivers/network/ip.h"               // pc_ip: the peer address an allowlist matches
#include "protocore_config.h"
#include "tcp_evt.h" // TcpEvt: what a listener's queue holds. The slots themselves are tcp.h's.

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

/**
 * @brief Create a listening socket on @p port and register it at @p idx.
 *
 * If the slot at @p idx is already active it is stopped first.
 * Creates a per-listener event queue and a listening control block, and installs
 * `listener_accept_cb` with the listener index as its callback argument.
 *
 * @param idx   Slot in listener_pool[] (0 … MAX_LISTENERS-1).
 * @param port  TCP port to bind and listen on.
 * @param proto Application protocol spoken on connections from this port.
 * @param tls   When true, connections accepted here start a TLS handshake.
 * @return Positive value on success; -1 on failure (pool full or a stack error).
 */
int32_t listener_add(uint8_t idx, uint16_t port, ConnProto proto, proto_bool tls);

/**
 * @brief Stop listening on the port at @p idx and release its resources.
 *
 * Idempotent - safe to call on an already-stopped slot.
 * Closes the listening control block and releases the event queue.
 * Does not close any connections already accepted on this port.
 *
 * @param idx  Slot in listener_pool[].
 */
void listener_stop(uint8_t idx);

/**
 * @brief Stop all active listeners.
 *
 * Convenience wrapper that calls listener_stop() for every slot in
 * listener_pool[].  Called by stop().
 */
void listener_stop_all(void);

/**
 * @brief Add / stop a listener from a running task.
 *
 * Used by the SSH remote-forward owner (`ssh -R`), which opens a listener when a client requests
 * one. TLS listeners are not supported here (forwarded ports are plaintext bridges).
 *
 * @return listener_add_dynamic: 1 on success, -1 on failure (bad idx, bind in use, or a stack
 *         error). listener_stop_dynamic: void, idempotent.
 */
int32_t listener_add_dynamic(uint8_t idx, uint16_t port, ConnProto proto);
void listener_stop_dynamic(uint8_t idx);

/**
 * @brief Post @p evt to the queue owned by listener @p listener_id.
 *
 * Called from the tcp.c callbacks to deliver connection events to the session layer. Posts with a
 * zero timeout - a full queue means the application is not calling server_tick() fast enough; the
 * dropped event is recoverable via connection timeout.
 *
 * @param listener_id  Index into listener_pool[]; must be < MAX_LISTENERS.
 * @param evt          Event to copy into the queue.
 * @return true if queued; false if dropped (full queue / inactive listener).
 */
proto_bool listener_enqueue(uint8_t listener_id, const TcpEvt *evt);

#if PC_WORKER_COUNT > 1
/** @brief Create the per-worker event queues (idempotent; called from listener_add). */
void listener_worker_queues_init(void);

/** @brief The event queue for worker @p worker_id (NULL if out of range). */
pc_platform_queue listener_worker_queue(int worker_id);
#endif

/**
 * @brief Fixed-window global accept-rate gate (connection-flood defense).
 *
 * Returns true if a new connection accepted at @p now_ms is within the
 * PC_ACCEPT_THROTTLE_MAX-per-PC_ACCEPT_THROTTLE_WINDOW_MS budget (and
 * counts it), false if the budget for the current window is exhausted. State is
 * two static counters shared across all listeners. The accept callback consults
 * this only when PC_ENABLE_ACCEPT_THROTTLE is set; the function is always
 * compiled so it can be unit-tested. Call listener_accept_throttle_reset() to
 * clear the window (e.g. between tests).
 */
proto_bool listener_accept_allowed(uint32_t now_ms);

/** @brief Reset the accept-throttle window counters. */
void listener_accept_throttle_reset(void);

/**
 * @brief Fixed-window per-IP accept-rate gate (connection-flood defense, keyed by source address).
 *
 * Returns true if a connection from source address @p ip accepted at @p now_ms is
 * within that address's PC_PER_IP_THROTTLE_MAX-per-PC_PER_IP_THROTTLE_WINDOW_MS
 * budget (and counts it), false once that address has exhausted its budget for the
 * current window. The key is the full family-tagged address (pc_ip): an IPv4 and an
 * IPv6 peer are always distinct buckets, and an IPv6 attacker cannot fold many
 * addresses onto one bucket (or evict a victim's) through a lossy hash. State is a
 * fixed BSS table of PC_PER_IP_THROTTLE_SLOTS buckets; a new address reuses an
 * empty, expired, or least-recently-started bucket so memory stays bounded. An
 * unspecified @p ip (family pc_ip_family::PC_IP_NONE) is passed through (allowed) since it cannot
 * be tracked. The accept callback consults this only when PC_ENABLE_PER_IP_THROTTLE
 * is set; the function is always compiled so it can be unit-tested. Call
 * listener_per_ip_throttle_reset() to clear the table.
 */
proto_bool listener_accept_allowed_ip(const pc_ip *ip, uint32_t now_ms);

/** @brief Reset the per-IP throttle bucket table. */
void listener_per_ip_throttle_reset(void);

// ---------------------------------------------------------------------------
// Source-IP allowlist (accept-time firewall)
// ---------------------------------------------------------------------------

/**
 * @brief Add a CIDR rule to the source-IP allowlist.
 *
 * @param network     Family-tagged network address (pc_ip, IPv4 or IPv6). Host bits
 *                    outside the prefix do not need to be pre-masked; matching masks
 *                    them at compare time.
 * @param prefix_len  CIDR prefix length: 0..32 for IPv4, 0..128 for IPv6 (32 / 128
 *                    for a single host, 0 to match every address of that family).
 * @return true if the rule was stored; false if @p network is unspecified, the
 *         prefix length exceeds the family width, or the table
 *         (PC_IP_ALLOWLIST_SLOTS entries) is full.
 */
proto_bool listener_ip_allow_add(const pc_ip *network, uint8_t prefix_len);

/**
 * @brief Add an allowlist rule from CIDR text (the ergonomic public entry point).
 *
 * Accepts IPv4 or IPv6 in `address/prefix` form (e.g. "192.168.1.0/24",
 * "2001:db8::/32") or a bare address (e.g. "10.0.0.5", "::1") which is treated as
 * a host route (/32 for v4, /128 for v6). The address is parsed with pc_ip_parse
 * so every RFC 4291 v6 text form is accepted.
 *
 * @return true if the rule was stored; false if @p cidr is malformed, the prefix
 *         is out of range for the family, or the table is full.
 */
proto_bool listener_ip_allow_add_cidr(const char *cidr);

/**
 * @brief Test a source address against the allowlist (accept-time firewall).
 *
 * @param ip  Family-tagged source address (pc_ip).
 * @return true if the address is allowed: always true while the allowlist is
 *         empty (so enabling the feature without rules never locks the device
 *         out), otherwise true only if @p ip is contained in at least one CIDR
 *         rule of the same family (prefix match on the full address, never a hash).
 *         The accept callback consults this only when PC_ENABLE_IP_ALLOWLIST
 *         is set; the function is always compiled so it can be unit-tested.
 */
proto_bool listener_ip_allowed(const pc_ip *ip);

/** @brief Clear all allowlist rules (the allowlist becomes empty = allow all). */
void listener_ip_allowlist_reset(void);

PROTO_END_DECLS

#endif
