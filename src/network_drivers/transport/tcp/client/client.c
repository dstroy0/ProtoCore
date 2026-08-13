// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file client.c
 * @brief Layer 4 (Transport) - the active OPEN: the outbound client pool. See client.h.
 *
 * Mirrors the server transport's cross-thread rule: every raw stack call runs in the stack's own
 * context. Each slot owns its pcb and an SPSC wire ring (producer = the stack's recv callback;
 * consumer = the caller's loop/blocking task). The rings use atomic indices, per the ordering rule
 * in mmgr/ring.h, which is what makes a producer's writes visible before the advanced index.
 */

#include "client.h"
#include "mmgr/protomem.h"

// Compiles only when a client transport is enabled (HTTP client / MQTT / WS client). A server-only
// build leaves DNS_RESOLVER off, so the resolver symbols this unit calls would not be declared -
// see PROTOCORE_NEED_CLIENT in protocore_config.h.
#if PROTOCORE_NEED_CLIENT

#include "../../diffserv/diffserv.h" // DiffServ DSCP marking for outbound client connections (compiles out when off)
#include "core_setup/board_profiles/protocore_platform.h" // the stack's TCP, under our names
#include "mmgr/ring.h" // PROTO_ATOMIC_LOAD/STORE + SPSC ring drain (same primitive as the server)
#include "network_drivers/network/dns/dns_resolver.h" // shared host->IP resolve (one DNS owner)
#include "server/clock/clock.h"                       // protocore_millis()

typedef struct
{
    protocore_pcb *pcb;
    // Written by the stack's callbacks in its own thread, read by the caller's task. Atomic for the
    // same reason the ring indices below are: volatile orders nothing and publishes nothing.
    _Atomic proto_bool in_use;
    _Atomic proto_bool connected;
    _Atomic proto_bool closed; // peer FIN or error
    const char *host;          // the caller's name, read each pump until it resolves
    uint16_t port;
    uint32_t timer;      // millis the open started at
    uint32_t timeout_ms; // what the whole open, resolve included, is given
    proto_bool resolving;
    uint8_t rx[PROTOCORE_CLIENT_RX_BUF];
    _Atomic size_t head; // producer (stack recv cb); acquire/release SPSC, same as the server ring
    _Atomic size_t tail; // consumer (caller)
} ClientConn;

static_assert(PROTOCORE_RING_POW2(PROTOCORE_CLIENT_RX_BUF),
              "PROTOCORE_CLIENT_RX_BUF must be a power of two: a ring index wraps with a mask");

/**
 * @brief The dialing side's compile-time storage: the outbound slot pool.
 *
 * All of it BSS, so a client connection costs no heap and nothing lands on a task stack.
 */
struct TcpClientStorage
{
    ClientConn cc[PROTOCORE_CLIENT_CONNS];
};

/**
 * @brief The dialing side's state and the calls that reach it - what TcpClientNs points at.
 *
 * @var TcpClientInternal::store  the outbound slot pool
 * @var TcpClientInternal::ns     the handle a caller sets a call's members on
 * @var TcpClientInternal::conn   the slot the private steps below act on
 * @var TcpClientInternal::pump   step one slot from resolving to connected
 */
struct TcpClientInternal
{
    struct TcpClientStorage *store;
    TcpClientNs *ns;
    ClientConn *conn;
    void (*pump)(struct TcpClientInternal *ctx);
};

// The private step every public call runs first, named here so the binding below can reach it.
static void cc_pump(struct TcpClientInternal *restrict ctx);

static struct TcpClientStorage s_store;

static struct TcpClientInternal s_client = {.store = &s_store, .ns = &TcpClient, .pump = cc_pump};

// Hostname resolution goes through @ref Resolver, the library's one DNS owner.

// --- stack callbacks (stack context); arg = the owning ClientConn* -----------

static protocore_net_err cc_recv(void *arg, protocore_pcb *tpcb, protocore_pbuf *p, protocore_net_err err)
{
    (void)err;
    ClientConn *c = (ClientConn *)arg;
    if (!c)
    {
        return PROTOCORE_NET_OK;
    }
    if (p == NULL)
    {
        c->closed = PROTO_TRUE; // peer closed
        return PROTOCORE_NET_OK;
    }
    // Wire bytes -> ring via the shared producer primitive (same as the server): if
    // the whole segment will not fit, refuse it (the stack retains + redelivers); else
    // move each span and publish head once.
    (void)tpcb;
    if (p->tot_len > protocore_ring_free(&c->head, &c->tail, PROTOCORE_CLIENT_RX_BUF))
    {
        return PROTOCORE_NET_ERR_MEM;
    }
    size_t h = PROTO_ATOMIC_LOAD(&c->head); // sole producer of head; advance a local and publish once
    for (protocore_pbuf *q = p; q != NULL; q = q->next)
    {
        h = protocore_ring_write_span(c->rx, PROTOCORE_CLIENT_RX_BUF, h, (const uint8_t *)q->payload, q->len);
    }
    PROTO_ATOMIC_STORE(&c->head, h); // one release store publishes the whole segment
    // Do NOT tcp_recved() here. The window is reopened by protocore_client_read() as the
    // caller drains (ack-on-consume), so it tracks ring occupancy and the peer can
    // never overflow the ring - same model as the server transport. ACKing on copy
    // would decouple the window from drainage and deadlock a large inbound transfer
    // once PROTOCORE_CLIENT_RX_BUF < TCP_WND.
    protocore_net_pbuf_free(p);
    return PROTOCORE_NET_OK;
}

static protocore_net_err cc_connected(void *arg, protocore_pcb *tpcb, protocore_net_err err)
{
    (void)tpcb;
    ClientConn *c = (ClientConn *)arg;
    if (c)
    {
        if (err == PROTOCORE_NET_OK)
        {
            c->connected = PROTO_TRUE;
        }
        else
        {
            c->closed = PROTO_TRUE;
        }
    }
    return PROTOCORE_NET_OK;
}

static void cc_err(void *arg, protocore_net_err err)
{
    (void)err;
    ClientConn *c = (ClientConn *)arg;
    if (c)
    {
        c->pcb = NULL; // the stack already freed it
        c->closed = PROTO_TRUE;
    }
}

// --- marshaled ops, each run in the stack's own context ----------------------

typedef struct
{
    protocore_net_call base;
    ClientConn *c;
    protocore_net_ip addr;
    uint16_t port;
    protocore_net_err result;
} CcConnCall;
typedef struct
{
    protocore_net_call base;
    ClientConn *c;
    const void *data;
    proto_u16 len;
    protocore_net_err result;
} CcSendCall;
typedef struct
{
    protocore_net_call base;
    ClientConn *c;
    proto_u16 len;
} CcRecvedCall;

static protocore_net_err cc_do_connect(protocore_net_call *cd)
{
    CcConnCall *k = (CcConnCall *)cd;
    ClientConn *c = k->c;
    c->pcb = protocore_net_new(PROTOCORE_NET_TYPE_V4);
    if (!c->pcb)
    {
        k->result = PROTOCORE_NET_ERR_MEM;
        return PROTOCORE_NET_OK;
    }
    protocore_net_arg(c->pcb, c);
    protocore_net_on_recv(c->pcb, cc_recv);
    protocore_net_on_err(c->pcb, cc_err);
    // RFC 9293 sec 3.9.2 MUST-49: stamped before the SYN goes out, so the whole connection carries
    // the configured TTL. Runs in the stack's thread (this is the marshalled connect op).
    c->pcb->ttl = protocore_tcp_ttl();
#if PROTOCORE_ENABLE_DIFFSERV
    {
        // Mark the outbound connection with the server-wide default DSCP (the SYN onward). Runs in
        // tcpip_thread (this is the marshalled connect op), so touching the pcb is race-free.
        uint8_t dscp = protocore_diffserv_default_dscp();
        if (dscp)
        {
            c->pcb->tos = protocore_dscp_to_tos(dscp);
        }
    }
#endif
    k->result = protocore_net_connect(c->pcb, &k->addr, k->port, cc_connected);
    return PROTOCORE_NET_OK;
}

static protocore_net_err cc_do_send(protocore_net_call *cd)
{
    CcSendCall *k = (CcSendCall *)cd;
    ClientConn *c = k->c;
    if (!c->pcb)
    {
        k->result = PROTOCORE_NET_ERR_CONN;
        return PROTOCORE_NET_OK;
    }
    k->result = protocore_net_write(c->pcb, k->data, k->len, PROTOCORE_NET_WRITE_COPY);
    if (k->result == PROTOCORE_NET_OK)
    {
        protocore_net_output(c->pcb);
    }
    return PROTOCORE_NET_OK;
}

static protocore_net_err cc_do_close(protocore_net_call *cd)
{
    CcSendCall *k = (CcSendCall *)cd;
    ClientConn *c = k->c;
    if (c->pcb)
    {
        protocore_net_arg(c->pcb, NULL);
        protocore_net_on_recv(c->pcb, NULL);
        protocore_net_on_err(c->pcb, NULL);
        if (protocore_net_close(c->pcb) != PROTOCORE_NET_OK)
        {
            protocore_net_abort(c->pcb);
        }
        c->pcb = NULL;
    }
    return PROTOCORE_NET_OK;
}

static protocore_net_err cc_do_recved(protocore_net_call *cd)
{
    CcRecvedCall *k = (CcRecvedCall *)cd;
    if (k->c->pcb)
    {
        protocore_net_recved(k->c->pcb, k->len); // reopen the window by the consumed bytes
    }
    return PROTOCORE_NET_OK;
}

// --- public API --------------------------------------------------------------

// Step the slot in ctx->conn from resolving to connected. Each call does at most one step and
// nothing waits: the slot's timer bounds the whole open, and a caller reaches this from its own tick
// through connected() or is_closed().
static void cc_pump(struct TcpClientInternal *restrict ctx)
{
    ClientConn *c = ctx->conn;
    if (!c->in_use || c->connected || c->closed)
    {
        return;
    }
    if ((uint32_t)(protocore_millis() - c->timer) >= c->timeout_ms)
    {
        c->closed = PROTO_TRUE; // out of time, whether it was still resolving or already connecting
        return;
    }
    if (!c->resolving)
    {
        return; // the connect is out; cc_connected / cc_err settle it
    }

    // Resolve through the shared DNS owner, which reports busy until its own answer lands.
    Resolver.query.host = c->host;
    Resolver.resolve(Resolver.internal);
    protocore_dns_state s = Resolver.state;
    uint32_t ip = Resolver.u32;
    if (s == PROTOCORE_DNS_BUSY)
    {
        return;
    }
    if (s == PROTOCORE_DNS_FAILED)
    {
        c->closed = PROTO_TRUE;
        return;
    }
    c->resolving = PROTO_FALSE;

    CcConnCall k;
    mem.set(&k, 0, sizeof(k));
    k.c = c;
    protocore_net_ip4_set(&k.addr, (uint8_t)(ip >> 24), (uint8_t)(ip >> 16), (uint8_t)(ip >> 8), (uint8_t)ip);
    k.port = c->port;
    protocore_net_call_marshal(cc_do_connect, &k.base);
    if (k.result != PROTOCORE_NET_OK)
    {
        c->closed = PROTO_TRUE;
    }
}

static void protocore_client_open(struct TcpClientInternal *restrict ctx)
{
    if (ctx->ns->dial.host == NULL)
    {
        ctx->ns->i32 = -2;
        return;
    }
    ctx->ns->i32 = -1;
    for (int i = 0; i < PROTOCORE_CLIENT_CONNS; i++)
    {
        if (!ctx->store->cc[i].in_use)
        {
            ctx->ns->i32 = i;
            break;
        }
    }
    if (ctx->ns->i32 < 0)
    {
        return; // pool full
    }

    ClientConn *c = &ctx->store->cc[ctx->ns->i32];
    c->pcb = NULL;
    c->connected = PROTO_FALSE;
    c->closed = PROTO_FALSE;
    PROTO_ATOMIC_STORE(&c->head, 0);
    PROTO_ATOMIC_STORE(&c->tail, 0);
    c->host = ctx->ns->dial.host;
    c->port = ctx->ns->dial.port;
    c->timeout_ms = ctx->ns->dial.timeout_ms;
    c->timer = protocore_millis();
    c->resolving = PROTO_TRUE;
    c->in_use = PROTO_TRUE;
    ctx->conn = c;
    ctx->pump(ctx); // a name that needs no query connects here, in the opening call
}

static void protocore_client_connected(struct TcpClientInternal *restrict ctx)
{
    if (ctx->ns->cid < 0 || ctx->ns->cid >= PROTOCORE_CLIENT_CONNS || !ctx->store->cc[ctx->ns->cid].in_use)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    ctx->conn = &ctx->store->cc[ctx->ns->cid];
    ctx->pump(ctx);
    ctx->ns->ok = ctx->conn->connected && !ctx->conn->closed;
}

static void protocore_client_is_closed(struct TcpClientInternal *restrict ctx)
{
    if (ctx->ns->cid < 0 || ctx->ns->cid >= PROTOCORE_CLIENT_CONNS)
    {
        ctx->ns->ok = PROTO_TRUE;
        return;
    }
    ctx->conn = &ctx->store->cc[ctx->ns->cid];
    ctx->pump(ctx);
    ctx->ns->ok = ctx->conn->closed;
}

static void protocore_client_send(struct TcpClientInternal *restrict ctx)
{
    if (ctx->ns->cid < 0 || ctx->ns->cid >= PROTOCORE_CLIENT_CONNS || !ctx->store->cc[ctx->ns->cid].in_use)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    CcSendCall k;
    mem.set(&k, 0, sizeof(k));
    k.c = &ctx->store->cc[ctx->ns->cid];
    k.data = ctx->ns->io.data;
    k.len = (proto_u16)(ctx->ns->io.len > 0xFFFF ? 0xFFFF : ctx->ns->io.len);
    protocore_net_call_marshal(cc_do_send, &k.base);
    ctx->ns->ok = k.result == PROTOCORE_NET_OK;
}

static void protocore_client_available(struct TcpClientInternal *restrict ctx)
{
    if (ctx->ns->cid < 0 || ctx->ns->cid >= PROTOCORE_CLIENT_CONNS)
    {
        ctx->ns->n = 0;
        return;
    }
    ctx->conn = &ctx->store->cc[ctx->ns->cid];
    // Step the open here too, not only in connected() / is_closed(): a caller that polls a slot by
    // asking what has arrived would otherwise never advance a connect that has not come up, and sit
    // on an empty ring until its own timer expired.
    ctx->pump(ctx);
    ctx->ns->n = protocore_ring_available(&ctx->conn->head, &ctx->conn->tail, PROTOCORE_CLIENT_RX_BUF);
}

static void protocore_client_read(struct TcpClientInternal *restrict ctx)
{
    if (ctx->ns->cid < 0 || ctx->ns->cid >= PROTOCORE_CLIENT_CONNS)
    {
        ctx->ns->n = 0;
        return;
    }
    ctx->conn = &ctx->store->cc[ctx->ns->cid];
    ctx->pump(ctx); // as in available(): a caller that only ever reads still steps its open along
    ctx->ns->n = protocore_ring_read(ctx->conn->rx, PROTOCORE_CLIENT_RX_BUF, &ctx->conn->head, &ctx->conn->tail,
                                     ctx->ns->io.buf, ctx->ns->io.cap);
    if (ctx->ns->n > 0 && ctx->conn->pcb)
    {
        // Ack-on-consume: reopen the receive window by exactly what we just drained.
        CcRecvedCall k;
        mem.set(&k, 0, sizeof(k));
        k.c = ctx->conn;
        k.len = (proto_u16)ctx->ns->n;
        protocore_net_call_marshal(cc_do_recved, &k.base);
    }
}

static void protocore_client_close(struct TcpClientInternal *restrict ctx)
{
    if (ctx->ns->cid < 0 || ctx->ns->cid >= PROTOCORE_CLIENT_CONNS || !ctx->store->cc[ctx->ns->cid].in_use)
    {
        return;
    }
    CcSendCall k;
    mem.set(&k, 0, sizeof(k));
    k.c = &ctx->store->cc[ctx->ns->cid];
    protocore_net_call_marshal(cc_do_close, &k.base);
    ctx->store->cc[ctx->ns->cid].in_use = PROTO_FALSE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
TcpClientNs TcpClient = {.open = protocore_client_open,
                         .connected = protocore_client_connected,
                         .is_closed = protocore_client_is_closed,
                         .send = protocore_client_send,
                         .available = protocore_client_available,
                         .read = protocore_client_read,
                         .close = protocore_client_close,
                         .internal = &s_client};

#endif // PROTOCORE_NEED_CLIENT
