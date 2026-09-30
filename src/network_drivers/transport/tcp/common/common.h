// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file common.h
 * @brief Layer 4 (Transport) - the connection state every half of TCP reads.
 *
 * RFC 9293 sec 3.3.1 calls these the key connection state variables. Here that is the pool slot:
 * its lifecycle state, its receive ring, and the identity fields the layers above filter on. The
 * server (sec 3.9.1.1 passive OPEN), the client (active OPEN), the protocol engine (sec 3.10) and
 * the lower-level seam (sec 3.9.2) all read this header and none of them declares its own copy.
 *
 * The event record itself is in evt.h, which this includes: the slot state below is `_Atomic`,
 * which is C11 and not C++, so the header that reaches the sketches has to be the smaller one.
 *
 * **Concurrency model**
 * | Context          | Reads                        | Writes                   |
 * |------------------|------------------------------|--------------------------|
 * | stack callbacks  | anularis.vacant (to check)   | anularis.put             |
 * | main loop        | anularis.available/read/peek | anularis.read/consume    |
 *
 * The receive ring is a memoria_anularis ring over the slot's own aligned rx_buffer: one producer
 * (the stack callbacks) and one consumer (the owning worker), with the ordering inside the ring.
 * `state` is `_Atomic`, read and written through PROTO_ATOMIC_LOAD / PROTO_ATOMIC_STORE.
 *
 * **Backpressure (lossless)**
 * When a whole inbound segment will not fit the free ring space, the recv callback refuses it
 * without taking ownership of the segment; the stack holds it and redelivers once the main loop has
 * drained the ring, so no received byte is dropped. Requires RX_BUF_SIZE > one TCP segment
 * (TCP_MSS).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TCP_COMMON_H
#define PROTOCORE_TCP_COMMON_H

#include "config/platform/platform.h"
#include "memoria_anularis/memoria_anularis.h"     // mmgr_ring: the receive ring, and the loculi the slots are
#include "network_drivers/transport/tcp/evt/evt.h" // EvtType, TcpEvt: what this layer posts to a listener queue
#include "shared/ip/ip.h"                          // protocore_ip (family-tagged peer address)
#include <stdatomic.h>                             // atomic_load_explicit / atomic_store_explicit on the slot state

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

// ConnState and the CONN_* names live in evt.h, which this header includes: the signaling layer
// reads a slot's state and reaches this layer through protocore.h.

/**
 * @brief A single TCP connection context.
 *
 * Sized so that `MAX_CONNS` instances fit in a static array without
 * fragmentation.  All fields except the ring and the state may
 * only be accessed from the main-loop task.
 */
typedef struct TcpConn
{
    uint8_t id;                ///< Fixed slot index (0 … MAX_CONNS-1).
    _Atomic ConnState state;   ///< Lifecycle state; acquire/release for inter-task visibility.
    protocore_pcb *pcb;        ///< Stack control block; null when slot is free.
    uint32_t last_activity_ms; ///< `protocore_millis()` timestamp of last TX/RX event.

    EMBED_ALIGN(MMGR_ALIGN_BYTES) uint8_t rx_buffer[RX_BUF_SIZE]; ///< The ring's bytes, owned here.
    mmgr_ring rx;                                                 ///< The receive ring over rx_buffer.
    size_t rx_unacked; ///< Bytes drained since the last ACK to the stack. Worker-only: the window is
                       ///< reopened by exactly these, so it tracks ring occupancy (ack-on-consume)
                       ///< rather than copy.

    uint8_t listener_id; ///< Index into listener_pool[]; set at accept time.
    uint8_t owner;       ///< Worker that owns this slot (round-robin at accept). Always 0 at N=1.
    ProtoConn proto;     ///< Application protocol for this connection.
    uint8_t
        proto_slot; ///< Per-protocol session/pool index (0xFF = none): the SSH session, an MQTT/Modbus session, etc.
    protocore_if_kind iface; ///< Interface this connection arrived on; set at accept time.
    uint8_t tls;             ///< Non-zero when this connection is TLS (set at accept time).
    // The response sink and the HTTP/2 and HTTP/3 per-connection fields are HTTP semantics and live
    // with HTTP, keyed on the same slot index, as http_req_count already does.
} TcpConn;

/** @brief Sentinel for TcpConn.proto_slot meaning "no per-protocol session bound". */
#define PROTOCORE_PROTO_SLOT_NONE 0xFFu

// ---------------------------------------------------------------------------
// Slot state, as loculi
// ---------------------------------------------------------------------------
//
// A slot's availability is one loculus of the pool's slot ring (protocol/protocol.c), rather than a
// field to load and compare. Loculus i is held while conn_pool[i] is anything but CONN_FREE, keeping
// out that slot's rx_buffer, and dropped when it returns to CONN_FREE. Written through
// protocore_conn_set_state() only, so it stays in lock-step with the state.
//
// A slot is allocatable only when its loculus is free AND not held: anularis.loculus_ready is
// `free & ~held`, and anularis.loculus_next picks the lowest. Holding is what makes reuse safe.
// Without it a slot reads free while its bytes are still in use, and the index is handed to a new
// connection on top of the old one's data - the collision RFC 9293 sec 3.6.1 keeps a connection
// identifier out of circulation to avoid, expressed as a bit rather than a timer, because a pool
// index is not a socket and has no quiet period to wait out.

_Static_assert(MAX_CONNS <= MMGR_RING_LOCULI,
               "every connection slot is a loculus; raise MMGR_RING_LOCULI (and MMGR_RING_WORDS) with MAX_CONNS");

/**
 * @brief Access-point IPv4 address (network byte order) for STA/AP interface tagging.
 *
 * Zero when no access point is configured. Set via set_ap_ip(); the
 * accept callback tags each connection PROTOCORE_IF_WIFI_AP when its local IP equals
 * this, else PROTOCORE_IF_WIFI_STA. Used by per-route interface filters.
 */
extern uint32_t protocore_ap_ip;

/** @brief Static pool of connection contexts. Defined in protocol/protocol.c.
 *  Sized CONN_POOL_SLOTS: MAX_CONNS TCP slots plus any reserved internal dispatch slot(s)
 *  (HTTP/3); the TCP accept path only ever uses [0, MAX_CONNS). */
extern TcpConn conn_pool[CONN_POOL_SLOTS];

// The receive ring is drained through ::ConnPool - available, read_byte, peek, consume and read -
// and a slot is asked about through active, iface, listener_id, tls, owner, proto_of and pcb_of.
// Transport owns the ring: nothing above this layer reaches rx_buffer or the ring itself.

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
 *   sizeof(protocore_pcb*) + sizeof(protocore_platform_queue_ctrl) + EVT_QUEUE_DEPTH*sizeof(TcpEvt)
 *   + sizeof(protocore_platform_queue) + 3 bytes overhead (port, proto, active).
 */
typedef struct
{
    uint16_t port;             ///< TCP port this listener binds.
    ProtoConn proto;           ///< Application protocol for all connections accepted here.
    protocore_pcb *listen_pcb; ///< the listening control block; NULL when inactive.
#if PROTOCORE_WORKER_COUNT == 1
    // One worker owns every slot, so the listener's own queue is the only path an event takes. Above
    // one, an event routes to its slot owner's queue instead and this one is never sent to.
    protocore_platform_queue_ctrl _queue_struct;              ///< Static queue descriptor.
    uint8_t _queue_storage[EVT_QUEUE_DEPTH * sizeof(TcpEvt)]; ///< Queue backing store.
    protocore_platform_queue queue;                           ///< Handle returned by protocore_platform_queue_create().
#endif
    proto_bool active; ///< True after listener_add(), false after listener_stop().
    proto_bool tls;    ///< True when connections accepted here begin a TLS handshake.
    uint8_t dscp;      ///< Per-listener DiffServ DSCP for accepted connections; PROTOCORE_DSCP_UNSET = use the default.
} Listener;

/**
 * @brief The row carries no code point of its own; accept() takes the server-wide default.
 *
 * Stated here rather than in diffserv.h because it is a value of ::Listener::dscp, which every
 * build has: a port either names a code point or does not, and only whether the accept callback
 * stamps one into the DS field depends on ::PROTOCORE_ENABLE_DIFFSERV.
 */
#define PROTOCORE_DSCP_UNSET 0xFF

/** @brief Static pool of listener contexts.  Defined in server.c. */
extern Listener listener_pool[MAX_LISTENERS];

PROTOCORE_END_DECLS

#endif // PROTOCORE_TCP_COMMON_H
