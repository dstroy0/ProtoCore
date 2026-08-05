// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file client.c
 * @brief Layer 4 outbound TCP client transport (pooled). See client.h.
 *
 * Mirrors the server transport's cross-thread rule: every raw lwIP call runs in
 * tcpip_thread via tcpip_api_call(). Each slot owns its pcb and an SPSC wire ring
 * (producer = the lwIP recv callback in tcpip_thread; consumer = the caller's
 * loop/blocking task). The rings use `volatile` indices, matching the shipped
 * per-client implementations this consolidates.
 */

#include "client.h"

// Compiles only on a target build AND only when a client transport is actually enabled
// (HTTP client / MQTT / WS client). A server-only build leaves DNS_RESOLVER off,
// so the resolver symbols this unit calls would not be declared - see
// PC_NEED_CLIENT in protocore_config.h.
#if PROTOCORE_HOT && PC_NEED_CLIENT

#include "board_drivers/board_profiles/pc_platform.h" // the target's TCP, under our names
#include "diffserv.h" // DiffServ DSCP marking for outbound client connections (compiles out when off)
#include "network_drivers/network/dns_resolver.h" // shared host->IP resolve (one DNS owner)
#include "server/clock/clock.h"                   // pc_millis()
#include "shared_primitives/ring.h" // PROTO_ATOMIC_LOAD/STORE + SPSC ring drain (same primitive as the server)

typedef struct
{
    pc_pcb *pcb;
    volatile proto_bool in_use;
    volatile proto_bool connected;
    volatile proto_bool closed; // peer FIN or error
    uint8_t rx[PC_CLIENT_RX_BUF];
    _Atomic size_t head; // producer (stack recv cb); acquire/release SPSC, same as the server ring
    _Atomic size_t tail; // consumer (caller)
} ClientConn;

// Outbound client connection pool, owned by one instance (internal linkage): the per-slot
// ClientConn state. One named owner, unreachable from any other translation unit.
typedef struct
{
    ClientConn cc[PC_CLIENT_CONNS];
} pc_client_ctx;
static pc_client_ctx s_client;

// Hostname resolution calls pc_dns_resolver_resolve (network_drivers/network/dns_resolver).

// --- lwIP callbacks (tcpip_thread); arg = the owning ClientConn* -------------

static pc_net_err cc_recv(void *arg, pc_pcb *tpcb, pc_pbuf *p, pc_net_err err)
{
    (void)err;
    ClientConn *c = (ClientConn *)arg;
    if (!c)
    {
        return PC_NET_OK;
    }
    if (p == NULL)
    {
        c->closed = PROTO_TRUE; // peer closed
        return PC_NET_OK;
    }
    // Wire bytes -> ring via the shared producer primitive (same as the server): if
    // the whole segment will not fit, refuse it (the stack retains + redelivers); else
    // move each span and publish head once.
    (void)tpcb;
    if (p->tot_len > pc_ring_free(&c->head, &c->tail, PC_CLIENT_RX_BUF))
    {
        return PC_NET_ERR_MEM;
    }
    size_t h = PROTO_ATOMIC_LOAD(&c->head); // sole producer of head; advance a local and publish once
    for (pc_pbuf *q = p; q != NULL; q = q->next)
    {
        h = pc_ring_write_span(c->rx, PC_CLIENT_RX_BUF, h, (const uint8_t *)q->payload, q->len);
    }
    PROTO_ATOMIC_STORE(&c->head, h); // one release store publishes the whole segment
    // Do NOT tcp_recved() here. The window is reopened by pc_client_read() as the
    // caller drains (ack-on-consume), so it tracks ring occupancy and the peer can
    // never overflow the ring - same model as the server transport. ACKing on copy
    // would decouple the window from drainage and deadlock a large inbound transfer
    // once PC_CLIENT_RX_BUF < TCP_WND.
    pc_net_pbuf_free(p);
    return PC_NET_OK;
}

static pc_net_err cc_connected(void *arg, pc_pcb *tpcb, pc_net_err err)
{
    (void)tpcb;
    ClientConn *c = (ClientConn *)arg;
    if (c)
    {
        if (err == PC_NET_OK)
        {
            c->connected = PROTO_TRUE;
        }
        else
        {
            c->closed = PROTO_TRUE;
        }
    }
    return PC_NET_OK;
}

static void cc_err(void *arg, pc_net_err err)
{
    (void)err;
    ClientConn *c = (ClientConn *)arg;
    if (c)
    {
        c->pcb = NULL; // the stack already freed it
        c->closed = PROTO_TRUE;
    }
}

// --- tcpip_thread-marshaled ops ---------------------------------------------

typedef struct
{
    pc_net_call base;
    ClientConn *c;
    pc_net_ip addr;
    uint16_t port;
    pc_net_err result;
} CcConnCall;
typedef struct
{
    pc_net_call base;
    ClientConn *c;
    const void *data;
    proto_u16 len;
    pc_net_err result;
} CcSendCall;
typedef struct
{
    pc_net_call base;
    ClientConn *c;
    proto_u16 len;
} CcRecvedCall;

static pc_net_err cc_do_connect(pc_net_call *cd)
{
    CcConnCall *k = (CcConnCall *)cd;
    ClientConn *c = k->c;
    c->pcb = pc_net_new(PC_NET_TYPE_V4);
    if (!c->pcb)
    {
        k->result = PC_NET_ERR_MEM;
        return PC_NET_OK;
    }
    pc_net_arg(c->pcb, c);
    pc_net_on_recv(c->pcb, cc_recv);
    pc_net_on_err(c->pcb, cc_err);
#if PC_ENABLE_DIFFSERV
    {
        // Mark the outbound connection with the server-wide default DSCP (the SYN onward). Runs in
        // tcpip_thread (this is the marshalled connect op), so touching the pcb is race-free.
        uint8_t dscp = pc_diffserv_default_dscp();
        if (dscp)
        {
            c->pcb->tos = pc_dscp_to_tos(dscp);
        }
    }
#endif
    k->result = pc_net_connect(c->pcb, &k->addr, k->port, cc_connected);
    return PC_NET_OK;
}

static pc_net_err cc_do_send(pc_net_call *cd)
{
    CcSendCall *k = (CcSendCall *)cd;
    ClientConn *c = k->c;
    if (!c->pcb)
    {
        k->result = PC_NET_ERR_CONN;
        return PC_NET_OK;
    }
    k->result = pc_net_write(c->pcb, k->data, k->len, PC_NET_WRITE_COPY);
    if (k->result == PC_NET_OK)
    {
        pc_net_output(c->pcb);
    }
    return PC_NET_OK;
}

static pc_net_err cc_do_close(pc_net_call *cd)
{
    CcSendCall *k = (CcSendCall *)cd;
    ClientConn *c = k->c;
    if (c->pcb)
    {
        pc_net_arg(c->pcb, NULL);
        pc_net_on_recv(c->pcb, NULL);
        pc_net_on_err(c->pcb, NULL);
        if (pc_net_close(c->pcb) != PC_NET_OK)
        {
            pc_net_abort(c->pcb);
        }
        c->pcb = NULL;
    }
    return PC_NET_OK;
}

static pc_net_err cc_do_recved(pc_net_call *cd)
{
    CcRecvedCall *k = (CcRecvedCall *)cd;
    if (k->c->pcb)
    {
        pc_net_recved(k->c->pcb, k->len); // reopen the window by the consumed bytes
    }
    return PC_NET_OK;
}

// --- public API --------------------------------------------------------------

int pc_client_open(const char *host, uint16_t port, uint32_t timeout_ms)
{
    int cid = -1;
    for (int i = 0; i < PC_CLIENT_CONNS; i++)
    {
        if (!s_client.cc[i].in_use)
        {
            cid = i;
            break;
        }
    }
    if (cid < 0)
    {
        return -1; // pool full
    }

    ClientConn *c = &s_client.cc[cid];
    c->pcb = NULL;
    c->connected = PROTO_FALSE;
    c->closed = PROTO_FALSE;
    PROTO_ATOMIC_STORE(&c->head, 0);
    PROTO_ATOMIC_STORE(&c->tail, 0);
    c->in_use = PROTO_TRUE;

    // Resolve through the shared DNS owner (its own PC_DNS_TIMEOUT_MS budget),
    // then give the connect its full timeout_ms.
    uint32_t ip = 0;
    if (!pc_dns_resolver_resolve(host, &ip))
    {
        c->in_use = PROTO_FALSE;
        return -2; // DNS failure
    }
    uint32_t deadline = pc_millis() + timeout_ms;

    CcConnCall k;
    memset(&k, 0, sizeof(k));
    k.c = c;
    pc_net_ip4_set(&k.addr, (uint8_t)(ip >> 24), (uint8_t)(ip >> 16), (uint8_t)(ip >> 8), (uint8_t)ip);
    k.port = port;
    pc_net_call_marshal(cc_do_connect, &k.base);
    if (k.result != PC_NET_OK)
    {
        pc_client_close(cid);
        return -3; // connect issue
    }
    while (!c->connected && !c->closed && (int32_t)(deadline - pc_millis()) > 0)
    {
        pcdelay(5);
    }
    if (!c->connected)
    {
        pc_client_close(cid);
        return -4; // connect timeout / refused
    }
    return cid;
}

proto_bool pc_client_connected(int cid)
{
    return cid >= 0 && cid < PC_CLIENT_CONNS && s_client.cc[cid].in_use && s_client.cc[cid].connected &&
           !s_client.cc[cid].closed;
}

proto_bool pc_client_is_closed(int cid)
{
    if (cid < 0 || cid >= PC_CLIENT_CONNS)
    {
        return PROTO_TRUE;
    }
    return s_client.cc[cid].closed;
}

proto_bool pc_client_send(int cid, const void *data, size_t len)
{
    if (cid < 0 || cid >= PC_CLIENT_CONNS || !s_client.cc[cid].in_use)
    {
        return PROTO_FALSE;
    }
    CcSendCall k;
    memset(&k, 0, sizeof(k));
    k.c = &s_client.cc[cid];
    k.data = data;
    k.len = (proto_u16)(len > 0xFFFF ? 0xFFFF : len);
    pc_net_call_marshal(cc_do_send, &k.base);
    return k.result == PC_NET_OK;
}

size_t pc_client_available(int cid)
{
    if (cid < 0 || cid >= PC_CLIENT_CONNS)
    {
        return 0;
    }
    ClientConn *c = &s_client.cc[cid];
    return pc_ring_available(&c->head, &c->tail, PC_CLIENT_RX_BUF);
}

size_t pc_client_read(int cid, uint8_t *buf, size_t cap)
{
    if (cid < 0 || cid >= PC_CLIENT_CONNS)
    {
        return 0;
    }
    ClientConn *c = &s_client.cc[cid];
    size_t n = pc_ring_read(c->rx, PC_CLIENT_RX_BUF, &c->head, &c->tail, buf, cap);
    if (n > 0 && c->pcb)
    {
        // Ack-on-consume: reopen the receive window by exactly what we just drained.
        CcRecvedCall k;
        memset(&k, 0, sizeof(k));
        k.c = c;
        k.len = (proto_u16)n;
        pc_net_call_marshal(cc_do_recved, &k.base);
    }
    return n;
}

void pc_client_close(int cid)
{
    if (cid < 0 || cid >= PC_CLIENT_CONNS || !s_client.cc[cid].in_use)
    {
        return;
    }
    CcSendCall k;
    memset(&k, 0, sizeof(k));
    k.c = &s_client.cc[cid];
    pc_net_call_marshal(cc_do_close, &k.base);
    s_client.cc[cid].in_use = PROTO_FALSE;
}

#else // host stub - the clients need a target's TCP, so a host build no-ops

int pc_client_open(const char *host, uint16_t port, uint32_t timeout_ms)
{
    (void)host;
    (void)port;
    (void)timeout_ms;
    return -1;
}
proto_bool pc_client_connected(int cid)
{
    (void)cid;
    return PROTO_FALSE;
}
proto_bool pc_client_is_closed(int cid)
{
    (void)cid;
    return PROTO_TRUE;
}
proto_bool pc_client_send(int cid, const void *data, size_t len)
{
    (void)cid;
    (void)data;
    (void)len;
    return PROTO_FALSE;
}
size_t pc_client_available(int cid)
{
    (void)cid;
    return 0;
}
size_t pc_client_read(int cid, uint8_t *buf, size_t cap)
{
    (void)cid;
    (void)buf;
    (void)cap;
    return 0;
}
void pc_client_close(int cid)
{
    (void)cid;
    // no-op: the native stub owns no socket to close
}

#endif // PROTOCORE_HOT && PC_NEED_CLIENT
