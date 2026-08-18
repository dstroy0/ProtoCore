// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocol.c
 * @brief Layer 4 (Transport) - the user/TCP interface and its event processing. See protocol.h.
 *
 * Every raw stack callback here runs in the stack's own task context. They are NOT hardware ISRs,
 * which is why the queue send is the ordinary variant with timeout=0 rather than the from-ISR form.
 *
 * The producer (recv callback) writes payload bytes into the slot's ring and then advances the
 * head. The consumer (worker) reads at the tail and advances the tail. Both indices are `_Atomic`,
 * reached acquire/release: the producer's buffer writes are published by the release store of the
 * head and observed by the consumer's acquire load. The ring math is the shared ring.h primitive.
 */

#include "protocol.h"
#include "../../net_addr/net_addr.h" // protocore_net_addr_to_ip(): the stack's address as a protocore_ip
#include "../common.h"               // TcpConn, conn_pool: the slots this engine drives
#include "../lower/lower.h"          // every call into the stack below goes through the seam
#include "../server/server.h"        // TcpListener.enqueue: the owning listener posts the event
#include "config/platform/platform.h"
#include "mmgr/plaintext.h"     // the persistent end this module's state is taken from
#include "mmgr/rawmemcpy.h"     // raw.read: the unaligned v6 address load
#include "server/clock/clock.h" // protocore_millis() pluggable monotonic clock

#include "server/core/worker.h" // Workers.wake() - resume a paced send when the window drains

#if PROTOCORE_ENABLE_TLS
#include "network_drivers/tls/tls.h"
#endif

static_assert(PROTOCORE_RING_POW2(RX_BUF_SIZE), "RX_BUF_SIZE must be a power of two: a ring index wraps with a mask");

TcpConn conn_pool[CONN_POOL_SLOTS];
ConnSlotBits protocore_conn_bits;
uint32_t protocore_ap_ip = 0;

/**
 * @brief The pool's compile-time storage.
 *
 * The zeroed template init resets a slot from. It lives here rather than as a compound literal
 * because the latter materializes a whole slot - the ring included - on the caller's stack, which
 * overflows the loop task's stack once RX_BUF_SIZE is set large.
 */
struct ConnPoolStorage
{
    TcpConn blank;
#if PROTOCORE_ENABLE_OBSERVABILITY
    _Atomic uint32_t ctr[9]; ///< one per protocore_conn_reason, indexed by the reason itself
#endif
    uint32_t conn_timeout_ms; ///< the idle deadline the sweep measures against; init loads it
#if PROTOCORE_ENABLE_OBSERVABILITY
    protocore_conn_event_cb event_cb; ///< the installed observer
#endif
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define CONN_POOL_OFF_CTX 0u
static_assert(CONN_POOL_OFF_CTX + sizeof(struct ConnPoolStorage) <= PROTOCORE_CONN_POOL_BORROW,
              "PROTOCORE_CONN_POOL_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define CONN_POOL_CTX(w) ((struct ConnPoolStorage *)(void *)((w) + CONN_POOL_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_CONN_POOL_BORROW persistent bytes, or null while the pool was short
} ConnPoolOwnCtx;
static ConnPoolOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_conn_pool_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_plaintext_persist_span(PROTOCORE_CONN_POOL_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
            // A borrow arrives zeroed, and these do not start at zero.
            CONN_POOL_CTX(s_own.span)->conn_timeout_ms = CONN_TIMEOUT_MS;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

// ---------------------------------------------------------------------------
// Observability - event hook and lock-free counters
// ---------------------------------------------------------------------------
// Zero cost when off: the macros expand to nothing and their arguments, including the reason names
// that only exist with the feature, are dropped unparsed by the preprocessor.
#if PROTOCORE_ENABLE_OBSERVABILITY

// The counters are relaxed atomics: bumped from the stack's callback context and from the workers,
// so the increments must not tear, but nothing orders anything against them.
static void obs_bump(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    // DRAINED is gauge-only: the close reason that entered the dwell was already counted.
    if (ConnPool.obs.reason != PROTOCORE_CONN_R_DRAINED)
    {
        atomic_fetch_add_explicit(&CONN_POOL_CTX(work)->ctr[ConnPool.obs.reason], 1, memory_order_relaxed);
    }
}

// Install the observer a call is carrying. Null unregisters.
static void on_event(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    CONN_POOL_CTX(work)->event_cb = ConnPool.obs.event_cb_in;
}

static void counters_get(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    protocore_conn_counters c;
    c.accepts = atomic_load_explicit(&CONN_POOL_CTX(work)->ctr[PROTOCORE_CONN_R_ACCEPT], memory_order_relaxed);
    c.closes_remote =
        atomic_load_explicit(&CONN_POOL_CTX(work)->ctr[PROTOCORE_CONN_R_CLOSE_REMOTE], memory_order_relaxed);
    c.closes_local =
        atomic_load_explicit(&CONN_POOL_CTX(work)->ctr[PROTOCORE_CONN_R_CLOSE_LOCAL], memory_order_relaxed);
    c.closes_error = atomic_load_explicit(&CONN_POOL_CTX(work)->ctr[PROTOCORE_CONN_R_ERROR], memory_order_relaxed);
    c.closes_timeout = atomic_load_explicit(&CONN_POOL_CTX(work)->ctr[PROTOCORE_CONN_R_TIMEOUT], memory_order_relaxed);
    c.closes_abort = atomic_load_explicit(&CONN_POOL_CTX(work)->ctr[PROTOCORE_CONN_R_ABORT], memory_order_relaxed);
    c.backpressure =
        atomic_load_explicit(&CONN_POOL_CTX(work)->ctr[PROTOCORE_CONN_R_BACKPRESSURE], memory_order_relaxed);
    c.defer_drops = atomic_load_explicit(&CONN_POOL_CTX(work)->ctr[PROTOCORE_CONN_R_DEFER_DROP], memory_order_relaxed);
    // Derived from the pool on read, so it cannot drift out of step with the actual slot states.
    c.closing_gauge = 0;
    for (int i = 0; i < MAX_CONNS; i++)
    {
        if (PROTO_ATOMIC_LOAD(&conn_pool[i].state) == CONN_CLOSING)
        {
            c.closing_gauge++;
        }
    }
    ConnPool.obs.counters = c;
}

static void counters_reset(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    for (int i = 0; i < 9; i++)
    {
        atomic_store_explicit(&CONN_POOL_CTX(work)->ctr[i], 0, memory_order_relaxed);
    }
}

// A real state transition: bump the reason counter and fire the callback.
static void obs_transition(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    obs_bump(work);
    if (CONN_POOL_CTX(work)->event_cb != NULL)
    {
        CONN_POOL_CTX(work)->event_cb(ConnPool.slot, ConnPool.obs.olds, ConnPool.obs.news, ConnPool.obs.reason);
    }
}

// A non-transition notice (backpressure / defer-drop): bump and fire with old == new.
static void obs_notice(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    obs_bump(work);
    if (CONN_POOL_CTX(work)->event_cb != NULL)
    {
        CONN_POOL_CTX(work)->event_cb(ConnPool.slot, ConnPool.st, ConnPool.st, ConnPool.obs.reason);
    }
}

void protocore_obs_transition(uint8_t slot, ConnState olds, ConnState news, protocore_conn_reason reason)
{
    ConnPool.slot = slot;
    ConnPool.obs.olds = olds;
    ConnPool.obs.news = news;
    ConnPool.obs.reason = reason;
    obs_transition(protocore_conn_pool_span());
}

void protocore_obs_notice(uint8_t slot, ConnState st, protocore_conn_reason reason)
{
    ConnPool.slot = slot;
    ConnPool.st = st;
    ConnPool.obs.reason = reason;
    obs_notice(protocore_conn_pool_span());
}
#endif // PROTOCORE_ENABLE_OBSERVABILITY

// ---------------------------------------------------------------------------
// Slot state
// ---------------------------------------------------------------------------

// The one place a slot's lifecycle state is written. Keeps the free bitmask in lock-step with the
// atomic state: publish availability only AFTER the release store to CONN_FREE - the caller has
// already cleaned the slot - and reserve BEFORE the store to any non-free state, so a concurrent
// allocator never picks a slot that is mid-claim. The bit ops are atomic because CONN_FREE is
// written from the stack callbacks and from the worker.
static void set_state(uint8_t *restrict work)
{
    (void)work;
    // Bound every write to the real array size, so the setter is memory-safe on its own rather than
    // relying on the caller never over-indexing.
    if (ConnPool.slot >= CONN_POOL_SLOTS)
    {
        return;
    }
#if PROTOCORE_INTERNAL_SLOTS > 0
    // Reserved internal slots are not part of the accept pool and are never handed out by the
    // allocator, so they carry state but no bitmask bit.
    if (ConnPool.slot >= MAX_CONNS)
    {
        PROTO_ATOMIC_STORE(&conn_pool[ConnPool.slot].state, ConnPool.st);
        return;
    }
#endif
    if (ConnPool.st == CONN_FREE)
    {
        PROTO_ATOMIC_STORE(&conn_pool[ConnPool.slot].state, ConnPool.st);
        protocore_slot_mark(&protocore_conn_bits.free, ConnPool.slot);
    }
    else
    {
        protocore_slot_clear(&protocore_conn_bits.free, ConnPool.slot);
        PROTO_ATOMIC_STORE(&conn_pool[ConnPool.slot].state, ConnPool.st);
    }
}

static void protocore_conn_set_state(uint8_t slot, ConnState st)
{
    ConnPool.slot = slot;
    ConnPool.st = st;
    set_state(protocore_conn_pool_span());
}

// First free slot as one ctz on the bitmask. Reports -1 when the pool is full. Free AND not held: a
// slot whose bytes the wire has not finished with is not available, however free its state says.
static void alloc_free(uint8_t *restrict work)
{
    (void)work;
    ConnPool.i32 =
        protocore_slot_next(protocore_slot_ready(&protocore_conn_bits.free, &protocore_conn_bits.held, MAX_CONNS));
}

static int32_t protocore_conn_alloc_free(void)
{
    alloc_free(protocore_conn_pool_span());
    return ConnPool.i32;
}

static void timeout_ms(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    ConnPool.u32 = CONN_POOL_CTX(work)->conn_timeout_ms;
}

// ---------------------------------------------------------------------------
// Connection output - the single send/flush/close path for every higher layer
// ---------------------------------------------------------------------------
// Presentation and application code never call the stack directly: they hand bytes to this layer,
// which decides whether they go out as plaintext or through the TLS record layer.

// The write target is always the slot's own control block, so it cannot disagree with the ingress
// reads that resolve it the same way.
static void send(uint8_t *restrict work)
{
    (void)work;
    TcpLower.op = PROTOCORE_OP_SEND;
    TcpLower.slot = ConnPool.slot;
    TcpLower.pcb = conn_pool[ConnPool.slot].pcb;
    TcpLower.data = ConnPool.io.data;
    TcpLower.len = ConnPool.io.len;
    TcpLower.flush = PROTO_FALSE;
    TcpLower.marshal(protocore_tcp_lower_span());
    ConnPool.ok = TcpLower.result == PROTOCORE_NET_OK;
}

// Terminal single-shot write: the bytes and their push happen in one round trip into the stack's
// context, so a small response costs one marshal instead of a send and a flush. For a TLS slot this
// is identical to send: the record BIO already pushes ciphertext per record.
static void send_flush(uint8_t *restrict work)
{
    (void)work;
    TcpLower.op = PROTOCORE_OP_SEND;
    TcpLower.slot = ConnPool.slot;
    TcpLower.pcb = conn_pool[ConnPool.slot].pcb;
    TcpLower.data = ConnPool.io.data;
    TcpLower.len = ConnPool.io.len;
    TcpLower.flush = PROTO_TRUE;
    TcpLower.marshal(protocore_tcp_lower_span());
    ConnPool.ok = TcpLower.result == PROTOCORE_NET_OK;
}

static void sndbuf(uint8_t *restrict work)
{
    (void)work;
    protocore_pcb *pcb = conn_pool[ConnPool.slot].pcb;
    if (pcb == NULL)
    {
        ConnPool.u16 = 0;
        return;
    }
    proto_u16 avail = protocore_net_sndbuf(pcb);
#if PROTOCORE_ENABLE_TLS
    // A TLS record adds header and tag overhead; report a conservative plaintext budget so a caller
    // that fills it does not overrun the cipher's framing.
    if (conn_pool[ConnPool.slot].tls)
    {
        avail = (avail > 64) ? (proto_u16)(avail - 64) : 0;
    }
#endif
    ConnPool.u16 = avail;
}

static void flush(uint8_t *restrict work)
{
    (void)work;
#if PROTOCORE_ENABLE_TLS
    if (conn_pool[ConnPool.slot].tls)
    {
        return; // the BIO already pushed ciphertext per record; a flush must not end the session,
                // since persistent TLS reuses it
    }
#endif
    TcpLower.op = PROTOCORE_OP_OUTPUT;
    TcpLower.slot = ConnPool.slot;
    TcpLower.pcb = conn_pool[ConnPool.slot].pcb;
    TcpLower.data = NULL;
    TcpLower.len = 0;
    TcpLower.flush = PROTO_FALSE;
    TcpLower.marshal(protocore_tcp_lower_span());
}

// The DS field is stamped where the control block is created - the listener's code point at accept,
// the server-wide default at connect - and not afterwards. RFC 9293 sec 3.9.2 SHLD-23: an
// application should not change the Diffserv field during a connection, because it has no knowledge
// of individual segments, so a mid-connection change lands on an arbitrary boundary.

// Reopen the receive window by exactly what the reader took (ack-on-consume), so the advertised
// window tracks ring occupancy and a slow consumer cannot overflow it.
static void ack_consumed(uint8_t *restrict work)
{
    (void)work;
    if (ConnPool.slot >= MAX_CONNS)
    {
        return;
    }
    TcpConn *c = &conn_pool[ConnPool.slot];
    // Only the owning worker calls this, so the tail and the ack cursor are read race-free; the
    // head is not touched. Ack nothing for a slot that is not actively receiving.
    if (PROTO_ATOMIC_LOAD(&c->state) != CONN_ACTIVE || c->pcb == NULL)
    {
        return;
    }
    size_t tail = PROTO_ATOMIC_LOAD(&c->rx_tail);
    size_t consumed = PROTOCORE_RING_WRAP(tail + RX_BUF_SIZE - c->rx_acked, RX_BUF_SIZE);
    if (consumed == 0)
    {
        return;
    }
    c->rx_acked = tail; // advance first: the marshaled window update is the slow part
    TcpLower.op = PROTOCORE_OP_RECVED;
    TcpLower.slot = ConnPool.slot;
    TcpLower.pcb = c->pcb;
    TcpLower.data = NULL;
    TcpLower.len = (proto_u16)consumed;
    TcpLower.flush = PROTO_FALSE;
    TcpLower.marshal(protocore_tcp_lower_span());
}

// A raw write of already-encrypted bytes, for a control block reached without its slot. The seam
// owns the context choice and re-checks the block is still bound.
static void raw_send(uint8_t *restrict work)
{
    (void)work;
    if (ConnPool.pcb == NULL)
    {
        ConnPool.ok = PROTO_FALSE;
        return;
    }
    TcpLower.op = PROTOCORE_OP_RAWSEND;
    TcpLower.slot = 0;
    TcpLower.pcb = ConnPool.pcb;
    TcpLower.data = ConnPool.io.data;
    TcpLower.len = ConnPool.io.len;
    TcpLower.flush = PROTO_FALSE;
    TcpLower.marshal(protocore_tcp_lower_span());
    ConnPool.ok = TcpLower.result == PROTOCORE_NET_OK;
}

// The application-initiated close. Remote FIN, error and timeout closes are observed at their own
// sites, so this one is uniquely local.
static void close_slot(uint8_t *restrict work)
{
    (void)work;
    if (ConnPool.slot >= MAX_CONNS)
    {
        return;
    }
    TcpConn *c = &conn_pool[ConnPool.slot];
    protocore_pcb *pcb = c->pcb;
    if (pcb == NULL)
    {
        return;
    }
    // The state is read rather than assumed: a worker, a stack callback and the sweep can all reach
    // the same slot, so it may already be in the drain dwell.
    const ConnState was = PROTO_ATOMIC_LOAD(&c->state);
    PROTOCORE_OBS_TRANSITION(ConnPool.slot, was, CONN_FREE, PROTOCORE_CONN_R_CLOSE_LOCAL);
    // Detach and free the slot before the close, so a late callback for this block finds a null arg.
    // The close targets the captured block, so nulling the slot first is safe.
    TcpLower.pcb = pcb;
    TcpLower.slot = ConnPool.slot;
    TcpLower.detach(protocore_tcp_lower_span());
    protocore_conn_set_state(c->id, CONN_FREE);
    c->pcb = NULL;
    TcpLower.op = PROTOCORE_OP_CLOSE;
    TcpLower.pcb = pcb;
    TcpLower.slot = ConnPool.slot;
    TcpLower.data = NULL;
    TcpLower.len = 0;
    TcpLower.flush = PROTO_FALSE;
    TcpLower.marshal(protocore_tcp_lower_span()); // TLS teardown and FIN, in the stack's context
}

static void abort_slot(uint8_t *restrict work)
{
    (void)work;
    if (ConnPool.slot >= MAX_CONNS)
    {
        return;
    }
    TcpConn *c = &conn_pool[ConnPool.slot];
    protocore_pcb *pcb = c->pcb;
    if (pcb == NULL)
    {
        return;
    }
    const ConnState was = PROTO_ATOMIC_LOAD(&c->state);
    PROTOCORE_OBS_TRANSITION(ConnPool.slot, was, CONN_FREE, PROTOCORE_CONN_R_ABORT);
#if PROTOCORE_ENABLE_TLS
    if (c->tls)
    {
        protocore_tls_conn_free(ConnPool.slot); // abrupt: free the per-conn context, no close_notify
    }
#endif
    TcpLower.pcb = pcb;
    TcpLower.slot = ConnPool.slot;
    TcpLower.detach(protocore_tcp_lower_span());
    protocore_conn_set_state(c->id, CONN_FREE);
    c->pcb = NULL;
    TcpLower.pcb = pcb;
    TcpLower.slot = ConnPool.slot;
    TcpLower.abort(protocore_tcp_lower_span());
}

// ---------------------------------------------------------------------------
// The CONN_CLOSING dwell: a graceful close that holds the slot until the peer ACKs
// ---------------------------------------------------------------------------
// These run in the stack's context, so they touch the control block directly.

// Tear the connection down and free the slot.
static void closing_finalize(uint8_t *restrict work)
{
    (void)work;
    TcpConn *c = &conn_pool[ConnPool.slot];
#if PROTOCORE_ENABLE_TLS
    if (c->tls)
    {
        protocore_tls_conn_end(ConnPool.slot); // close_notify and free the context, in-thread
    }
#endif
    protocore_conn_set_state(c->id, CONN_FREE);
    c->pcb = NULL;
    if (ConnPool.pcb != NULL)
    {
        protocore_net_arg(ConnPool.pcb, NULL);
        if (protocore_net_close(ConnPool.pcb) != PROTOCORE_NET_OK)
        {
            protocore_net_abort(ConnPool.pcb);
        }
    }
    PROTOCORE_OBS_TRANSITION(ConnPool.slot, CONN_CLOSING, CONN_FREE, PROTOCORE_CONN_R_DRAINED);
}

// Finalize now if the slot is dwelling and its transmit queue has drained.
static void closing_check(uint8_t *restrict work)
{
    if (ConnPool.slot >= MAX_CONNS || PROTO_ATOMIC_LOAD(&conn_pool[ConnPool.slot].state) != CONN_CLOSING)
    {
        return;
    }
    if (ConnPool.pcb == NULL || ConnPool.pcb->snd_queuelen == 0)
    {
        closing_finalize(work);
    }
}

static void protocore_conn_closing_check(uint8_t slot, protocore_pcb *pcb)
{
    ConnPool.slot = slot;
    ConnPool.pcb = pcb;
    closing_check(protocore_conn_pool_span());
}

static void begin_close(uint8_t *restrict work)
{
    (void)work;
    if (ConnPool.slot >= MAX_CONNS)
    {
        return;
    }
    TcpConn *c = &conn_pool[ConnPool.slot];
    if (PROTO_ATOMIC_LOAD(&c->state) != CONN_ACTIVE) // an error during the write may have freed it
    {
        return;
    }
    protocore_pcb *pcb = c->pcb;
    c->last_activity_ms = Clock.ms;                // start the dwell clock
    protocore_conn_set_state(c->id, CONN_CLOSING); // release store: the callbacks now see CLOSING
    PROTOCORE_OBS_TRANSITION(ConnPool.slot, CONN_ACTIVE, CONN_CLOSING, PROTOCORE_CONN_R_CLOSE_LOCAL);
    // Finalize immediately if the response already drained, else dwell until the sent callback or
    // the sweep reclaims it. The control-block read happens in the stack's context, so it marshals.
    TcpLower.op = PROTOCORE_OP_CLOSE_CHECK;
    TcpLower.slot = ConnPool.slot;
    TcpLower.pcb = pcb;
    TcpLower.data = NULL;
    TcpLower.len = 0;
    TcpLower.flush = PROTO_FALSE;
    TcpLower.marshal(protocore_tcp_lower_span());
}

// Forward the event to the queue owned by the connection's listener. The enqueue does not block: it
// returns immediately if the queue is full. A full queue means the application is not draining fast
// enough; dropped events are recoverable via the idle sweep.
static void enqueue(uint8_t *restrict work)
{
    (void)work;
    TcpListener.idx = conn_pool[ConnPool.slot].listener_id;
    TcpListener.q.evt = ConnPool.evt;
    TcpListener.enqueue(protocore_tcp_listener_span());
    if (!TcpListener.ok)
    {
        PROTOCORE_OBS_NOTICE(ConnPool.slot, PROTO_ATOMIC_LOAD(&conn_pool[ConnPool.slot].state),
                             PROTOCORE_CONN_R_DEFER_DROP);
    }
}

static void protocore_conn_enqueue(TcpConn *slot, const TcpEvt *evt)
{
    ConnPool.slot = slot->id;
    ConnPool.evt = evt;
    enqueue(protocore_conn_pool_span());
}

// ---------------------------------------------------------------------------
// Pool lifecycle
// ---------------------------------------------------------------------------

static void init(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    CONN_POOL_CTX(work)->conn_timeout_ms = ConnPool.life.conn_timeout_ms;
    // The template lives in storage and the copy runs before any listener is accepting, so the
    // non-atomic struct assignment over the atomic members races nothing.
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = CONN_POOL_CTX(work)->blank;
        conn_pool[i].id = (uint8_t)i;
        protocore_conn_set_state((uint8_t)i, CONN_FREE);
    }
}

// Abort every live connection. Listening control blocks and queues belong to the server half and
// must be torn down through it first.
static void stop(uint8_t *restrict work)
{
    (void)work;
    for (int i = 0; i < MAX_CONNS; i++)
    {
        ConnState st = PROTO_ATOMIC_LOAD(&conn_pool[i].state);
        if ((st == CONN_ACTIVE || st == CONN_CLOSING) && conn_pool[i].pcb != NULL)
        {
            protocore_pcb *pcb = conn_pool[i].pcb;
            protocore_conn_set_state((uint8_t)i, CONN_FREE);
            conn_pool[i].pcb = NULL;
            TcpLower.pcb = pcb;
            TcpLower.slot = (uint8_t)i;
            TcpLower.detach(protocore_tcp_lower_span());
            TcpLower.pcb = pcb;
            TcpLower.slot = (uint8_t)i;
            TcpLower.abort(protocore_tcp_lower_span());
            PROTOCORE_OBS_TRANSITION((uint8_t)i, st, CONN_FREE, PROTOCORE_CONN_R_ABORT);
        }
        protocore_conn_set_state((uint8_t)i, CONN_FREE);
        conn_pool[i].pcb = NULL;
    }
}

// ---------------------------------------------------------------------------
// The receive ring, and what a slot is
// ---------------------------------------------------------------------------
// Transport owns the ring. A layer above drains it only through these, and never indexes the
// buffer or advances the tail itself. The consuming calls advance the tail only; the window is
// reopened by ack_consumed once per worker loop, so there is no per-byte ACK. Single-consumer per
// slot (the owning worker), so nothing locks here. All five delegate to the shared SPSC ring
// primitive over the slot's rx_buffer - this layer never reimplements the ring math.

static void available(uint8_t *restrict work)
{
    (void)work;
    const TcpConn *c = &conn_pool[ConnPool.slot];
    ConnPool.n = protocore_ring_available(&c->rx_head, &c->rx_tail, RX_BUF_SIZE);
}

static void read_byte(uint8_t *restrict work)
{
    (void)work;
    TcpConn *c = &conn_pool[ConnPool.slot];
    ConnPool.ok = protocore_ring_read_byte(c->rx_buffer, RX_BUF_SIZE, &c->rx_head, &c->rx_tail, &ConnPool.u8);
}

static void peek(uint8_t *restrict work)
{
    (void)work;
    const TcpConn *c = &conn_pool[ConnPool.slot];
    protocore_ring_peek(c->rx_buffer, RX_BUF_SIZE, &c->rx_tail, ConnPool.io.off, ConnPool.io.buf, ConnPool.io.count);
}

static void consume(uint8_t *restrict work)
{
    (void)work;
    protocore_ring_consume(&conn_pool[ConnPool.slot].rx_tail, RX_BUF_SIZE, ConnPool.io.count);
}

static void read(uint8_t *restrict work)
{
    (void)work;
    TcpConn *c = &conn_pool[ConnPool.slot];
    ConnPool.n =
        protocore_ring_read(c->rx_buffer, RX_BUF_SIZE, &c->rx_head, &c->rx_tail, ConnPool.io.buf, ConnPool.io.cap);
}

static void active(uint8_t *restrict work)
{
    (void)work;
    const TcpConn *c = &conn_pool[ConnPool.slot];
    ConnPool.ok = PROTO_ATOMIC_LOAD(&c->state) == CONN_ACTIVE && c->pcb != NULL;
}

static void iface(uint8_t *restrict work)
{
    (void)work;
    ConnPool.if_kind = conn_pool[ConnPool.slot].iface;
}

static void listener_id(uint8_t *restrict work)
{
    (void)work;
    ConnPool.u8 = conn_pool[ConnPool.slot].listener_id;
}

static void tls(uint8_t *restrict work)
{
    (void)work;
    ConnPool.ok = conn_pool[ConnPool.slot].tls != 0;
}

static void owner(uint8_t *restrict work)
{
    (void)work;
    ConnPool.u8 = conn_pool[ConnPool.slot].owner;
}

static void proto_of(uint8_t *restrict work)
{
    (void)work;
    ConnPool.proto = conn_pool[ConnPool.slot].proto;
}

static void pcb_of(uint8_t *restrict work)
{
    (void)work;
    ConnPool.pcb = conn_pool[ConnPool.slot].pcb;
}

static void active_count(uint8_t *restrict work)
{
    (void)work;
    uint8_t n = 0;
    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        if (PROTO_ATOMIC_LOAD(&conn_pool[i].state) == CONN_ACTIVE)
        {
            n++;
        }
    }
    ConnPool.u8 = n;
}

static void remote_ip(uint8_t *restrict work)
{
    (void)work;
    ConnPool.u32 = 0;
    if (ConnPool.slot >= MAX_CONNS)
    {
        return;
    }
    TcpConn *conn = &conn_pool[ConnPool.slot];
    if (PROTO_ATOMIC_LOAD(&conn->state) == CONN_ACTIVE && conn->pcb != NULL)
    {
        ConnPool.u32 = protocore_net_ip4_u32(protocore_net_ip_as_v4(&conn->pcb->remote_ip));
    }
}

static void remote_addr(uint8_t *restrict work)
{
    (void)work;
    if (ConnPool.out != NULL)
    {
        ConnPool.out->family = PROTOCORE_IP_NONE;
    }
    if (ConnPool.out == NULL || ConnPool.slot >= MAX_CONNS)
    {
        ConnPool.ok = PROTO_FALSE;
        return;
    }
    TcpConn *conn = &conn_pool[ConnPool.slot];
    if (PROTO_ATOMIC_LOAD(&conn->state) != CONN_ACTIVE || conn->pcb == NULL)
    {
        ConnPool.ok = PROTO_FALSE;
        return;
    }
    protocore_net_addr_to_ip(&conn->pcb->remote_ip, ConnPool.out);
    ConnPool.ok = PROTO_TRUE;
}

// Restart a slot's idle timer while a response body is still being paged out. Such a slot is
// streaming, or briefly blocked on a full send window, NOT idle - so the sweep must not reap it
// mid-transfer and truncate a body larger than one window. Dead-peer teardown for an in-flight
// response stays with the stack's retransmission timers, which abort through the error callback.
static void touch_active(uint8_t *restrict work)
{
    (void)work;
    if (ConnPool.slot >= MAX_CONNS)
    {
        return;
    }
    TcpConn *c = &conn_pool[ConnPool.slot];
    if (PROTO_ATOMIC_LOAD(&c->state) == CONN_ACTIVE)
    {
        c->last_activity_ms = Clock.ms;
    }
}

static void check_timeouts(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    uint32_t now = Clock.ms;
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TcpConn *slot = &conn_pool[i];
        if (slot->owner != ConnPool.life.worker_id) // each worker reaps only its own slots
        {
            continue;
        }

        // A graceful close whose peer never ACKs would dwell forever, so the fixed pool cannot be
        // allowed to leak. The fast path is the sent callback finalizing on ACK; this only catches a
        // black-holed peer.
        if (PROTO_ATOMIC_LOAD(&slot->state) == CONN_CLOSING)
        {
            if ((now - slot->last_activity_ms) < PROTOCORE_CLOSING_TIMEOUT_MS)
            {
                continue;
            }
            protocore_pcb *cpcb = slot->pcb;
            if (cpcb != NULL)
            {
                // The dwell ran out with data still unacknowledged: RFC 9293 sec 3.9.1.4 turns that
                // close into an abort, so this resets rather than emitting a FIN. The TLS context is
                // freed while the slot is still owned, before a release could expose it to a
                // re-accept.
#if PROTOCORE_ENABLE_TLS
                if (slot->tls)
                {
                    protocore_tls_conn_free(slot->id);
                }
#endif
                TcpLower.pcb = cpcb;
                TcpLower.slot = slot->id;
                TcpLower.detach(protocore_tcp_lower_span());
                TcpLower.pcb = cpcb;
                TcpLower.slot = slot->id;
                TcpLower.abort(protocore_tcp_lower_span());
            }
            protocore_conn_set_state(slot->id, CONN_FREE);
            slot->pcb = NULL;
            // The dwell expired; this is not the clean drain DRAINED records, and the counters have
            // to tell them apart.
            PROTOCORE_OBS_TRANSITION((uint8_t)i, CONN_CLOSING, CONN_FREE, PROTOCORE_CONN_R_TIMEOUT);
            continue;
        }

        if (PROTO_ATOMIC_LOAD(&slot->state) != CONN_ACTIVE)
        {
            continue;
        }
        if ((now - slot->last_activity_ms) < CONN_POOL_CTX(work)->conn_timeout_ms)
        {
            continue;
        }

        protocore_pcb *pcb = slot->pcb;
        // Clear the state BEFORE aborting, so any stack callback firing on the same connection
        // during or after the abort sees CONN_FREE and exits without touching freed memory.
        protocore_conn_set_state(slot->id, CONN_FREE);
        slot->pcb = NULL;
        if (pcb != NULL)
        {
            TcpLower.pcb = pcb;
            TcpLower.slot = slot->id;
            TcpLower.detach(protocore_tcp_lower_span());
            TcpLower.pcb = pcb;
            TcpLower.slot = slot->id;
            TcpLower.abort(protocore_tcp_lower_span());
        }
        PROTOCORE_OBS_TRANSITION((uint8_t)i, CONN_ACTIVE, CONN_FREE, PROTOCORE_CONN_R_TIMEOUT);
        TcpEvt evt = {EVT_ERROR, (uint8_t)i, 0};
        protocore_conn_enqueue(slot, &evt);
    }
}

// ---------------------------------------------------------------------------
// Event processing - the stack's callbacks (RFC 9293 sec 3.10.7 SEGMENT ARRIVES)
// ---------------------------------------------------------------------------
// The stack calls these directly, so their shapes are its, not this module's.

protocore_net_err lowlevel_recv_cb(void *arg, protocore_pcb *tpcb, protocore_pbuf *p, protocore_net_err err)
{
    (void)err;
    TcpConn *slot = (TcpConn *)arg;
    if (slot == NULL)
    {
        return PROTOCORE_NET_ERR_VAL;
    }

    // While dwelling in CONN_CLOSING the application has closed and reads no more, so a segment
    // carrying data is lost. Reset to say so (RFC 9293 sec 3.6.1 SHLD-3) rather than acknowledge it,
    // since an ACK is the promise to deliver it (sec 3.10.7). A null segment is the peer's FIN and
    // an empty one carries nothing: neither is a loss, so both only free and leave the dwell to
    // finalize on the next sent or timeout.
    if (PROTO_ATOMIC_LOAD(&slot->state) == CONN_CLOSING)
    {
        if (p == NULL || p->tot_len == 0)
        {
            if (p != NULL)
            {
                protocore_net_pbuf_free(p);
            }
            return PROTOCORE_NET_OK;
        }
        protocore_net_pbuf_free(p);
#if PROTOCORE_ENABLE_TLS
        if (slot->tls)
        {
            protocore_tls_conn_free(slot->id); // abrupt: no close_notify precedes a reset
        }
#endif
        // Free the slot before the reset, so a late callback for this block finds a null arg.
        protocore_conn_set_state(slot->id, CONN_FREE);
        slot->pcb = NULL;
        protocore_net_arg(tpcb, NULL);
        protocore_net_abort(tpcb);
        PROTOCORE_OBS_TRANSITION(slot->id, CONN_CLOSING, CONN_FREE, PROTOCORE_CONN_R_ABORT);
        return PROTOCORE_NET_ERR_ABRT;
    }

    if (PROTO_ATOMIC_LOAD(&slot->state) != CONN_ACTIVE)
    {
        return PROTOCORE_NET_ERR_VAL;
    }

    if (p == NULL)
    {
        // A null segment is a graceful remote close. Clear the state and the block before closing,
        // so any stale callback is harmless.
        protocore_conn_set_state(slot->id, CONN_FREE);
        slot->pcb = NULL;
        protocore_net_arg(tpcb, NULL);
        if (protocore_net_close(tpcb) != PROTOCORE_NET_OK)
        {
            protocore_net_abort(tpcb);
        }
        PROTOCORE_OBS_TRANSITION(slot->id, CONN_ACTIVE, CONN_FREE, PROTOCORE_CONN_R_CLOSE_REMOTE);
        TcpEvt evt = {EVT_DISCONNECT, slot->id, 0};
        protocore_conn_enqueue(slot, &evt);
        return PROTOCORE_NET_OK;
    }

    // Backpressure without loss: if the whole segment will not fit the free ring space, refuse it
    // without taking ownership, so the stack retains it and redelivers once the application has
    // drained; nudge the loop to drain. Copying only what fits corrupts bodies larger than the ring.
    // Needs RX_BUF_SIZE greater than the largest incoming segment so a full one can eventually fit.
    if (p->tot_len > protocore_ring_free(&slot->rx_head, &slot->rx_tail, RX_BUF_SIZE))
    {
        PROTOCORE_OBS_NOTICE(slot->id, CONN_ACTIVE, PROTOCORE_CONN_R_BACKPRESSURE);
        TcpEvt evt = {EVT_DATA, slot->id, 0}; // wake the loop so it drains the ring
        protocore_conn_enqueue(slot, &evt);
        // Do NOT refresh the idle timer here: a refused segment is redelivered on every retransmit
        // until the ring drains, so refreshing on refusal keeps a stuck connection alive forever and
        // the sweep never reaps it. The timer is refreshed below only when data is accepted.
        return PROTOCORE_NET_ERR_MEM; // do NOT free the segment: the stack keeps it and redelivers
    }

    slot->last_activity_ms = Clock.ms; // accepted data is progress: refresh the idle timer

    // Move the segment into the ring a contiguous span per chain link, two across the wrap,
    // advancing a local head and publishing it once at the end. The free-space check above
    // guarantees it fits, so the head can never overrun the tail.
    size_t head = PROTO_ATOMIC_LOAD(&slot->rx_head); // sole producer of head; one acquire load
    for (protocore_pbuf *q = p; q != NULL; q = q->next)
    {
        head = protocore_ring_write_span(slot->rx_buffer, RX_BUF_SIZE, head, (const uint8_t *)q->payload, q->len);
    }
    PROTO_ATOMIC_STORE(&slot->rx_head, head); // one release store publishes the whole segment
    size_t bytes_copied = p->tot_len;         // the whole segment fit, checked above

    // Do NOT reopen the window here: that is ack_consumed()'s job as the worker drains, so the
    // advertised window tracks ring occupancy. Acking on copy decouples the window from drainage and
    // deadlocks a streamed upload once the ring is smaller than the peer's window.
    protocore_net_pbuf_free(p);

    if (bytes_copied > 0)
    {
        TcpEvt evt = {EVT_DATA, slot->id, bytes_copied};
        protocore_conn_enqueue(slot, &evt);
    }

    return PROTOCORE_NET_OK;
}

// Refresh the idle timestamp so an active sender is not reaped while its responses are in flight,
// and - for a slot dwelling in CONN_CLOSING - finalize the close once the peer has ACKed everything.
protocore_net_err lowlevel_sent_cb(void *arg, protocore_pcb *tpcb, proto_u16 len)
{
    (void)len;
    TcpConn *slot = (TcpConn *)arg;
    if (slot != NULL)
    {
        slot->last_activity_ms = Clock.ms;
        if (PROTO_ATOMIC_LOAD(&slot->state) == CONN_CLOSING)
        {
            protocore_conn_closing_check(slot->id, tpcb); // drained? tear down and free the slot
        }
        else
        {
            // The send window just freed: wake the owning worker so a paced response resumes now
            // rather than on the next idle sweep.
            Workers.worker_id = slot->owner;
            Workers.wake(protocore_worker_span());
        }
    }
    return PROTOCORE_NET_OK;
}

// By the time this fires the control block is already gone internally, so it must NOT be closed or
// aborted here - only the slot's pointer is dropped, and an error is posted so the session layer
// resets the protocol state.
void lowlevel_err_cb(void *arg, protocore_net_err err)
{
    (void)err;
    TcpConn *slot = (TcpConn *)arg;
    if (slot == NULL)
    {
        return;
    }

    ConnState old = PROTO_ATOMIC_LOAD(&slot->state);
    protocore_conn_set_state(slot->id, CONN_FREE);
    slot->pcb = NULL;

    // A slot that errored while dwelling is already done from the session's view: its response was
    // sent and the protocol state reset. Release the slot; do not re-post a close event.
    if (old == CONN_CLOSING)
    {
        PROTOCORE_OBS_TRANSITION(slot->id, CONN_CLOSING, CONN_FREE, PROTOCORE_CONN_R_DRAINED);
        return;
    }

    PROTOCORE_OBS_TRANSITION(slot->id, CONN_ACTIVE, CONN_FREE, PROTOCORE_CONN_R_ERROR);
    TcpEvt evt = {EVT_ERROR, slot->id, 0};
    protocore_conn_enqueue(slot, &evt);
}

// Designated, so a member's position in the struct does not decide what it binds to.

// The same calls the pool reaches internally, published so a caller outside this file can invoke
// them. Designated, so a member's position in the struct does not decide what it binds to.
ConnPoolNs ConnPool = {.set_state = set_state,
                       .alloc_free = alloc_free,
                       .timeout_ms = timeout_ms,
                       .send = send,
                       .send_flush = send_flush,
                       .sndbuf = sndbuf,
                       .flush = flush,
                       .ack_consumed = ack_consumed,
                       .raw_send = raw_send,
                       .close = close_slot,
                       .abort_slot = abort_slot,
                       .closing_finalize = closing_finalize,
                       .closing_check = closing_check,
                       .begin_close = begin_close,
                       .enqueue = enqueue,
                       .init = init,
                       .stop = stop,
                       .active_count = active_count,
                       .remote_ip = remote_ip,
                       .remote_addr = remote_addr,
                       .touch_active = touch_active,
                       .check_timeouts = check_timeouts,
#if PROTOCORE_ENABLE_OBSERVABILITY
                       .on_event = on_event,
                       .counters_get = counters_get,
                       .counters_reset = counters_reset,
                       .obs_bump = obs_bump,
                       .obs_transition = obs_transition,
                       .obs_notice = obs_notice,
#endif
                       .available = available,
                       .read_byte = read_byte,
                       .peek = peek,
                       .consume = consume,
                       .read = read,
                       .active = active,
                       .iface = iface,
                       .listener_id = listener_id,
                       .tls = tls,
                       .owner = owner,
                       .proto_of = proto_of,
                       .pcb_of = pcb_of};
