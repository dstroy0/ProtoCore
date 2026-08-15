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
#include "core_setup/board_profiles/protocore_platform.h"
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
    struct TcpLowerInternal *ctx; ///< the seam this op belongs to
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
};

/**
 * @brief The seam's state and the calls that reach it.
 *
 *
 * @var TcpLowerInternal::tcpip_task  the stack's thread, captured on the first op
 * @var TcpLowerInternal::ttl         RFC 9293 sec 3.9.2 MUST-49: the TTL outbound segments carry
 * @var TcpLowerInternal::store       the per-issuer op records
 */
struct TcpLowerInternal
{
    protocore_platform_task tcpip_task;
    uint8_t ttl;
    struct TcpLowerStorage *store;
    TcpLowerNs *ns; ///< the handle a caller sets an op on, and reads its outcome from

    proto_bool (*on_stack_thread)(const struct TcpLowerInternal *ctx);
    proto_bool (*pcb_bound)(const struct TcpLowerInternal *ctx);
    protocore_net_err (*dispatch)(struct TcpLowerInternal *ctx);
    protocore_net_err (*marshal)(struct TcpLowerInternal *ctx);
    void (*detach)(struct TcpLowerInternal *ctx);
    void (*abort)(struct TcpLowerInternal *ctx);
    proto_bool (*set_ttl)(struct TcpLowerInternal *ctx);
    void (*apply_ttl)(struct TcpLowerInternal *ctx);
};

static struct TcpLowerStorage s_lower_store;

// Tentative here so the functions below can reach the state; the members are bound at the bottom,
// where every function they name has been defined.
static struct TcpLowerInternal s_tp;

// True when the caller may run a raw stack op directly instead of marshaling. The stack has two
// threading models and the answer differs, so branch on which one the framework built:
static proto_bool on_stack_thread(const struct TcpLowerInternal *restrict ctx)
{
    return ctx->tcpip_task != NULL && protocore_platform_task_self() == ctx->tcpip_task;
}

// True if the op's control block is still bound to a live connection slot. A marshalled send or
// output captures the block on the worker thread; by the time the op runs the connection can have
// been torn down, and writing through a freed block trips the stack's invalid-pcb assert. Re-check
// here, in the context where teardown also runs. Looking the block up rather than reading the slot
// is what RAWSEND needs, since it carries no slot.
static proto_bool pcb_bound(const struct TcpLowerInternal *restrict ctx)
{
    const protocore_pcb *pcb = ctx->ns->pcb;
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

static protocore_net_err protocore_tcp_do(struct TcpLowerInternal *restrict ctx)
{
    ctx->ns->result = PROTOCORE_NET_OK;
    if (ctx->tcpip_task == NULL) // capture the stack task once; the dispatch only ever runs in that thread
    {
        ctx->tcpip_task = protocore_platform_task_self();
    }
    switch (ctx->ns->op)
    {
    case PROTOCORE_OP_RAWSEND:
        // RAWSEND (TLS BIO) carries only the pcb, not its slot, so liveness needs a pool lookup. This
        // is the cool TLS handshake / read-pump path (not per-packet app data), and CONN_POOL_SLOTS is
        // small + compile-time so -O2 unrolls the scan (see docs/ROADMAP: unroll loops to bitmask).
        if (!ctx->pcb_bound(ctx)) // stale pcb (connection torn down between capture and now)
        {
            ctx->ns->result = PROTOCORE_NET_ERR_CLSD;
            break;
        }
        ctx->ns->result = protocore_net_write(ctx->ns->pcb, ctx->ns->data, ctx->ns->len, PROTOCORE_NET_WRITE_COPY);
        if (ctx->ns->result == PROTOCORE_NET_OK)
        {
            protocore_net_output(ctx->ns->pcb);
        }
        break;
    case PROTOCORE_OP_SEND:
        // Hot path: SEND carries the real slot, so a stale pcb is just ctx->ns->pcb != the slot's live pcb.
        // O(1), no scan - the send/flush pair runs on every HTTP response. The `ctx->ns->pcb` null test is
        // essential: a torn-down slot has pcb == null, and comparing a captured-null against a live-null
        // (null == null) would otherwise pass the guard and protocore_net_write(null).
        if (!ctx->ns->pcb || ctx->ns->pcb != conn_pool[ctx->ns->slot].pcb)
        {
            ctx->ns->result =
                PROTOCORE_NET_ERR_CLSD; // connection torn down between capture and now; skip, do not assert
            break;
        }
#if PROTOCORE_ENABLE_TLS
        if (conn_pool[ctx->ns->slot].tls)
        {
            ctx->ns->result = (protocore_tls_write(ctx->ns->slot, ctx->ns->data, ctx->ns->len) >= 0)
                                  ? PROTOCORE_NET_OK
                                  : PROTOCORE_NET_ERR_MEM;
            break;
        }
#endif
        ctx->ns->result = protocore_net_write(ctx->ns->pcb, ctx->ns->data, ctx->ns->len, PROTOCORE_NET_WRITE_COPY);
        if (ctx->ns->flush && ctx->ns->result == PROTOCORE_NET_OK)
        {
            protocore_net_output(
                ctx->ns->pcb); // coalesced write+flush: one marshal for a terminal single-shot response
        }
        break;
    case PROTOCORE_OP_OUTPUT:
        // Hot path (O(1)): flush only if the slot still owns a LIVE pcb; else it was torn down - skip
        // rather than flushing freed memory (the stack's "invalid pcb" assert -> panic). The `ctx->ns->pcb`
        // null test is essential and was the missing piece here: protocore_conn_flush() marshals this op with
        // conn_pool[slot].pcb, which is null for a torn-down slot, so a captured-null vs a live-null
        // (null == null) passed the guard and called protocore_net_output(null) -> panic. Coredump-confirmed:
        // slot 0, ctx->ns->pcb == 0, conn_pool[0].pcb == 0, state CONN_FREE.
        if (ctx->ns->pcb && ctx->ns->pcb == conn_pool[ctx->ns->slot].pcb)
        {
            protocore_net_output(ctx->ns->pcb);
        }
        else
        {
            ctx->ns->result = PROTOCORE_NET_ERR_CLSD;
        }
        break;
    case PROTOCORE_OP_CLOSE:
#if PROTOCORE_ENABLE_TLS
        if (conn_pool[ctx->ns->slot].tls)
        {
            protocore_tls_conn_end(ctx->ns->slot); // close_notify + free the TLS context
        }
#endif
        if (protocore_net_close(ctx->ns->pcb) != PROTOCORE_NET_OK)
        {
            protocore_net_abort(ctx->ns->pcb);
        }
        break;
    case PROTOCORE_OP_ABORT:
        protocore_net_abort(ctx->ns->pcb);
        break;
    case PROTOCORE_OP_DETACH:
        protocore_net_arg(ctx->ns->pcb, NULL);
        break;
    case PROTOCORE_OP_CLOSE_CHECK:
        ConnPool.slot = ctx->ns->slot;
        ConnPool.pcb = ctx->ns->pcb;
        ConnPool.closing_check(ConnPool.internal); // safe pcb access: we are in tcpip_thread
        break;
    case PROTOCORE_OP_RECVED:
        // Same O(1) liveness guard as SEND/OUTPUT: the worker captured the pcb when it acked consumed
        // RX bytes, but the connection can be torn down (a remote RST frees the pcb via the error
        // callback) before this op runs. protocore_net_recved on a freed pcb walks into protocore_net_rcv_wnd_update
        // (assert new_rcv_ann_wnd <= 0xffff) and a window-update protocore_net_output ("invalid pcb") - i.e. a
        // remotely-triggerable panic under connection churn. Skip if the slot no longer owns a live pcb
        // (the null test guards the captured-null vs live-null case, as in SEND/OUTPUT).
        if (ctx->ns->pcb && ctx->ns->pcb == conn_pool[ctx->ns->slot].pcb)
        {
            protocore_net_recved(ctx->ns->pcb, ctx->ns->len); // reopen the receive window by the consumed bytes
        }
        else
        {
            ctx->ns->result = PROTOCORE_NET_ERR_CLSD;
        }
        break;
    case PROTOCORE_OP_SET_TTL:
        // RFC 9293 sec 3.9.2 MUST-49. Stamped where the control block is created, before it carries
        // anything, so no segment of a connection goes out with a different TTL than its first.
        if (ctx->ns->pcb)
        {
            ctx->ns->pcb->ttl = (uint8_t)ctx->ns->len;
        }
        break;
    }
    return PROTOCORE_NET_OK;
}

// The stack's marshaling call hands back a record, not the context. One context owns every record
// in the store, so the context is recovered here and the record is only the vehicle.
static protocore_net_err dispatch_trampoline(protocore_net_call *c)
{
    protocore_tcp_call *k = (protocore_tcp_call *)c;
    return protocore_tcp_do(k->ctx);
}

static protocore_net_err marshal(struct TcpLowerInternal *restrict ctx)
{
    // In stack context already (a raw callback's teardown reaching a send or a close): run the op
    // inline. Re-marshaling would call into the mailbox from the very thread that services it and
    // block forever. Off-thread: marshal.
    if (ctx->on_stack_thread(ctx))
    {
        (void)ctx->dispatch(ctx);
        return ctx->ns->result;
    }
    protocore_tcp_call *k = &ctx->store->call[ctx->ns->slot];
    k->ctx = ctx;
    protocore_net_call_marshal(dispatch_trampoline, &k->base);
    return ctx->ns->result;
}

// Disassociate the slot from this control block's stack callbacks before the slot is freed, so any
// late callback for it finds a null arg and does nothing.
static void detach(struct TcpLowerInternal *restrict ctx)
{
    ctx->ns->op = PROTOCORE_OP_DETACH;
    (void)ctx->marshal(ctx);
}

// Hard reset (RST) for a fatal condition - no graceful FIN.
static void conn_abort(struct TcpLowerInternal *restrict ctx)
{
    ctx->ns->op = PROTOCORE_OP_ABORT;
    (void)ctx->marshal(ctx);
}

// RFC 1122 sec 3.2.1.7: a datagram must leave with a non-zero TTL, so zero is refused rather than
// stored and stamped onto every later connection. The candidate arrives in len.
static proto_bool set_ttl(struct TcpLowerInternal *restrict ctx)
{
    if (ctx->ns->len == 0 || ctx->ns->len > 0xFFu)
    {
        return PROTO_FALSE;
    }
    ctx->ttl = (uint8_t)ctx->ns->len;
    return PROTO_TRUE;
}

// Stamp the control block the handle carries with the configured TTL.
static void apply_ttl(struct TcpLowerInternal *restrict ctx)
{
    ctx->ns->op = PROTOCORE_OP_SET_TTL;
    ctx->ns->len = ctx->ttl;
    (void)ctx->marshal(ctx);
}

// Designated, so a member's position in the struct does not decide what it binds to. tcpip_task is
// left zero: the dispatch captures it the first time it runs.
static struct TcpLowerInternal s_tp = {.ttl = PROTOCORE_TCP_TTL,
                                       .store = &s_lower_store,
                                       .ns = &TcpLower,
                                       .on_stack_thread = on_stack_thread,
                                       .pcb_bound = pcb_bound,
                                       .dispatch = protocore_tcp_do,
                                       .marshal = marshal,
                                       .detach = detach,
                                       .abort = conn_abort,
                                       .set_ttl = set_ttl,
                                       .apply_ttl = apply_ttl};

TcpLowerNs TcpLower = {.marshal = marshal,
                       .detach = detach,
                       .abort = conn_abort,
                       .set_ttl = set_ttl,
                       .apply_ttl = apply_ttl,
                       .internal = &s_tp};
