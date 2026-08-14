// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
#include "../common.h"               // TcpConn, conn_pool: the slots this engine drives
#include "../../net_addr/net_addr.h" // protocore_net_addr_to_ip(): the stack's address as a protocore_ip
#include "../lower/lower.h"          // every call into the stack below goes through the seam
#include "../server/server.h"        // TcpListener.enqueue: the owning listener posts the event
#include "core_setup/board_profiles/protocore_platform.h"
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
};

/**
 * @brief The pool's state, the call it is serving, and the calls that reach both.
 *
 * @var ConnPoolInternal::conn_timeout_ms  the idle deadline the sweep measures against
 * @var ConnPoolInternal::store            the zeroed template and the counters
 * @var ConnPoolInternal::slot             the connection a call acts on
 * @var ConnPoolInternal::st               the state a write installs
 * @var ConnPoolInternal::data             bytes for a send
 * @var ConnPoolInternal::len              how many
 * @var ConnPoolInternal::pcb              the control block a raw call acts on
 * @var ConnPoolInternal::cfg              the config init reads
 * @var ConnPoolInternal::worker_id        whose slots the sweep reaps
 * @var ConnPoolInternal::out              where the peer address is written
 * @var ConnPoolInternal::evt              the event an enqueue posts
 * @var ConnPoolInternal::conn             the slot an enqueue posts for
 * @var ConnPoolInternal::ok               a call's true/false outcome
 * @var ConnPoolInternal::u16              a call's 16-bit outcome
 * @var ConnPoolInternal::u32              a call's 32-bit outcome
 * @var ConnPoolInternal::u8               a call's 8-bit outcome
 * @var ConnPoolInternal::i32              a call's signed outcome
 */
struct ConnPoolInternal
{
    uint32_t conn_timeout_ms;
    struct ConnPoolStorage *store;
    ConnPoolNs *ns; ///< the handle a caller sets a call's members on, and reads its result from
#if PROTOCORE_ENABLE_OBSERVABILITY
    protocore_conn_event_cb event_cb; ///< the installed observer
#endif

    void (*set_state)(struct ConnPoolInternal *ctx);
    void (*alloc_free)(struct ConnPoolInternal *ctx);
    void (*timeout_ms)(struct ConnPoolInternal *ctx);
    void (*send)(struct ConnPoolInternal *ctx);
    void (*send_flush)(struct ConnPoolInternal *ctx);
    void (*sndbuf)(struct ConnPoolInternal *ctx);
    void (*flush)(struct ConnPoolInternal *ctx);
    void (*ack_consumed)(struct ConnPoolInternal *ctx);
    void (*raw_send)(struct ConnPoolInternal *ctx);
    void (*close)(struct ConnPoolInternal *ctx);
    void (*abort_slot)(struct ConnPoolInternal *ctx);
    void (*closing_finalize)(struct ConnPoolInternal *ctx);
    void (*closing_check)(struct ConnPoolInternal *ctx);
    void (*begin_close)(struct ConnPoolInternal *ctx);
    void (*enqueue)(struct ConnPoolInternal *ctx);
    void (*init)(struct ConnPoolInternal *ctx);
    void (*stop)(struct ConnPoolInternal *ctx);
    void (*active_count)(struct ConnPoolInternal *ctx);
    void (*remote_ip)(struct ConnPoolInternal *ctx);
    void (*remote_addr)(struct ConnPoolInternal *ctx);
    void (*touch_active)(struct ConnPoolInternal *ctx);
    void (*check_timeouts)(struct ConnPoolInternal *ctx);
#if PROTOCORE_ENABLE_OBSERVABILITY
    void (*on_event)(struct ConnPoolInternal *ctx);
    void (*counters_get)(struct ConnPoolInternal *ctx);
    void (*counters_reset)(struct ConnPoolInternal *ctx);
    void (*obs_bump)(struct ConnPoolInternal *ctx);
    void (*obs_transition)(struct ConnPoolInternal *ctx);
    void (*obs_notice)(struct ConnPoolInternal *ctx);
#endif

    void (*available)(struct ConnPoolInternal *ctx);
    void (*read_byte)(struct ConnPoolInternal *ctx);
    void (*peek)(struct ConnPoolInternal *ctx);
    void (*consume)(struct ConnPoolInternal *ctx);
    void (*read)(struct ConnPoolInternal *ctx);
    void (*active)(struct ConnPoolInternal *ctx);
    void (*iface)(struct ConnPoolInternal *ctx);
    void (*listener_id)(struct ConnPoolInternal *ctx);
    void (*tls)(struct ConnPoolInternal *ctx);
    void (*owner)(struct ConnPoolInternal *ctx);
    void (*proto_of)(struct ConnPoolInternal *ctx);
    void (*pcb_of)(struct ConnPoolInternal *ctx);
};

static struct ConnPoolStorage s_store;

// Tentative here so the functions below can reach the state; the members are bound at the bottom,
// where every function they name has been defined.
static struct ConnPoolInternal s_conn;

// ---------------------------------------------------------------------------
// Observability - event hook and lock-free counters
// ---------------------------------------------------------------------------
// Zero cost when off: the macros expand to nothing and their arguments, including the reason names
// that only exist with the feature, are dropped unparsed by the preprocessor.
#if PROTOCORE_ENABLE_OBSERVABILITY

// The counters are relaxed atomics: bumped from the stack's callback context and from the workers,
// so the increments must not tear, but nothing orders anything against them.
static void obs_bump(struct ConnPoolInternal *restrict ctx)
{
    // DRAINED is gauge-only: the close reason that entered the dwell was already counted.
    if (ctx->ns->obs.reason != PROTOCORE_CONN_R_DRAINED)
    {
        atomic_fetch_add_explicit(&ctx->store->ctr[ctx->ns->obs.reason], 1, memory_order_relaxed);
    }
}

// Install the observer a call is carrying. Null unregisters.
static void on_event(struct ConnPoolInternal *restrict ctx)
{
    ctx->event_cb = ctx->ns->obs.event_cb_in;
}

static void counters_get(struct ConnPoolInternal *restrict ctx)
{
    protocore_conn_counters c;
    c.accepts = atomic_load_explicit(&ctx->store->ctr[PROTOCORE_CONN_R_ACCEPT], memory_order_relaxed);
    c.closes_remote = atomic_load_explicit(&ctx->store->ctr[PROTOCORE_CONN_R_CLOSE_REMOTE], memory_order_relaxed);
    c.closes_local = atomic_load_explicit(&ctx->store->ctr[PROTOCORE_CONN_R_CLOSE_LOCAL], memory_order_relaxed);
    c.closes_error = atomic_load_explicit(&ctx->store->ctr[PROTOCORE_CONN_R_ERROR], memory_order_relaxed);
    c.closes_timeout = atomic_load_explicit(&ctx->store->ctr[PROTOCORE_CONN_R_TIMEOUT], memory_order_relaxed);
    c.closes_abort = atomic_load_explicit(&ctx->store->ctr[PROTOCORE_CONN_R_ABORT], memory_order_relaxed);
    c.backpressure = atomic_load_explicit(&ctx->store->ctr[PROTOCORE_CONN_R_BACKPRESSURE], memory_order_relaxed);
    c.defer_drops = atomic_load_explicit(&ctx->store->ctr[PROTOCORE_CONN_R_DEFER_DROP], memory_order_relaxed);
    // Derived from the pool on read, so it cannot drift out of step with the actual slot states.
    c.closing_gauge = 0;
    for (int i = 0; i < MAX_CONNS; i++)
    {
        if (PROTO_ATOMIC_LOAD(&conn_pool[i].state) == CONN_CLOSING)
        {
            c.closing_gauge++;
        }
    }
    ctx->ns->obs.counters = c;
}

static void counters_reset(struct ConnPoolInternal *restrict ctx)
{
    for (int i = 0; i < 9; i++)
    {
        atomic_store_explicit(&ctx->store->ctr[i], 0, memory_order_relaxed);
    }
}

// A real state transition: bump the reason counter and fire the callback.
static void obs_transition(struct ConnPoolInternal *restrict ctx)
{
    ctx->obs_bump(ctx);
    if (ctx->event_cb != NULL)
    {
        ctx->event_cb(ctx->ns->slot, ctx->ns->obs.olds, ctx->ns->obs.news, ctx->ns->obs.reason);
    }
}

// A non-transition notice (backpressure / defer-drop): bump and fire with old == new.
static void obs_notice(struct ConnPoolInternal *restrict ctx)
{
    ctx->obs_bump(ctx);
    if (ctx->event_cb != NULL)
    {
        ctx->event_cb(ctx->ns->slot, ctx->ns->st, ctx->ns->st, ctx->ns->obs.reason);
    }
}

void protocore_obs_transition(uint8_t slot, ConnState olds, ConnState news, protocore_conn_reason reason)
{
    ConnPool.slot = slot;
    ConnPool.obs.olds = olds;
    ConnPool.obs.news = news;
    ConnPool.obs.reason = reason;
    s_conn.obs_transition(&s_conn);
}

void protocore_obs_notice(uint8_t slot, ConnState st, protocore_conn_reason reason)
{
    ConnPool.slot = slot;
    ConnPool.st = st;
    ConnPool.obs.reason = reason;
    s_conn.obs_notice(&s_conn);
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
static void set_state(struct ConnPoolInternal *restrict ctx)
{
    // Bound every write to the real array size, so the setter is memory-safe on its own rather than
    // relying on the caller never over-indexing.
    if (ctx->ns->slot >= CONN_POOL_SLOTS)
    {
        return;
    }
#if PROTOCORE_INTERNAL_SLOTS > 0
    // Reserved internal slots are not part of the accept pool and are never handed out by the
    // allocator, so they carry state but no bitmask bit.
    if (ctx->ns->slot >= MAX_CONNS)
    {
        PROTO_ATOMIC_STORE(&conn_pool[ctx->ns->slot].state, ctx->ns->st);
        return;
    }
#endif
    if (ctx->ns->st == CONN_FREE)
    {
        PROTO_ATOMIC_STORE(&conn_pool[ctx->ns->slot].state, ctx->ns->st);
        protocore_slot_mark(&protocore_conn_bits.free, ctx->ns->slot);
    }
    else
    {
        protocore_slot_clear(&protocore_conn_bits.free, ctx->ns->slot);
        PROTO_ATOMIC_STORE(&conn_pool[ctx->ns->slot].state, ctx->ns->st);
    }
}

static void protocore_conn_set_state(uint8_t slot, ConnState st)
{
    ConnPool.slot = slot;
    ConnPool.st = st;
    s_conn.set_state(&s_conn);
}

// First free slot as one ctz on the bitmask. Reports -1 when the pool is full. Free AND not held: a
// slot whose bytes the wire has not finished with is not available, however free its state says.
static void alloc_free(struct ConnPoolInternal *restrict ctx)
{
    ctx->ns->i32 = protocore_slot_next(protocore_slot_ready(&protocore_conn_bits.free, &protocore_conn_bits.held, MAX_CONNS));
}

static int32_t protocore_conn_alloc_free(void)
{
    s_conn.alloc_free(&s_conn);
    return ConnPool.i32;
}

static void timeout_ms(struct ConnPoolInternal *restrict ctx)
{
    ctx->ns->u32 = ctx->conn_timeout_ms;
}

// ---------------------------------------------------------------------------
// Connection output - the single send/flush/close path for every higher layer
// ---------------------------------------------------------------------------
// Presentation and application code never call the stack directly: they hand bytes to this layer,
// which decides whether they go out as plaintext or through the TLS record layer.

// The write target is always the slot's own control block, so it cannot disagree with the ingress
// reads that resolve it the same way.
static void send(struct ConnPoolInternal *restrict ctx)
{
    TcpLower.op = PROTOCORE_OP_SEND;
    TcpLower.slot = ctx->ns->slot;
    TcpLower.pcb = conn_pool[ctx->ns->slot].pcb;
    TcpLower.data = ctx->ns->io.data;
    TcpLower.len = ctx->ns->io.len;
    TcpLower.flush = PROTO_FALSE;
    ctx->ns->ok = TcpLower.marshal(TcpLower.internal) == PROTOCORE_NET_OK;
}

// Terminal single-shot write: the bytes and their push happen in one round trip into the stack's
// context, so a small response costs one marshal instead of a send and a flush. For a TLS slot this
// is identical to send: the record BIO already pushes ciphertext per record.
static void send_flush(struct ConnPoolInternal *restrict ctx)
{
    TcpLower.op = PROTOCORE_OP_SEND;
    TcpLower.slot = ctx->ns->slot;
    TcpLower.pcb = conn_pool[ctx->ns->slot].pcb;
    TcpLower.data = ctx->ns->io.data;
    TcpLower.len = ctx->ns->io.len;
    TcpLower.flush = PROTO_TRUE;
    ctx->ns->ok = TcpLower.marshal(TcpLower.internal) == PROTOCORE_NET_OK;
}

static void sndbuf(struct ConnPoolInternal *restrict ctx)
{
    protocore_pcb *pcb = conn_pool[ctx->ns->slot].pcb;
    if (pcb == NULL)
    {
        ctx->ns->u16 = 0;
        return;
    }
    proto_u16 avail = protocore_net_sndbuf(pcb);
#if PROTOCORE_ENABLE_TLS
    // A TLS record adds header and tag overhead; report a conservative plaintext budget so a caller
    // that fills it does not overrun the cipher's framing.
    if (conn_pool[ctx->ns->slot].tls)
    {
        avail = (avail > 64) ? (proto_u16)(avail - 64) : 0;
    }
#endif
    ctx->ns->u16 = avail;
}

static void flush(struct ConnPoolInternal *restrict ctx)
{
#if PROTOCORE_ENABLE_TLS
    if (conn_pool[ctx->ns->slot].tls)
    {
        return; // the BIO already pushed ciphertext per record; a flush must not end the session,
                // since persistent TLS reuses it
    }
#endif
    TcpLower.op = PROTOCORE_OP_OUTPUT;
    TcpLower.slot = ctx->ns->slot;
    TcpLower.pcb = conn_pool[ctx->ns->slot].pcb;
    TcpLower.data = NULL;
    TcpLower.len = 0;
    TcpLower.flush = PROTO_FALSE;
    (void)TcpLower.marshal(TcpLower.internal);
}

// The DS field is stamped where the control block is created - the listener's code point at accept,
// the server-wide default at connect - and not afterwards. RFC 9293 sec 3.9.2 SHLD-23: an
// application should not change the Diffserv field during a connection, because it has no knowledge
// of individual segments, so a mid-connection change lands on an arbitrary boundary.

// Reopen the receive window by exactly what the reader took (ack-on-consume), so the advertised
// window tracks ring occupancy and a slow consumer cannot overflow it.
static void ack_consumed(struct ConnPoolInternal *restrict ctx)
{
    if (ctx->ns->slot >= MAX_CONNS)
    {
        return;
    }
    TcpConn *c = &conn_pool[ctx->ns->slot];
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
    TcpLower.slot = ctx->ns->slot;
    TcpLower.pcb = c->pcb;
    TcpLower.data = NULL;
    TcpLower.len = (proto_u16)consumed;
    TcpLower.flush = PROTO_FALSE;
    (void)TcpLower.marshal(TcpLower.internal);
}

// A raw write of already-encrypted bytes, for a control block reached without its slot. The seam
// owns the context choice and re-checks the block is still bound.
static void raw_send(struct ConnPoolInternal *restrict ctx)
{
    if (ctx->ns->pcb == NULL)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    TcpLower.op = PROTOCORE_OP_RAWSEND;
    TcpLower.slot = 0;
    TcpLower.pcb = ctx->ns->pcb;
    TcpLower.data = ctx->ns->io.data;
    TcpLower.len = ctx->ns->io.len;
    TcpLower.flush = PROTO_FALSE;
    ctx->ns->ok = TcpLower.marshal(TcpLower.internal) == PROTOCORE_NET_OK;
}

// The application-initiated close. Remote FIN, error and timeout closes are observed at their own
// sites, so this one is uniquely local.
static void close_slot(struct ConnPoolInternal *restrict ctx)
{
    if (ctx->ns->slot >= MAX_CONNS)
    {
        return;
    }
    TcpConn *c = &conn_pool[ctx->ns->slot];
    protocore_pcb *pcb = c->pcb;
    if (pcb == NULL)
    {
        return;
    }
    // The state is read rather than assumed: a worker, a stack callback and the sweep can all reach
    // the same slot, so it may already be in the drain dwell.
    const ConnState was = PROTO_ATOMIC_LOAD(&c->state);
    PROTOCORE_OBS_TRANSITION(ctx->ns->slot, was, CONN_FREE, PROTOCORE_CONN_R_CLOSE_LOCAL);
    // Detach and free the slot before the close, so a late callback for this block finds a null arg.
    // The close targets the captured block, so nulling the slot first is safe.
    TcpLower.pcb = pcb;
    TcpLower.slot = ctx->ns->slot;
    TcpLower.detach(TcpLower.internal);
    protocore_conn_set_state(c->id, CONN_FREE);
    c->pcb = NULL;
    TcpLower.op = PROTOCORE_OP_CLOSE;
    TcpLower.pcb = pcb;
    TcpLower.slot = ctx->ns->slot;
    TcpLower.data = NULL;
    TcpLower.len = 0;
    TcpLower.flush = PROTO_FALSE;
    (void)TcpLower.marshal(TcpLower.internal); // TLS teardown and FIN, in the stack's context
}

static void abort_slot(struct ConnPoolInternal *restrict ctx)
{
    if (ctx->ns->slot >= MAX_CONNS)
    {
        return;
    }
    TcpConn *c = &conn_pool[ctx->ns->slot];
    protocore_pcb *pcb = c->pcb;
    if (pcb == NULL)
    {
        return;
    }
    const ConnState was = PROTO_ATOMIC_LOAD(&c->state);
    PROTOCORE_OBS_TRANSITION(ctx->ns->slot, was, CONN_FREE, PROTOCORE_CONN_R_ABORT);
#if PROTOCORE_ENABLE_TLS
    if (c->tls)
    {
        protocore_tls_conn_free(ctx->ns->slot); // abrupt: free the per-conn context, no close_notify
    }
#endif
    TcpLower.pcb = pcb;
    TcpLower.slot = ctx->ns->slot;
    TcpLower.detach(TcpLower.internal);
    protocore_conn_set_state(c->id, CONN_FREE);
    c->pcb = NULL;
    TcpLower.pcb = pcb;
    TcpLower.slot = ctx->ns->slot;
    TcpLower.abort(TcpLower.internal);
}

// ---------------------------------------------------------------------------
// The CONN_CLOSING dwell: a graceful close that holds the slot until the peer ACKs
// ---------------------------------------------------------------------------
// These run in the stack's context, so they touch the control block directly.

// Tear the connection down and free the slot.
static void closing_finalize(struct ConnPoolInternal *restrict ctx)
{
    TcpConn *c = &conn_pool[ctx->ns->slot];
#if PROTOCORE_ENABLE_TLS
    if (c->tls)
    {
        protocore_tls_conn_end(ctx->ns->slot); // close_notify and free the context, in-thread
    }
#endif
    protocore_conn_set_state(c->id, CONN_FREE);
    c->pcb = NULL;
    if (ctx->ns->pcb != NULL)
    {
        protocore_net_arg(ctx->ns->pcb, NULL);
        if (protocore_net_close(ctx->ns->pcb) != PROTOCORE_NET_OK)
        {
            protocore_net_abort(ctx->ns->pcb);
        }
    }
    PROTOCORE_OBS_TRANSITION(ctx->ns->slot, CONN_CLOSING, CONN_FREE, PROTOCORE_CONN_R_DRAINED);
}

// Finalize now if the slot is dwelling and its transmit queue has drained.
static void closing_check(struct ConnPoolInternal *restrict ctx)
{
    if (ctx->ns->slot >= MAX_CONNS || PROTO_ATOMIC_LOAD(&conn_pool[ctx->ns->slot].state) != CONN_CLOSING)
    {
        return;
    }
    if (ctx->ns->pcb == NULL || ctx->ns->pcb->snd_queuelen == 0)
    {
        ctx->closing_finalize(ctx);
    }
}

static void protocore_conn_closing_check(uint8_t slot, protocore_pcb *pcb)
{
    ConnPool.slot = slot;
    ConnPool.pcb = pcb;
    s_conn.closing_check(&s_conn);
}

static void begin_close(struct ConnPoolInternal *restrict ctx)
{
    if (ctx->ns->slot >= MAX_CONNS)
    {
        return;
    }
    TcpConn *c = &conn_pool[ctx->ns->slot];
    if (PROTO_ATOMIC_LOAD(&c->state) != CONN_ACTIVE) // an error during the write may have freed it
    {
        return;
    }
    protocore_pcb *pcb = c->pcb;
    Clock.millis(Clock.internal);
    c->last_activity_ms = Clock.ms;     // start the dwell clock
    protocore_conn_set_state(c->id, CONN_CLOSING); // release store: the callbacks now see CLOSING
    PROTOCORE_OBS_TRANSITION(ctx->ns->slot, CONN_ACTIVE, CONN_CLOSING, PROTOCORE_CONN_R_CLOSE_LOCAL);
    // Finalize immediately if the response already drained, else dwell until the sent callback or
    // the sweep reclaims it. The control-block read happens in the stack's context, so it marshals.
    TcpLower.op = PROTOCORE_OP_CLOSE_CHECK;
    TcpLower.slot = ctx->ns->slot;
    TcpLower.pcb = pcb;
    TcpLower.data = NULL;
    TcpLower.len = 0;
    TcpLower.flush = PROTO_FALSE;
    (void)TcpLower.marshal(TcpLower.internal);
}

// Forward the event to the queue owned by the connection's listener. The enqueue does not block: it
// returns immediately if the queue is full. A full queue means the application is not draining fast
// enough; dropped events are recoverable via the idle sweep.
static void enqueue(struct ConnPoolInternal *restrict ctx)
{
    TcpListener.idx = conn_pool[ctx->ns->slot].listener_id;
    TcpListener.q.evt = ctx->ns->evt;
    TcpListener.enqueue(TcpListener.internal);
    if (!TcpListener.ok)
    {
        PROTOCORE_OBS_NOTICE(ctx->ns->slot, PROTO_ATOMIC_LOAD(&conn_pool[ctx->ns->slot].state),
                             PROTOCORE_CONN_R_DEFER_DROP);
    }
}

static void protocore_conn_enqueue(TcpConn *slot, const TcpEvt *evt)
{
    ConnPool.slot = slot->id;
    ConnPool.evt = evt;
    s_conn.enqueue(&s_conn);
}

// ---------------------------------------------------------------------------
// Pool lifecycle
// ---------------------------------------------------------------------------

static void init(struct ConnPoolInternal *restrict ctx)
{
    ctx->conn_timeout_ms = (ctx->ns->life.cfg != NULL) ? ctx->ns->life.cfg->conn_timeout_ms : CONN_TIMEOUT_MS;
    // The template lives in storage and the copy runs before any listener is accepting, so the
    // non-atomic struct assignment over the atomic members races nothing.
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = ctx->store->blank;
        conn_pool[i].id = (uint8_t)i;
        protocore_conn_set_state((uint8_t)i, CONN_FREE);
    }
}

// Abort every live connection. Listening control blocks and queues belong to the server half and
// must be torn down through it first.
static void stop(struct ConnPoolInternal *restrict ctx)
{
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
            TcpLower.detach(TcpLower.internal);
            TcpLower.pcb = pcb;
            TcpLower.slot = (uint8_t)i;
            TcpLower.abort(TcpLower.internal);
            PROTOCORE_OBS_TRANSITION((uint8_t)i, st, CONN_FREE, PROTOCORE_CONN_R_ABORT);
        }
        protocore_conn_set_state((uint8_t)i, CONN_FREE);
        conn_pool[i].pcb = NULL;
    }
    (void)ctx;
}

// ---------------------------------------------------------------------------
// The receive ring, and what a slot is
// ---------------------------------------------------------------------------
// Transport owns the ring. A layer above drains it only through these, and never indexes the
// buffer or advances the tail itself. The consuming calls advance the tail only; the window is
// reopened by ack_consumed once per worker loop, so there is no per-byte ACK. Single-consumer per
// slot (the owning worker), so nothing locks here. All five delegate to the shared SPSC ring
// primitive over the slot's rx_buffer - this layer never reimplements the ring math.

static void available(struct ConnPoolInternal *restrict ctx)
{
    const TcpConn *c = &conn_pool[ctx->ns->slot];
    ctx->ns->n = protocore_ring_available(&c->rx_head, &c->rx_tail, RX_BUF_SIZE);
}

static void read_byte(struct ConnPoolInternal *restrict ctx)
{
    TcpConn *c = &conn_pool[ctx->ns->slot];
    ctx->ns->ok = protocore_ring_read_byte(c->rx_buffer, RX_BUF_SIZE, &c->rx_head, &c->rx_tail, &ctx->ns->u8);
}

static void peek(struct ConnPoolInternal *restrict ctx)
{
    const TcpConn *c = &conn_pool[ctx->ns->slot];
    protocore_ring_peek(c->rx_buffer, RX_BUF_SIZE, &c->rx_tail, ctx->ns->io.off, ctx->ns->io.buf, ctx->ns->io.count);
}

static void consume(struct ConnPoolInternal *restrict ctx)
{
    protocore_ring_consume(&conn_pool[ctx->ns->slot].rx_tail, RX_BUF_SIZE, ctx->ns->io.count);
}

static void read(struct ConnPoolInternal *restrict ctx)
{
    TcpConn *c = &conn_pool[ctx->ns->slot];
    ctx->ns->n =
        protocore_ring_read(c->rx_buffer, RX_BUF_SIZE, &c->rx_head, &c->rx_tail, ctx->ns->io.buf, ctx->ns->io.cap);
}

static void active(struct ConnPoolInternal *restrict ctx)
{
    const TcpConn *c = &conn_pool[ctx->ns->slot];
    ctx->ns->ok = PROTO_ATOMIC_LOAD(&c->state) == CONN_ACTIVE && c->pcb != NULL;
}

static void iface(struct ConnPoolInternal *restrict ctx)
{
    ctx->ns->if_kind = conn_pool[ctx->ns->slot].iface;
}

static void listener_id(struct ConnPoolInternal *restrict ctx)
{
    ctx->ns->u8 = conn_pool[ctx->ns->slot].listener_id;
}

static void tls(struct ConnPoolInternal *restrict ctx)
{
    ctx->ns->ok = conn_pool[ctx->ns->slot].tls != 0;
}

static void owner(struct ConnPoolInternal *restrict ctx)
{
    ctx->ns->u8 = conn_pool[ctx->ns->slot].owner;
}

static void proto_of(struct ConnPoolInternal *restrict ctx)
{
    ctx->ns->proto = conn_pool[ctx->ns->slot].proto;
}

static void pcb_of(struct ConnPoolInternal *restrict ctx)
{
    ctx->ns->pcb = conn_pool[ctx->ns->slot].pcb;
}

static void active_count(struct ConnPoolInternal *restrict ctx)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        if (PROTO_ATOMIC_LOAD(&conn_pool[i].state) == CONN_ACTIVE)
        {
            n++;
        }
    }
    ctx->ns->u8 = n;
}

static void remote_ip(struct ConnPoolInternal *restrict ctx)
{
    ctx->ns->u32 = 0;
    if (ctx->ns->slot >= MAX_CONNS)
    {
        return;
    }
    TcpConn *conn = &conn_pool[ctx->ns->slot];
    if (PROTO_ATOMIC_LOAD(&conn->state) == CONN_ACTIVE && conn->pcb != NULL)
    {
        ctx->ns->u32 = protocore_net_ip4_u32(protocore_net_ip_as_v4(&conn->pcb->remote_ip));
    }
}

static void remote_addr(struct ConnPoolInternal *restrict ctx)
{
    if (ctx->ns->out != NULL)
    {
        ctx->ns->out->family = PROTOCORE_IP_NONE;
    }
    if (ctx->ns->out == NULL || ctx->ns->slot >= MAX_CONNS)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    TcpConn *conn = &conn_pool[ctx->ns->slot];
    if (PROTO_ATOMIC_LOAD(&conn->state) != CONN_ACTIVE || conn->pcb == NULL)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    protocore_net_addr_to_ip(&conn->pcb->remote_ip, ctx->ns->out);
    ctx->ns->ok = PROTO_TRUE;
}

// Restart a slot's idle timer while a response body is still being paged out. Such a slot is
// streaming, or briefly blocked on a full send window, NOT idle - so the sweep must not reap it
// mid-transfer and truncate a body larger than one window. Dead-peer teardown for an in-flight
// response stays with the stack's retransmission timers, which abort through the error callback.
static void touch_active(struct ConnPoolInternal *restrict ctx)
{
    if (ctx->ns->slot >= MAX_CONNS)
    {
        return;
    }
    TcpConn *c = &conn_pool[ctx->ns->slot];
    if (PROTO_ATOMIC_LOAD(&c->state) == CONN_ACTIVE)
    {
        Clock.millis(Clock.internal);
        c->last_activity_ms = Clock.ms;
    }
}

static void check_timeouts(struct ConnPoolInternal *restrict ctx)
{
    Clock.millis(Clock.internal);
    uint32_t now = Clock.ms;
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TcpConn *slot = &conn_pool[i];
        if (slot->owner != ctx->ns->life.worker_id) // each worker reaps only its own slots
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
                TcpLower.detach(TcpLower.internal);
                TcpLower.pcb = cpcb;
                TcpLower.slot = slot->id;
                TcpLower.abort(TcpLower.internal);
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
        if ((now - slot->last_activity_ms) < ctx->conn_timeout_ms)
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
            TcpLower.detach(TcpLower.internal);
            TcpLower.pcb = pcb;
            TcpLower.slot = slot->id;
            TcpLower.abort(TcpLower.internal);
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

    Clock.millis(Clock.internal);
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
        Clock.millis(Clock.internal);
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
            Workers.wake(Workers.internal);
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
static struct ConnPoolInternal s_conn = {.conn_timeout_ms = CONN_TIMEOUT_MS,
                                         .store = &s_store,
                                         .ns = &ConnPool,
                                         .set_state = set_state,
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
                       .pcb_of = pcb_of,
                       .internal = &s_conn};
