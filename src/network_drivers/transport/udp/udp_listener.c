// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file udp_listener.c
 * @brief Layer 4 UDP receiving side. See udp_listener.h.
 *
 * The ring mechanics are one body. Two things differ between a target and a host build: how a port
 * is bound, and where a queued datagram goes when it leaves. Those are the seam functions below
 * (bind_port, bind_group, unbind_port, wire_send); everything above them is shared. An address is
 * a pc_ip on both, so nothing about parsing, printing, or classifying one has an arm.
 */

#include "network_drivers/transport/udp/udp_listener.h"
#include "network_drivers/transport/udp/udp_datagram.h" // the wire layout the rings carry

#if PROTOCORE_HOT
#include "board_drivers/board_profiles/pc_platform.h" // the target's UDP, under our names
#include "network_drivers/transport/diffserv.h"       // DSCP marking; compiles out when off
#include "network_drivers/transport/net_addr.h"       // NetAddr: the stack's address as a pc_ip
#endif

PROTO_BEGIN_DECLS

// ---------------------------------------------------------------------------
// One bound port
// ---------------------------------------------------------------------------

/**
 * @brief State for one bound UDP port.
 *
 * Two rings. The receive ring's producer is the stack's trampoline and its consumer is poll(); the
 * send ring's producer is reply() / sendto() and its consumer is the marshaled flush, which runs in
 * the stack's thread. Each index pair is single-producer / single-consumer, so both are `_Atomic`
 * and reached only through PROTO_ATOMIC_LOAD / PROTO_ATOMIC_STORE.
 */
typedef struct
{
    uint16_t port;          ///< Bound port, 0 when the slot is free.
    pc_udp_handler handler; ///< Called once per received datagram by poll().
    void *ctx;              ///< Opaque context handed back to the handler.
    pc_ip group;            ///< Multicast group this slot joined; meaningful only when mcast is set.
    proto_bool mcast;       ///< The slot joined a group and must leave it on teardown.
    proto_bool used;        ///< The slot is bound.

    uint8_t rx[PC_UDP_RX_RING];
    _Atomic size_t rx_head; ///< Producer: the stack's trampoline.
    _Atomic size_t rx_tail; ///< Consumer: poll().

    uint8_t tx[PC_UDP_TX_RING];
    _Atomic size_t tx_head; ///< Producer: reply() / sendto().
    _Atomic size_t tx_tail; ///< Consumer: the marshaled flush.

#if PROTOCORE_HOT
    pc_udp_pcb *pcb; ///< The stack's control block; NULL when the slot is free.
#endif
} UdpBind;

/**
 * @brief All receiving-side UDP state, owned by one instance.
 *
 * One staging buffer per role rather than per slot. A payload stage is held for the length of one
 * delivery and one flush, and each header stage belongs to exactly one end of one ring, so no two
 * tasks reach the same buffer: the trampoline writes rx_whdr, poll() reads rx_rhdr, a send writes
 * tx_whdr, and the flush reads tx_rhdr.
 */
typedef struct
{
    UdpBind bind[PC_MAX_UDP_LISTENERS];
    uint8_t rx_stage[PC_UDP_RX_BUF_SIZE]; ///< Contiguous payload handed to the handler.
    uint8_t tx_stage[PC_UDP_RX_BUF_SIZE]; ///< Contiguous payload handed to the wire.
    uint8_t rx_whdr[PC_UDP_DGRAM_HDR];    ///< Header staged by the receive ring's producer.
    uint8_t rx_rhdr[PC_UDP_DGRAM_HDR];    ///< Header staged by the receive ring's consumer.
    uint8_t tx_whdr[PC_UDP_DGRAM_HDR];    ///< Header staged by the send ring's producer.
    uint8_t tx_rhdr[PC_UDP_DGRAM_HDR];    ///< Header staged by the send ring's consumer.
    char group_text[PC_IP_STR_MAX];       ///< Where joined_group() formats the group it reports.
    proto_bool polling;                   ///< Set for the duration of poll(); a reentrant call returns.
#if !PROTOCORE_HOT
    proto_bool cap_on;
    uint8_t cap_buf[PC_UDP_RX_BUF_SIZE];
    size_t cap_len;
    proto_bool sendto_ok;
#endif
} UdpListenerCtx;

#if PROTOCORE_HOT
static UdpListenerCtx s_lst;
#else
// sendto_ok starts true: a test that never touches the knob gets a listener whose sends succeed.
static UdpListenerCtx s_lst = {.sendto_ok = PROTO_TRUE};
#endif

/** @brief The reply token a handler is given: the sender, and the slot the datagram arrived on. */
typedef struct pc_udp_peer
{
    pc_ip addr;
    uint16_t port;
    UdpBind *bind;
} pc_udp_peer;

/** @brief True when @p a is an IPv4 multicast group (224.0.0.0/4), the only kind IGMP joins. */
static proto_bool addr_is_group(const pc_ip *a)
{
    if (a->family != PC_IP_V4)
    {
        return PROTO_FALSE;
    }
    return Ip.classify(a) == PC_IP_SCOPE_MULTICAST;
}

/** @brief The bound slot for @p port, or NULL. */
static UdpBind *find_bind(uint16_t port)
{
    for (int i = 0; i < PC_MAX_UDP_LISTENERS; i++)
    {
        if (s_lst.bind[i].used && s_lst.bind[i].port == port)
        {
            return &s_lst.bind[i];
        }
    }
    return NULL;
}

/** @brief The first free slot, or NULL when the pool is full. */
static UdpBind *free_bind(void)
{
    for (int i = 0; i < PC_MAX_UDP_LISTENERS; i++)
    {
        if (!s_lst.bind[i].used)
        {
            return &s_lst.bind[i];
        }
    }
    return NULL;
}

/** @brief Reset a slot's rings and handler state, leaving it free. */
static void bind_clear(UdpBind *b)
{
    pc_ip empty = {PC_IP_NONE, {0}};
    b->port = 0;
    b->handler = NULL;
    b->ctx = NULL;
    b->group = empty;
    b->mcast = PROTO_FALSE;
    b->used = PROTO_FALSE;
    PROTO_ATOMIC_STORE(&b->rx_head, 0);
    PROTO_ATOMIC_STORE(&b->rx_tail, 0);
    PROTO_ATOMIC_STORE(&b->tx_head, 0);
    PROTO_ATOMIC_STORE(&b->tx_tail, 0);
}

// ---------------------------------------------------------------------------
// The seam: everything that differs between a target and a host build
// ---------------------------------------------------------------------------

#if PROTOCORE_HOT

/** @brief Ops that must run in the stack's thread, reached through pc_net_call_marshal. */
typedef enum PROTO_ENUM_PACKED
{
    UDP_OP_BIND,        ///< new + bind + arm recv on a slot
    UDP_OP_BIND_MCAST,  ///< as BIND, plus SO_REUSEADDR + IGMP join
    UDP_OP_LEAVE_MCAST, ///< IGMP leave + remove
    UDP_OP_UNBIND,      ///< remove
    UDP_OP_FLUSH        ///< drain a slot's send ring to the wire
} pc_udp_op;

typedef struct
{
    pc_net_call base;
    pc_udp_op op;
    UdpBind *b;
    uint16_t port;
    pc_ip group;
    proto_bool result;
} pc_udp_call;

// Stamp a control block with the configured UDP DSCP, applied per flush so a DiffServ.set_udp()
// change reaches the next datagram.
static void apply_dscp(pc_udp_pcb *pcb)
{
#if PC_ENABLE_DIFFSERV
    uint8_t dscp = DiffServ.udp_dscp();
    if (pcb != NULL && dscp != 0)
    {
        pcb->tos = pc_dscp_to_tos(dscp);
    }
#else
    (void)pcb;
#endif
}

// Allocate, copy, send, free. Runs in the stack's thread only.
static proto_bool wire_send(pc_udp_pcb *pcb, const pc_ip *a, uint16_t port, const uint8_t *data, size_t len)
{
    pc_net_ip dst;
    if (!NetAddr.from_ip(a, &dst))
    {
        return PROTO_FALSE; // a family this stack cannot send to, refused before a pbuf is taken
    }
    pc_pbuf *p = pc_net_pbuf_alloc(PC_NET_PBUF_TRANSPORT, (proto_u16)len, PC_NET_PBUF_RAM);
    if (p == NULL)
    {
        return PROTO_FALSE;
    }
    proto_raw_read((uint8_t *)p->payload, data, len);
    pc_net_err e = pc_net_udp_sendto(pcb, p, &dst, port);
    pc_net_pbuf_free(p);
    return e == PC_NET_OK;
}

/**
 * @brief Receive trampoline: frame the datagram into the slot's receive ring and return.
 *
 * Runs in the stack's thread and is the sole producer of that ring. The handler is not called here;
 * poll() calls it in the task that drains. The payload arrives as a chain, so the header is written
 * first and each segment follows it, all against a local head that is published once.
 */
static void udp_trampoline(void *arg, pc_udp_pcb *pcb, pc_pbuf *p, const pc_net_ip *addr, proto_u16 port)
{
    (void)pcb;
    UdpBind *b = (UdpBind *)arg;
    if (p == NULL)
    {
        return;
    }
    if (b == NULL)
    {
        pc_net_pbuf_free(p);
        return;
    }
    proto_u16 n = p->tot_len;
    if (n > PC_UDP_RX_BUF_SIZE)
    {
        n = (proto_u16)PC_UDP_RX_BUF_SIZE; // a longer datagram is truncated to the staged length
    }
    pc_udp_dgram d = {{PC_IP_NONE, {0}}, 0, 0};
    NetAddr.to_ip(addr, &d.addr);
    d.port = port;
    d.len = n;
    if ((PC_UDP_DGRAM_HDR + (size_t)n) > pc_ring_free(&b->rx_head, &b->rx_tail, PC_UDP_RX_RING))
    {
        pc_net_pbuf_free(p); // ring full: drop, which is what UDP already means
        return;
    }
    pc_span w = pc_span_from(s_lst.rx_whdr, sizeof(s_lst.rx_whdr));
    pc_udp_dgram_encode(&w, &d);
    if (!pc_span_ok(w))
    {
        pc_net_pbuf_free(p);
        return;
    }
    size_t h = PROTO_ATOMIC_LOAD(&b->rx_head);
    h = pc_ring_write_span(b->rx, PC_UDP_RX_RING, h, s_lst.rx_whdr, PC_UDP_DGRAM_HDR);
    size_t left = n;
    for (pc_pbuf *q = p; q != NULL && left > 0; q = q->next)
    {
        size_t take = q->len;
        if (take > left)
        {
            take = left;
        }
        h = pc_ring_write_span(b->rx, PC_UDP_RX_RING, h, (const uint8_t *)q->payload, take);
        left -= take;
    }
    PROTO_ATOMIC_STORE(&b->rx_head, h); // one release store publishes the whole entry
    pc_net_pbuf_free(p);
}

// Drain one slot's send ring to the wire, as the marshaled FLUSH op, so this is the ring's sole
// consumer and it runs in the stack's thread.
static void flush_ring(UdpBind *b)
{
    apply_dscp(b->pcb);
    pc_udp_dgram d = {{PC_IP_NONE, {0}}, 0, 0};
    while (pc_udp_dgram_take(b->tx, PC_UDP_TX_RING, &b->tx_head, &b->tx_tail, s_lst.tx_rhdr, &d, s_lst.tx_stage,
                             sizeof(s_lst.tx_stage)))
    {
        if (b->pcb != NULL)
        {
            (void)wire_send(b->pcb, &d.addr, d.port, s_lst.tx_stage, d.len);
        }
    }
}

static pc_net_err udp_do(pc_net_call *c)
{
    pc_udp_call *k = (pc_udp_call *)c;
    k->result = PROTO_FALSE;
    switch (k->op)
    {
    case UDP_OP_BIND: {
        pc_udp_pcb *pcb = pc_net_udp_new();
        if (pcb != NULL)
        {
            if (pc_net_udp_bind(pcb, PC_NET_ADDR_ANY, k->port) == PC_NET_OK)
            {
                k->b->pcb = pcb;
                apply_dscp(pcb);
                pc_net_udp_recv(pcb, udp_trampoline, k->b);
                k->result = PROTO_TRUE;
            }
            else
            {
                pc_net_udp_remove(pcb);
            }
        }
        break;
    }
    case UDP_OP_BIND_MCAST: {
#if PC_NET_HAS_IGMP
        pc_net_ip grp;
        if (!NetAddr.from_ip(&k->group, &grp))
        {
            break;
        }
        pc_udp_pcb *pcb = pc_net_udp_new();
        if (pcb != NULL)
        {
            // A well-known multicast port is normally already bound by whoever implements that
            // protocol, so co-bind. Bind IPv4-only rather than ANY: a dual-stack ANY control block
            // also matches the IPv4 datagrams the other responder is waiting on, and the stack hands
            // each datagram to the first match.
            pc_net_opt_set(pcb, PC_NET_OPT_REUSEADDR);
            if (pc_net_udp_bind(pcb, PC_NET_ADDR_ANY4, k->port) == PC_NET_OK &&
                pc_net_igmp_join(PC_NET_ADDR_ANY4_P, pc_net_ip_as_v4(&grp)) == PC_NET_OK)
            {
                k->b->pcb = pcb;
                k->b->group = k->group;
                k->b->mcast = PROTO_TRUE;
                apply_dscp(pcb);
                pc_net_udp_recv(pcb, udp_trampoline, k->b);
                k->result = PROTO_TRUE;
            }
            else
            {
                pc_net_udp_remove(pcb);
            }
        }
#endif
        break;
    }
    case UDP_OP_LEAVE_MCAST: {
#if PC_NET_HAS_IGMP
        pc_net_ip grp;
        if (NetAddr.from_ip(&k->b->group, &grp))
        {
            pc_net_igmp_leave(PC_NET_ADDR_ANY4_P, pc_net_ip_as_v4(&grp));
        }
        if (k->b->pcb != NULL)
        {
            pc_net_udp_remove(k->b->pcb);
        }
        k->b->pcb = NULL;
        k->result = PROTO_TRUE;
#endif
        break;
    }
    case UDP_OP_UNBIND:
        if (k->b->pcb != NULL)
        {
            pc_net_udp_remove(k->b->pcb);
        }
        k->b->pcb = NULL;
        k->result = PROTO_TRUE;
        break;
    case UDP_OP_FLUSH:
        flush_ring(k->b);
        k->result = PROTO_TRUE;
        break;
    }
    return PC_NET_OK;
}

// Run one marshaled op and report what it set.
static proto_bool marshal_op(pc_udp_op op, UdpBind *b, uint16_t port, const pc_ip *group)
{
    pc_udp_call k = {{0}, UDP_OP_BIND, NULL, 0, {PC_IP_NONE, {0}}, PROTO_FALSE};
    k.op = op;
    k.b = b;
    k.port = port;
    if (group != NULL)
    {
        k.group = *group;
    }
    pc_net_call_marshal(udp_do, &k.base);
    return k.result;
}

static proto_bool bind_port(UdpBind *b, uint16_t port)
{
    return marshal_op(UDP_OP_BIND, b, port, NULL);
}

static proto_bool bind_group(UdpBind *b, uint16_t port, const pc_ip *group)
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

static void flush_bind(UdpBind *b)
{
    if (pc_ring_available(&b->tx_head, &b->tx_tail, PC_UDP_TX_RING) > 0)
    {
        (void)marshal_op(UDP_OP_FLUSH, b, 0, NULL);
    }
}

#else // host build: no stack, so the seam ends at a capture a test reads back

// The host wire is the capture a test reads back.
static proto_bool wire_send(const uint8_t *data, size_t len)
{
    if (s_lst.cap_on && data != NULL && len > 0 && len <= sizeof(s_lst.cap_buf))
    {
        proto_raw_read(s_lst.cap_buf, data, len);
        s_lst.cap_len = len;
    }
    return s_lst.sendto_ok;
}

static void flush_bind(UdpBind *b)
{
    pc_udp_dgram d = {{PC_IP_NONE, {0}}, 0, 0};
    while (pc_udp_dgram_take(b->tx, PC_UDP_TX_RING, &b->tx_head, &b->tx_tail, s_lst.tx_rhdr, &d, s_lst.tx_stage,
                             sizeof(s_lst.tx_stage)))
    {
        (void)wire_send(s_lst.tx_stage, d.len);
    }
}

static proto_bool bind_port(UdpBind *b, uint16_t port)
{
    (void)b;
    (void)port;
    return PROTO_TRUE; // nothing to bind: a host slot is the table entry itself
}

static proto_bool bind_group(UdpBind *b, uint16_t port, const pc_ip *group)
{
    (void)port;
    b->group = *group;
    b->mcast = PROTO_TRUE;
    return PROTO_TRUE;
}

static void unbind_port(UdpBind *b)
{
    (void)b;
}

#endif // PROTOCORE_HOT

// ---------------------------------------------------------------------------
// The bodies behind the table
// ---------------------------------------------------------------------------

static proto_bool listen_on(uint16_t port, pc_udp_handler handler, void *ctx)
{
    UdpBind *b = free_bind();
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
    b->used = PROTO_TRUE;
    return PROTO_TRUE;
}

static proto_bool listen_group(const char *group_ip, uint16_t port, pc_udp_handler handler, void *ctx)
{
    pc_ip group = {PC_IP_NONE, {0}};
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
    b->used = PROTO_TRUE;
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
    for (int i = 0; i < PC_MAX_UDP_LISTENERS; i++)
    {
        UdpBind *b = &s_lst.bind[i];
        if (b->used)
        {
            pc_udp_dgram d = {{PC_IP_NONE, {0}}, 0, 0};
            while (pc_udp_dgram_take(b->rx, PC_UDP_RX_RING, &b->rx_head, &b->rx_tail, s_lst.rx_rhdr, &d, s_lst.rx_stage,
                                     sizeof(s_lst.rx_stage)))
            {
                if (b->handler != NULL)
                {
                    pc_udp_peer peer = {d.addr, d.port, b};
                    b->handler(s_lst.rx_stage, d.len, &peer, b->ctx);
                }
            }
            flush_bind(b);
        }
    }
    s_lst.polling = PROTO_FALSE;
}

// Queue one datagram on a slot's send ring.
static proto_bool queue_tx(UdpBind *b, const pc_ip *a, uint16_t port, const uint8_t *data, size_t len)
{
    if (b == NULL || data == NULL || len == 0 || len > PC_UDP_RX_BUF_SIZE)
    {
        return PROTO_FALSE;
    }
    pc_udp_dgram d = {*a, port, (uint16_t)len};
    return pc_udp_dgram_put(b->tx, PC_UDP_TX_RING, &b->tx_head, &b->tx_tail, s_lst.tx_whdr, &d, data, len);
}

static proto_bool reply_to(const struct pc_udp_peer *peer, const uint8_t *data, size_t len)
{
    if (peer == NULL)
    {
        return PROTO_FALSE;
    }
    return queue_tx(peer->bind, &peer->addr, peer->port, data, len);
}

static proto_bool peer_addr_of(const struct pc_udp_peer *peer, char *ip_out, size_t ip_cap, uint16_t *port_out)
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

static proto_bool send_from(uint16_t listen_port, const char *dst_ip, uint16_t dst_port, const uint8_t *data,
                            size_t len)
{
    UdpBind *b = find_bind(listen_port);
    if (b == NULL)
    {
        return PROTO_FALSE;
    }
    pc_ip dst = {PC_IP_NONE, {0}};
    if (!Ip.parse(dst_ip, &dst))
    {
        return PROTO_FALSE;
    }
    return queue_tx(b, &dst, dst_port, data, len);
}

static size_t sndbuf_of(uint16_t listen_port)
{
    UdpBind *b = find_bind(listen_port);
    if (b == NULL)
    {
        return 0;
    }
    return pc_udp_dgram_room(&b->tx_head, &b->tx_tail, PC_UDP_TX_RING);
}

#if !PROTOCORE_HOT
// ---------------------------------------------------------------------------
// Host test seams
// ---------------------------------------------------------------------------

static void inject_one(uint16_t listen_port, const char *src_ip, uint16_t src_port, const uint8_t *data, size_t len)
{
    UdpBind *b = find_bind(listen_port);
    if (b == NULL || len > PC_UDP_RX_BUF_SIZE)
    {
        return;
    }
    pc_udp_dgram d = {{PC_IP_NONE, {0}}, src_port, (uint16_t)len};
    if (src_ip != NULL)
    {
        (void)Ip.parse(src_ip, &d.addr);
    }
    if (pc_udp_dgram_put(b->rx, PC_UDP_RX_RING, &b->rx_head, &b->rx_tail, s_lst.rx_whdr, &d, data, len))
    {
        poll_all(); // the same consumer the stack's datagrams reach
    }
}

static void reset_all(void)
{
    for (int i = 0; i < PC_MAX_UDP_LISTENERS; i++)
    {
        bind_clear(&s_lst.bind[i]);
    }
    s_lst.sendto_ok = PROTO_TRUE;
    s_lst.cap_len = 0;
}

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

static void set_sendto_result(proto_bool ok)
{
    s_lst.sendto_ok = ok;
}

static void capture_enable(void)
{
    s_lst.cap_on = PROTO_TRUE;
    s_lst.cap_len = 0;
}

static void capture_reset(void)
{
    s_lst.cap_len = 0;
}

static const uint8_t *captured(void)
{
    if (s_lst.cap_len == 0)
    {
        return NULL;
    }
    return s_lst.cap_buf;
}

static size_t captured_len(void)
{
    return s_lst.cap_len;
}
#endif // !PROTOCORE_HOT

const UdpListenerNs UdpListener = {listen_on,     listen_group, leave_group, poll_all,          reply_to,
                                   peer_addr_of,  send_from,    sndbuf_of,
#if !PROTOCORE_HOT
                                   inject_one,    reset_all,    group_on,    set_sendto_result, capture_enable,
                                   capture_reset, captured,     captured_len
#endif
};

PROTO_END_DECLS
