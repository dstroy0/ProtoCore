// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tcp.c
 * @brief Layer 4 (Transport) - TCP connection management implementation.
 *
 * Every raw stack callback here runs in the stack's own task context. They are
 * NOT hardware ISRs, which is why the queue send is the ordinary variant with
 * timeout=0 rather than the from-ISR form.
 *
 * **Ring buffer write ordering**
 * The producer (recv callback) writes payload bytes into `rx_buffer[]` and
 * then advances `rx_head`.  The consumer (worker) reads from `rx_buffer[]` at
 * `rx_tail` and advances `rx_tail`.  `rx_head`/`rx_tail` are `_Atomic`, reached
 * through PROTO_ATOMIC_LOAD / PROTO_ATOMIC_STORE (acquire/release): the
 * producer's buffer writes are published by the release store of `rx_head` and
 * observed by the consumer's acquire load, correct on either core. The ring
 * math itself is the shared `ring.h` primitive.
 *
 * **Listener coupling**
 * Each TcpConn carries `listener_id`, set at accept time by listener_accept_cb. The `enqueue()`
 * helper posts events through `Tcp.listener->enqueue()`, so this file names the listener that owns the
 * queue and never the queue itself.
 */

#include "tcp_conn.h"
#include "../diffserv.h" // pc_dscp_to_tos and the server-wide default this connection starts from
#include "../net_addr.h" // NetAddr.to_ip(): the stack's address as a pc_ip
#include "board_drivers/board_profiles/pc_platform.h"
#include "mmgr/rawmemcpy.h"     // proto_raw_read: the unaligned v6 address load
#include "server/clock/clock.h" // pc_millis() pluggable monotonic clock
#include "tcp_listener.h"       // Tcp.listener->enqueue(): the owning listener posts the event, not this file

#if PROTOCORE_HOT
#include "network_drivers/session/worker.h" // pc_worker_wake() - resume a paced send when the window drains
#endif

#if PC_ENABLE_TLS
#include "network_drivers/tls/tls.h"
#endif

// ---------------------------------------------------------------------------
// Observability (PC_ENABLE_OBSERVABILITY) - event hook + lock-free counters.
// Zero cost when off: OBS_TRANSITION / OBS_NOTICE expand to nothing and their
// arguments (incl. the pc_conn_reason names, which are only declared when the
// feature is on) are dropped unparsed by the preprocessor.
// ---------------------------------------------------------------------------
#if PROTOCORE_HOT

#endif
#if PC_ENABLE_OBSERVABILITY

// All connection-observability state, owned by one instance (internal linkage): the event
// callback and the cumulative per-reason counters (indexed 0..7). The live CONN_CLOSING gauge
// is not a counter - it is derived on read by scanning the pool, so it can never drift out of
// sync with the actual slot states. One named owner, unreachable from any other TU.
//
// The counters are relaxed atomics: they are bumped from the stack's callback context and from
// the workers, so the increments must not tear, but nothing orders anything against them.
typedef struct
{
    pc_conn_event_cb conn_event_cb;
    _Atomic uint32_t ctr[8];
} ObsCtx;
static ObsCtx s_obs;

void pc_conn_on_event(pc_conn_event_cb cb)
{
    s_obs.conn_event_cb = cb;
}

pc_conn_counters pc_conn_counters_get(void)
{
    pc_conn_counters c;
    c.accepts = atomic_load_explicit(&s_obs.ctr[0], memory_order_relaxed);
    c.closes_remote = atomic_load_explicit(&s_obs.ctr[1], memory_order_relaxed);
    c.closes_local = atomic_load_explicit(&s_obs.ctr[2], memory_order_relaxed);
    c.closes_error = atomic_load_explicit(&s_obs.ctr[3], memory_order_relaxed);
    c.closes_timeout = atomic_load_explicit(&s_obs.ctr[4], memory_order_relaxed);
    c.closes_abort = atomic_load_explicit(&s_obs.ctr[5], memory_order_relaxed);
    c.backpressure = atomic_load_explicit(&s_obs.ctr[6], memory_order_relaxed);
    c.defer_drops = atomic_load_explicit(&s_obs.ctr[7], memory_order_relaxed);
    // Derive the live gauge from the actual pool so it cannot drift.
    c.closing_gauge = 0;
    for (int i = 0; i < MAX_CONNS; i++)
    {
        if (PROTO_ATOMIC_LOAD(&conn_pool[i].state) == CONN_CLOSING)
        {
            c.closing_gauge++;
        }
    }
    return c;
}

void pc_conn_counters_reset(void)
{
    for (int i = 0; i < 8; i++)
    {
        atomic_store_explicit(&s_obs.ctr[i], 0, memory_order_relaxed);
    }
}

static void obs_bump(pc_conn_reason reason)
{
    int idx = -1;
    // Every pc_conn_reason enumerator has a case and there is no default label, so the compiler's
    // implicit-default arm is unreachable for any valid enum value.
    switch (reason)
    {
    case PC_CONN_R_ACCEPT:
        idx = 0;
        break;
    case PC_CONN_R_CLOSE_REMOTE:
        idx = 1;
        break;
    case PC_CONN_R_CLOSE_LOCAL:
        idx = 2;
        break;
    case PC_CONN_R_ERROR:
        idx = 3;
        break;
    case PC_CONN_R_TIMEOUT:
        idx = 4;
        break;
    case PC_CONN_R_ABORT:
        idx = 5;
        break;
    case PC_CONN_R_BACKPRESSURE:
        idx = 6;
        break;
    case PC_CONN_R_DEFER_DROP:
        idx = 7;
        break;
    case PC_CONN_R_DRAINED:
        idx = -1; // the entering close reason was already counted; DRAINED is gauge-only
        break;
    }
    if (idx >= 0)
    {
        atomic_fetch_add_explicit(&s_obs.ctr[idx], 1, memory_order_relaxed);
    }
}

// A real state transition: bump the reason counter and fire the callback. The
// CONN_CLOSING gauge is derived on read (see pc_conn_counters), so there is no
// per-transition gauge bookkeeping to get wrong. Non-static so listener.cpp
// (accept) can notify through the PC_OBS_TRANSITION macro declared in tcp.h.
void pc_obs_transition(uint8_t slot, ConnState olds, ConnState news, pc_conn_reason reason)
{
    obs_bump(reason);
    if (s_obs.conn_event_cb != NULL)
    {
        s_obs.conn_event_cb(slot, olds, news, reason);
    }
}

// A non-transition notice (backpressure / defer-drop): bump + fire with old==new.
void pc_obs_notice(uint8_t slot, ConnState st, pc_conn_reason reason)
{
    obs_bump(reason);
    if (s_obs.conn_event_cb != NULL)
    {
        s_obs.conn_event_cb(slot, st, st, reason);
    }
}
#endif // PC_ENABLE_OBSERVABILITY

// ---------------------------------------------------------------------------
// Cross-thread TCP serialization
// ---------------------------------------------------------------------------
// The raw stack API is not thread-safe: its callbacks run in the stack's own task,
// while this library issues writes/closes from the main-loop task.
// Issuing one from the main loop concurrently with the stack processing
// an inbound segment corrupts the connection state - under a streaming upload (the peer
// is actively sending as the server responds/closes) it trips the stack's
// "wrong state" assert and panics.
//
// The portable fix is the stack's own marshaling call: it runs a
// function *inside* the stack's context and blocks the caller until it completes, so
// every main-loop-originated op executes in the one safe context. The stack's
// own callbacks already run in that context and must NOT marshal again (they
// issue their op directly).
// CONN_CLOSING dwell helpers (defined below, near pc_conn_begin_close). Forward
// declared so the in-context op dispatch can reach closing_check().
static void closing_check(uint8_t slot, pc_pcb *pcb);

#if PROTOCORE_HOT

typedef enum PROTO_ENUM_PACKED
{
    PC_OP_SEND,
    PC_OP_OUTPUT,
    PC_OP_CLOSE,
    PC_OP_ABORT,
    PC_OP_DETACH,
    PC_OP_RAWSEND,     // raw write of already-encrypted bytes (TLS BIO), no TLS re-entry
    PC_OP_CLOSE_CHECK, // in stack context: finalize a CONN_CLOSING slot if its TX has drained
    PC_OP_RECVED,      // in stack context: reopen the receive window (ack-on-consume)
#if PC_ENABLE_DIFFSERV
    PC_OP_SET_TOS // in stack context: set the DS field (DiffServ DSCP) on a live connection; k->len carries the byte
#endif
} pc_tcp_op;
static_assert(sizeof(pc_tcp_op) == 1, "pc_tcp_op must stay one byte (PROTO_ENUM_PACKED)");

// TCP transport context, owned by one instance (internal linkage): the stack task
// handle, captured the first time pc_tcp_do() runs (that op is always marshaled into stack context).
// pc_tcp_marshal() compares the running task against it, so a raw stack callback - which runs in
// that context but does NOT enter through pc_tcp_do, so a plain "inside pc_tcp_do" flag reads false
// there - performs its op inline instead of re-marshaling, which would block on the very mailbox the
// callback's thread services (self-deadlock: a close from the sent callback that sends a TLS
// close_notify, found on hardware with a real TLS 1.3 client). One named owner, unreachable cross-TU.
typedef struct
{
    pc_platform_task tcpip_task;
} TransportCtx;
static TransportCtx s_tp;

// True when the caller may run a raw stack op directly instead of marshaling. The stack has two
// threading models and the answer differs, so branch on which one the framework built:
static inline proto_bool on_tcpip_thread(void)
{
#if defined(LWIP_TCPIP_CORE_LOCKING) && LWIP_TCPIP_CORE_LOCKING
    // Core-locking (arduino-esp32 3.x / IDF 5.x): pc_net_call_marshal takes the core lock and runs the op
    // INLINE on the calling task - there is no dedicated tcpip thread to marshal to - so a direct lwIP
    // call is safe exactly when we already hold the core lock. Use lwIP's own holder query (the one
    // LWIP_ASSERT_CORE_LOCKED checks). A task-handle compare is meaningless here (every caller runs its
    // own pc_net_call_marshal inline, so the captured "tcpip task" is just whoever ran the first op), and a
    // false positive calls pc_net_write unlocked -> the "Required to lock TCPIP core functionality!" assert
    // (found running TLS on the PSRAM/IDF-5.5 core: the handshake's record flush crashed the device).
    return sys_thread_tcpip(LWIP_CORE_LOCK_QUERY_HOLDER);
#else
    // Mailbox (arduino-esp32 2.x / IDF 4.x): pc_net_call_marshal marshals to the single dedicated tcpip
    // thread, so a direct call is safe exactly when we run on that thread. Captured on the first
    // pc_tcp_do (boot / first send), which precedes every raw-callback teardown path.
    return s_tp.tcpip_task != NULL && pc_platform_task_self() == s_tp.tcpip_task;
#endif
}

typedef struct
{
    pc_net_call base;
    pc_tcp_op op;
    uint8_t slot;
    pc_pcb *pcb;
    const void *data;
    proto_u16 len;
    proto_bool flush;  ///< PC_OP_SEND: also flush after a successful write (coalesced write+flush)
    pc_net_err result; ///< outcome of the op (PC_OP_SEND: whether the write was queued)
} pc_tcp_call;

// True if @p pcb is still bound to a live connection slot. A marshalled send/output captures the
// pcb on the worker thread (from conn_pool[slot].pcb); by the time the op runs here the connection
// can have been torn down - teardown nulls conn_pool[slot].pcb on the worker and then frees the pcb
// in stack context (PC_OP_CLOSE/ABORT), and a remote RST frees it via the error callback. A
// write or flush on that freed pcb trips the stack's `invalid pcb` assert and panics
// the device (found by the pentest rig: oversized request line / connection saturation). Re-check
// against the pool here - we are in stack context, where teardown also runs, so the compare is
// race-free. The scan (not conn_pool[slot]) is correct for PC_OP_RAWSEND too, whose slot is 0.
static proto_bool pcb_still_bound(const pc_pcb *pcb)
{
    if (pcb == NULL)
    {
        return PROTO_FALSE;
    }
    for (uint8_t i = 0; i < CONN_POOL_SLOTS; i++)
    {
        if (conn_pool[i].pcb == pcb)
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

// Runs in stack context (via the stack's marshaling call). Performs the requested raw op
// in the one context where it is safe; TLS record I/O (which also reaches
// the write path through the BIO) is done here too.
static pc_net_err pc_tcp_do(pc_net_call *c)
{
    pc_tcp_call *k = (pc_tcp_call *)c;
    k->result = PC_NET_OK;
    if (s_tp.tcpip_task == NULL) // capture the stack task once; pc_tcp_do only ever runs in that thread
    {
        s_tp.tcpip_task = pc_platform_task_self();
    }
    switch (k->op)
    {
    case PC_OP_RAWSEND:
        // RAWSEND (TLS BIO) carries only the pcb, not its slot, so liveness needs a pool lookup. This
        // is the cool TLS handshake / read-pump path (not per-packet app data), and CONN_POOL_SLOTS is
        // small + compile-time so -O2 unrolls the scan (see docs/ROADMAP: unroll loops to bitmask).
        if (!pcb_still_bound(k->pcb)) // stale pcb (connection torn down between capture and now)
        {
            k->result = PC_NET_ERR_CLSD;
            break;
        }
        k->result = pc_net_write(k->pcb, k->data, k->len, PC_NET_WRITE_COPY);
        if (k->result == PC_NET_OK)
        {
            pc_net_output(k->pcb);
        }
        break;
    case PC_OP_SEND:
        // Hot path: SEND carries the real slot, so a stale pcb is just k->pcb != the slot's live pcb.
        // O(1), no scan - the send/flush pair runs on every HTTP response. The `k->pcb` null test is
        // essential: a torn-down slot has pcb == null, and comparing a captured-null against a live-null
        // (null == null) would otherwise pass the guard and pc_net_write(null).
        if (!k->pcb || k->pcb != conn_pool[k->slot].pcb)
        {
            k->result = PC_NET_ERR_CLSD; // connection torn down between capture and now; skip, do not assert
            break;
        }
#if PC_ENABLE_TLS
        if (conn_pool[k->slot].tls)
        {
            k->result = (pc_tls_write(k->slot, k->data, k->len) >= 0) ? PC_NET_OK : PC_NET_ERR_MEM;
            break;
        }
#endif
        k->result = pc_net_write(k->pcb, k->data, k->len, PC_NET_WRITE_COPY);
        if (k->flush && k->result == PC_NET_OK)
        {
            pc_net_output(k->pcb); // coalesced write+flush: one marshal for a terminal single-shot response
        }
        break;
    case PC_OP_OUTPUT:
        // Hot path (O(1)): flush only if the slot still owns a LIVE pcb; else it was torn down - skip
        // rather than flushing freed memory (the stack's "invalid pcb" assert -> panic). The `k->pcb`
        // null test is essential and was the missing piece here: pc_conn_flush() marshals this op with
        // conn_pool[slot].pcb, which is null for a torn-down slot, so a captured-null vs a live-null
        // (null == null) passed the guard and called pc_net_output(null) -> panic. Coredump-confirmed:
        // slot 0, k->pcb == 0, conn_pool[0].pcb == 0, state CONN_FREE.
        if (k->pcb && k->pcb == conn_pool[k->slot].pcb)
        {
            pc_net_output(k->pcb);
        }
        else
        {
            k->result = PC_NET_ERR_CLSD;
        }
        break;
    case PC_OP_CLOSE:
#if PC_ENABLE_TLS
        if (conn_pool[k->slot].tls)
        {
            pc_tls_conn_end(k->slot); // close_notify + free the TLS context
        }
#endif
        if (pc_net_close(k->pcb) != PC_NET_OK)
        {
            pc_net_abort(k->pcb);
        }
        break;
    case PC_OP_ABORT:
        pc_net_abort(k->pcb);
        break;
    case PC_OP_DETACH:
        pc_net_arg(k->pcb, NULL);
        break;
    case PC_OP_CLOSE_CHECK:
        closing_check(k->slot, k->pcb); // safe pcb access: we are in tcpip_thread
        break;
    case PC_OP_RECVED:
        // Same O(1) liveness guard as SEND/OUTPUT: the worker captured the pcb when it acked consumed
        // RX bytes, but the connection can be torn down (a remote RST frees the pcb via the error
        // callback) before this op runs. pc_net_recved on a freed pcb walks into pc_net_rcv_wnd_update
        // (assert new_rcv_ann_wnd <= 0xffff) and a window-update pc_net_output ("invalid pcb") - i.e. a
        // remotely-triggerable panic under connection churn. Skip if the slot no longer owns a live pcb
        // (the null test guards the captured-null vs live-null case, as in SEND/OUTPUT).
        if (k->pcb && k->pcb == conn_pool[k->slot].pcb)
        {
            pc_net_recved(k->pcb, k->len); // reopen the receive window by the consumed bytes
        }
        else
        {
            k->result = PC_NET_ERR_CLSD;
        }
        break;
#if PC_ENABLE_DIFFSERV
    case PC_OP_SET_TOS:
        // Per-connection DSCP (RFC 2474): stamp the DS field so this flow's outbound IP packets carry the
        // requested class. Same O(1) liveness guard as SEND - the slot can be torn down between the worker
        // capturing the pcb and this op running. k->len carries the ready-made TOS byte (DSCP << 2).
        if (k->pcb && k->pcb == conn_pool[k->slot].pcb)
        {
            k->pcb->tos = (uint8_t)k->len;
        }
        else
        {
            k->result = PC_NET_ERR_CLSD;
        }
        break;
#endif
    }
    return PC_NET_OK;
}

static inline pc_net_err pc_tcp_marshal(pc_tcp_op op, uint8_t slot, pc_pcb *pcb, const void *data, proto_u16 len,
                                        proto_bool flush)
{
    pc_tcp_call k;
    memset(&k, 0, sizeof(k));
    k.op = op;
    k.slot = slot;
    k.pcb = pcb;
    k.data = data;
    k.len = len;
    k.flush = flush;
    // In stack context already (a raw callback's teardown reaching a send/close): run the op inline.
    // Re-marshaling here would call into the mailbox from the very thread that services it and block
    // forever (the TLS close_notify-from-sent-callback self-deadlock). Off-thread (worker): marshal.
    if (on_tcpip_thread())
    {
        pc_tcp_do(&k.base);
    }
    else
    {
        pc_net_call_marshal(pc_tcp_do, &k.base);
    }
    return k.result;
}
#endif // PROTOCORE_HOT

TcpConn conn_pool[CONN_POOL_SLOTS];

// Owns the connection pool's own state (unconditional - the state transitions it tracks happen on
// both targets):
//
//   free_mask       bit i set = conn_pool[i] is CONN_FREE (available for a new accept). Kept in
//                   lock-step with every state write through the single pc_conn_set_state() choke
//                   point, so the accept free-slot lookup is one ctz instead of a MAX_CONNS scan
//                   (measured 20 vs 71 cyc on the S3). Atomic: CONN_FREE is written from the stack
//                   callbacks AND the worker (proto_tcp_check_timeouts).
//   conn_timeout_ms the idle deadline the sweep measures against, loaded at pool_init. It lives
//                   here because the sweep that reads it walks this pool; callers reach it through
//                   proto_tcp_conn_timeout_ms() rather than a bare global.
typedef struct
{
    _Atomic uint32_t free_mask;
    uint32_t conn_timeout_ms;
} ConnPoolCtx;
static ConnPoolCtx s_pool = {0, CONN_TIMEOUT_MS};

_Static_assert(MAX_CONNS <= 32, "the free-slot bitmask (s_pool.free_mask) is a uint32; raise it to uint64_t "
                                "or fall back to a scan if MAX_CONNS ever exceeds 32");

// The one place a conn_pool slot's lifecycle state is written. Keeps the free-slot bitmask (s_pool.free_mask)
// in lock-step with the atomic state: publish availability (set the bit) only AFTER the release store to
// CONN_FREE - the caller has already cleaned the slot - and reserve (clear the bit) BEFORE the store to any
// non-free state, so a concurrent allocator never picks a slot that is mid-claim. The bit ops are atomic
// because CONN_FREE is written from the stack callbacks and from the worker (the timeout sweep).
void pc_conn_set_state(uint8_t slot, ConnState st)
{
    // Bound every write to the real array size up front (CONN_POOL_SLOTS = MAX_CONNS + the reserved internal
    // slots), so the setter is memory-safe on its own rather than relying on the caller never over-indexing.
    if (slot >= CONN_POOL_SLOTS)
    {
        return;
    }

#if PC_INTERNAL_SLOTS > 0
    // Reserved internal slots (>= MAX_CONNS, e.g. PC_H3_DISPATCH_SLOT) are not part of the TCP accept pool
    // and are never handed out by the allocator, so they carry state but no bitmask bit. Compiled out when
    // no reserved slots exist (CONN_POOL_SLOTS == MAX_CONNS), where the test would be dead.
    if (slot >= MAX_CONNS)
    {
        PROTO_ATOMIC_STORE(&conn_pool[slot].state, st);
        return;
    }
#endif
    const uint32_t bit = 1u << slot;
    if (st == CONN_FREE)
    {
        PROTO_ATOMIC_STORE(&conn_pool[slot].state, st);
        atomic_fetch_or_explicit(&s_pool.free_mask, bit, memory_order_release);
    }
    else
    {
        atomic_fetch_and_explicit(&s_pool.free_mask, ~bit, memory_order_release);
        PROTO_ATOMIC_STORE(&conn_pool[slot].state, st);
    }
}

// First free slot as one ctz on the bitmask, rather than a MAX_CONNS linear scan. Returns -1 if the pool
// is full. Runs in stack context (accept); the acquire load pairs with the release stores above.
int32_t pc_conn_alloc_free(void)
{
    const uint32_t valid = (MAX_CONNS >= 32) ? 0xFFFFFFFFu : ((1u << MAX_CONNS) - 1u);
    uint32_t m = atomic_load_explicit(&s_pool.free_mask, memory_order_acquire) & valid;
    return m ? (int32_t)__builtin_ctz(m) : -1;
}

uint32_t pc_ap_ip = 0;

uint32_t proto_tcp_conn_timeout_ms(void)
{
    return s_pool.conn_timeout_ms;
}

// ---------------------------------------------------------------------------
// Connection output API
// ---------------------------------------------------------------------------
// The single send/flush/close path for every higher layer (HTTP app, WebSocket,
// SSE, SSH). Keeping it here means presentation and application code never call
// the stack directly - they hand bytes to the transport layer, which decides whether
// they go out as plaintext or through the TLS record layer. With
// PC_ENABLE_TLS off this is a bare write and flush.

proto_bool pc_conn_send(uint8_t slot, const void *data, proto_u16 len)
{
    // The write target is always the slot's own pcb (ingress reads resolve it the
    // same way) - callers no longer thread it through, so it cannot disagree.
#if PROTOCORE_HOT
    return pc_tcp_marshal(PC_OP_SEND, slot, conn_pool[slot].pcb, data, len, /*flush=*/PROTO_FALSE) ==
           PC_NET_OK; // the write runs in stack context
#else
#if PC_ENABLE_TLS
    if (conn_pool[slot].tls)
    {
        return pc_tls_write(slot, data, len) >= 0;
    }
#endif
    return pc_net_write(conn_pool[slot].pcb, data, len, PC_NET_WRITE_COPY) == PC_NET_OK;
#endif
}

proto_bool pc_conn_send_flush(uint8_t slot, const void *data, proto_u16 len)
{
    // Terminal single-shot write: the bytes AND their flush happen in one round-trip into stack
    // context, so a small response costs one marshal instead of the send()+flush() pair (each
    // a ~23 us marshal on-device). For a TLS slot this is identical to pc_conn_send: the record
    // BIO already pushes ciphertext per record, so there is no separate flush to fold in.
#if PROTOCORE_HOT
    return pc_tcp_marshal(PC_OP_SEND, slot, conn_pool[slot].pcb, data, len, /*flush=*/PROTO_TRUE) == PC_NET_OK;
#else
#if PC_ENABLE_TLS
    if (conn_pool[slot].tls)
    {
        return pc_tls_write(slot, data, len) >= 0; // TLS BIO already output the record
    }
#endif
    if (pc_net_write(conn_pool[slot].pcb, data, len, PC_NET_WRITE_COPY) != PC_NET_OK)
    {
        return PROTO_FALSE;
    }
    pc_net_output(conn_pool[slot].pcb);
    return PROTO_TRUE;
#endif
}

proto_u16 pc_conn_sndbuf(uint8_t slot)
{
    pc_pcb *pcb = conn_pool[slot].pcb;
    if (pcb == NULL)
    {
        return 0;
    }
    proto_u16 avail = pc_net_sndbuf(pcb);
#if PC_ENABLE_TLS
    // A TLS record adds header + MAC/tag overhead; report a conservative plaintext
    // budget so a caller that fills it does not overrun the cipher's framing.
    if (conn_pool[slot].tls)
    {
        avail = (avail > 64) ? (proto_u16)(avail - 64) : 0;
    }
#endif
    return avail;
}

void pc_conn_flush(uint8_t slot)
{
#if PC_ENABLE_TLS
    if (conn_pool[slot].tls)
    {
        return; // ciphertext was already pushed by the TLS BIO (a flush per record);
                // flush must NOT end the session - persistent TLS (wss / TLS SSE) reuses it
    }
#endif
#if PROTOCORE_HOT
    pc_tcp_marshal(PC_OP_OUTPUT, slot, conn_pool[slot].pcb, NULL, 0, /*flush=*/PROTO_FALSE);
#else
    pc_net_output(conn_pool[slot].pcb);
#endif
}

#if PC_ENABLE_DIFFSERV
static proto_bool set_dscp(uint8_t slot, uint8_t dscp)
{
    if (slot >= MAX_CONNS || conn_pool[slot].pcb == NULL)
    {
        return PROTO_FALSE;
    }
#if PROTOCORE_HOT
    // Marshalled into stack context (PC_OP_SET_TOS) - the stack reads the DS field while building
    // each outbound segment, so setting it from a worker task must not race it.
    return pc_tcp_marshal(PC_OP_SET_TOS, slot, conn_pool[slot].pcb, NULL, pc_dscp_to_tos(dscp),
                          /*flush=*/PROTO_FALSE) == PC_NET_OK;
#else
    conn_pool[slot].pcb->tos = pc_dscp_to_tos(dscp); // host: no separate stack thread, set directly
    return PROTO_TRUE;
#endif
}
#endif // PC_ENABLE_DIFFSERV

void pc_conn_ack_consumed(uint8_t slot)
{
    if (slot >= MAX_CONNS)
    {
        return;
    }
    TcpConn *c = &conn_pool[slot];
    // Only the owning worker calls this, so rx_tail/rx_acked are read race-free
    // here; rx_head (producer) is not touched. Ack nothing for a slot that is not
    // actively receiving (the CONN_CLOSING discard path ACKs its own bytes).
    if (PROTO_ATOMIC_LOAD(&c->state) != CONN_ACTIVE || c->pcb == NULL)
    {
        return;
    }
    size_t tail = PROTO_ATOMIC_LOAD(&c->rx_tail);
    size_t consumed = (tail + RX_BUF_SIZE - c->rx_acked) % RX_BUF_SIZE;
    if (consumed == 0)
    {
        return;
    }
    c->rx_acked = tail; // advance first: the marshaled window update is the slow part
#if PROTOCORE_HOT
    pc_tcp_marshal(PC_OP_RECVED, slot, c->pcb, NULL, (proto_u16)consumed, /*flush=*/PROTO_FALSE);
#else
    pc_net_recved(c->pcb, (proto_u16)consumed);
#endif
}

proto_bool pc_conn_raw_send(pc_pcb *pcb, const void *data, proto_u16 len)
{
    if (pcb == NULL)
    {
        return PROTO_FALSE;
    }
#if PROTOCORE_HOT
    // pc_tcp_marshal owns the context choice: it runs the raw write inline when already in stack
    // context (a TLS close_notify/alert emitted from inside a raw callback) and marshals it
    // from the worker task (the handshake / read pump), so the write neither races the stack
    // nor self-deadlocks on its mailbox. The RAWSEND op also re-checks the pcb is still bound.
    return pc_tcp_marshal(PC_OP_RAWSEND, 0, pcb, data, len, /*flush=*/PROTO_FALSE) == PC_NET_OK;
#else
    pc_net_err e = pc_net_write(pcb, data, len, PC_NET_WRITE_COPY);
    if (e == PC_NET_OK)
    {
        pc_net_output(pcb);
    }
    return e == PC_NET_OK;
#endif
}

void pc_conn_close(uint8_t slot)
{
    if (slot >= MAX_CONNS)
    {
        return;
    }
    TcpConn *c = &conn_pool[slot];
    pc_pcb *pcb = c->pcb;
    if (pcb == NULL)
    {
        return;
    }
    // The application-initiated close path (L4 primitive). Remote FIN, error, and
    // timeout closes are observed at their own sites, so this is uniquely "local".
    PC_OBS_TRANSITION(slot, CONN_ACTIVE, CONN_FREE, PC_CONN_R_CLOSE_LOCAL);
    // Detach the pcb and free the slot before the close, so a late callback for
    // this pcb finds a null arg and does nothing. The close itself targets the
    // captured pcb (PC_OP_CLOSE carries it), so nulling the slot first is safe.
    pc_conn_detach(pcb);
    pc_conn_set_state(c->id, CONN_FREE);
    c->pcb = NULL;
#if PROTOCORE_HOT
    pc_tcp_marshal(PC_OP_CLOSE, slot, pcb, NULL, 0, /*flush=*/PROTO_FALSE); // TLS teardown + FIN in stack context
#else
#if PC_ENABLE_TLS
    if (c->tls)
    {
        pc_tls_conn_end(slot);
    }
#endif
    if (pc_net_close(pcb) != PC_NET_OK)
    {
        pc_net_abort(pcb);
    }
#endif
}

void pc_conn_abort_slot(uint8_t slot)
{
    if (slot >= MAX_CONNS)
    {
        return;
    }
    TcpConn *c = &conn_pool[slot];
    pc_pcb *pcb = c->pcb;
    if (pcb == NULL)
    {
        return;
    }
    PC_OBS_TRANSITION(slot, CONN_ACTIVE, CONN_FREE, PC_CONN_R_ABORT);
#if PC_ENABLE_TLS
    if (c->tls)
    {
        pc_tls_conn_free(slot); // abrupt: free the per-conn TLS context, no close_notify
    }
#endif
    // Detach + free the slot before the RST, so a late callback finds a null arg.
    pc_conn_detach(pcb);
    pc_conn_set_state(c->id, CONN_FREE);
    c->pcb = NULL;
    pc_conn_abort(pcb);
}

void pc_conn_detach(pc_pcb *pcb)
{
    // Disassociate the slot from this pcb's stack callbacks before freeing the
    // slot, so any late callback for the pcb finds a null arg and does nothing.
#if PROTOCORE_HOT
    pc_tcp_marshal(PC_OP_DETACH, 0, pcb, NULL, 0, /*flush=*/PROTO_FALSE);
#else
    pc_net_arg(pcb, NULL);
#endif
}

void pc_conn_abort(pc_pcb *pcb)
{
    // Hard reset (RST) for a fatal condition - no graceful FIN.
#if PROTOCORE_HOT
    pc_tcp_marshal(PC_OP_ABORT, 0, pcb, NULL, 0, /*flush=*/PROTO_FALSE);
#else
    pc_net_abort(pcb);
#endif
}

// ---------------------------------------------------------------------------
// CONN_CLOSING dwell: a graceful close that holds the slot until the peer ACKs.
// ---------------------------------------------------------------------------
// These run in stack context (the sent callback, or the PC_OP_CLOSE_CHECK
// marshaled op), so they touch the control block directly - never marshal from here.

// Finalize a CONN_CLOSING slot: tear down the connection and free the slot.
static void closing_finalize(uint8_t slot, pc_pcb *pcb)
{
    TcpConn *c = &conn_pool[slot];
#if PC_ENABLE_TLS
    if (c->tls)
    {
        pc_tls_conn_end(slot); // close_notify + free the TLS context (in-thread)
    }
#endif
    pc_conn_set_state(c->id, CONN_FREE);
    c->pcb = NULL;
    if (pcb != NULL)
    {
        pc_net_arg(pcb, NULL);
        if (pc_net_close(pcb) != PC_NET_OK)
        {
            pc_net_abort(pcb);
        }
    }
    PC_OBS_TRANSITION(slot, CONN_CLOSING, CONN_FREE, PC_CONN_R_DRAINED);
}

// If the slot is CONN_CLOSING and its TX queue has drained (peer ACKed the whole
// response), finalize it now. Called only from stack context.
//
// The early-return guard below is unreachable: closing_check() has exactly two callers,
// pc_conn_begin_close()'s host path (which already validated slot < MAX_CONNS and just set
// state to CONN_CLOSING immediately before this call) and lowlevel_sent_cb() (which only
// calls here after checking the slot's state is CONN_CLOSING itself, with slot->id
// always a valid conn_pool index by construction). Both guarantee the condition is false.
static void closing_check(uint8_t slot, pc_pcb *pcb)
{
    if (slot >= MAX_CONNS || PROTO_ATOMIC_LOAD(&conn_pool[slot].state) != CONN_CLOSING)
    {
        return;
    }
    if (pcb == NULL || pcb->snd_queuelen == 0)
    {
        closing_finalize(slot, pcb);
    }
}

void pc_conn_begin_close(uint8_t slot_id)
{
    if (slot_id >= MAX_CONNS)
    {
        return;
    }
    TcpConn *c = &conn_pool[slot_id];
    if (PROTO_ATOMIC_LOAD(&c->state) != CONN_ACTIVE) // an error during the write may have freed it
    {
        return;
    }
    pc_pcb *pcb = c->pcb;
    c->last_activity_ms = pc_millis();      // start the CONN_CLOSING dwell clock
    pc_conn_set_state(c->id, CONN_CLOSING); // release store: the stack callbacks now see CLOSING
    PC_OBS_TRANSITION(slot_id, CONN_ACTIVE, CONN_CLOSING, PC_CONN_R_CLOSE_LOCAL);
    // Finalize immediately if the response already drained, else dwell until the
    // sent callback (or the CLOSING-timeout sweep) reclaims it. The control-block read
    // must happen in stack context, so marshal the check on device.
#if PROTOCORE_HOT
    pc_tcp_marshal(PC_OP_CLOSE_CHECK, slot_id, pcb, NULL, 0, /*flush=*/PROTO_FALSE);
#else
    closing_check(slot_id, pcb);
#endif
}

/**
 * @brief Non-blocking event enqueue helper.
 *
 * Forwards the event to the queue owned by the connection's listener.
 * The enqueue does not block: it returns immediately if the queue is full.
 * A full queue indicates the application is not calling server_tick() fast
 * enough; dropped events are recoverable via the idle-timeout sweep.
 */
static inline void enqueue(TcpConn *slot, const TcpEvt *evt)
{
    if (!Tcp.listener->enqueue(slot->listener_id, evt))
    {
        PC_OBS_NOTICE(slot->id, PROTO_ATOMIC_LOAD(&slot->state), PC_CONN_R_DEFER_DROP);
    }
}

void proto_tcp_pool_init(const WebServerConfig *cfg)
{
    s_pool.conn_timeout_ms = (cfg != NULL) ? cfg->conn_timeout_ms : CONN_TIMEOUT_MS;
    // Reset from a single zeroed template in BSS rather than a compound literal per slot: the
    // latter materializes a full sizeof(TcpConn) temporary on the caller's stack (the whole
    // rx_buffer[RX_BUF_SIZE]), which overflows the loop task's stack at begin() once RX_BUF_SIZE
    // is set large. The template lives in rodata and the copy runs before any listener is
    // accepting, so the non-atomic struct assignment over the _Atomic members races nothing.
    static const TcpConn blank = {0};
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = blank;
        conn_pool[i].id = (uint8_t)i;
        pc_conn_set_state((uint8_t)i, CONN_FREE);
    }
}

void proto_tcp_stop(void)
{
    // Abort all active connections - listener control blocks and queues are owned by
    // the listener layer and must be cleaned up via Tcp.listener->stop_all() first.
    for (int i = 0; i < MAX_CONNS; i++)
    {
        ConnState st = PROTO_ATOMIC_LOAD(&conn_pool[i].state);
        if ((st == CONN_ACTIVE || st == CONN_CLOSING) && conn_pool[i].pcb != NULL)
        {
            pc_pcb *pcb = conn_pool[i].pcb;
            pc_conn_set_state((uint8_t)i, CONN_FREE);
            conn_pool[i].pcb = NULL;
            pc_conn_detach(pcb); // marshaled detach + abort
            pc_conn_abort(pcb);
            PC_OBS_TRANSITION((uint8_t)i, st, CONN_FREE, PC_CONN_R_ABORT);
        }
        pc_conn_set_state((uint8_t)i, CONN_FREE);
        conn_pool[i].pcb = NULL;
    }
}

uint8_t pc_conn_active_count(void)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        if (PROTO_ATOMIC_LOAD(&conn_pool[i].state) == CONN_ACTIVE)
        {
            n++;
        }
    }
    return n;
}

uint32_t pc_conn_remote_ip(uint8_t slot)
{
#if PROTOCORE_HOT
    if (slot >= MAX_CONNS)
    {
        return 0;
    }
    TcpConn *conn = &conn_pool[slot];
    if (PROTO_ATOMIC_LOAD(&conn->state) == CONN_ACTIVE && conn->pcb != NULL)
    {
        return pc_net_ip4_u32(pc_net_ip_as_v4(&conn->pcb->remote_ip));
    }
#else
    (void)slot;
#endif
    return 0;
}

proto_bool pc_conn_remote_addr(uint8_t slot, pc_ip *out)
{
    if (out != NULL)
    {
        out->family = PC_IP_NONE;
    }
#if PROTOCORE_HOT
    if (out == NULL || slot >= MAX_CONNS)
    {
        return PROTO_FALSE;
    }
    TcpConn *conn = &conn_pool[slot];
    if (PROTO_ATOMIC_LOAD(&conn->state) != CONN_ACTIVE || conn->pcb == NULL)
    {
        return PROTO_FALSE;
    }
    NetAddr.to_ip(&conn->pcb->
#if PC_ENABLE_DIFFSERV
                   set_dscp,
#endif
                  remote_ip, out);
    return PROTO_TRUE;
#else
    (void)slot;
    return PROTO_FALSE;
#endif
}

// Refresh a slot's idle-timeout timestamp from the owning worker while a response body is still
// being paged out (the file/chunk send pumps call this every poll they run). Such a slot is
// actively streaming - or briefly blocked on a full send window / a transient link stall - NOT
// idle, so the CONN_TIMEOUT_MS idle sweep must not reap it mid-transfer: that truncates any body
// larger than one TCP window (seen on a multi-hundred-MB download as an rc=56 reset or a short
// read). Dead-peer teardown for an in-flight response stays owned by the stack's retransmission
// timers, which abort a black-holed connection through the error callback. Worker-context safe: it
// only writes our own last_activity_ms hint (the sent callback writes the same uint32 from stack
// context; a torn read of a timestamp is benign).
void pc_conn_touch_active(uint8_t slot_id)
{
    if (slot_id >= MAX_CONNS)
    {
        return;
    }
    TcpConn *c = &conn_pool[slot_id];
    if (PROTO_ATOMIC_LOAD(&c->state) == CONN_ACTIVE)
    {
        c->last_activity_ms = pc_millis();
    }
}

void proto_tcp_check_timeouts(int worker_id)
{
    uint32_t now = pc_millis();
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TcpConn *slot = &conn_pool[i];
        if (slot->owner != worker_id) // each worker reaps only its own slots
        {
            continue;
        }

        // CONN_CLOSING safety net: a graceful close whose peer never ACKs would
        // dwell forever. After PC_CLOSING_TIMEOUT_MS, force it free so the
        // fixed pool cannot leak. (The fast path is the sent callback finalizing
        // on ACK; this only catches a black-holed peer.)
        if (PROTO_ATOMIC_LOAD(&slot->state) == CONN_CLOSING)
        {
            if ((now - slot->last_activity_ms) < PC_CLOSING_TIMEOUT_MS)
            {
                continue;
            }
            pc_pcb *cpcb = slot->pcb;
            pc_conn_set_state(slot->id, CONN_FREE);
            slot->pcb = NULL;
            if (cpcb != NULL)
            {
                pc_conn_detach(cpcb);
                pc_conn_abort(cpcb);
            }
            PC_OBS_TRANSITION((uint8_t)i, CONN_CLOSING, CONN_FREE, PC_CONN_R_DRAINED);
            continue;
        }

        if (PROTO_ATOMIC_LOAD(&slot->state) != CONN_ACTIVE)
        {
            continue;
        }
        if ((now - slot->last_activity_ms) < s_pool.conn_timeout_ms)
        {
            continue;
        }

        pc_pcb *pcb = slot->pcb;
        /*
         * Clear state BEFORE aborting so that any stack callback
         * firing on the same connection during or after the abort sees CONN_FREE
         * and exits immediately without accessing freed memory.
         */
        pc_conn_set_state(slot->id, CONN_FREE);
        slot->pcb = NULL;
        if (pcb != NULL)
        {
            pc_conn_detach(pcb); // marshaled detach + abort
            pc_conn_abort(pcb);
        }
        PC_OBS_TRANSITION((uint8_t)i, CONN_ACTIVE, CONN_FREE, PC_CONN_R_TIMEOUT);
        TcpEvt evt = {EVT_ERROR, (uint8_t)i, 0};
        enqueue(slot, &evt);
    }
}

// ---------------------------------------------------------------------------
// Stack callbacks - execute in the stack's own task context
// These are non-static so listener.cpp can take their address.
// ---------------------------------------------------------------------------

/**
 * @brief Receive callback - fires when data arrives on a connection.
 *
 * Copies the received chain into the ring buffer; the window is reopened later by
 * pc_conn_ack_consumed() as the worker drains (ack-on-consume), not here. If the
 * whole segment will not fit it is refused for lossless backpressure.
 * A null segment signals graceful remote close (FIN received).
 */
pc_net_err lowlevel_recv_cb(void *arg, pc_pcb *tpcb, pc_pbuf *p, pc_net_err err)
{
    TcpConn *slot = (TcpConn *)arg;
    if (slot == NULL)
    {
        return PC_NET_ERR_VAL;
    }

    // While dwelling in CONN_CLOSING we have already sent our final response and
    // are waiting for the ACK. Drain (and ACK) anything the peer still sends so
    // the window keeps moving, but do not process it. A peer FIN here just means
    // both sides are done - finalize on the next sent/timeout.
    if (PROTO_ATOMIC_LOAD(&slot->state) == CONN_CLOSING)
    {
        if (p != NULL)
        {
            pc_net_recved(tpcb, p->tot_len);
            pc_net_pbuf_free(p);
        }
        return PC_NET_OK;
    }

    if (PROTO_ATOMIC_LOAD(&slot->state) != CONN_ACTIVE)
    {
        return PC_NET_ERR_VAL;
    }

    if (p == NULL)
    {
        /*
         * A null segment signals graceful remote close (FIN received).
         * Clear state and pcb before closing so any stale callbacks
         * are harmless.
         */
        pc_conn_set_state(slot->id, CONN_FREE);
        slot->pcb = NULL;
        pc_net_arg(tpcb, NULL);
        if (pc_net_close(tpcb) != PC_NET_OK)
        {
            pc_net_abort(tpcb);
        }
        PC_OBS_TRANSITION(slot->id, CONN_ACTIVE, CONN_FREE, PC_CONN_R_CLOSE_REMOTE);
        TcpEvt evt = {EVT_DISCONNECT, slot->id, 0};
        enqueue(slot, &evt);
        return PC_NET_OK;
    }

    /*
     * Backpressure without data loss: if the whole segment will not fit in the
     * free ring space, refuse it without taking ownership so the stack retains
     * it and redelivers once the application has drained the
     * ring; nudge the main loop to drain. Copying only what fits and
     * dropping the rest silently corrupts bodies larger than the ring (e.g.
     * streamed uploads). NOTE: needs RX_BUF_SIZE > the largest incoming segment
     * (TCP_MSS) so a full segment can eventually fit; smaller rings only ever see
     * sub-MSS requests, which always fit.
     */
    if (p->tot_len > pc_ring_free(&slot->rx_head, &slot->rx_tail, RX_BUF_SIZE))
    {
        PC_OBS_NOTICE(slot->id, CONN_ACTIVE, PC_CONN_R_BACKPRESSURE);
        TcpEvt evt = {EVT_DATA, slot->id, 0}; // wake the loop so it drains the ring
        enqueue(slot, &evt);
        // Do NOT refresh the idle timer here: a refused segment is redelivered by the stack every
        // retransmit until the ring drains, so refreshing on refusal keeps a backpressure-stuck
        // connection alive forever (idle sweep never reaps it -> slot leak / pool-exhaustion DoS,
        // e.g. an oversized request line that fills the ring and never completes). The timer is
        // refreshed below only when data is actually ACCEPTED (real progress), so a connection
        // that makes no progress times out and is reaped.
        return PC_NET_ERR_MEM; // do NOT free the segment: the stack keeps it and redelivers
    }

    uint32_t rx_now = pc_millis();
    slot->last_activity_ms = rx_now; // accepted data = progress: refresh the idle timer
    if (slot->req_start_ms == 0)     // first byte of a new request: arm the completion deadline (slow-loris)
    {
        slot->req_start_ms = rx_now ? rx_now : 1;
    }

    // Move the segment into the ring via the shared producer primitive: a
    // contiguous span per chain link (two across the wrap), advancing a LOCAL head and
    // publishing rx_head once at the end (one release store for the whole segment).
    // The free-space check above guarantees it fits, so head can never overrun tail.
    size_t head = PROTO_ATOMIC_LOAD(&slot->rx_head); // sole producer of head; one acquire load
    for (pc_pbuf *q = p; q != NULL; q = q->next)
    {
        head = pc_ring_write_span(slot->rx_buffer, RX_BUF_SIZE, head, (const uint8_t *)q->payload, q->len);
    }
    PROTO_ATOMIC_STORE(&slot->rx_head, head); // one release store: publishes the whole segment at once
    size_t bytes_copied = p->tot_len;         // the whole segment fit (checked above)

    // Do NOT reopen the window here: that is pc_conn_ack_consumed()'s job
    // as the worker drains the ring (ack-on-consume), so the advertised window
    // tracks ring occupancy and a slow consumer cannot overflow the ring. ACKing
    // on copy decouples the window from drainage and deadlocks streamed uploads
    // once RX_BUF_SIZE < TCP_WND (the refused segment past one ring-full stalls).
    pc_net_pbuf_free(p);

    if (bytes_copied > 0)
    {
        TcpEvt evt = {EVT_DATA, slot->id, bytes_copied};
        enqueue(slot, &evt);
    }

    return PC_NET_OK;
}

/**
 * @brief Sent callback - fires after the stack acknowledges sent bytes.
 *
 * Refreshes the idle-timeout timestamp so an active sender is not reaped while its
 * responses are in flight, and - for a slot dwelling in CONN_CLOSING - finalizes
 * the close once the response has fully drained (the peer ACKed everything).
 */
pc_net_err lowlevel_sent_cb(void *arg, pc_pcb *tpcb, proto_u16 len)
{
    TcpConn *slot = (TcpConn *)arg;
    if (slot != NULL)
    {
        slot->last_activity_ms = pc_millis();
        if (PROTO_ATOMIC_LOAD(&slot->state) == CONN_CLOSING)
        {
            closing_check(slot->id, tpcb); // drained? -> tear down + free the slot
        }
#if PROTOCORE_HOT
        // The send window just freed: wake the owning worker so a paced response
        // (e.g. a large file) resumes now rather than on the next idle sweep.
        else
        {
            pc_worker_wake(slot->owner);
        }
#endif
    }
    (void)len;
    return PC_NET_OK;
}

/**
 * @brief Error callback - fires when the stack detects a fatal error.
 *
 * By the time this fires the control block is already gone internally, so we must NOT
 * close or abort it.  Null out the slot's pointer and post
 * EVT_ERROR so the session layer resets the protocol state.
 */
void lowlevel_err_cb(void *arg, pc_net_err err)
{
    TcpConn *slot = (TcpConn *)arg;
    if (slot == NULL)
    {
        return;
    }

    /*
     * When the error callback fires the control block has already been freed
     * internally.  We must NOT close or abort here - just null
     * out our pointer to prevent any future access.
     */
    ConnState old = PROTO_ATOMIC_LOAD(&slot->state);
    pc_conn_set_state(slot->id, CONN_FREE);
    slot->pcb = NULL;

    // A slot that errored while dwelling in CONN_CLOSING is already done from the
    // session's view (its response was sent and the protocol state reset). Just
    // release the slot + the CLOSING gauge; do not re-post a close event.
    if (old == CONN_CLOSING)
    {
        PC_OBS_TRANSITION(slot->id, CONN_CLOSING, CONN_FREE, PC_CONN_R_DRAINED);
        (void)err;
        return;
    }

    PC_OBS_TRANSITION(slot->id, CONN_ACTIVE, CONN_FREE, PC_CONN_R_ERROR);
    TcpEvt evt = {EVT_ERROR, slot->id, 0};
    enqueue(slot, &evt);
    (void)err;
}

const ConnPoolNs ConnPool = {proto_tcp_pool_init,
                             proto_tcp_stop,
                             proto_tcp_check_timeouts,
                             proto_tcp_conn_timeout_ms,
                             pc_conn_set_state,
                             pc_conn_send,
                             pc_conn_send_flush,
                             pc_conn_flush,
                             pc_conn_touch_active,
                             pc_conn_ack_consumed,
                             pc_conn_active_count,
                             pc_conn_raw_send,
                             pc_conn_close,
                             pc_conn_begin_close,
                             pc_conn_detach,
                             pc_conn_abort,
                             pc_conn_abort_slot,
#if PC_ENABLE_DIFFSERV
                             set_dscp,
#endif
                             pc_conn_remote_ip,
                             pc_conn_remote_addr,
#if PC_ENABLE_OBSERVABILITY
                             pc_conn_on_event,
                             pc_conn_counters_get,
                             pc_conn_counters_reset
#endif
};
