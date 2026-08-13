// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file udp_listener.c
 * @brief Layer 4 UDP receiving side. See udp_listener.h.
 *
 * One body. A port is bound, and a datagram leaves, through the stack surface protocore_platform.h
 * names; a build with no vendor gets that surface from the host driver on its include path, so
 * this file compiles and runs the same way on both.
 */

#include "network_drivers/transport/udp/udp_listener.h"
#include "network_drivers/transport/udp/udp_datagram.h" // the wire layout the receive ring carries

#include "core_setup/board_profiles/protocore_platform.h" // the stack's UDP, under our names
#include "network_drivers/transport/diffserv.h"    // DSCP marking; compiles out when off
#include "network_drivers/transport/net_addr.h"    // NetAddr: the stack's address as a protocore_ip

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
    uint16_t port;          ///< Bound port, 0 when the slot is free.
    protocore_udp_handler handler; ///< Called once per received datagram by poll().
    void *ctx;              ///< Opaque context handed back to the handler.
    protocore_ip group;            ///< Multicast group this slot joined; meaningful only when mcast is set.
    proto_bool mcast;       ///< The slot joined a group and must leave it on teardown.

    uint8_t rx[PROTOCORE_UDP_RX_RING];
    _Atomic size_t rx_head; ///< Producer: the stack's trampoline.
    _Atomic size_t rx_tail; ///< Consumer: poll().

    protocore_udp_pcb *pcb; ///< The stack's control block; NULL when the slot is free.
} UdpBind;

static_assert(PROTOCORE_RING_POW2(PROTOCORE_UDP_RX_RING), "PROTOCORE_UDP_RX_RING must be a power of two: a ring index wraps with a mask");
static_assert(PROTOCORE_MAX_UDP_LISTENERS <= PROTOCORE_RING_SLOTS_MAX,
              "the bound-slot bitmask (UdpListenerCtx::bound) is a uint32; raise it or fall back to a scan");

/**
 * @brief All receiving-side UDP state, owned by one instance.
 *
 * One staging buffer per role rather than per slot. The payload stage is held for the length of one
 * delivery, and each header stage belongs to exactly one end of the receive ring, so no two tasks
 * reach the same buffer: the trampoline writes rx_whdr and poll() reads rx_rhdr.
 */
typedef struct
{
    UdpBind bind[PROTOCORE_MAX_UDP_LISTENERS];
    uint8_t rx_stage[PROTOCORE_UDP_RX_BUF_SIZE]; ///< Contiguous payload handed to the handler.
    uint8_t rx_whdr[PROTOCORE_UDP_DGRAM_HDR];    ///< Header staged by the receive ring's producer.
    uint8_t rx_rhdr[PROTOCORE_UDP_DGRAM_HDR];    ///< Header staged by the receive ring's consumer.
    char group_text[PROTOCORE_IP_STR_MAX];       ///< Where joined_group() formats the group it reports.
    _Atomic uint32_t bound;               ///< Bit i set = bind[i] is bound. One ctz instead of a scan.
    proto_bool polling;                   ///< Set for the duration of poll(); a reentrant call returns.
} UdpListenerCtx;

static UdpListenerCtx s_lst;

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
    return Ip.classify(a) == PROTOCORE_IP_SCOPE_MULTICAST;
}

/** @brief The slot index @p b sits at. */
static size_t bind_idx(const UdpBind *b)
{
    return (size_t)(b - s_lst.bind);
}

/** @brief True when slot @p idx is bound. */
static proto_bool bind_used(size_t idx)
{
    return (PROTO_ATOMIC_LOAD(&s_lst.bound) & protocore_slot_bit(idx)) != 0u;
}

/** @brief The bound slot for @p port, or NULL. */
static UdpBind *find_bind(uint16_t port)
{
    uint32_t m = PROTO_ATOMIC_LOAD(&s_lst.bound) & protocore_slot_all(PROTOCORE_MAX_UDP_LISTENERS);
    while (m != 0u)
    {
        int32_t i = protocore_slot_next(m);
        if (s_lst.bind[i].port == port)
        {
            return &s_lst.bind[i];
        }
        m &= ~protocore_slot_bit((size_t)i);
    }
    return NULL;
}

/** @brief The first free slot, or NULL when the pool is full. */
static UdpBind *free_bind(void)
{
    uint32_t free_slots = ~PROTO_ATOMIC_LOAD(&s_lst.bound) & protocore_slot_all(PROTOCORE_MAX_UDP_LISTENERS);
    int32_t i = protocore_slot_next(free_slots);
    if (i < 0)
    {
        return NULL;
    }
    return &s_lst.bind[i];
}

/** @brief Reset a slot's ring and handler state, leaving it free. */
static void bind_clear(UdpBind *b)
{
    protocore_ip empty = {PROTOCORE_IP_NONE, {0}};
    b->port = 0;
    b->handler = NULL;
    b->ctx = NULL;
    b->group = empty;
    b->mcast = PROTO_FALSE;
    protocore_slot_clear(&s_lst.bound, bind_idx(b));
    PROTO_ATOMIC_STORE(&b->rx_head, 0);
    PROTO_ATOMIC_STORE(&b->rx_tail, 0);
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
    uint8_t dscp = DiffServ.udp_dscp();
    if (pcb != NULL && dscp != 0)
    {
        pcb->tos = protocore_dscp_to_tos(dscp);
    }
#else
    (void)pcb;
#endif
}

// Allocate, copy, send, free. Runs in the stack's thread only.
static proto_bool wire_send(protocore_udp_pcb *pcb, const protocore_ip *a, uint16_t port, const uint8_t *data, size_t len)
{
    protocore_net_ip dst;
    if (!NetAddr.from_ip(a, &dst))
    {
        return PROTO_FALSE; // a family this stack cannot send to, refused before a pbuf is taken
    }
    protocore_pbuf *p = protocore_net_pbuf_alloc(PROTOCORE_NET_PBUF_TRANSPORT, (proto_u16)len, PROTOCORE_NET_PBUF_RAM);
    if (p == NULL)
    {
        return PROTO_FALSE;
    }
    proto_raw_read((uint8_t *)p->payload, data, len);
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
static void udp_trampoline(void *arg, protocore_udp_pcb *pcb, protocore_pbuf *p, const protocore_net_ip *addr, proto_u16 port)
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
    NetAddr.to_ip(addr, &d.addr);
    d.port = port;
    d.len = n;
    if ((PROTOCORE_UDP_DGRAM_HDR + (size_t)n) > protocore_ring_free(&b->rx_head, &b->rx_tail, PROTOCORE_UDP_RX_RING))
    {
        protocore_net_pbuf_free(p); // ring full: drop, which is what UDP already means
        return;
    }
    protocore_span w = protocore_span_from(s_lst.rx_whdr, sizeof(s_lst.rx_whdr));
    protocore_udp_dgram_encode(&w, &d);
    if (!protocore_span_ok(w))
    {
        protocore_net_pbuf_free(p);
        return;
    }
    size_t h = PROTO_ATOMIC_LOAD(&b->rx_head);
    h = protocore_ring_write_span(b->rx, PROTOCORE_UDP_RX_RING, h, s_lst.rx_whdr, PROTOCORE_UDP_DGRAM_HDR);
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
        if (!NetAddr.from_ip(&k->group, &grp))
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
        if (NetAddr.from_ip(&k->b->group, &grp))
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

// Run one marshaled op and report what it set.
static proto_bool marshal_op(protocore_udp_op op, UdpBind *b, uint16_t port, const protocore_ip *group)
{
    protocore_udp_call k = {{0}, UDP_OP_BIND, NULL, 0, {PROTOCORE_IP_NONE, {0}}, PROTO_FALSE};
    k.op = op;
    k.b = b;
    k.port = port;
    if (group != NULL)
    {
        k.group = *group;
    }
    protocore_net_call_marshal(udp_do, &k.base);
    return k.result;
}

static proto_bool bind_port(UdpBind *b, uint16_t port)
{
    return marshal_op(UDP_OP_BIND, b, port, NULL);
}

static proto_bool bind_group(UdpBind *b, uint16_t port, const protocore_ip *group)
{
    return marshal_op(UDP_OP_BIND_MCAST, b, port, group);
}

static void unbind_port(UdpBind *b)
{
    if (b->mcast)
    {
        (void)marshal_op(UDP_OP_LEAVE_MCAST, b, 0, NULL);
        return;
    }
    (void)marshal_op(UDP_OP_UNBIND, b, 0, NULL);
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

// Send one datagram out of slot @p b, from where the caller's bytes already are.
static proto_bool send_now(UdpBind *b, const protocore_ip *a, uint16_t port, const uint8_t *data, size_t len)
{
    if (b == NULL || a == NULL || data == NULL || len == 0 || len > PROTOCORE_UDP_RX_BUF_SIZE)
    {
        return PROTO_FALSE;
    }
    // The marshal is synchronous, so this outlives the call and carries its answer back.
    protocore_udp_send_call k = {{0}, b, a, data, len, port, PROTO_FALSE};
    (void)protocore_net_call_marshal(send_do, &k.base);
    return k.ok;
}

// ---------------------------------------------------------------------------
// The bodies behind the table
// ---------------------------------------------------------------------------

static proto_bool listen_on(uint16_t port, protocore_udp_handler handler, void *ctx)
{
    // A port already bound rebinds its own slot: a second slot on one port is one find_bind() can
    // never reach, and it spends a slot the pool has two of.
    UdpBind *b = find_bind(port);
    if (b != NULL)
    {
        b->handler = handler;
        b->ctx = ctx;
        return PROTO_TRUE;
    }
    b = free_bind();
    if (b == NULL)
    {
        return PROTO_FALSE; // pool exhausted
    }
    bind_clear(b);
    // The trampoline reads handler and ctx as soon as recv is armed, so set them first.
    b->handler = handler;
    b->ctx = ctx;
    b->port = port;
    if (!bind_port(b, port))
    {
        b->handler = NULL;
        return PROTO_FALSE;
    }
    protocore_slot_mark(&s_lst.bound, bind_idx(b));
    return PROTO_TRUE;
}

static proto_bool listen_group(const char *group_ip, uint16_t port, protocore_udp_handler handler, void *ctx)
{
    protocore_ip group = {PROTOCORE_IP_NONE, {0}};
    if (!Ip.parse(group_ip, &group))
    {
        return PROTO_FALSE;
    }
    if (!addr_is_group(&group))
    {
        return PROTO_FALSE; // joining a unicast address would silently never deliver
    }
    UdpBind *b = free_bind();
    if (b == NULL)
    {
        return PROTO_FALSE;
    }
    bind_clear(b);
    b->handler = handler;
    b->ctx = ctx;
    b->port = port;
    if (!bind_group(b, port, &group))
    {
        b->handler = NULL;
        return PROTO_FALSE;
    }
    protocore_slot_mark(&s_lst.bound, bind_idx(b));
    return PROTO_TRUE;
}

static proto_bool leave_group(uint16_t port)
{
    UdpBind *b = find_bind(port);
    if (b == NULL || !b->mcast)
    {
        return PROTO_FALSE;
    }
    unbind_port(b);
    bind_clear(b);
    return PROTO_TRUE;
}

static void poll_all(void)
{
    if (s_lst.polling)
    {
        return; // a handler called back into poll(); the stage is already in use
    }
    s_lst.polling = PROTO_TRUE;
    for (int i = 0; i < PROTOCORE_MAX_UDP_LISTENERS; i++)
    {
        UdpBind *b = &s_lst.bind[i];
        if (bind_used((size_t)i))
        {
            protocore_udp_dgram d = {{PROTOCORE_IP_NONE, {0}}, 0, 0};
            while (protocore_udp_dgram_take(b->rx, PROTOCORE_UDP_RX_RING, &b->rx_head, &b->rx_tail, s_lst.rx_rhdr, &d, s_lst.rx_stage,
                                     sizeof(s_lst.rx_stage)))
            {
                if (b->handler != NULL)
                {
                    protocore_udp_peer peer = {d.addr, d.port, b};
                    b->handler(s_lst.rx_stage, d.len, &peer, b->ctx);
                }
            }
        }
    }
    s_lst.polling = PROTO_FALSE;
}

static proto_bool reply_to(const struct protocore_udp_peer *peer, const uint8_t *data, size_t len)
{
    if (peer == NULL)
    {
        return PROTO_FALSE;
    }
    return send_now(peer->bind, &peer->addr, peer->port, data, len);
}

static proto_bool peer_addr_of(const struct protocore_udp_peer *peer, char *ip_out, size_t ip_cap, uint16_t *port_out)
{
    if (peer == NULL || ip_out == NULL || ip_cap < 8u)
    {
        return PROTO_FALSE;
    }
    if (Ip.format(&peer->addr, ip_out, ip_cap) == 0)
    {
        return PROTO_FALSE;
    }
    if (port_out != NULL)
    {
        *port_out = peer->port;
    }
    return PROTO_TRUE;
}

static proto_bool send_from(uint16_t listen_port, const protocore_ip *dst, uint16_t dst_port, const uint8_t *data, size_t len)
{
    UdpBind *b = find_bind(listen_port);
    if (b == NULL || dst == NULL || dst->family == PROTOCORE_IP_NONE)
    {
        return PROTO_FALSE;
    }
    return send_now(b, dst, dst_port, data, len);
}

// Close @p port: leave its group when it joined one, drop the stack's control block, free the slot.
static proto_bool close_port(uint16_t port)
{
    UdpBind *b = find_bind(port);
    if (b == NULL)
    {
        return PROTO_FALSE;
    }
    unbind_port(b);
    bind_clear(b);
    return PROTO_TRUE;
}

// The group @p port joined, formatted, or NULL when the port is unbound or joined none.
static const char *group_on(uint16_t port)
{
    UdpBind *b = find_bind(port);
    if (b == NULL || !b->mcast)
    {
        return NULL;
    }
    if (Ip.format(&b->group, s_lst.group_text, sizeof(s_lst.group_text)) == 0)
    {
        return NULL;
    }
    return s_lst.group_text;
}

// Designated, so a member's position in the struct does not decide what it binds to.
const UdpListenerNs UdpListener = {
    .listen = listen_on,
    .listen_multicast = listen_group,
    .leave_multicast = leave_group,
    .poll = poll_all,
    .reply = reply_to,
    .peer_addr = peer_addr_of,
    .sendto = send_from,
    .close = close_port,
    .joined_group = group_on,
};

PROTOCORE_END_DECLS
