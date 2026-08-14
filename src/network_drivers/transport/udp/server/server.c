// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file server.c
 * @brief Layer 4 UDP receiving side. See server.h.
 *
 * One body. A port is bound, and a datagram leaves, through the stack surface protocore_platform.h
 * names; a build with no vendor gets that surface from the host driver on its include path, so
 * this file compiles and runs the same way on both.
 */

#include "network_drivers/transport/udp/server/server.h"
#include "network_drivers/transport/udp/common.h" // the wire layout the receive ring carries

#include "core_setup/board_profiles/protocore_platform.h" // the stack's UDP, under our names
#include "network_drivers/transport/diffserv/diffserv.h"  // DSCP marking; compiles out when off
#include "network_drivers/transport/net_addr/net_addr.h"  // NetAddr: the stack's address as a protocore_ip

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// One bound port
// ---------------------------------------------------------------------------

/**
 * @brief State for one bound UDP port.
 *
 * One ring, on the receive side: its producer is the stack's trampoline and its consumer is poll().
 * The index pair is single-producer / single-consumer, so both are `_Atomic` and reached only
 * through PROTO_ATOMIC_LOAD / PROTO_ATOMIC_STORE. A send carries the caller's buffer to the wire
 * inside one marshaled call, so nothing queues on the way out.
 */
typedef struct
{
    uint16_t port;                 ///< Bound port, 0 when the slot is free.
    protocore_udp_handler handler; ///< Called once per received datagram by poll().
    void *ctx;                     ///< Opaque context handed back to the handler.
    protocore_ip group;            ///< Multicast group this slot joined; meaningful only when mcast is set.
    proto_bool mcast;              ///< The slot joined a group and must leave it on teardown.

    uint8_t rx[PROTOCORE_UDP_RX_RING];
    _Atomic size_t rx_head; ///< Producer: the stack's trampoline.
    _Atomic size_t rx_tail; ///< Consumer: poll().

    protocore_udp_pcb *pcb; ///< The stack's control block; NULL when the slot is free.
} UdpBind;

static_assert(PROTOCORE_RING_POW2(PROTOCORE_UDP_RX_RING),
              "PROTOCORE_UDP_RX_RING must be a power of two: a ring index wraps with a mask");
static_assert(PROTOCORE_MAX_UDP_LISTENERS <= PROTOCORE_RING_SLOTS_MAX,
              "the bound-slot bitmask (UdpListenerInternal::bound) is a uint32; raise it or fall back to a scan");

/**
 * @brief The receiving side's compile-time storage: the slot pool and every staging buffer.
 *
 * One staging buffer per role rather than per slot. The payload stage is held for the length of one
 * delivery, and each header stage belongs to exactly one end of the receive ring, so no two tasks
 * reach the same buffer: the trampoline writes rx_whdr and poll() reads rx_rhdr.
 */
struct UdpListenerStorage
{
    UdpBind bind[PROTOCORE_MAX_UDP_LISTENERS];
    uint8_t rx_stage[PROTOCORE_UDP_RX_BUF_SIZE]; ///< Contiguous payload handed to the handler.
    uint8_t rx_whdr[PROTOCORE_UDP_DGRAM_HDR];    ///< Header staged by the receive ring's producer.
    uint8_t rx_rhdr[PROTOCORE_UDP_DGRAM_HDR];    ///< Header staged by the receive ring's consumer.
    char group_text[PROTOCORE_IP_STR_MAX];       ///< Where joined_group() formats the group it reports.
};

/**
 * @brief The receiving side's state and the calls that reach it - what UdpListenerNs points at.
 *
 * @var UdpListenerInternal::store    the slot pool and the staging buffers
 * @var UdpListenerInternal::ns       the handle a caller sets a call's members on
 * @var UdpListenerInternal::bound    bit i set = store->bind[i] is bound; one ctz instead of a scan
 * @var UdpListenerInternal::polling  set for the duration of poll(); a reentrant call returns
 * @var UdpListenerInternal::bind     the slot the private steps below act on
 */
struct UdpListenerInternal
{
    struct UdpListenerStorage *store;
    UdpListenerNs *ns;
    _Atomic uint32_t bound;
    proto_bool polling;
    UdpBind *bind;
};

static struct UdpListenerStorage s_store;

static struct UdpListenerInternal s_server = {.store = &s_store, .ns = &UdpListener};

/** @brief The reply token a handler is given: the sender, and the slot the datagram arrived on. */
typedef struct protocore_udp_peer
{
    protocore_ip addr;
    uint16_t port;
    UdpBind *bind;
} protocore_udp_peer;

/** @brief True when @p a is an IPv4 multicast group (224.0.0.0/4), the only kind IGMP joins. */
static proto_bool addr_is_group(const protocore_ip *a)
{
    if (a->family != PROTOCORE_IP_V4)
    {
        return PROTO_FALSE;
    }
    Ip.args.ip = a;
    Ip.classify(Ip.internal);
    return Ip.scope == PROTOCORE_IP_SCOPE_MULTICAST;
}

/** @brief The slot index ctx->bind sits at. */
static size_t bind_idx(struct UdpListenerInternal *restrict ctx)
{
    return (size_t)(ctx->bind - ctx->store->bind);
}

/** @brief True when slot @p idx is bound. */
static proto_bool bind_used(struct UdpListenerInternal *restrict ctx, size_t idx)
{
    return (PROTO_ATOMIC_LOAD(&ctx->bound) & protocore_slot_bit(idx)) != 0u;
}

/** @brief Point ctx->bind at the bound slot for ns->port, or NULL. */
static void find_bind(struct UdpListenerInternal *restrict ctx)
{
    uint32_t m = PROTO_ATOMIC_LOAD(&ctx->bound) & protocore_slot_all(PROTOCORE_MAX_UDP_LISTENERS);
    while (m != 0u)
    {
        int32_t i = protocore_slot_next(m);
        if (ctx->store->bind[i].port == ctx->ns->port)
        {
            ctx->bind = &ctx->store->bind[i];
            return;
        }
        m &= ~protocore_slot_bit((size_t)i);
    }
    ctx->bind = NULL;
}

/** @brief Point ctx->bind at the first free slot, or NULL when the pool is full. */
static void free_bind(struct UdpListenerInternal *restrict ctx)
{
    uint32_t free_slots = ~PROTO_ATOMIC_LOAD(&ctx->bound) & protocore_slot_all(PROTOCORE_MAX_UDP_LISTENERS);
    int32_t i = protocore_slot_next(free_slots);
    ctx->bind = (i < 0) ? NULL : &ctx->store->bind[i];
}

/** @brief Reset ctx->bind's ring and handler state, leaving it free. */
static void bind_clear(struct UdpListenerInternal *restrict ctx)
{
    protocore_ip empty = {PROTOCORE_IP_NONE, {0}};
    ctx->bind->port = 0;
    ctx->bind->handler = NULL;
    ctx->bind->ctx = NULL;
    ctx->bind->group = empty;
    ctx->bind->mcast = PROTO_FALSE;
    protocore_slot_clear(&ctx->bound, bind_idx(ctx));
    PROTO_ATOMIC_STORE(&ctx->bind->rx_head, 0);
    PROTO_ATOMIC_STORE(&ctx->bind->rx_tail, 0);
}

// ---------------------------------------------------------------------------
// The stack: binding a port and putting a datagram on the wire
// ---------------------------------------------------------------------------

/** @brief Ops that must run in the stack's thread, reached through protocore_net_call_marshal. */
typedef enum PROTO_ENUM_PACKED
{
    UDP_OP_BIND,        ///< new + bind + arm recv on a slot
    UDP_OP_BIND_MCAST,  ///< as BIND, plus SO_REUSEADDR + IGMP join
    UDP_OP_LEAVE_MCAST, ///< IGMP leave + remove
    UDP_OP_UNBIND       ///< remove
} protocore_udp_op;

typedef struct
{
    protocore_net_call base;
    protocore_udp_op op;
    UdpBind *b;
    uint16_t port;
    protocore_ip group;
    proto_bool result;
} protocore_udp_call;

// Stamp a control block with the configured UDP DSCP, applied per send so a DiffServ.set_udp()
// change reaches the next datagram.
static void apply_dscp(protocore_udp_pcb *pcb)
{
#if PROTOCORE_ENABLE_DIFFSERV
    uint8_t dscp = protocore_diffserv_udp_dscp();
    if (pcb != NULL && dscp != 0)
    {
        pcb->tos = protocore_dscp_to_tos(dscp);
    }
#else
    (void)pcb;
#endif
}

// Allocate, copy, send, free. Runs in the stack's thread only.
static proto_bool wire_send(protocore_udp_pcb *pcb, const protocore_ip *a, uint16_t port, const uint8_t *data,
                            size_t len)
{
    protocore_net_ip dst;
    if (!protocore_net_addr_from_ip(a, &dst))
    {
        return PROTO_FALSE; // a family this stack cannot send to, refused before a pbuf is taken
    }
    protocore_pbuf *p = protocore_net_pbuf_alloc(PROTOCORE_NET_PBUF_TRANSPORT, (proto_u16)len, PROTOCORE_NET_PBUF_RAM);
    if (p == NULL)
    {
        return PROTO_FALSE;
    }
    raw.read((uint8_t *)p->payload, data, len);
    protocore_net_err e = protocore_net_udp_sendto(pcb, p, &dst, port);
    protocore_net_pbuf_free(p);
    return e == PROTOCORE_NET_OK;
}

/**
 * @brief Receive trampoline: frame the datagram into the slot's receive ring and return.
 *
 * Runs in the stack's thread and is the sole producer of that ring. The handler is not called here;
 * poll() calls it in the task that drains. The payload arrives as a chain, so the header is written
 * first and each segment follows it, all against a local head that is published once.
 */
static void udp_trampoline(void *arg, protocore_udp_pcb *pcb, protocore_pbuf *p, const protocore_net_ip *addr,
                           proto_u16 port)
{
    (void)pcb;
    UdpBind *b = (UdpBind *)arg;
    if (p == NULL)
    {
        return;
    }
    if (b == NULL)
    {
        protocore_net_pbuf_free(p);
        return;
    }
    proto_u16 n = p->tot_len;
    if (n > PROTOCORE_UDP_RX_BUF_SIZE)
    {
        n = (proto_u16)PROTOCORE_UDP_RX_BUF_SIZE; // a longer datagram is truncated to the staged length
    }
    protocore_udp_dgram d = {{PROTOCORE_IP_NONE, {0}}, 0, 0};
    protocore_net_addr_to_ip(addr, &d.addr);
    d.port = port;
    d.len = n;
    if ((PROTOCORE_UDP_DGRAM_HDR + (size_t)n) > protocore_ring_free(&b->rx_head, &b->rx_tail, PROTOCORE_UDP_RX_RING))
    {
        protocore_net_pbuf_free(p); // ring full: drop, which is what UDP already means
        return;
    }
    protocore_span w = span.from(s_store.rx_whdr, sizeof(s_store.rx_whdr));
    protocore_udp_dgram_encode(&w, &d);
    if (!span.ok(w))
    {
        protocore_net_pbuf_free(p);
        return;
    }
    size_t h = PROTO_ATOMIC_LOAD(&b->rx_head);
    h = protocore_ring_write_span(b->rx, PROTOCORE_UDP_RX_RING, h, s_store.rx_whdr, PROTOCORE_UDP_DGRAM_HDR);
    size_t left = n;
    for (protocore_pbuf *q = p; q != NULL && left > 0; q = q->next)
    {
        size_t take = q->len;
        if (take > left)
        {
            take = left;
        }
        h = protocore_ring_write_span(b->rx, PROTOCORE_UDP_RX_RING, h, (const uint8_t *)q->payload, take);
        left -= take;
    }
    PROTO_ATOMIC_STORE(&b->rx_head, h); // one release store publishes the whole entry
    protocore_net_pbuf_free(p);
}

static protocore_net_err udp_do(protocore_net_call *c)
{
    protocore_udp_call *k = (protocore_udp_call *)c;
    k->result = PROTO_FALSE;
    switch (k->op)
    {
    case UDP_OP_BIND: {
        protocore_udp_pcb *pcb = protocore_net_udp_new();
        if (pcb != NULL)
        {
            if (protocore_net_udp_bind(pcb, PROTOCORE_NET_ADDR_ANY, k->port) == PROTOCORE_NET_OK)
            {
                k->b->pcb = pcb;
                apply_dscp(pcb);
                protocore_net_udp_recv(pcb, udp_trampoline, k->b);
                k->result = PROTO_TRUE;
            }
            else
            {
                protocore_net_udp_remove(pcb);
            }
        }
        break;
    }
    case UDP_OP_BIND_MCAST: {
#if PROTOCORE_NET_HAS_IGMP
        protocore_net_ip grp;
        if (!protocore_net_addr_from_ip(&k->group, &grp))
        {
            break;
        }
        protocore_udp_pcb *pcb = protocore_net_udp_new();
        if (pcb != NULL)
        {
            // A well-known multicast port is normally already bound by whoever implements that
            // protocol, so co-bind. Bind IPv4-only rather than ANY: a dual-stack ANY control block
            // also matches the IPv4 datagrams the other responder is waiting on, and the stack hands
            // each datagram to the first match.
            protocore_net_opt_set(pcb, PROTOCORE_NET_OPT_REUSEADDR);
            if (protocore_net_udp_bind(pcb, PROTOCORE_NET_ADDR_ANY4, k->port) == PROTOCORE_NET_OK &&
                protocore_net_igmp_join(PROTOCORE_NET_ADDR_ANY4_P, protocore_net_ip_as_v4(&grp)) == PROTOCORE_NET_OK)
            {
                k->b->pcb = pcb;
                k->b->group = k->group;
                k->b->mcast = PROTO_TRUE;
                apply_dscp(pcb);
                protocore_net_udp_recv(pcb, udp_trampoline, k->b);
                k->result = PROTO_TRUE;
            }
            else
            {
                protocore_net_udp_remove(pcb);
            }
        }
#endif
        break;
    }
    case UDP_OP_LEAVE_MCAST: {
#if PROTOCORE_NET_HAS_IGMP
        protocore_net_ip grp;
        if (protocore_net_addr_from_ip(&k->b->group, &grp))
        {
            protocore_net_igmp_leave(PROTOCORE_NET_ADDR_ANY4_P, protocore_net_ip_as_v4(&grp));
        }
        if (k->b->pcb != NULL)
        {
            protocore_net_udp_remove(k->b->pcb);
        }
        k->b->pcb = NULL;
        k->result = PROTO_TRUE;
#endif
        break;
    }
    case UDP_OP_UNBIND:
        if (k->b->pcb != NULL)
        {
            protocore_net_udp_remove(k->b->pcb);
        }
        k->b->pcb = NULL;
        k->result = PROTO_TRUE;
        break;
    }
    return PROTOCORE_NET_OK;
}

// Run one marshaled op on ctx->bind and report what it set.
static proto_bool marshal_op(struct UdpListenerInternal *restrict ctx, protocore_udp_op op, uint16_t port,
                             const protocore_ip *group)
{
    protocore_udp_call k = {{0}, UDP_OP_BIND, NULL, 0, {PROTOCORE_IP_NONE, {0}}, PROTO_FALSE};
    k.op = op;
    k.b = ctx->bind;
    k.port = port;
    if (group != NULL)
    {
        k.group = *group;
    }
    protocore_net_call_marshal(udp_do, &k.base);
    return k.result;
}

// Drop the stack's control block for ctx->bind, leaving its group first when it joined one.
static void unbind_port(struct UdpListenerInternal *restrict ctx)
{
    if (ctx->bind->mcast)
    {
        (void)marshal_op(ctx, UDP_OP_LEAVE_MCAST, 0, NULL);
        return;
    }
    (void)marshal_op(ctx, UDP_OP_UNBIND, 0, NULL);
}

// The datagram, carried to the stack's thread by pointer. The caller's buffer is the send buffer,
// so nothing is copied into this and it holds no storage of its own.
typedef struct
{
    protocore_net_call base;
    UdpBind *b;
    const protocore_ip *dst;
    const uint8_t *data;
    size_t len;
    uint16_t port;
    proto_bool ok;
} protocore_udp_send_call;

// The send, on the stack's thread, from the slot's bound control block so the peer sees the port it
// is answering.
static protocore_net_err send_do(protocore_net_call *c)
{
    protocore_udp_send_call *k = (protocore_udp_send_call *)c;
    if (k->b->pcb == NULL)
    {
        return PROTOCORE_NET_OK; // unbound: k->ok stays false and the caller still holds its bytes
    }
    apply_dscp(k->b->pcb);
    k->ok = wire_send(k->b->pcb, k->dst, k->port, k->data, k->len);
    return PROTOCORE_NET_OK;
}

// Send one datagram out of ctx->bind to @p a, from where the caller's bytes already are.
static proto_bool send_now(struct UdpListenerInternal *restrict ctx, const protocore_ip *a, uint16_t port)
{
    if (ctx->bind == NULL || a == NULL || ctx->ns->send_args.data == NULL || ctx->ns->send_args.len == 0 ||
        ctx->ns->send_args.len > PROTOCORE_UDP_RX_BUF_SIZE)
    {
        return PROTO_FALSE;
    }
    // The marshal is synchronous, so this outlives the call and carries its answer back.
    protocore_udp_send_call k = {{0}, ctx->bind, a, ctx->ns->send_args.data, ctx->ns->send_args.len, port, PROTO_FALSE};
    (void)protocore_net_call_marshal(send_do, &k.base);
    return k.ok;
}

// ---------------------------------------------------------------------------
// The bodies behind the table
// ---------------------------------------------------------------------------

static void listen_on(struct UdpListenerInternal *restrict ctx)
{
    // A port already bound rebinds its own slot: a second slot on one port is one find_bind() can
    // never reach, and it spends a slot the pool has two of.
    find_bind(ctx);
    if (ctx->bind != NULL)
    {
        ctx->bind->handler = ctx->ns->bind.handler;
        ctx->bind->ctx = ctx->ns->bind.handler_ctx;
        ctx->ns->ok = PROTO_TRUE;
        return;
    }
    free_bind(ctx);
    if (ctx->bind == NULL)
    {
        ctx->ns->ok = PROTO_FALSE; // pool exhausted
        return;
    }
    bind_clear(ctx);
    // The trampoline reads handler and ctx as soon as recv is armed, so set them first.
    ctx->bind->handler = ctx->ns->bind.handler;
    ctx->bind->ctx = ctx->ns->bind.handler_ctx;
    ctx->bind->port = ctx->ns->port;
    if (!marshal_op(ctx, UDP_OP_BIND, ctx->ns->port, NULL))
    {
        ctx->bind->handler = NULL;
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    protocore_slot_mark(&ctx->bound, bind_idx(ctx));
    ctx->ns->ok = PROTO_TRUE;
}

static void listen_group(struct UdpListenerInternal *restrict ctx)
{
    protocore_ip group = {PROTOCORE_IP_NONE, {0}};
    ctx->ns->ok = PROTO_FALSE;
    Ip.args.text = ctx->ns->bind.group_ip;
    Ip.args.out = &group;
    Ip.parse(Ip.internal);
    if (!Ip.ok)
    {
        return;
    }
    if (!addr_is_group(&group))
    {
        return; // joining a unicast address would silently never deliver
    }
    free_bind(ctx);
    if (ctx->bind == NULL)
    {
        return;
    }
    bind_clear(ctx);
    ctx->bind->handler = ctx->ns->bind.handler;
    ctx->bind->ctx = ctx->ns->bind.handler_ctx;
    ctx->bind->port = ctx->ns->port;
    if (!marshal_op(ctx, UDP_OP_BIND_MCAST, ctx->ns->port, &group))
    {
        ctx->bind->handler = NULL;
        return;
    }
    protocore_slot_mark(&ctx->bound, bind_idx(ctx));
    ctx->ns->ok = PROTO_TRUE;
}

static void leave_group(struct UdpListenerInternal *restrict ctx)
{
    find_bind(ctx);
    if (ctx->bind == NULL || !ctx->bind->mcast)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    unbind_port(ctx);
    bind_clear(ctx);
    ctx->ns->ok = PROTO_TRUE;
}

static void poll_all(struct UdpListenerInternal *restrict ctx)
{
    if (ctx->polling)
    {
        return; // a handler called back into poll(); the stage is already in use
    }
    ctx->polling = PROTO_TRUE;
    for (int i = 0; i < PROTOCORE_MAX_UDP_LISTENERS; i++)
    {
        UdpBind *b = &ctx->store->bind[i];
        if (bind_used(ctx, (size_t)i))
        {
            protocore_udp_dgram d = {{PROTOCORE_IP_NONE, {0}}, 0, 0};
            while (protocore_udp_dgram_take(b->rx, PROTOCORE_UDP_RX_RING, &b->rx_head, &b->rx_tail, ctx->store->rx_rhdr,
                                            &d, ctx->store->rx_stage, sizeof(ctx->store->rx_stage)))
            {
                if (b->handler != NULL)
                {
                    protocore_udp_peer peer = {d.addr, d.port, b};
                    b->handler(ctx->store->rx_stage, d.len, &peer, b->ctx);
                }
            }
        }
    }
    ctx->polling = PROTO_FALSE;
}

static void reply_to(struct UdpListenerInternal *restrict ctx)
{
    if (ctx->ns->peer_args.peer == NULL)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    ctx->bind = ctx->ns->peer_args.peer->bind;
    ctx->ns->ok = send_now(ctx, &ctx->ns->peer_args.peer->addr, ctx->ns->peer_args.peer->port);
}

static void peer_addr_of(struct UdpListenerInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    if (ctx->ns->peer_args.peer == NULL || ctx->ns->peer_args.ip_out == NULL || ctx->ns->peer_args.ip_cap < 8u)
    {
        return;
    }
    Ip.args.ip = &ctx->ns->peer_args.peer->addr;
    Ip.args.buf = ctx->ns->peer_args.ip_out;
    Ip.args.cap = ctx->ns->peer_args.ip_cap;
    Ip.format(Ip.internal);
    if (Ip.n == 0)
    {
        return;
    }
    if (ctx->ns->peer_args.port_out != NULL)
    {
        *ctx->ns->peer_args.port_out = ctx->ns->peer_args.peer->port;
    }
    ctx->ns->ok = PROTO_TRUE;
}

static void send_from(struct UdpListenerInternal *restrict ctx)
{
    find_bind(ctx);
    if (ctx->bind == NULL || ctx->ns->send_args.dst == NULL || ctx->ns->send_args.dst->family == PROTOCORE_IP_NONE)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    ctx->ns->ok = send_now(ctx, ctx->ns->send_args.dst, ctx->ns->send_args.dst_port);
}

// Close ns->port: leave its group when it joined one, drop the control block, free the slot.
static void close_port(struct UdpListenerInternal *restrict ctx)
{
    find_bind(ctx);
    if (ctx->bind == NULL)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    unbind_port(ctx);
    bind_clear(ctx);
    ctx->ns->ok = PROTO_TRUE;
}

// The group ns->port joined, formatted, or NULL when the port is unbound or joined none.
static void group_on(struct UdpListenerInternal *restrict ctx)
{
    ctx->ns->text = NULL;
    find_bind(ctx);
    if (ctx->bind == NULL || !ctx->bind->mcast)
    {
        return;
    }
    Ip.args.ip = &ctx->bind->group;
    Ip.args.buf = ctx->store->group_text;
    Ip.args.cap = sizeof(ctx->store->group_text);
    Ip.format(Ip.internal);
    if (Ip.n == 0)
    {
        return;
    }
    ctx->ns->text = ctx->store->group_text;
}

// Designated, so a member's position in the struct does not decide what it binds to.
UdpListenerNs UdpListener = {
    .listen = listen_on,
    .listen_multicast = listen_group,
    .leave_multicast = leave_group,
    .poll = poll_all,
    .reply = reply_to,
    .peer_addr = peer_addr_of,
    .sendto = send_from,
    .close = close_port,
    .joined_group = group_on,
    .internal = &s_server,
};

PROTOCORE_END_DECLS
