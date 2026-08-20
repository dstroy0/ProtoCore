// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protomem/protomem.h"

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_TCP_CLIENT_BORROW persistent bytes
} TcpClientOwnCtx;
static TcpClientOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_tcp_client_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_TCP_CLIENT_BORROW).buf;
    }
    return s_own.span;
}

// Compiles only when a client transport is enabled (HTTP client / MQTT / WS client). A server-only
// build leaves DNS_RESOLVER off, so the resolver symbols this unit calls would not be declared -
// see PROTOCORE_NEED_CLIENT in protocore_config.h.
#if PROTOCORE_NEED_CLIENT

#include "../../diffserv/diffserv.h" // DiffServ DSCP marking for outbound client connections (compiles out when off)
#include "config/platform/platform.h" // the stack's TCP, under our names
#include "mmgr/ring.h" // PROTO_ATOMIC_LOAD/STORE + SPSC ring drain (same primitive as the server)
#include "network_drivers/network/dns/dns_resolver/dns_resolver.h"  // shared host->IP resolve (one DNS owner)
#include "network_drivers/transport/tcp/lower/lower.h" // TcpLower: the TTL stamp on the outbound pcb
#include "server/clock/clock.h"                        // Clock.millis

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
    ClientConn *conn;
};

// The private step every public call runs first, named here so the binding below can reach it.
static void cc_pump(uint8_t *restrict work);

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define TCP_CLIENT_OFF_CTX 0u
static_assert(TCP_CLIENT_OFF_CTX + sizeof(struct TcpClientStorage) <= PROTOCORE_TCP_CLIENT_BORROW,
              "PROTOCORE_TCP_CLIENT_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define TCP_CLIENT_CTX(w) ((struct TcpClientStorage *)(void *)((w) + TCP_CLIENT_OFF_CTX))

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
    TcpLower.pcb = c->pcb;
    TcpLower.apply_ttl(protocore_tcp_lower_span());
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

// Step the slot in TCP_CLIENT_CTX(work)->conn from resolving to connected. Each call does at most one step and
// nothing waits: the slot's timer bounds the whole open, and a caller reaches this from its own tick
// through connected() or is_closed().
// The one time source (server/clock/clock.h). Clock.ms is where the last reading landed, and the
// caller polling this module is in a loop of its own with no dispatch pass in it, so the reading is
// taken here.
static uint32_t cc_now(void)
{
    Clock.millis(Clock.internal);
    return Clock.ms;
}

static void cc_pump(uint8_t *restrict work)
{
    ClientConn *c = TCP_CLIENT_CTX(work)->conn;
    if (!c->in_use || c->connected || c->closed)
    {
        return;
    }
    if ((uint32_t)(cc_now() - c->timer) >= c->timeout_ms)
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
    Resolver.resolve(protocore_dns_resolver_span());
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

static void protocore_client_open(uint8_t *restrict work)
{
    if (TcpClient.dial.host == NULL)
    {
        TcpClient.i32 = -2;
        return;
    }
    TcpClient.i32 = -1;
    for (int i = 0; i < PROTOCORE_CLIENT_CONNS; i++)
    {
        if (!TCP_CLIENT_CTX(work)->cc[i].in_use)
        {
            TcpClient.i32 = i;
            break;
        }
    }
    if (TcpClient.i32 < 0)
    {
        return; // pool full
    }

    ClientConn *c = &TCP_CLIENT_CTX(work)->cc[TcpClient.i32];
    c->pcb = NULL;
    c->connected = PROTO_FALSE;
    c->closed = PROTO_FALSE;
    PROTO_ATOMIC_STORE(&c->head, 0);
    PROTO_ATOMIC_STORE(&c->tail, 0);
    c->host = TcpClient.dial.host;
    c->port = TcpClient.dial.port;
    c->timeout_ms = TcpClient.dial.timeout_ms;
    c->timer = cc_now();
    c->resolving = PROTO_TRUE;
    c->in_use = PROTO_TRUE;
    TCP_CLIENT_CTX(work)->conn = c;
    cc_pump(work); // a name that needs no query connects here, in the opening call
}

static void protocore_client_connected(uint8_t *restrict work)
{
    if (TcpClient.cid < 0 || TcpClient.cid >= PROTOCORE_CLIENT_CONNS || !TCP_CLIENT_CTX(work)->cc[TcpClient.cid].in_use)
    {
        TcpClient.ok = PROTO_FALSE;
        return;
    }
    TCP_CLIENT_CTX(work)->conn = &TCP_CLIENT_CTX(work)->cc[TcpClient.cid];
    cc_pump(work);
    TcpClient.ok = TCP_CLIENT_CTX(work)->conn->connected && !TCP_CLIENT_CTX(work)->conn->closed;
}

static void protocore_client_is_closed(uint8_t *restrict work)
{
    if (TcpClient.cid < 0 || TcpClient.cid >= PROTOCORE_CLIENT_CONNS)
    {
        TcpClient.ok = PROTO_TRUE;
        return;
    }
    TCP_CLIENT_CTX(work)->conn = &TCP_CLIENT_CTX(work)->cc[TcpClient.cid];
    cc_pump(work);
    TcpClient.ok = TCP_CLIENT_CTX(work)->conn->closed;
}

static void protocore_client_send(uint8_t *restrict work)
{
    if (TcpClient.cid < 0 || TcpClient.cid >= PROTOCORE_CLIENT_CONNS || !TCP_CLIENT_CTX(work)->cc[TcpClient.cid].in_use)
    {
        TcpClient.ok = PROTO_FALSE;
        return;
    }
    CcSendCall k;
    mem.set(&k, 0, sizeof(k));
    k.c = &TCP_CLIENT_CTX(work)->cc[TcpClient.cid];
    k.data = TcpClient.io.data;
    k.len = (proto_u16)(TcpClient.io.len > 0xFFFF ? 0xFFFF : TcpClient.io.len);
    protocore_net_call_marshal(cc_do_send, &k.base);
    TcpClient.ok = k.result == PROTOCORE_NET_OK;
}

static void protocore_client_available(uint8_t *restrict work)
{
    if (TcpClient.cid < 0 || TcpClient.cid >= PROTOCORE_CLIENT_CONNS)
    {
        TcpClient.n = 0;
        return;
    }
    TCP_CLIENT_CTX(work)->conn = &TCP_CLIENT_CTX(work)->cc[TcpClient.cid];
    // Step the open here too, not only in connected() / is_closed(): a caller that polls a slot by
    // asking what has arrived would otherwise never advance a connect that has not come up, and sit
    // on an empty ring until its own timer expired.
    cc_pump(work);
    TcpClient.n = protocore_ring_available(&TCP_CLIENT_CTX(work)->conn->head, &TCP_CLIENT_CTX(work)->conn->tail,
                                           PROTOCORE_CLIENT_RX_BUF);
}

static void protocore_client_read(uint8_t *restrict work)
{
    if (TcpClient.cid < 0 || TcpClient.cid >= PROTOCORE_CLIENT_CONNS)
    {
        TcpClient.n = 0;
        return;
    }
    TCP_CLIENT_CTX(work)->conn = &TCP_CLIENT_CTX(work)->cc[TcpClient.cid];
    cc_pump(work); // as in available(): a caller that only ever reads still steps its open along
    TcpClient.n =
        protocore_ring_read(TCP_CLIENT_CTX(work)->conn->rx, PROTOCORE_CLIENT_RX_BUF, &TCP_CLIENT_CTX(work)->conn->head,
                            &TCP_CLIENT_CTX(work)->conn->tail, TcpClient.io.buf, TcpClient.io.cap);
    if (TcpClient.n > 0 && TCP_CLIENT_CTX(work)->conn->pcb)
    {
        // Ack-on-consume: reopen the receive window by exactly what we just drained.
        CcRecvedCall k;
        mem.set(&k, 0, sizeof(k));
        k.c = TCP_CLIENT_CTX(work)->conn;
        k.len = (proto_u16)TcpClient.n;
        protocore_net_call_marshal(cc_do_recved, &k.base);
    }
}

static void protocore_client_close(uint8_t *restrict work)
{
    if (TcpClient.cid < 0 || TcpClient.cid >= PROTOCORE_CLIENT_CONNS || !TCP_CLIENT_CTX(work)->cc[TcpClient.cid].in_use)
    {
        return;
    }
    CcSendCall k;
    mem.set(&k, 0, sizeof(k));
    k.c = &TCP_CLIENT_CTX(work)->cc[TcpClient.cid];
    protocore_net_call_marshal(cc_do_close, &k.base);
    TCP_CLIENT_CTX(work)->cc[TcpClient.cid].in_use = PROTO_FALSE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
TcpClientNs TcpClient = {.open = protocore_client_open,
                         .connected = protocore_client_connected,
                         .is_closed = protocore_client_is_closed,
                         .send = protocore_client_send,
                         .available = protocore_client_available,
                         .read = protocore_client_read,
                         .close = protocore_client_close};

#endif // PROTOCORE_NEED_CLIENT
