// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file listener.h
 * @brief Layer 4 (Listener) - per-port TCP listener abstraction.
 *
 * Each active listener owns one lwIP listening PCB and one statically-
 * allocated FreeRTOS queue.  When a new client connects, `listener_accept_cb`
 * claims a slot from the shared `conn_pool`, wires the standard per-connection
 * callbacks, and posts `EvtType::EVT_CONNECT` to the owning listener's queue.
 *
 * The session layer drains all active listener queues each `server_tick()`,
 * routing events to the correct protocol handler via `TcpConn::proto`.
 *
 * **Single accept callback**
 * `tcp_arg(listen_pcb, (void*)(uintptr_t)idx)` embeds the listener index in
 * the PCB user-data so a single static `listener_accept_cb` handles all ports.
 *
 * **Circular-dependency resolution**
 * tcp.cpp needs to post events to listener queues but cannot include
 * this header (listener.h already includes tcp.h).  The symbol
 * `listener_enqueue()` is exported from listener.cpp; tcp.cpp calls it
 * via a forward declaration added to tcp.h so no circular include
 * is introduced.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_LISTENER_H
#define PROTOCORE_LISTENER_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lwip/tcp.h"
#include "protocore_config.h"
#include "tcp.h"

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
 *   sizeof(tcp_pcb*) + sizeof(StaticQueue_t) + EVT_QUEUE_DEPTH*sizeof(TcpEvt)
 *   + sizeof(QueueHandle_t) + 3 bytes overhead (port, proto, active).
 */
struct Listener
{
    uint16_t port;               ///< TCP port this listener binds.
    ConnProto proto;             ///< Application protocol for all connections accepted here.
    struct tcp_pcb *listen_pcb;  ///< lwIP listen PCB; nullptr when inactive.
    StaticQueue_t _queue_struct; ///< FreeRTOS static queue descriptor.
    uint8_t _queue_storage[EVT_QUEUE_DEPTH * sizeof(TcpEvt)]; ///< Queue backing store.
    QueueHandle_t queue;                                      ///< Handle returned by xQueueCreateStatic().
    bool active; ///< True after listener_add(), false after listener_stop().
    bool tls;    ///< True when connections accepted here begin a TLS handshake.
#if PC_ENABLE_DIFFSERV
    uint8_t dscp; ///< Per-listener DiffServ DSCP for accepted connections; PC_DSCP_UNSET = use the default.
#endif
};

/** @brief Static pool of listener contexts.  Defined in listener.cpp. */
extern Listener listener_pool[MAX_LISTENERS];

/**
 * @brief lwIP accept callback - single handler for all listener ports (defined in listener.cpp).
 *
 * Non-static so the host unit tests can call it directly with a fabricated newpcb, the same
 * convention tcp.cpp uses for lowlevel_recv_cb / lowlevel_sent_cb / lowlevel_err_cb - production
 * code never calls this directly, it is wired in via tcp_arg()+tcp_accept() in listener_add().
 */
err_t listener_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err);

// ---------------------------------------------------------------------------
// Listener management API
// ---------------------------------------------------------------------------

/**
 * @brief Create a listening socket on @p port and register it at @p idx.
 *
 * If the slot at @p idx is already active it is stopped first.
 * Creates a per-listener FreeRTOS static queue and an lwIP listening PCB.
 * Wires `listener_accept_cb` as the accept handler with the listener index
 * embedded as the PCB user-data argument.
 *
 * @param idx   Slot in listener_pool[] (0 … MAX_LISTENERS-1).
 * @param port  TCP port to bind and listen on.
 * @param proto Application protocol spoken on connections from this port.
 * @param tls   When true, connections accepted here start a TLS handshake.
 * @return Positive value on success; -1 on failure (pool full or lwIP error).
 */
int32_t listener_add(uint8_t idx, uint16_t port, ConnProto proto, bool tls = false);

/**
 * @brief Stop listening on the port at @p idx and release its resources.
 *
 * Idempotent - safe to call on an already-stopped slot.
 * Closes the lwIP listening PCB and deletes the FreeRTOS queue.
 * Does not close any connections already accepted on this port.
 *
 * @param idx  Slot in listener_pool[].
 */
void listener_stop(uint8_t idx);

/**
 * @brief Stop all active listeners.
 *
 * Convenience wrapper that calls listener_stop() for every slot in
 * listener_pool[].  Called by PC::stop().
 */
void listener_stop_all();

/**
 * @brief Add / stop a listener from a running task (thread-safe variant).
 *
 * listener_add() runs the raw lwIP tcp_bind/tcp_listen inline, which is only safe at
 * setup (before tcpip_thread is servicing our sockets). These variants marshal the
 * lwIP operations onto tcpip_thread via tcpip_api_call(), so a listener may be created
 * or torn down dynamically from a worker/session task - used by the SSH remote-forward
 * owner (`ssh -R`), which opens a listener when a client requests one. TLS listeners
 * are not supported here (forwarded ports are plaintext bridges). On the native host
 * (no lwIP) these behave like the inline path for unit tests.
 *
 * @return listener_add_dynamic: 1 on success, -1 on failure (bad idx, bind in use,
 *         or lwIP error). listener_stop_dynamic: void, idempotent.
 */
int32_t listener_add_dynamic(uint8_t idx, uint16_t port, ConnProto proto);
void listener_stop_dynamic(uint8_t idx);

/**
 * @brief Post @p evt to the queue owned by listener @p listener_id.
 *
 * Called from tcp.cpp callbacks (running in tcpip_thread context) to
 * deliver connection events to the session layer.  Uses xQueueSend with
 * timeout=0 - a full queue means the application is not calling server_tick()
 * fast enough; the dropped event is recoverable via connection timeout.
 *
 * @param listener_id  Index into listener_pool[]; must be < MAX_LISTENERS.
 * @param evt          Event to copy into the queue.
 * @return true if queued; false if dropped (full queue / inactive listener).
 */
bool listener_enqueue(uint8_t listener_id, const TcpEvt *evt);

#if PC_WORKER_COUNT > 1
/** @brief Create the per-worker event queues (idempotent; called from listener_add). */
void listener_worker_queues_init(void);

/** @brief The FreeRTOS event queue for worker @p worker_id (nullptr if out of range). */
QueueHandle_t listener_worker_queue(int worker_id);
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
bool listener_accept_allowed(uint32_t now_ms);

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
bool listener_accept_allowed_ip(const pc_ip *ip, uint32_t now_ms);

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
bool listener_ip_allow_add(const pc_ip *network, uint8_t prefix_len);

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
bool listener_ip_allow_add_cidr(const char *cidr);

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
bool listener_ip_allowed(const pc_ip *ip);

/** @brief Clear all allowlist rules (the allowlist becomes empty = allow all). */
void listener_ip_allowlist_reset(void);

#endif
