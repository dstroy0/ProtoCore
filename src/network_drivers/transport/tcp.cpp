// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tcp.cpp
 * @brief Layer 4 (Transport) - TCP connection management implementation.
 *
 * All lwIP raw-API callbacks run in the `tcpip_thread` FreeRTOS task context.
 * They are NOT hardware ISRs, which is why we use `xQueueSend()` (non-ISR
 * variant) with timeout=0 rather than `xQueueSendFromISR()`.
 *
 * **Ring buffer write ordering**
 * The producer (recv callback) writes payload bytes into `rx_buffer[]` and
 * then advances `rx_head`.  The consumer (worker) reads from `rx_buffer[]` at
 * `rx_tail` and advances `rx_tail`.  `rx_head`/`rx_tail` are `pc_atomic`
 * (acquire/release): the producer's buffer writes are published by the release
 * store of `rx_head` and observed by the consumer's acquire load, correct on
 * either core. The ring math itself is the shared `ring.h` primitive.
 *
 * **Listener coupling**
 * Each TcpConn carries `listener_id` (set at accept time by listener_accept_cb
 * in listener.cpp).  The `enqueue()` helper forwards events to
 * `listener_enqueue()` - defined in listener.cpp - so tcp.cpp needs no
 * direct knowledge of the Listener struct.  `listener_enqueue()` is forward-
 * declared in tcp.h to avoid a circular include.
 */

#include "tcp.h"
#include "diffserv.h" // DiffServ DSCP marking (pc_dscp_to_tos, pc_conn_set_dscp); compiles out when off
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h" // xTaskGetCurrentTaskHandle() - tcpip_thread self-detection for pc_tcp_marshal
#include "lwip/tcp.h"
#include "services/system/clock.h" // pc_millis() pluggable monotonic clock

#ifdef ARDUINO
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
#if defined(ARDUINO)
#include "lwip/priv/tcpip_priv.h"
#include <string.h>
#endif
#if PC_ENABLE_OBSERVABILITY
#include <atomic>

// All connection-observability state, owned by one instance (internal linkage): the event
// callback and the cumulative per-reason counters (indexed 0..7). The live ConnState::CONN_CLOSING gauge
// is not a counter - it is derived on read by scanning the pool, so it can never drift out of
// sync with the actual slot states. One named owner, unreachable from any other TU.
struct ObsCtx
{
    pc_conn_event_cb conn_event_cb = nullptr;
    std::atomic<uint32_t> ctr[8];
};
static ObsCtx s_obs;

void pc_conn_on_event(pc_conn_event_cb cb)
{
    s_obs.conn_event_cb = cb;
}

pc_conn_counters pc_conn_counters_get()
{
    pc_conn_counters c;
    c.accepts = s_obs.ctr[0].load(std::memory_order_relaxed);
    c.closes_remote = s_obs.ctr[1].load(std::memory_order_relaxed);
    c.closes_local = s_obs.ctr[2].load(std::memory_order_relaxed);
    c.closes_error = s_obs.ctr[3].load(std::memory_order_relaxed);
    c.closes_timeout = s_obs.ctr[4].load(std::memory_order_relaxed);
    c.closes_abort = s_obs.ctr[5].load(std::memory_order_relaxed);
    c.backpressure = s_obs.ctr[6].load(std::memory_order_relaxed);
    c.defer_drops = s_obs.ctr[7].load(std::memory_order_relaxed);
    // Derive the live gauge from the actual pool so it cannot drift.
    c.closing_gauge = 0;
    for (int i = 0; i < MAX_CONNS; i++)
    {
        if (conn_pool[i].state == ConnState::CONN_CLOSING)
        {
            c.closing_gauge++;
        }
    }
    return c;
}

void pc_conn_counters_reset()
{
    for (int i = 0; i < 8; i++)
    {
        s_obs.ctr[i].store(0, std::memory_order_relaxed);
    }
}

static void obs_bump(pc_conn_reason reason)
{
    int idx = -1;
    // Every pc_conn_reason enumerator has a case and there is no default label, so the compiler's
    // implicit-default arm is unreachable for any valid enum value.
    switch (reason) // GCOVR_EXCL_BR_LINE
    {
    case pc_conn_reason::PC_CONN_R_ACCEPT:
        idx = 0;
        break;
    case pc_conn_reason::PC_CONN_R_CLOSE_REMOTE:
        idx = 1;
        break;
    case pc_conn_reason::PC_CONN_R_CLOSE_LOCAL:
        idx = 2;
        break;
    case pc_conn_reason::PC_CONN_R_ERROR:
        idx = 3;
        break;
    case pc_conn_reason::PC_CONN_R_TIMEOUT:
        idx = 4;
        break;
    case pc_conn_reason::PC_CONN_R_ABORT:
        idx = 5;
        break;
    case pc_conn_reason::PC_CONN_R_BACKPRESSURE:
        idx = 6;
        break;
    case pc_conn_reason::PC_CONN_R_DEFER_DROP:
        idx = 7;
        break;
    case pc_conn_reason::PC_CONN_R_DRAINED:
        idx = -1; // the entering close reason was already counted; DRAINED is gauge-only
        break;
    }
    if (idx >= 0)
    {
        s_obs.ctr[idx].fetch_add(1, std::memory_order_relaxed);
    }
}

// A real state transition: bump the reason counter and fire the callback. The
// ConnState::CONN_CLOSING gauge is derived on read (see pc_conn_counters), so there is no
// per-transition gauge bookkeeping to get wrong. Non-static so listener.cpp
// (accept) can notify through the PC_OBS_TRANSITION macro declared in tcp.h.
void pc_obs_transition(uint8_t slot, ConnState olds, ConnState news, pc_conn_reason reason)
{
    obs_bump(reason);
    if (s_obs.conn_event_cb)
    {
        s_obs.conn_event_cb(slot, olds, news, reason);
    }
}

// A non-transition notice (backpressure / defer-drop): bump + fire with old==new.
void pc_obs_notice(uint8_t slot, ConnState st, pc_conn_reason reason)
{
    obs_bump(reason);
    if (s_obs.conn_event_cb)
    {
        s_obs.conn_event_cb(slot, st, st, reason);
    }
}
#endif // PC_ENABLE_OBSERVABILITY

// ---------------------------------------------------------------------------
// Cross-thread TCP serialization
// ---------------------------------------------------------------------------
// The raw lwIP API is not thread-safe: its callbacks run in the tcpip_thread
// task, while this library issues writes/closes from the Arduino main-loop task.
// Calling tcp_*() from the main loop concurrently with tcpip_thread processing
// an inbound segment corrupts the PCB state - under a streaming upload (the peer
// is actively sending as the server responds/closes) it trips lwIP's
// "tcp_receive: wrong state" assert and panics.
//
// Arduino-esp32 ships lwIP with LWIP_TCPIP_CORE_LOCKING disabled, so
// LOCK_TCPIP_CORE() is a no-op. The portable fix is tcpip_api_call(): it runs a
// function *inside* tcpip_thread and blocks the caller until it completes, so
// every main-loop-originated tcp_*() executes in the one safe context. lwIP's
// own callbacks already run in that context and must NOT marshal again (they
// call tcp_*() directly).
// ConnState::CONN_CLOSING dwell helpers (defined below, near pc_conn_begin_close). Forward
// declared so the tcpip-thread op dispatch can reach closing_check().
static void closing_check(uint8_t slot, struct tcp_pcb *pcb);

#if defined(ARDUINO)

enum class pc_tcp_op : uint8_t
{
    PC_OP_SEND,
    PC_OP_OUTPUT,
    PC_OP_CLOSE,
    PC_OP_ABORT,
    PC_OP_DETACH,
    PC_OP_RAWSEND,     // raw tcp_write of already-encrypted bytes (TLS BIO), no TLS re-entry
    PC_OP_CLOSE_CHECK, // in tcpip_thread: finalize a ConnState::CONN_CLOSING slot if its TX has drained
    PC_OP_RECVED,      // in tcpip_thread: tcp_recved() to reopen the window (ack-on-consume)
#if PC_ENABLE_DIFFSERV
    PC_OP_SET_TOS // in tcpip_thread: set pcb->tos (DiffServ DSCP) on a live connection; k->len carries the byte
#endif
};

// TCP transport context, owned by one instance (internal linkage): the tcpip_thread FreeRTOS task
// handle, captured the first time pc_tcp_do() runs (that op is always marshaled onto tcpip_thread).
// pc_tcp_marshal() compares the running task against it, so a raw lwIP callback - which runs in
// tcpip_thread but does NOT enter through pc_tcp_do, so a plain "inside pc_tcp_do" flag reads false
// there - performs its op inline instead of re-marshaling, which would block on the very mailbox the
// callback's thread services (self-deadlock: a close from the sent callback that sends a TLS
// close_notify, found on hardware with a real TLS 1.3 client). One named owner, unreachable cross-TU.
struct TransportCtx
{
    TaskHandle_t tcpip_task = nullptr;
};
static TransportCtx s_tp;

// True when the caller may run a raw lwIP op directly instead of marshaling. lwIP has two threading
// models and the answer differs, so branch on which one the framework built:
static inline bool on_tcpip_thread()
{
#if defined(LWIP_TCPIP_CORE_LOCKING) && LWIP_TCPIP_CORE_LOCKING
    // Core-locking (arduino-esp32 3.x / IDF 5.x): tcpip_api_call takes the core lock and runs the op
    // INLINE on the calling task - there is no dedicated tcpip thread to marshal to - so a direct lwIP
    // call is safe exactly when we already hold the core lock. Use lwIP's own holder query (the one
    // LWIP_ASSERT_CORE_LOCKED checks). A task-handle compare is meaningless here (every caller runs its
    // own tcpip_api_call inline, so the captured "tcpip task" is just whoever ran the first op), and a
    // false positive calls tcp_write unlocked -> the "Required to lock TCPIP core functionality!" assert
    // (found running TLS on the PSRAM/IDF-5.5 core: the handshake's record flush crashed the device).
    return sys_thread_tcpip(LWIP_CORE_LOCK_QUERY_HOLDER);
#else
    // Mailbox (arduino-esp32 2.x / IDF 4.x): tcpip_api_call marshals to the single dedicated tcpip
    // thread, so a direct call is safe exactly when we run on that thread. Captured on the first
    // pc_tcp_do (boot / first send), which precedes every raw-callback teardown path.
    return s_tp.tcpip_task != nullptr && xTaskGetCurrentTaskHandle() == s_tp.tcpip_task;
#endif
}

struct pc_tcp_call
{
    struct tcpip_api_call_data base;
    pc_tcp_op op;
    uint8_t slot;
    struct tcp_pcb *pcb;
    const void *data;
    u16_t len;
    bool flush;   ///< pc_tcp_op::PC_OP_SEND: also tcp_output() after a successful write (coalesced write+flush)
    err_t result; ///< outcome of the op (pc_tcp_op::PC_OP_SEND: whether the write was queued)
};

// True if @p pcb is still bound to a live connection slot. A marshalled send/output captures the
// pcb on the worker thread (from conn_pool[slot].pcb); by the time the op runs here the connection
// can have been torn down - teardown nulls conn_pool[slot].pcb on the worker and then frees the pcb
// on tcpip_thread (PC_OP_CLOSE/ABORT), and a remote RST frees it via the error callback. A
// tcp_write/tcp_output on that freed pcb trips lwIP's `tcp_output: invalid pcb` assert and panics
// the device (found by the pentest rig: oversized request line / connection saturation). Re-check
// against the pool here - we are in tcpip_thread, where teardown also runs, so the compare is
// race-free. The scan (not conn_pool[slot]) is correct for PC_OP_RAWSEND too, whose slot is 0.
static bool pcb_still_bound(const struct tcp_pcb *pcb)
{
    if (!pcb)
    {
        return false;
    }
    for (uint8_t i = 0; i < CONN_POOL_SLOTS; i++)
    {
        if (conn_pool[i].pcb == pcb)
        {
            return true;
        }
    }
    return false;
}

// Runs in tcpip_thread (via tcpip_api_call). Performs the requested raw lwIP op
// in the one context where it is safe; TLS record I/O (which also reaches
// tcp_write through the BIO) is done here too.
static err_t pc_tcp_do(struct tcpip_api_call_data *c)
{
    pc_tcp_call *k = (pc_tcp_call *)c;
    k->result = ERR_OK;
    if (!s_tp.tcpip_task) // capture the tcpip_thread task once; pc_tcp_do only ever runs in that thread
    {
        s_tp.tcpip_task = xTaskGetCurrentTaskHandle();
    }
    switch (k->op)
    {
    case pc_tcp_op::PC_OP_RAWSEND:
        // RAWSEND (TLS BIO) carries only the pcb, not its slot, so liveness needs a pool lookup. This
        // is the cool TLS handshake / read-pump path (not per-packet app data), and CONN_POOL_SLOTS is
        // small + compile-time so -O2 unrolls the scan (see docs/ROADMAP: unroll loops to bitmask).
        if (!pcb_still_bound(k->pcb)) // stale pcb (connection torn down between capture and now)
        {
            k->result = ERR_CLSD;
            break;
        }
        k->result = tcp_write(k->pcb, k->data, k->len, TCP_WRITE_FLAG_COPY);
        if (k->result == ERR_OK)
        {
            tcp_output(k->pcb);
        }
        break;
    case pc_tcp_op::PC_OP_SEND:
        // Hot path: SEND carries the real slot, so a stale pcb is just k->pcb != the slot's live pcb.
        // O(1), no scan - the send/flush pair runs on every HTTP response. The `k->pcb` null test is
        // essential: a torn-down slot has pcb == null, and comparing a captured-null against a live-null
        // (null == null) would otherwise pass the guard and tcp_write(null).
        if (!k->pcb || k->pcb != conn_pool[k->slot].pcb)
        {
            k->result = ERR_CLSD; // connection torn down between capture and now; skip, do not assert
            break;
        }
#if PC_ENABLE_TLS
        if (conn_pool[k->slot].tls)
        {
            k->result = (pc_tls_write(k->slot, k->data, k->len) >= 0) ? ERR_OK : ERR_MEM;
            break;
        }
#endif
        k->result = tcp_write(k->pcb, k->data, k->len, TCP_WRITE_FLAG_COPY);
        if (k->flush && k->result == ERR_OK)
        {
            tcp_output(k->pcb); // coalesced write+flush: one marshal for a terminal single-shot response
        }
        break;
    case pc_tcp_op::PC_OP_OUTPUT:
        // Hot path (O(1)): flush only if the slot still owns a LIVE pcb; else it was torn down - skip
        // rather than tcp_output on freed memory (lwIP's "invalid pcb" assert -> panic). The `k->pcb`
        // null test is essential and was the missing piece here: pc_conn_flush() marshals this op with
        // conn_pool[slot].pcb, which is null for a torn-down slot, so a captured-null vs a live-null
        // (null == null) passed the guard and called tcp_output(null) -> panic. Coredump-confirmed:
        // slot 0, k->pcb == 0, conn_pool[0].pcb == 0, state CONN_FREE.
        if (k->pcb && k->pcb == conn_pool[k->slot].pcb)
        {
            tcp_output(k->pcb);
        }
        else
        {
            k->result = ERR_CLSD;
        }
        break;
    case pc_tcp_op::PC_OP_CLOSE:
#if PC_ENABLE_TLS
        if (conn_pool[k->slot].tls)
        {
            pc_tls_conn_end(k->slot); // close_notify + free the TLS context
        }
#endif
        if (tcp_close(k->pcb) != ERR_OK)
        {
            tcp_abort(k->pcb);
        }
        break;
    case pc_tcp_op::PC_OP_ABORT:
        tcp_abort(k->pcb);
        break;
    case pc_tcp_op::PC_OP_DETACH:
        tcp_arg(k->pcb, nullptr);
        break;
    case pc_tcp_op::PC_OP_CLOSE_CHECK:
        closing_check(k->slot, k->pcb); // safe pcb access: we are in tcpip_thread
        break;
    case pc_tcp_op::PC_OP_RECVED:
        // Same O(1) liveness guard as SEND/OUTPUT: the worker captured the pcb when it acked consumed
        // RX bytes, but the connection can be torn down (a remote RST frees the pcb via the error
        // callback) before this op runs. tcp_recved on a freed pcb walks into tcp_update_rcv_ann_wnd
        // (assert new_rcv_ann_wnd <= 0xffff) and a window-update tcp_output ("invalid pcb") - i.e. a
        // remotely-triggerable panic under connection churn. Skip if the slot no longer owns a live pcb
        // (the null test guards the captured-null vs live-null case, as in SEND/OUTPUT).
        if (k->pcb && k->pcb == conn_pool[k->slot].pcb)
        {
            tcp_recved(k->pcb, k->len); // reopen the receive window by the consumed bytes
        }
        else
        {
            k->result = ERR_CLSD;
        }
        break;
#if PC_ENABLE_DIFFSERV
    case pc_tcp_op::PC_OP_SET_TOS:
        // Per-connection DSCP (RFC 2474): stamp the DS field so this flow's outbound IP packets carry the
        // requested class. Same O(1) liveness guard as SEND - the slot can be torn down between the worker
        // capturing the pcb and this op running. k->len carries the ready-made TOS byte (DSCP << 2).
        if (k->pcb && k->pcb == conn_pool[k->slot].pcb)
        {
            k->pcb->tos = (uint8_t)k->len;
        }
        else
        {
            k->result = ERR_CLSD;
        }
        break;
#endif
    }
    return ERR_OK;
}

static inline err_t pc_tcp_marshal(pc_tcp_op op, uint8_t slot, struct tcp_pcb *pcb, const void *data, u16_t len,
                                   bool flush = false)
{
    pc_tcp_call k;
    memset(&k, 0, sizeof(k));
    k.op = op;
    k.slot = slot;
    k.pcb = pcb;
    k.data = data;
    k.len = len;
    k.flush = flush;
    // On tcpip_thread already (a raw lwIP callback's teardown reaching a send/close): run the op inline.
    // Re-marshaling here would call tcpip_api_call from the thread that services its mailbox and block
    // forever (the TLS close_notify-from-sent-callback self-deadlock). Off-thread (worker): marshal.
    if (on_tcpip_thread())
    {
        pc_tcp_do(&k.base);
    }
    else
    {
        tcpip_api_call(pc_tcp_do, &k.base);
    }
    return k.result;
}
#endif // ARDUINO

TcpConn conn_pool[CONN_POOL_SLOTS];

// Owns the live-slot bitmask (unconditional - the state transitions it tracks happen on both targets).
// bit i set = conn_pool[i] is CONN_FREE (available for a new accept). Kept in lock-step with every state
// write through the single pc_conn_set_state() choke point, so the accept free-slot lookup is one ctz
// instead of a MAX_CONNS scan (measured 20 vs 71 cyc on the S3). Atomic: CONN_FREE is written from the
// tcpip callbacks AND the worker (check_timeouts).
struct ConnPoolCtx
{
    std::atomic<uint32_t> free_mask{0};
};
static ConnPoolCtx s_pool;

static_assert(MAX_CONNS <= 32, "the free-slot bitmask (s_pool.free_mask) is a uint32; raise it to uint64_t "
                               "or fall back to a scan if MAX_CONNS ever exceeds 32");

// The one place a conn_pool slot's lifecycle state is written. Keeps the free-slot bitmask (s_pool.free_mask)
// in lock-step with the atomic state: publish availability (set the bit) only AFTER the release store to
// CONN_FREE - the caller has already cleaned the slot - and reserve (clear the bit) BEFORE the store to any
// non-free state, so a concurrent allocator never picks a slot that is mid-claim. The bit ops are atomic
// because CONN_FREE is written from tcpip callbacks and from the worker (check_timeouts).
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
        conn_pool[slot].state = st;
        return;
    }
#endif
    const uint32_t bit = 1u << slot;
    if (st == ConnState::CONN_FREE)
    {
        conn_pool[slot].state = st; // pc_atomic release store
        s_pool.free_mask.fetch_or(bit, std::memory_order_release);
    }
    else
    {
        s_pool.free_mask.fetch_and(~bit, std::memory_order_release);
        conn_pool[slot].state = st; // pc_atomic release store
    }
}

// First free slot as one ctz on the bitmask, replacing the MAX_CONNS linear scan. Returns -1 if the pool is
// full. Runs on tcpip_thread (accept); the acquire load pairs with the release stores in pc_conn_set_state.
int32_t pc_conn_alloc_free()
{
    const uint32_t valid = (MAX_CONNS >= 32) ? 0xFFFFFFFFu : ((1u << MAX_CONNS) - 1u);
    uint32_t m = s_pool.free_mask.load(std::memory_order_acquire) & valid;
    return m ? (int32_t)__builtin_ctz(m) : -1;
}

uint32_t pc_ap_ip = 0;

uint32_t DeterministicAsyncTCP::conn_timeout_ms = CONN_TIMEOUT_MS;

// ---------------------------------------------------------------------------
// Connection output API
// ---------------------------------------------------------------------------
// The single send/flush/close path for every higher layer (HTTP app, WebSocket,
// SSE, SSH). Keeping it here means presentation and application code never call
// lwIP directly - they hand bytes to the transport layer, which decides whether
// they go out as plaintext (tcp_write) or through the TLS record layer. With
// PC_ENABLE_TLS off this is byte-identical to a direct tcp_write/tcp_output.

bool pc_conn_send(uint8_t slot, const void *data, u16_t len)
{
    // The write target is always the slot's own pcb (ingress reads resolve it the
    // same way) - callers no longer thread it through, so it cannot disagree.
#if defined(ARDUINO)
    return pc_tcp_marshal(pc_tcp_op::PC_OP_SEND, slot, conn_pool[slot].pcb, data, len) ==
           ERR_OK; // write runs in tcpip_thread
#else
#if PC_ENABLE_TLS
    if (conn_pool[slot].tls)
    {
        return pc_tls_write(slot, data, len) >= 0;
    }
#endif
    return tcp_write(conn_pool[slot].pcb, data, len, TCP_WRITE_FLAG_COPY) == ERR_OK;
#endif
}

bool pc_conn_send_flush(uint8_t slot, const void *data, u16_t len)
{
    // Terminal single-shot write: the bytes AND their tcp_output() happen in one tcpip_thread
    // round-trip, so a small response costs one marshal instead of the send()+flush() pair (each
    // a ~23 us marshal on-device). For a TLS slot this is identical to pc_conn_send: the record
    // BIO already pushes ciphertext per record, so there is no separate flush to fold in.
#if defined(ARDUINO)
    return pc_tcp_marshal(pc_tcp_op::PC_OP_SEND, slot, conn_pool[slot].pcb, data, len, /*flush=*/true) == ERR_OK;
#else
#if PC_ENABLE_TLS
    if (conn_pool[slot].tls)
    {
        return pc_tls_write(slot, data, len) >= 0; // TLS BIO already output the record
    }
#endif
    if (tcp_write(conn_pool[slot].pcb, data, len, TCP_WRITE_FLAG_COPY) != ERR_OK)
    {
        return false;
    }
    tcp_output(conn_pool[slot].pcb);
    return true;
#endif
}

u16_t pc_conn_sndbuf(uint8_t slot)
{
    struct tcp_pcb *pcb = conn_pool[slot].pcb;
    if (!pcb)
    {
        return 0;
    }
    u16_t avail = tcp_sndbuf(pcb);
#if PC_ENABLE_TLS
    // A TLS record adds header + MAC/tag overhead; report a conservative plaintext
    // budget so a caller that fills it does not overrun the cipher's framing.
    if (conn_pool[slot].tls)
    {
        avail = (avail > 64) ? (u16_t)(avail - 64) : 0;
    }
#endif
    return avail;
}

void pc_conn_flush(uint8_t slot)
{
#if PC_ENABLE_TLS
    if (conn_pool[slot].tls)
    {
        return; // ciphertext was already pushed by the TLS BIO (tcp_output per record);
                // flush must NOT end the session - persistent TLS (wss / TLS SSE) reuses it
    }
#endif
#if defined(ARDUINO)
    pc_tcp_marshal(pc_tcp_op::PC_OP_OUTPUT, slot, conn_pool[slot].pcb, nullptr, 0);
#else
    tcp_output(conn_pool[slot].pcb);
#endif
}

#if PC_ENABLE_DIFFSERV
bool pc_conn_set_dscp(uint8_t slot, uint8_t dscp)
{
    if (slot >= MAX_CONNS || conn_pool[slot].pcb == nullptr)
    {
        return false;
    }
#if defined(ARDUINO)
    // Marshalled to tcpip_thread (PC_OP_SET_TOS) - lwIP reads pcb->tos while building each outbound
    // segment, so setting it from a worker task must not race the stack.
    return pc_tcp_marshal(pc_tcp_op::PC_OP_SET_TOS, slot, conn_pool[slot].pcb, nullptr, pc_dscp_to_tos(dscp)) == ERR_OK;
#else
    conn_pool[slot].pcb->tos = pc_dscp_to_tos(dscp); // host: no tcpip thread, set directly
    return true;
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
    // actively receiving (the ConnState::CONN_CLOSING discard path ACKs its own bytes).
    if (c->state != ConnState::CONN_ACTIVE || !c->pcb)
    {
        return;
    }
    size_t tail = c->rx_tail;
    size_t consumed = (tail + RX_BUF_SIZE - c->rx_acked) % RX_BUF_SIZE;
    if (!consumed)
    {
        return;
    }
    c->rx_acked = tail; // advance first: the marshaled tcp_recved is the slow part
#if defined(ARDUINO)
    pc_tcp_marshal(pc_tcp_op::PC_OP_RECVED, slot, c->pcb, nullptr, (u16_t)consumed);
#else
    tcp_recved(c->pcb, (u16_t)consumed);
#endif
}

bool pc_conn_raw_send(struct tcp_pcb *pcb, const void *data, u16_t len)
{
    if (!pcb)
    {
        return false;
    }
#if defined(ARDUINO)
    // pc_tcp_marshal owns the context choice: it runs the raw write inline when already in
    // tcpip_thread (a TLS close_notify/alert emitted from inside a raw lwIP callback) and marshals it
    // from the worker task (the handshake / read pump), so the tcp_write neither races the lwIP thread
    // nor self-deadlocks on the tcpip mailbox. The RAWSEND op also re-checks the pcb is still bound.
    return pc_tcp_marshal(pc_tcp_op::PC_OP_RAWSEND, 0, pcb, data, len) == ERR_OK;
#else
    err_t e = tcp_write(pcb, data, len, TCP_WRITE_FLAG_COPY);
    if (e == ERR_OK)
    {
        tcp_output(pcb);
    }
    return e == ERR_OK;
#endif
}

void pc_conn_close(uint8_t slot)
{
    if (slot >= MAX_CONNS)
    {
        return;
    }
    TcpConn *c = &conn_pool[slot];
    struct tcp_pcb *pcb = c->pcb;
    if (!pcb)
    {
        return;
    }
    // The application-initiated close path (L4 primitive). Remote FIN, error, and
    // timeout closes are observed at their own sites, so this is uniquely "local".
    PC_OBS_TRANSITION(slot, ConnState::CONN_ACTIVE, ConnState::CONN_FREE, pc_conn_reason::PC_CONN_R_CLOSE_LOCAL);
    // Detach the pcb and free the slot before the close, so a late callback for
    // this pcb finds a null arg and does nothing. The close itself targets the
    // captured pcb (pc_tcp_op::PC_OP_CLOSE carries it), so nulling the slot first is safe.
    pc_conn_detach(pcb);
    pc_conn_set_state(c->id, ConnState::CONN_FREE);
    c->pcb = nullptr;
#if defined(ARDUINO)
    pc_tcp_marshal(pc_tcp_op::PC_OP_CLOSE, slot, pcb, nullptr, 0); // TLS teardown + FIN in tcpip_thread
#else
#if PC_ENABLE_TLS
    if (c->tls)
    {
        pc_tls_conn_end(slot);
    }
#endif
    if (tcp_close(pcb) != ERR_OK)
    {
        tcp_abort(pcb);
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
    struct tcp_pcb *pcb = c->pcb;
    if (!pcb)
    {
        return;
    }
    PC_OBS_TRANSITION(slot, ConnState::CONN_ACTIVE, ConnState::CONN_FREE, pc_conn_reason::PC_CONN_R_ABORT);
#if PC_ENABLE_TLS
    if (c->tls)
    {
        pc_tls_conn_free(slot); // abrupt: free the per-conn TLS context, no close_notify
    }
#endif
    // Detach + free the slot before the RST, so a late callback finds a null arg.
    pc_conn_detach(pcb);
    pc_conn_set_state(c->id, ConnState::CONN_FREE);
    c->pcb = nullptr;
    pc_conn_abort(pcb);
}

void pc_conn_detach(struct tcp_pcb *pcb)
{
    // Disassociate the slot from this pcb's lwIP callbacks before freeing the
    // slot, so any late callback for the pcb finds a null arg and does nothing.
#if defined(ARDUINO)
    pc_tcp_marshal(pc_tcp_op::PC_OP_DETACH, 0, pcb, nullptr, 0);
#else
    tcp_arg(pcb, nullptr);
#endif
}

void pc_conn_abort(struct tcp_pcb *pcb)
{
    // Hard reset (RST) for a fatal condition - no graceful FIN.
#if defined(ARDUINO)
    pc_tcp_marshal(pc_tcp_op::PC_OP_ABORT, 0, pcb, nullptr, 0);
#else
    tcp_abort(pcb);
#endif
}

// ---------------------------------------------------------------------------
// ConnState::CONN_CLOSING dwell: a graceful close that holds the slot until the peer ACKs.
// ---------------------------------------------------------------------------
// These run in tcpip_thread context (the sent callback, or the pc_tcp_op::PC_OP_CLOSE_CHECK
// marshaled op), so they touch the PCB directly - never marshal from here.

// Finalize a ConnState::CONN_CLOSING slot: tear down the PCB and free the slot.
static void closing_finalize(uint8_t slot, struct tcp_pcb *pcb)
{
    TcpConn *c = &conn_pool[slot];
#if PC_ENABLE_TLS
    if (c->tls)
    {
        pc_tls_conn_end(slot); // close_notify + free the TLS context (in-thread)
    }
#endif
    pc_conn_set_state(c->id, ConnState::CONN_FREE);
    c->pcb = nullptr;
    if (pcb)
    {
        tcp_arg(pcb, nullptr);
        if (tcp_close(pcb) != ERR_OK)
        {
            tcp_abort(pcb);
        }
    }
    PC_OBS_TRANSITION(slot, ConnState::CONN_CLOSING, ConnState::CONN_FREE, pc_conn_reason::PC_CONN_R_DRAINED);
}

// If the slot is ConnState::CONN_CLOSING and its TX queue has drained (peer ACKed the whole
// response), finalize it now. Called only from tcpip_thread context.
//
// The early-return guard below is unreachable: closing_check() has exactly two callers,
// pc_conn_begin_close()'s host path (which already validated slot < MAX_CONNS and just set
// state to CONN_CLOSING immediately before this call) and lowlevel_sent_cb() (which only
// calls here after checking slot->state == ConnState::CONN_CLOSING itself, with slot->id
// always a valid conn_pool index by construction). Both guarantee the condition is false.
static void closing_check(uint8_t slot, struct tcp_pcb *pcb)
{
    if (slot >= MAX_CONNS || conn_pool[slot].state != ConnState::CONN_CLOSING) // GCOVR_EXCL_BR_LINE - see above
    {
        return; // GCOVR_EXCL_LINE - see above
    }
    if (!pcb || pcb->snd_queuelen == 0)
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
    if (c->state != ConnState::CONN_ACTIVE) // an error during the write may have freed it
    {
        return;
    }
    struct tcp_pcb *pcb = c->pcb;
    c->last_activity_ms = pc_millis();                 // start the ConnState::CONN_CLOSING dwell clock
    pc_conn_set_state(c->id, ConnState::CONN_CLOSING); // release store: tcpip-thread callbacks now see CLOSING
    PC_OBS_TRANSITION(slot_id, ConnState::CONN_ACTIVE, ConnState::CONN_CLOSING, pc_conn_reason::PC_CONN_R_CLOSE_LOCAL);
    // Finalize immediately if the response already drained, else dwell until the
    // sent callback (or the CLOSING-timeout sweep) reclaims it. The PCB read must
    // happen in tcpip_thread, so marshal the check on device.
#if defined(ARDUINO)
    pc_tcp_marshal(pc_tcp_op::PC_OP_CLOSE_CHECK, slot_id, pcb, nullptr, 0);
#else
    closing_check(slot_id, pcb);
#endif
}

/**
 * @brief Non-blocking event enqueue helper.
 *
 * Forwards the event to the queue owned by the connection's listener.
 * xQueueSend with timeout=0 returns immediately if the queue is full.
 * A full queue indicates the application is not calling server_tick() fast
 * enough; dropped events are recoverable via the idle-timeout sweep.
 */
static inline void enqueue(TcpConn *slot, const TcpEvt &evt)
{
    if (!listener_enqueue(slot->listener_id, &evt))
    {
        PC_OBS_NOTICE(slot->id, slot->state, pc_conn_reason::PC_CONN_R_DEFER_DROP);
    }
}

void DeterministicAsyncTCP::pool_init(const WebServerConfig *cfg)
{
    conn_timeout_ms = cfg ? cfg->conn_timeout_ms : CONN_TIMEOUT_MS;
    // Reset from a single zeroed template in BSS rather than `conn_pool[i] = {}`:
    // the latter materializes a full sizeof(TcpConn) temporary on the caller's
    // stack (the whole rx_buffer[RX_BUF_SIZE]), which overflows the loopTask stack
    // at begin() once RX_BUF_SIZE is set large. Copy-assigning from the static
    // template stays in BSS and uses pc_atomic::operator= (no atomic memset UB).
    // GCOVR_EXCL_BR_LINE - the static-local guard's exception-cleanup arm is unreachable: the
    // TcpConn aggregate init only value-initializes noexcept pc_atomic members, so it never throws.
    static const TcpConn blank = {}; // GCOVR_EXCL_BR_LINE
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = blank;
        conn_pool[i].id = i;
        pc_conn_set_state((uint8_t)i, ConnState::CONN_FREE);
    }
}

void DeterministicAsyncTCP::stop()
{
    // Abort all active connections - listener PCBs and queues are owned by
    // the listener layer and must be cleaned up via listener_stop_all() first.
    for (int i = 0; i < MAX_CONNS; i++)
    {
        ConnState st = conn_pool[i].state;
        if ((st == ConnState::CONN_ACTIVE || st == ConnState::CONN_CLOSING) && conn_pool[i].pcb)
        {
            struct tcp_pcb *pcb = conn_pool[i].pcb;
            pc_conn_set_state((uint8_t)i, ConnState::CONN_FREE);
            conn_pool[i].pcb = nullptr;
            pc_conn_detach(pcb); // tcpip_thread-marshaled tcp_arg(null) + abort
            pc_conn_abort(pcb);
            PC_OBS_TRANSITION((uint8_t)i, st, ConnState::CONN_FREE, pc_conn_reason::PC_CONN_R_ABORT);
        }
        pc_conn_set_state((uint8_t)i, ConnState::CONN_FREE);
        conn_pool[i].pcb = nullptr;
    }
}

uint8_t pc_conn_active_count()
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        if (conn_pool[i].state == ConnState::CONN_ACTIVE)
        {
            n++;
        }
    }
    return n;
}

uint32_t pc_conn_remote_ip(uint8_t slot)
{
#ifdef ARDUINO
    if (slot >= MAX_CONNS)
    {
        return 0;
    }
    TcpConn *conn = &conn_pool[slot];
    if (conn->state == ConnState::CONN_ACTIVE && conn->pcb)
    {
        return ip4_addr_get_u32(ip_2_ip4(&conn->pcb->remote_ip));
    }
#else
    (void)slot;
#endif
    return 0;
}

#ifdef ARDUINO
// Convert an lwIP address (itself a family-tagged union) into the portable pc_ip, network-order
// bytes preserved. The one owner of the pcb ip_addr_t -> pc_ip mapping, for the per-slot accessor
// below and the accept callback (which has the pcb but no slot yet).
void pc_lwip_to_ip(const ip_addr_t *ra, pc_ip *out)
{
#if LWIP_IPV6
    if (IP_IS_V6(ra))
    {
        uint8_t b[16];
        memcpy(b, ip_2_ip6(ra)->addr, 16); // lwIP holds the 16 bytes in network order
        *out = pc_ip_from_v6_bytes(b);
        return;
    }
#endif
    // ip4_addr_get_u32 is network-order; on the (little-endian) ESP32 the first octet is the low
    // byte. Peel the octets so pc_ip holds them left-to-right.
    uint32_t be = ip4_addr_get_u32(ip_2_ip4(ra));
    *out = pc_ip_from_v4_octets((uint8_t)be, (uint8_t)(be >> 8), (uint8_t)(be >> 16), (uint8_t)(be >> 24));
}
#endif

bool pc_conn_remote_addr(uint8_t slot, pc_ip *out)
{
    if (out)
    {
        out->family = pc_ip_family::PC_IP_NONE;
    }
#ifdef ARDUINO
    if (!out || slot >= MAX_CONNS)
    {
        return false;
    }
    TcpConn *conn = &conn_pool[slot];
    if (conn->state != ConnState::CONN_ACTIVE || !conn->pcb)
    {
        return false;
    }
    pc_lwip_to_ip(&conn->pcb->remote_ip, out);
    return true;
#else
    (void)slot;
    return false;
#endif
}

// Refresh a slot's idle-timeout timestamp from the owning worker while a response body is still
// being paged out (the file/chunk send pumps call this every poll they run). Such a slot is
// actively streaming - or briefly blocked on a full send window / a transient link stall - NOT
// idle, so the CONN_TIMEOUT_MS idle sweep must not reap it mid-transfer: that truncates any body
// larger than one TCP window (seen on a multi-hundred-MB download as an rc=56 reset or a short
// read). Dead-peer teardown for an in-flight response stays owned by lwIP's retransmission timers,
// which abort a black-holed pcb through the err callback. Worker-context safe: it only writes our
// own last_activity_ms hint (the sent callback writes the same uint32 from tcpip_thread; a torn
// read of a timestamp is benign).
void pc_conn_touch_active(uint8_t slot_id)
{
    if (slot_id >= MAX_CONNS)
    {
        return;
    }
    TcpConn *c = &conn_pool[slot_id];
    if (c->state == ConnState::CONN_ACTIVE)
    {
        c->last_activity_ms = pc_millis();
    }
}

void DeterministicAsyncTCP::check_timeouts(int worker_id)
{
    uint32_t now = pc_millis();
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TcpConn *slot = &conn_pool[i];
        if (slot->owner != worker_id) // each worker reaps only its own slots
        {
            continue;
        }

        // ConnState::CONN_CLOSING safety net: a graceful close whose peer never ACKs would
        // dwell forever. After PC_CLOSING_TIMEOUT_MS, force it free so the
        // fixed pool cannot leak. (The fast path is the sent callback finalizing
        // on ACK; this only catches a black-holed peer.)
        if (slot->state == ConnState::CONN_CLOSING)
        {
            if ((now - slot->last_activity_ms) < PC_CLOSING_TIMEOUT_MS)
            {
                continue;
            }
            struct tcp_pcb *cpcb = slot->pcb;
            pc_conn_set_state(slot->id, ConnState::CONN_FREE);
            slot->pcb = nullptr;
            if (cpcb)
            {
                pc_conn_detach(cpcb);
                pc_conn_abort(cpcb);
            }
            PC_OBS_TRANSITION((uint8_t)i, ConnState::CONN_CLOSING, ConnState::CONN_FREE,
                              pc_conn_reason::PC_CONN_R_DRAINED);
            continue;
        }

        if (slot->state != ConnState::CONN_ACTIVE)
        {
            continue;
        }
        if ((now - slot->last_activity_ms) < conn_timeout_ms)
        {
            continue;
        }

        struct tcp_pcb *pcb = slot->pcb;
        /*
         * Clear state BEFORE calling tcp_abort so that any lwIP callback
         * firing on the same PCB during or after abort sees state==ConnState::CONN_FREE
         * and exits immediately without accessing freed memory.
         */
        pc_conn_set_state(slot->id, ConnState::CONN_FREE);
        slot->pcb = nullptr;
        if (pcb)
        {
            pc_conn_detach(pcb); // tcpip_thread-marshaled tcp_arg(null) + abort
            pc_conn_abort(pcb);
        }
        PC_OBS_TRANSITION((uint8_t)i, ConnState::CONN_ACTIVE, ConnState::CONN_FREE, pc_conn_reason::PC_CONN_R_TIMEOUT);
        TcpEvt evt = {EvtType::EVT_ERROR, (uint8_t)i, 0};
        enqueue(slot, evt);
    }
}

// ---------------------------------------------------------------------------
// lwIP callbacks - execute in tcpip_thread task context
// These are non-static so listener.cpp can take their address.
// ---------------------------------------------------------------------------

/**
 * @brief lwIP receive callback - fires when data arrives on a connection.
 *
 * Copies pbuf chain bytes into the ring buffer; the window is reopened later by
 * pc_conn_ack_consumed() as the worker drains (ack-on-consume), not here. If the
 * whole segment will not fit it is refused (ERR_MEM) for lossless backpressure.
 * A null pbuf signals graceful remote close (FIN received).
 */
err_t lowlevel_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    TcpConn *slot = (TcpConn *)arg;
    if (!slot)
    {
        return ERR_VAL;
    }

    // While dwelling in ConnState::CONN_CLOSING we have already sent our final response and
    // are waiting for the ACK. Drain (and ACK) anything the peer still sends so
    // the window keeps moving, but do not process it. A peer FIN here just means
    // both sides are done - finalize on the next sent/timeout.
    if (slot->state == ConnState::CONN_CLOSING)
    {
        if (p)
        {
            tcp_recved(tpcb, p->tot_len);
            pbuf_free(p);
        }
        return ERR_OK;
    }

    if (slot->state != ConnState::CONN_ACTIVE)
    {
        return ERR_VAL;
    }

    if (p == nullptr)
    {
        /*
         * Null pbuf signals graceful remote close (FIN received).
         * Clear state and pcb before tcp_close so any stale callbacks
         * are harmless.
         */
        pc_conn_set_state(slot->id, ConnState::CONN_FREE);
        slot->pcb = nullptr;
        tcp_arg(tpcb, nullptr);
        if (tcp_close(tpcb) != ERR_OK)
        {
            tcp_abort(tpcb);
        }
        PC_OBS_TRANSITION(slot->id, ConnState::CONN_ACTIVE, ConnState::CONN_FREE,
                          pc_conn_reason::PC_CONN_R_CLOSE_REMOTE);
        TcpEvt evt = {EvtType::EVT_DISCONNECT, slot->id, 0};
        enqueue(slot, evt);
        return ERR_OK;
    }

    /*
     * Backpressure without data loss: if the whole segment will not fit in the
     * free ring space, refuse it (return ERR_MEM without freeing) so lwIP retains
     * it as refused_data and redelivers once the application has drained the
     * ring; nudge the main loop to drain. The previous code copied what fit and
     * dropped the rest, silently corrupting bodies larger than the ring (e.g.
     * streamed uploads). NOTE: needs RX_BUF_SIZE > the largest incoming segment
     * (TCP_MSS) so a full segment can eventually fit; smaller rings only ever see
     * sub-MSS requests, which always fit.
     */
    if (p->tot_len > pc_ring_free(slot->rx_head, slot->rx_tail, RX_BUF_SIZE))
    {
        PC_OBS_NOTICE(slot->id, ConnState::CONN_ACTIVE, pc_conn_reason::PC_CONN_R_BACKPRESSURE);
        TcpEvt evt = {EvtType::EVT_DATA, slot->id, 0}; // wake the loop so it drains the ring
        enqueue(slot, evt);
        // Do NOT refresh the idle timer here: a refused segment is redelivered by lwIP every
        // retransmit until the ring drains, so refreshing on refusal keeps a backpressure-stuck
        // connection alive forever (idle sweep never reaps it -> slot leak / pool-exhaustion DoS,
        // e.g. an oversized request line that fills the ring and never completes). The timer is
        // refreshed below only when data is actually ACCEPTED (real progress), so a connection
        // that makes no progress times out and is reaped.
        return ERR_MEM; // do NOT pbuf_free(p): lwIP keeps it and redelivers
    }

    uint32_t rx_now = pc_millis();
    slot->last_activity_ms = rx_now; // accepted data = progress: refresh the idle timer
    if (slot->req_start_ms == 0)     // first byte of a new request: arm the completion deadline (slow-loris)
    {
        slot->req_start_ms = rx_now ? rx_now : 1;
    }

    // Bulk-copy the segment into the ring via the shared producer primitive: a
    // contiguous memcpy per pbuf (two across the wrap), advancing a LOCAL head and
    // publishing rx_head once at the end (one release store for the whole segment).
    // The free-space check above guarantees it fits, so head can never overrun tail.
    size_t head = slot->rx_head; // sole producer of head; one acquire load
    for (struct pbuf *q = p; q != nullptr; q = q->next)
    {
        head = pc_ring_write_span(slot->rx_buffer, RX_BUF_SIZE, head, (const uint8_t *)q->payload, q->len);
    }
    slot->rx_head = head;             // single release store: publishes the whole segment at once
    size_t bytes_copied = p->tot_len; // the whole segment fit (checked above)

    // Do NOT tcp_recved() here: the window is reopened by pc_conn_ack_consumed()
    // as the worker drains the ring (ack-on-consume), so the advertised window
    // tracks ring occupancy and a slow consumer cannot overflow the ring. ACKing
    // on copy decoupled the window from drainage and deadlocked streamed uploads
    // once RX_BUF_SIZE < TCP_WND (the refused segment past one ring-full stalled).
    pbuf_free(p);

    if (bytes_copied > 0)
    {
        TcpEvt evt = {EvtType::EVT_DATA, slot->id, bytes_copied};
        enqueue(slot, evt);
    }

    return ERR_OK;
}

/**
 * @brief lwIP sent callback - fires after the stack acknowledges sent bytes.
 *
 * Refreshes the idle-timeout timestamp so an active sender is not reaped while its
 * responses are in flight, and - for a slot dwelling in ConnState::CONN_CLOSING - finalizes
 * the close once the response has fully drained (the peer ACKed everything).
 */
err_t lowlevel_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    TcpConn *slot = (TcpConn *)arg;
    if (slot)
    {
        slot->last_activity_ms = pc_millis();
        if (slot->state == ConnState::CONN_CLOSING)
        {
            closing_check(slot->id, tpcb); // drained? -> tear down + free the slot
        }
#ifdef ARDUINO
        // The send window just freed: wake the owning worker so a paced response
        // (e.g. a large file) resumes now rather than on the next idle sweep.
        else
        {
            pc_worker_wake(slot->owner);
        }
#endif
    }
    (void)len;
    return ERR_OK;
}

/**
 * @brief lwIP error callback - fires when the stack detects a fatal error.
 *
 * By the time this fires the PCB is already gone internally, so we must NOT
 * call tcp_close() or tcp_abort().  Null out the slot's pcb pointer and post
 * EvtType::EVT_ERROR so the session layer resets the protocol state.
 */
void lowlevel_err_cb(void *arg, err_t err)
{
    TcpConn *slot = (TcpConn *)arg;
    if (!slot)
    {
        return;
    }

    /*
     * When lwIP fires the error callback the PCB has already been freed
     * internally.  We must NOT call tcp_close/tcp_abort here - just null
     * out our pointer to prevent any future access.
     */
    ConnState old = slot->state;
    pc_conn_set_state(slot->id, ConnState::CONN_FREE);
    slot->pcb = nullptr;

    // A slot that errored while dwelling in ConnState::CONN_CLOSING is already done from the
    // session's view (its response was sent and the protocol state reset). Just
    // release the slot + the CLOSING gauge; do not re-post a close event.
    if (old == ConnState::CONN_CLOSING)
    {
        PC_OBS_TRANSITION(slot->id, ConnState::CONN_CLOSING, ConnState::CONN_FREE, pc_conn_reason::PC_CONN_R_DRAINED);
        (void)err;
        return;
    }

    PC_OBS_TRANSITION(slot->id, ConnState::CONN_ACTIVE, ConnState::CONN_FREE, pc_conn_reason::PC_CONN_R_ERROR);
    TcpEvt evt = {EvtType::EVT_ERROR, slot->id, 0};
    enqueue(slot, evt);
    (void)err;
}
