// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file lower.c
 * @brief Layer 4 (Transport) - the TCP/lower-level interface. See lower.h.
 *
 * One switch, run in the stack's own context, holding every call this library makes into the
 * module below it.
 */

#include "lower.h"
#include "../protocol/protocol.h" // closing_check: the CLOSE_CHECK op finalizes a drained slot
#include "config/platform/platform.h"
#include "mmgr/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_DIFFSERV
#include "../../diffserv/diffserv.h" // protocore_dscp_to_tos: the DS field byte the SET_TOS op stamps
#endif

#if PROTOCORE_ENABLE_TLS
#include "network_drivers/tls/tls.h"
#endif

/**
 * @brief One op, carried to the stack's context.
 *
 */
typedef struct protocore_tcp_call
{
    protocore_net_call base;
    uint8_t *work; ///< the borrow this op reads its region out of
    protocore_tcp_op op;
    uint8_t slot;
    protocore_pcb *pcb;
    const void *data;
    proto_u16 len;
    proto_bool flush;         ///< PROTOCORE_OP_SEND: also flush after a successful write (coalesced write+flush)
    protocore_net_err result; ///< outcome of the op (PROTOCORE_OP_SEND: whether the write was queued)
} protocore_tcp_call;

/**
 * @brief The op records, one per connection slot.
 *
 */
struct TcpLowerStorage
{
    protocore_tcp_call call[CONN_POOL_SLOTS];
    protocore_platform_task tcpip_task; ///< the stack's thread, captured on the first op
    uint8_t ttl;                        ///< RFC 9293 sec 3.9.2 MUST-49: the TTL outbound segments carry
};

// ttl starts at PROTOCORE_TCP_TTL, which RFC 9293 sec 3.9.2 MUST-49 makes configurable; a zero fill
// would put 0 in the DS byte's TTL field and every outbound segment would die at the first hop.
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define TCP_LOWER_OFF_CTX 0u
static_assert(TCP_LOWER_OFF_CTX + sizeof(struct TcpLowerStorage) <= PROTOCORE_TCP_LOWER_BORROW,
              "PROTOCORE_TCP_LOWER_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define TCP_LOWER_CTX(w) ((struct TcpLowerStorage *)(void *)((w) + TCP_LOWER_OFF_CTX))

// True when the caller may run a raw stack op directly instead of marshaling. The stack has two
// threading models and the answer differs, so branch on which one the framework built:
static proto_bool on_stack_thread(const uint8_t *restrict work)
{
    return TCP_LOWER_CTX(work)->tcpip_task != NULL && protocore_platform_task_self() == TCP_LOWER_CTX(work)->tcpip_task;
}

// True if the op's control block is still bound to a live connection slot. A marshalled send or
// output captures the block on the worker thread; by the time the op runs the connection can have
// been torn down, and writing through a freed block trips the stack's invalid-pcb assert. Re-check
// here, in the context where teardown also runs. Looking the block up rather than reading the slot
// is what RAWSEND needs, since it carries no slot.
static proto_bool pcb_bound(const uint8_t *restrict work)
{
    const protocore_pcb *pcb = TcpLower.pcb;
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

static protocore_net_err protocore_tcp_do(uint8_t *restrict work)
{
    TcpLower.result = PROTOCORE_NET_OK;
    if (TCP_LOWER_CTX(work)->tcpip_task ==
        NULL) // capture the stack task once; the dispatch only ever runs in that thread
    {
        TCP_LOWER_CTX(work)->tcpip_task = protocore_platform_task_self();
    }
    switch (TcpLower.op)
    {
    case PROTOCORE_OP_RAWSEND:
        // RAWSEND (TLS BIO) carries only the pcb, not its slot, so liveness needs a pool lookup. This
        // is the cool TLS handshake / read-pump path (not per-packet app data), and CONN_POOL_SLOTS is
        // small + compile-time so -O2 unrolls the scan (see docs/ROADMAP: unroll loops to bitmask).
        if (!pcb_bound(work)) // stale pcb (connection torn down between capture and now)
        {
            TcpLower.result = PROTOCORE_NET_ERR_CLSD;
            break;
        }
        TcpLower.result = protocore_net_write(TcpLower.pcb, TcpLower.data, TcpLower.len, PROTOCORE_NET_WRITE_COPY);
        if (TcpLower.result == PROTOCORE_NET_OK)
        {
            protocore_net_output(TcpLower.pcb);
        }
        break;
    case PROTOCORE_OP_SEND:
        // Hot path: SEND carries the real slot, so a stale pcb is just TcpLower.pcb != the slot's live pcb.
        // O(1), no scan - the send/flush pair runs on every HTTP response. The `TcpLower.pcb` null test is
        // essential: a torn-down slot has pcb == null, and comparing a captured-null against a live-null
        // (null == null) would otherwise pass the guard and protocore_net_write(null).
        if (!TcpLower.pcb || TcpLower.pcb != conn_pool[TcpLower.slot].pcb)
        {
            TcpLower.result =
                PROTOCORE_NET_ERR_CLSD; // connection torn down between capture and now; skip, do not assert
            break;
        }
#if PROTOCORE_ENABLE_TLS
        if (conn_pool[TcpLower.slot].tls)
        {
            TcpLower.result = (protocore_tls_write(TcpLower.slot, TcpLower.data, TcpLower.len) >= 0)
                                  ? PROTOCORE_NET_OK
                                  : PROTOCORE_NET_ERR_MEM;
            break;
        }
#endif
        TcpLower.result = protocore_net_write(TcpLower.pcb, TcpLower.data, TcpLower.len, PROTOCORE_NET_WRITE_COPY);
        if (TcpLower.flush && TcpLower.result == PROTOCORE_NET_OK)
        {
            protocore_net_output(
                TcpLower.pcb); // coalesced write+flush: one marshal for a terminal single-shot response
        }
        break;
    case PROTOCORE_OP_OUTPUT:
        // Hot path (O(1)): flush only if the slot still owns a LIVE pcb; else it was torn down - skip
        // rather than flushing freed memory (the stack's "invalid pcb" assert -> panic). The `TcpLower.pcb`
        // null test is essential and was the missing piece here: protocore_conn_flush() marshals this op with
        // conn_pool[slot].pcb, which is null for a torn-down slot, so a captured-null vs a live-null
        // (null == null) passed the guard and called protocore_net_output(null) -> panic. Coredump-confirmed:
        // slot 0, TcpLower.pcb == 0, conn_pool[0].pcb == 0, state CONN_FREE.
        if (TcpLower.pcb && TcpLower.pcb == conn_pool[TcpLower.slot].pcb)
        {
            protocore_net_output(TcpLower.pcb);
        }
        else
        {
            TcpLower.result = PROTOCORE_NET_ERR_CLSD;
        }
        break;
    case PROTOCORE_OP_CLOSE:
#if PROTOCORE_ENABLE_TLS
        if (conn_pool[TcpLower.slot].tls)
        {
            protocore_tls_conn_end(TcpLower.slot); // close_notify + free the TLS context
        }
#endif
        if (protocore_net_close(TcpLower.pcb) != PROTOCORE_NET_OK)
        {
            protocore_net_abort(TcpLower.pcb);
        }
        break;
    case PROTOCORE_OP_ABORT:
        protocore_net_abort(TcpLower.pcb);
        break;
    case PROTOCORE_OP_DETACH:
        protocore_net_arg(TcpLower.pcb, NULL);
        break;
    case PROTOCORE_OP_CLOSE_CHECK:
        ConnPool.slot = TcpLower.slot;
        ConnPool.pcb = TcpLower.pcb;
        ConnPool.closing_check(protocore_conn_pool_span()); // safe pcb access: we are in tcpip_thread
        break;
    case PROTOCORE_OP_RECVED:
        // Same O(1) liveness guard as SEND/OUTPUT: the worker captured the pcb when it acked consumed
        // RX bytes, but the connection can be torn down (a remote RST frees the pcb via the error
        // callback) before this op runs. protocore_net_recved on a freed pcb walks into protocore_net_rcv_wnd_update
        // (assert new_rcv_ann_wnd <= 0xffff) and a window-update protocore_net_output ("invalid pcb") - i.e. a
        // remotely-triggerable panic under connection churn. Skip if the slot no longer owns a live pcb
        // (the null test guards the captured-null vs live-null case, as in SEND/OUTPUT).
        if (TcpLower.pcb && TcpLower.pcb == conn_pool[TcpLower.slot].pcb)
        {
            protocore_net_recved(TcpLower.pcb, TcpLower.len); // reopen the receive window by the consumed bytes
        }
        else
        {
            TcpLower.result = PROTOCORE_NET_ERR_CLSD;
        }
        break;
    case PROTOCORE_OP_SET_TTL:
        // RFC 9293 sec 3.9.2 MUST-49. Stamped where the control block is created, before it carries
        // anything, so no segment of a connection goes out with a different TTL than its first.
        if (TcpLower.pcb)
        {
            TcpLower.pcb->ttl = (uint8_t)TcpLower.len;
        }
        break;
    }
    return PROTOCORE_NET_OK;
}

// The stack's marshaling call hands back a record, not the borrow. Every record in the store is
// carved out of one borrow, so the borrow rides on the record and is recovered here.
static protocore_net_err dispatch_trampoline(protocore_net_call *c)
{
    protocore_tcp_call *k = (protocore_tcp_call *)c;
    return protocore_tcp_do(k->work);
}

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_TCP_LOWER_BORROW persistent bytes, or null while the pool was short
} TcpLowerOwnCtx;
static TcpLowerOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_tcp_lower_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_plaintext_persist_span(PROTOCORE_TCP_LOWER_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
            // A borrow arrives zeroed, and these do not start at zero.
            TCP_LOWER_CTX(s_own.span)->ttl = PROTOCORE_TCP_TTL;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

static void marshal(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    // In stack context already (a raw callback's teardown reaching a send or a close): run the op
    // inline. Re-marshaling would call into the mailbox from the very thread that services it and
    // block forever. Off-thread: marshal. Either way the op leaves its answer in ns->result.
    if (on_stack_thread(work))
    {
        (void)protocore_tcp_do(work);
        return;
    }
    protocore_tcp_call *k = &TCP_LOWER_CTX(work)->call[TcpLower.slot];
    k->work = work;
    protocore_net_call_marshal(dispatch_trampoline, &k->base);
}

// Disassociate the slot from this control block's stack callbacks before the slot is freed, so any
// late callback for it finds a null arg and does nothing.
static void detach(uint8_t *restrict work)
{
    TcpLower.op = PROTOCORE_OP_DETACH;
    marshal(work);
}

// Hard reset (RST) for a fatal condition - no graceful FIN.
static void conn_abort(uint8_t *restrict work)
{
    TcpLower.op = PROTOCORE_OP_ABORT;
    marshal(work);
}

// RFC 1122 sec 3.2.1.7: a datagram must leave with a non-zero TTL, so zero is refused rather than
// stored and stamped onto every later connection. The candidate arrives in len.
static void set_ttl(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    TcpLower.ok = PROTO_FALSE;
    if (TcpLower.len == 0 || TcpLower.len > 0xFFu)
    {
        return;
    }
    TCP_LOWER_CTX(work)->ttl = (uint8_t)TcpLower.len;
    TcpLower.ok = PROTO_TRUE;
}

// Stamp the control block the handle carries with the configured TTL.
static void apply_ttl(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    TcpLower.op = PROTOCORE_OP_SET_TTL;
    TcpLower.len = TCP_LOWER_CTX(work)->ttl;
    marshal(work);
}

// Designated, so a member's position in the struct does not decide what it binds to.

TcpLowerNs TcpLower = {
    .marshal = marshal, .detach = detach, .abort = conn_abort, .set_ttl = set_ttl, .apply_ttl = apply_ttl};
