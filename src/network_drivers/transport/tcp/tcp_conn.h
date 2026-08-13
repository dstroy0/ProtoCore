// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tcp.h
 * @brief Layer 4 (Transport) - TCP connection pool, ring buffers, and the network stack seam.
 *
 * Defines the static connection pool and the per-connection event plumbing.
 * Each listener port owns its own event queue (see listener.h); the
 * session layer drains all active queues each tick via Session.tick().
 *
 * This layer and tls/ are the only two that speak the platform network stack, so the
 * stack's types appear in the signatures below. Every layer above reaches the connection
 * through the protocore_conn_* API and never sees them.
 *
 * **Concurrency model**
 * | Context          | Reads                  | Writes                  |
 * |------------------|------------------------|-------------------------|
 * | stack callbacks  | rx_head (to check full)| rx_buffer[], rx_head    |
 * | main loop        | rx_buffer[], rx_tail   | rx_tail                 |
 *
 * `state`, `rx_head`, and `rx_tail` are `_Atomic`, read and written through
 * PROTO_ATOMIC_LOAD / PROTO_ATOMIC_STORE (acquire/release): the
 * single-producer / single-consumer ring buffer is correct without a mutex
 * because the release store of an index publishes the preceding buffer writes
 * and the acquire load observes them, on either core.
 *
 * **Backpressure (lossless)**
 * When a whole inbound segment will not fit the free ring space, the recv
 * callback refuses it without taking ownership of the segment; the stack holds it
 * and redelivers once the main loop has drained the ring, so no received byte is
 * dropped. Requires RX_BUF_SIZE > one TCP segment (TCP_MSS).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TCP_CONN_H
#define PROTOCORE_TCP_CONN_H

#include "../tcp_evt.h" // EvtType, TcpEvt: what this layer posts to a listener queue
#include "core_setup/board_profiles/protocore_platform.h"
#include "mmgr/ring.h" // PROTO_ATOMIC_LOAD/STORE + the shared SPSC ring drain primitive
#include "protocore_config.h"
#include "shared_primitives/ip.h" // protocore_ip (family-tagged peer address)

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Connection state
// ---------------------------------------------------------------------------

/**
 * @brief Lifecycle state of a connection pool slot.
 *
 * NOT the RFC 9293 sec 3.3.2 connection state machine. That machine (LISTEN, SYN-RECEIVED,
 * ESTABLISHED, FIN-WAIT-1/2, CLOSE-WAIT, CLOSING, LAST-ACK, TIME-WAIT) belongs to the stack under
 * this layer; these three name only whether the pool slot is available. A test mapping CONN_* to
 * RFC state names 1:1 is testing the wrong thing.
 *
 * Transitions, as the code performs them:
 * - `CONN_FREE → CONN_ACTIVE`     accept callback fires.
 * - `CONN_ACTIVE → CONN_CLOSING`  protocore_conn_begin_close().
 * - `CONN_CLOSING → CONN_FREE`    the peer ACKed the outbound data (snd_queuelen == 0), or the
 *                                 dwell timed out.
 * - `CONN_ACTIVE → CONN_FREE`     local close, remote FIN, stack error, or the idle sweep.
 *
 * Every terminal edge detaches the pcb and frees the slot before handing the pcb to the stack, so
 * the slot's lifetime ends before the connection's does; the FIN and its retransmission are the
 * stack's from that point (RFC 9293 sec 3.6).
 */
typedef enum PROTO_ENUM_PACKED
{
    CONN_FREE,   ///< Slot is available; no PCB is attached.
    CONN_ACTIVE, ///< Live connection; PCB is valid.
    CONN_CLOSING ///< Transmit-drain dwell: holding the slot until the peer ACKs what was sent. No
                 ///< FIN has been emitted yet - it goes out as the slot is released.
} ConnState;
static_assert(sizeof(ConnState) == 1,
              "ConnState must stay one byte (PROTO_ENUM_PACKED); TcpConn and conn_pool[] size themselves on it");

/**
 * @brief A single TCP connection context.
 *
 * Sized so that `MAX_CONNS` instances fit in a static array without
 * fragmentation.  All fields except the ring-buffer indices may
 * only be accessed from the main-loop task.
 */
typedef struct TcpConn
{
    uint8_t id;                ///< Fixed slot index (0 … MAX_CONNS-1).
    _Atomic ConnState state;   ///< Lifecycle state; acquire/release for inter-task visibility.
    protocore_pcb *pcb;        ///< Stack control block; null when slot is free.
    uint32_t last_activity_ms; ///< `protocore_millis()` timestamp of last TX/RX event.
    uint32_t req_start_ms;     ///< `protocore_millis()` at the first byte of the in-progress request (0 = none). The
                               ///< request-header deadline (PROTOCORE_REQUEST_TIMEOUT_MS, slow-loris defense) measures
                               ///< against this; unlike last_activity_ms a trickle byte cannot reset it.

    uint8_t rx_buffer[RX_BUF_SIZE]; ///< Ring buffer storage.
    _Atomic size_t rx_head;         ///< Producer write index (stack callback context).
    _Atomic size_t rx_tail;         ///< Consumer read index (worker context).
    size_t rx_acked;                ///< rx_tail position last ACKed to the stack. Worker-only:
                                    ///< the window is reopened by exactly the bytes drained since, so it
                                    ///< tracks ring occupancy (ack-on-consume) rather than copy.

    uint8_t listener_id; ///< Index into listener_pool[]; set at accept time.
    uint8_t owner;       ///< Worker that owns this slot (round-robin at accept). Always 0 at N=1.
    ProtoConn proto;     ///< Application protocol for this connection.
    uint8_t
        proto_slot; ///< Per-protocol session/pool index (0xFF = none): the SSH session, an MQTT/Modbus session, etc.
    protocore_if_kind iface; ///< Interface this connection arrived on; set at accept time.
    uint8_t tls;             ///< Non-zero when this connection is TLS (set at accept time).
#if PROTOCORE_ENABLE_HTTP2 || PROTOCORE_ENABLE_HTTP3
    /// Self-framing protocol response sink (Layer 5 TX seam): HTTP/2 installs it at ALPN, HTTP/3 at
    /// dispatch, so the response methods route through it instead of building an HTTP/1.1 message.
    /// Null means plain HTTP/1.1 (the default builder). Extends the ProtoHandler seam to the TX side.
    proto_bool (*protocore_resp_sink)(uint8_t slot, int code, const char *content_type, const char *body, size_t len);
#endif
#if PROTOCORE_ENABLE_HTTP2
    uint8_t h2;                   ///< Non-zero once this connection negotiated HTTP/2 (ALPN "h2").
    uint8_t protocore_h2_checked; ///< The post-handshake ALPN check ran (once per connection).
    uint32_t protocore_h2_stream; ///< Stream id of the request currently being dispatched (for the response).
#endif
#if PROTOCORE_ENABLE_HTTP3
    uint8_t h3;                    ///< Non-zero when this is the reserved HTTP/3 dispatch slot (no TCP pcb).
    uint32_t protocore_h3_conn_id; ///< protocore_quic_server connection id the response routes back to.
    uint64_t protocore_h3_stream;  ///< HTTP/3 request stream id the response is written on.
#endif
} TcpConn;

/** @brief Sentinel for TcpConn.proto_slot meaning "no per-protocol session bound". */
#define PROTOCORE_PROTO_SLOT_NONE 0xFFu

// ---------------------------------------------------------------------------
// Slot state, as bits
// ---------------------------------------------------------------------------
//
// A slot's availability is two questions, and each is one bit in a mask rather than a field to
// load and compare:
//
//   free  bit i set = conn_pool[i] is CONN_FREE. Written through protocore_conn_set_state() only,
//         so it stays in lock-step with the state.
//   held  bit i set = something still owns bytes in slot i - a transfer the wire has not finished
//         reading. Taken when that begins and dropped when it completes.
//
// A slot is allocatable only when it is free AND not held: protocore_slot_ready() is
// `free & ~held`, and protocore_slot_next() picks the lowest with one ctz. Holding is what makes
// reuse safe. Without it a slot reads free while a transfer is still walking its bytes, and the
// index is handed to a new connection on top of the old one's in-flight data - the collision RFC
// 9293 sec 3.6.1 keeps a connection identifier out of circulation to avoid, expressed as a bit
// rather than a timer, because a pool index is not a socket and has no quiet period to wait out.

/** @brief The pool's slot bitmaps. Both are read by the allocator and written from stack and
 *  worker context, so both are atomic. */
typedef struct
{
    _Atomic uint32_t free; ///< bit i = conn_pool[i] is CONN_FREE.
    _Atomic uint32_t held; ///< bit i = slot i still owns bytes in flight.
} ConnSlotBits;

/** @brief The one instance, defined in tcp_conn.c. */
extern ConnSlotBits protocore_conn_bits;

_Static_assert(MAX_CONNS <= PROTOCORE_RING_SLOTS_MAX,
               "the slot bitmaps are uint32; raise them or fall back to a scan if MAX_CONNS exceeds 32");

/**
 * @brief Access-point IPv4 address (network byte order) for STA/AP interface tagging.
 *
 * Zero when no access point is configured. Set via set_ap_ip(); the
 * accept callback tags each connection PROTOCORE_IF_WIFI_AP when its local IP equals
 * this, else PROTOCORE_IF_WIFI_STA. Used by per-route interface filters.
 */
extern uint32_t protocore_ap_ip;

/** @brief Static pool of connection contexts.  Defined in tcp.c.
 *  Sized CONN_POOL_SLOTS: MAX_CONNS TCP slots plus any reserved internal dispatch slot(s)
 *  (HTTP/3); the TCP accept path only ever uses [0, MAX_CONNS). */
extern TcpConn conn_pool[CONN_POOL_SLOTS];

// ---------------------------------------------------------------------------
// Event queue
// ---------------------------------------------------------------------------

// ::EvtType and ::TcpEvt are in tcp_evt.h, included above: they are what the layers over the
// transport post and drain, and none of those layers touches a connection slot.

// ---------------------------------------------------------------------------
// Connection pool lifecycle
// ---------------------------------------------------------------------------
// proto_tcp_pool_init() initializes the connection pool and the runtime timeout config once
// per boot (or per restart cycle). Listening sockets and per-listener queues are owned by
// the listener layer (see listener.h); these manage only the shared conn_pool[] and the
// idle-timeout sweep.

// ---------------------------------------------------------------------------
// Connection output API (defined in tcp.c)
// ---------------------------------------------------------------------------
// The one send/flush/close path for all higher layers. Presentation (WebSocket,
// SSE, SSH) and the HTTP application call these instead of touching the stack, so the
// transport layer stays the sole owner of TCP I/O. protocore_conn_send/flush are
// TLS-aware (route through the TLS record layer when the slot is a TLS conn);
// with PROTOCORE_ENABLE_TLS off they are a bare write and flush.

// ---------------------------------------------------------------------------
// RX ring read API - the single way any layer drains received bytes.
//
// Transport owns the ring; consumers (HTTP/WS/Telnet/SSH/TLS and the framed
// services) must never index rx_buffer or advance rx_tail themselves - they call
// these. Consuming functions advance rx_tail only; the window is reopened by the
// worker's protocore_conn_ack_consumed() once per loop (one owner, no per-byte ACK).
// Single-consumer per slot (the owning worker), so no locking here. These are
// inline because the byte path is hot and the ring internals live in this header.
// ---------------------------------------------------------------------------

// All five delegate to the shared SPSC ring primitive (ring.h) over the slot's
// rx_buffer - the server transport never reimplements the ring math.

/** @brief Bytes currently available to read from @p slot's ring. */
static inline size_t protocore_conn_available(uint8_t slot)
{
    const TcpConn *c = &conn_pool[slot];
    return protocore_ring_available(&c->rx_head, &c->rx_tail, RX_BUF_SIZE);
}

/** @brief Pop one byte into @p out; false if the ring is empty. */
static inline proto_bool protocore_conn_read_byte(uint8_t slot, uint8_t *out)
{
    TcpConn *c = &conn_pool[slot];
    return protocore_ring_read_byte(c->rx_buffer, RX_BUF_SIZE, &c->rx_head, &c->rx_tail, out);
}

/** @brief Copy @p n bytes at @p off from the tail into @p dst WITHOUT consuming (lookahead). */
static inline void protocore_conn_peek(uint8_t slot, size_t off, uint8_t *dst, size_t n)
{
    const TcpConn *c = &conn_pool[slot];
    protocore_ring_peek(c->rx_buffer, RX_BUF_SIZE, &c->rx_tail, off, dst, n);
}

/** @brief Drop @p n bytes from the tail (advance past already-peeked data). */
static inline void protocore_conn_consume(uint8_t slot, size_t n)
{
    protocore_ring_consume(&conn_pool[slot].rx_tail, RX_BUF_SIZE, n);
}

/** @brief Pop up to @p cap bytes into @p buf; returns the count read. */
static inline size_t protocore_conn_read(uint8_t slot, uint8_t *buf, size_t cap)
{
    TcpConn *c = &conn_pool[slot];
    return protocore_ring_read(c->rx_buffer, RX_BUF_SIZE, &c->rx_head, &c->rx_tail, buf, cap);
}

/**
 * @brief True if @p slot holds a live connection that can accept a send or close.
 *
 * The single predicate every layer uses to ask "is this slot sendable": it folds the
 * CONN_ACTIVE state check and the non-null pcb check the send / flush / close paths
 * require. Callers outside transport/ + tls/ must NOT test conn_pool[slot].state or
 * .pcb themselves - .pcb is a raw stack pointer, so poking it couples a higher layer to
 * the transport's internals. Guard a send with `if (!protocore_conn_active(slot)) return;`.
 */
static inline proto_bool protocore_conn_active(uint8_t slot)
{
    const TcpConn *c = &conn_pool[slot];
    return PROTO_ATOMIC_LOAD(&c->state) == CONN_ACTIVE && c->pcb != NULL;
}

/** @brief The network interface (STA / AP / ANY) @p slot's connection arrived on. */
static inline protocore_if_kind protocore_conn_iface(uint8_t slot)
{
    return conn_pool[slot].iface;
}

/** @brief The id of the listener @p slot's connection was accepted on. */
static inline uint8_t protocore_conn_listener_id(uint8_t slot)
{
    return conn_pool[slot].listener_id;
}

/**
 * @brief A stable per-peer 32-bit identity key for @p slot (the v4 address, or an FNV-1a hash of a
 *        v6 address). For rate-limit / auth-lockout buckets, where a v6 peer must not silently
 *        share the all-zero v4 bucket. Returns 0 if the slot has no active connection.
 */

// ---------------------------------------------------------------------------
// Observability (PROTOCORE_ENABLE_OBSERVABILITY) - connection event hook + counters
// ---------------------------------------------------------------------------
#if PROTOCORE_ENABLE_OBSERVABILITY

/** @brief Why a connection event fired (the reason for a transition or notice). */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_CONN_R_ACCEPT,       ///< New connection accepted (CONN_FREE -> CONN_ACTIVE).
    PROTOCORE_CONN_R_CLOSE_REMOTE, ///< Peer closed gracefully (FIN received).
    PROTOCORE_CONN_R_CLOSE_LOCAL,  ///< Application initiated the close.
    PROTOCORE_CONN_R_ERROR,        ///< The stack reported a fatal error on the connection.
    PROTOCORE_CONN_R_TIMEOUT,      ///< Idle-timeout sweep reaped the slot.
    PROTOCORE_CONN_R_ABORT,        ///< Forced abort (server stop / pool reset).
    PROTOCORE_CONN_R_DRAINED,      ///< CONN_CLOSING slot finished draining -> closed.
    PROTOCORE_CONN_R_BACKPRESSURE, ///< RX segment refused (ring full); no state change.
    PROTOCORE_CONN_R_DEFER_DROP    ///< Event queue full; an event was dropped (no state change).
} protocore_conn_reason;
static_assert(sizeof(protocore_conn_reason) == 1, "protocore_conn_reason must stay one byte (PROTO_ENUM_PACKED)");

/** @brief Snapshot of the transport's lifetime counters (plus a live gauge). */
typedef struct protocore_conn_counters
{
    uint32_t accepts;        ///< Connections accepted.
    uint32_t closes_remote;  ///< Closed by peer FIN.
    uint32_t closes_local;   ///< Closed by the application.
    uint32_t closes_error;   ///< Closed by a stack error.
    uint32_t closes_timeout; ///< Reaped by the idle-timeout sweep.
    uint32_t closes_abort;   ///< Force-aborted (stop / reset).
    uint32_t backpressure;   ///< RX segments refused for lack of ring space.
    uint32_t defer_drops;    ///< Deferred events dropped because the queue was full.
    uint32_t closing_gauge;  ///< Slots currently in CONN_CLOSING (live, not cumulative).
} protocore_conn_counters;

/**
 * @brief Callback fired on every connection state transition.
 *
 * Runs in whichever task drove the transition (the stack's callback context for
 * accept / recv / error, a worker for close / timeout), so keep it short and non-blocking and do
 * not call back into the server from it. @p old_state == @p new_state for the
 * non-transition notices (backpressure, defer-drop).
 */
typedef void (*protocore_conn_event_cb)(uint8_t slot, ConnState old_state, ConnState new_state,
                                        protocore_conn_reason reason);

// Internal notify points (tcp.c), reached via the macros below so both
// tcp.c and listener.c (accept) record through one path.
void protocore_obs_transition(uint8_t slot, ConnState olds, ConnState news, protocore_conn_reason reason);
void protocore_obs_notice(uint8_t slot, ConnState st, protocore_conn_reason reason);
#define PROTOCORE_OBS_TRANSITION(slot, olds, news, reason) protocore_obs_transition((slot), (olds), (news), (reason))
#define PROTOCORE_OBS_NOTICE(slot, st, reason) protocore_obs_notice((slot), (st), (reason))

#else // !PROTOCORE_ENABLE_OBSERVABILITY

// Compile to nothing; the arguments (incl. protocore_conn_reason names, only declared
// when the feature is on) are dropped unparsed by the preprocessor.
#define PROTOCORE_OBS_TRANSITION(slot, olds, news, reason) ((void)0)
#define PROTOCORE_OBS_NOTICE(slot, st, reason) ((void)0)

#endif // PROTOCORE_ENABLE_OBSERVABILITY

// ---------------------------------------------------------------------------
// Per-connection stack callbacks (defined in tcp.c, used in listener.c)
// ---------------------------------------------------------------------------

/**
 * @brief Receive callback - wired to each new connection by listener_accept_cb.
 * @see tcp.c
 */
protocore_net_err lowlevel_recv_cb(void *arg, protocore_pcb *tpcb, protocore_pbuf *p, protocore_net_err err);

/**
 * @brief Sent callback - refreshes the idle-timeout timestamp.
 * @see tcp.c
 */
protocore_net_err lowlevel_sent_cb(void *arg, protocore_pcb *tpcb, proto_u16 len);

/**
 * @brief Error callback - fires when the stack detects a fatal error.
 * @see tcp.c
 */
void lowlevel_err_cb(void *arg, protocore_net_err err);

/**
 * @brief The connection pool: one accepted TCP connection per slot.
 *
 * Named for the pool rather than the slot, because ::TcpConn is the slot's own type.
 *
 * The per-slot ring accessors above stay inline and are not members here. A member is an indirect
 * call through rodata; those accessors are a load and a compare on the request path.
 *
 * @var ConnPoolNs::alloc_free   claim the lowest free slot, or -1 when every slot is taken
 * @var ConnPoolNs::sndbuf       room the stack will accept for this slot right now
 * @var ConnPoolNs::init           size and clear the pool from the server config
 * @var ConnPoolNs::stop           tear every slot down
 * @var ConnPoolNs::check_timeouts sweep the slots one worker owns
 * @var ConnPoolNs::timeout_ms     the idle bound a slot is swept against
 * @var ConnPoolNs::set_state      the one slot-state write path, keeping the free mask in step
 * @var ConnPoolNs::send           queue bytes on a slot
 * @var ConnPoolNs::send_flush     queue bytes and push them in one call
 * @var ConnPoolNs::flush          push what is queued
 * @var ConnPoolNs::touch_active   restart a slot's idle timer
 * @var ConnPoolNs::ack_consumed   tell the stack how much the reader took
 * @var ConnPoolNs::active_count   slots currently carrying a connection
 * @var ConnPoolNs::raw_send       write to a control block that has no slot
 * @var ConnPoolNs::close          close a slot
 * @var ConnPoolNs::begin_close    start a close the drain finishes
 * @var ConnPoolNs::detach         drop a control block without touching its slot
 * @var ConnPoolNs::abort          reset a control block
 * @var ConnPoolNs::abort_slot     reset the connection a slot holds
 * @var ConnPoolNs::set_dscp   tag one live connection, overriding the server-wide default
 * @var ConnPoolNs::remote_ip      the peer's IPv4 address as a word
 * @var ConnPoolNs::remote_addr    the peer's address, either family
 * @var ConnPoolNs::on_event       install the slot-transition observer
 * @var ConnPoolNs::counters_get   read the pool's counters
 * @var ConnPoolNs::counters_reset zero the pool's counters
 */
typedef struct
{
    int32_t (*alloc_free)(void);
    proto_u16 (*sndbuf)(uint8_t slot);
    void (*init)(const WebServerConfig *cfg);
    void (*stop)(void);
    void (*check_timeouts)(int worker_id);
    uint32_t (*timeout_ms)(void);
    void (*set_state)(uint8_t slot, ConnState st);
    proto_bool (*send)(uint8_t slot, const void *data, proto_u16 len);
    proto_bool (*send_flush)(uint8_t slot, const void *data, proto_u16 len);
    void (*flush)(uint8_t slot);
    void (*touch_active)(uint8_t slot);
    void (*ack_consumed)(uint8_t slot);
    uint8_t (*active_count)(void);
    proto_bool (*raw_send)(protocore_pcb *pcb, const void *data, proto_u16 len);
    void (*close)(uint8_t slot);
    void (*begin_close)(uint8_t slot_id);
    void (*detach)(protocore_pcb *pcb);
    void (*abort)(protocore_pcb *pcb);
    void (*abort_slot)(uint8_t slot);
#if PROTOCORE_ENABLE_DIFFSERV
    proto_bool (*set_dscp)(uint8_t slot, uint8_t dscp);
#endif
    uint32_t (*remote_ip)(uint8_t slot);
    proto_bool (*remote_addr)(uint8_t slot, protocore_ip *out);
#if PROTOCORE_ENABLE_OBSERVABILITY
    // The callback and counter types exist only with the feature, so the members do too. A caller
    // tests the pointer rather than repeating the flag.
    void (*on_event)(protocore_conn_event_cb cb);
    protocore_conn_counters (*counters_get)(void);
    void (*counters_reset)(void);
#endif
} ConnPoolNs;

/** @brief The one symbol this module exports. */
extern const ConnPoolNs ConnPool;

PROTOCORE_END_DECLS

#endif
