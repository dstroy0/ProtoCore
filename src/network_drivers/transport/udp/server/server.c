// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
#include "mmgr/plaintext.h"                       // the persistent end this module's state is taken from
#include "network_drivers/transport/udp/common.h" // the wire layout the receive ring carries

#include "config/platform/platform.h" // the stack's UDP, under our names
#include "network_drivers/transport/diffserv/diffserv.h"  // DSCP marking; compiles out when off
#include "network_drivers/transport/net_addr/net_addr.h"  // NetAddr: the stack's address as a protocore_ip

static uint8_t ip_work[16]; // the borrow an entry takes; Ip never reads it

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
    _Atomic uint32_t bound;                      ///< Bit i set = bind[i] is bound; one ctz instead of a scan.
    proto_bool polling;                          ///< Set for the duration of poll(); a reentrant call returns.
    UdpBind *slot;                               ///< The slot the private steps below act on.
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define UDP_LISTENER_OFF_CTX 0u
static_assert(UDP_LISTENER_OFF_CTX + sizeof(struct UdpListenerStorage) <= PROTOCORE_UDP_LISTENER_BORROW,
              "PROTOCORE_UDP_LISTENER_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define UDP_LISTENER_CTX(w) ((struct UdpListenerStorage *)(void *)((w) + UDP_LISTENER_OFF_CTX))

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
    Ip.classify(ip_work);
    return Ip.scope == PROTOCORE_IP_SCOPE_MULTICAST;
}

/** @brief The slot index UDP_LISTENER_CTX(work)->slot sits at. */
static size_t bind_idx(uint8_t *restrict work)
{
    return (size_t)(UDP_LISTENER_CTX(work)->slot - UDP_LISTENER_CTX(work)->bind);
}

/** @brief True when slot @p idx is bound. */
static proto_bool bind_used(uint8_t *restrict work, size_t idx)
{
    return (PROTO_ATOMIC_LOAD(&UDP_LISTENER_CTX(work)->bound) & protocore_slot_bit(idx)) != 0u;
}

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_UDP_LISTENER_BORROW persistent bytes, or null while the pool was short
} UdpListenerOwnCtx;
static UdpListenerOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_udp_listener_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_plaintext_persist_span(PROTOCORE_UDP_LISTENER_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

/** @brief Point UDP_LISTENER_CTX(work)->slot at the bound slot for ns->port, or NULL. */
static void find_bind(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    uint32_t m = PROTO_ATOMIC_LOAD(&UDP_LISTENER_CTX(work)->bound) & protocore_slot_all(PROTOCORE_MAX_UDP_LISTENERS);
    while (m != 0u)
    {
        int32_t i = protocore_slot_next(m);
        if (UDP_LISTENER_CTX(work)->bind[i].port == UdpListener.port)
        {
            UDP_LISTENER_CTX(work)->slot = &UDP_LISTENER_CTX(work)->bind[i];
            return;
        }
        m &= ~protocore_slot_bit((size_t)i);
    }
    UDP_LISTENER_CTX(work)->slot = NULL;
}

/** @brief Point UDP_LISTENER_CTX(work)->slot at the first free slot, or NULL when the pool is full. */
static void free_bind(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    uint32_t free_slots =
        ~PROTO_ATOMIC_LOAD(&UDP_LISTENER_CTX(work)->bound) & protocore_slot_all(PROTOCORE_MAX_UDP_LISTENERS);
    int32_t i = protocore_slot_next(free_slots);
    UDP_LISTENER_CTX(work)->slot = (i < 0) ? NULL : &UDP_LISTENER_CTX(work)->bind[i];
}

/** @brief Reset UDP_LISTENER_CTX(work)->slot's ring and handler state, leaving it free. */
static void bind_clear(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    protocore_ip empty = {PROTOCORE_IP_NONE, {0}};
    UDP_LISTENER_CTX(work)->slot->port = 0;
    UDP_LISTENER_CTX(work)->slot->handler = NULL;
    UDP_LISTENER_CTX(work)->slot->ctx = NULL;
    UDP_LISTENER_CTX(work)->slot->group = empty;
    UDP_LISTENER_CTX(work)->slot->mcast = PROTO_FALSE;
    protocore_slot_clear(&UDP_LISTENER_CTX(work)->bound, bind_idx(work));
    PROTO_ATOMIC_STORE(&UDP_LISTENER_CTX(work)->slot->rx_head, 0);
    PROTO_ATOMIC_STORE(&UDP_LISTENER_CTX(work)->slot->rx_tail, 0);
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
    // The stack fixes this signature, so there is no borrow to take and the module's own span is
    // what the producer's header stage lives in.
    uint8_t *work = protocore_udp_listener_span();
    if (work == NULL)
    {
        protocore_net_pbuf_free(p); // the pool was short: drop, which is what UDP already means
        return;
    }
    uint8_t *whdr = UDP_LISTENER_CTX(work)->rx_whdr;
    protocore_span w = span.from(whdr, PROTOCORE_UDP_DGRAM_HDR);
    protocore_udp_dgram_encode(&w, &d);
    if (!span.ok(w))
    {
        protocore_net_pbuf_free(p);
        return;
    }
    size_t h = PROTO_ATOMIC_LOAD(&b->rx_head);
    h = protocore_ring_write_span(b->rx, PROTOCORE_UDP_RX_RING, h, whdr, PROTOCORE_UDP_DGRAM_HDR);
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

// Run one marshaled op on UDP_LISTENER_CTX(work)->slot and report what it set.
static proto_bool marshal_op(uint8_t *restrict work, protocore_udp_op op, uint16_t port, const protocore_ip *group)
{
    protocore_udp_call k = {{0}, UDP_OP_BIND, NULL, 0, {PROTOCORE_IP_NONE, {0}}, PROTO_FALSE};
    k.op = op;
    k.b = UDP_LISTENER_CTX(work)->slot;
    k.port = port;
    if (group != NULL)
    {
        k.group = *group;
    }
    protocore_net_call_marshal(udp_do, &k.base);
    return k.result;
}

// Drop the stack's control block for UDP_LISTENER_CTX(work)->slot, leaving its group first when it joined one.
static void unbind_port(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    if (UDP_LISTENER_CTX(work)->slot->mcast)
    {
        (void)marshal_op(work, UDP_OP_LEAVE_MCAST, 0, NULL);
        return;
    }
    (void)marshal_op(work, UDP_OP_UNBIND, 0, NULL);
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

// Send one datagram out of UDP_LISTENER_CTX(work)->slot to @p a, from where the caller's bytes already are.
static proto_bool send_now(uint8_t *restrict work, const protocore_ip *a, uint16_t port)
{
    if (UDP_LISTENER_CTX(work)->slot == NULL || a == NULL || UdpListener.send_args.data == NULL ||
        UdpListener.send_args.len == 0 || UdpListener.send_args.len > PROTOCORE_UDP_RX_BUF_SIZE)
    {
        return PROTO_FALSE;
    }
    // The marshal is synchronous, so this outlives the call and carries its answer back.
    protocore_udp_send_call k = {
        {0}, UDP_LISTENER_CTX(work)->slot, a, UdpListener.send_args.data, UdpListener.send_args.len, port, PROTO_FALSE};
    (void)protocore_net_call_marshal(send_do, &k.base);
    return k.ok;
}

// ---------------------------------------------------------------------------
// The bodies behind the table
// ---------------------------------------------------------------------------

static void listen_on(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    // A port already bound rebinds its own slot: a second slot on one port is one find_bind() can
    // never reach, and it spends a slot the pool has two of.
    find_bind(work);
    if (UDP_LISTENER_CTX(work)->slot != NULL)
    {
        UDP_LISTENER_CTX(work)->slot->handler = UdpListener.bind.handler;
        UDP_LISTENER_CTX(work)->slot->ctx = UdpListener.bind.handler_ctx;
        UdpListener.ok = PROTO_TRUE;
        return;
    }
    free_bind(work);
    if (UDP_LISTENER_CTX(work)->slot == NULL)
    {
        UdpListener.ok = PROTO_FALSE; // pool exhausted
        return;
    }
    bind_clear(work);
    // The trampoline reads handler and work as soon as recv is armed, so set them first.
    UDP_LISTENER_CTX(work)->slot->handler = UdpListener.bind.handler;
    UDP_LISTENER_CTX(work)->slot->ctx = UdpListener.bind.handler_ctx;
    UDP_LISTENER_CTX(work)->slot->port = UdpListener.port;
    if (!marshal_op(work, UDP_OP_BIND, UdpListener.port, NULL))
    {
        UDP_LISTENER_CTX(work)->slot->handler = NULL;
        UdpListener.ok = PROTO_FALSE;
        return;
    }
    protocore_slot_mark(&UDP_LISTENER_CTX(work)->bound, bind_idx(work));
    UdpListener.ok = PROTO_TRUE;
}

static void listen_group(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    protocore_ip group = {PROTOCORE_IP_NONE, {0}};
    UdpListener.ok = PROTO_FALSE;
    Ip.args.text = UdpListener.bind.group_ip;
    Ip.args.out = &group;
    Ip.parse(ip_work);
    if (!Ip.ok)
    {
        return;
    }
    if (!addr_is_group(&group))
    {
        return; // joining a unicast address would silently never deliver
    }
    free_bind(work);
    if (UDP_LISTENER_CTX(work)->slot == NULL)
    {
        return;
    }
    bind_clear(work);
    UDP_LISTENER_CTX(work)->slot->handler = UdpListener.bind.handler;
    UDP_LISTENER_CTX(work)->slot->ctx = UdpListener.bind.handler_ctx;
    UDP_LISTENER_CTX(work)->slot->port = UdpListener.port;
    if (!marshal_op(work, UDP_OP_BIND_MCAST, UdpListener.port, &group))
    {
        UDP_LISTENER_CTX(work)->slot->handler = NULL;
        return;
    }
    protocore_slot_mark(&UDP_LISTENER_CTX(work)->bound, bind_idx(work));
    UdpListener.ok = PROTO_TRUE;
}

static void leave_group(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    find_bind(work);
    if (UDP_LISTENER_CTX(work)->slot == NULL || !UDP_LISTENER_CTX(work)->slot->mcast)
    {
        UdpListener.ok = PROTO_FALSE;
        return;
    }
    unbind_port(work);
    bind_clear(work);
    UdpListener.ok = PROTO_TRUE;
}

static void poll_all(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    if (UDP_LISTENER_CTX(work)->polling)
    {
        return; // a handler called back into poll(); the stage is already in use
    }
    UDP_LISTENER_CTX(work)->polling = PROTO_TRUE;
    for (int i = 0; i < PROTOCORE_MAX_UDP_LISTENERS; i++)
    {
        UdpBind *b = &UDP_LISTENER_CTX(work)->bind[i];
        if (bind_used(work, (size_t)i))
        {
            protocore_udp_dgram d = {{PROTOCORE_IP_NONE, {0}}, 0, 0};
            while (protocore_udp_dgram_take(b->rx, PROTOCORE_UDP_RX_RING, &b->rx_head, &b->rx_tail,
                                            UDP_LISTENER_CTX(work)->rx_rhdr, &d, UDP_LISTENER_CTX(work)->rx_stage,
                                            sizeof(UDP_LISTENER_CTX(work)->rx_stage)))
            {
                if (b->handler != NULL)
                {
                    protocore_udp_peer peer = {d.addr, d.port, b};
                    b->handler(UDP_LISTENER_CTX(work)->rx_stage, d.len, &peer, b->ctx);
                }
            }
        }
    }
    UDP_LISTENER_CTX(work)->polling = PROTO_FALSE;
}

static void reply_to(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    if (UdpListener.peer_args.peer == NULL)
    {
        UdpListener.ok = PROTO_FALSE;
        return;
    }
    UDP_LISTENER_CTX(work)->slot = UdpListener.peer_args.peer->bind;
    UdpListener.ok = send_now(work, &UdpListener.peer_args.peer->addr, UdpListener.peer_args.peer->port);
}

static void peer_addr_of(uint8_t *restrict work)
{
    (void)work;
    UdpListener.ok = PROTO_FALSE;
    if (UdpListener.peer_args.peer == NULL || UdpListener.peer_args.ip_out == NULL || UdpListener.peer_args.ip_cap < 8u)
    {
        return;
    }
    Ip.args.ip = &UdpListener.peer_args.peer->addr;
    Ip.args.buf = UdpListener.peer_args.ip_out;
    Ip.args.cap = UdpListener.peer_args.ip_cap;
    Ip.format(ip_work);
    if (Ip.n == 0)
    {
        return;
    }
    if (UdpListener.peer_args.port_out != NULL)
    {
        *UdpListener.peer_args.port_out = UdpListener.peer_args.peer->port;
    }
    UdpListener.ok = PROTO_TRUE;
}

static void send_from(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    find_bind(work);
    if (UDP_LISTENER_CTX(work)->slot == NULL || UdpListener.send_args.dst == NULL ||
        UdpListener.send_args.dst->family == PROTOCORE_IP_NONE)
    {
        UdpListener.ok = PROTO_FALSE;
        return;
    }
    UdpListener.ok = send_now(work, UdpListener.send_args.dst, UdpListener.send_args.dst_port);
}

// Close ns->port: leave its group when it joined one, drop the control block, free the slot.
static void close_port(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    find_bind(work);
    if (UDP_LISTENER_CTX(work)->slot == NULL)
    {
        UdpListener.ok = PROTO_FALSE;
        return;
    }
    unbind_port(work);
    bind_clear(work);
    UdpListener.ok = PROTO_TRUE;
}

// The group ns->port joined, formatted, or NULL when the port is unbound or joined none.
static void group_on(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    UdpListener.text = NULL;
    find_bind(work);
    if (UDP_LISTENER_CTX(work)->slot == NULL || !UDP_LISTENER_CTX(work)->slot->mcast)
    {
        return;
    }
    Ip.args.ip = &UDP_LISTENER_CTX(work)->slot->group;
    Ip.args.buf = UDP_LISTENER_CTX(work)->group_text;
    Ip.args.cap = sizeof(UDP_LISTENER_CTX(work)->group_text);
    Ip.format(ip_work);
    if (Ip.n == 0)
    {
        return;
    }
    UdpListener.text = UDP_LISTENER_CTX(work)->group_text;
}

// Designated, so a member's position in the struct does not decide what it binds to.
UdpListenerNs UdpListener = {.listen = listen_on,
                             .listen_multicast = listen_group,
                             .leave_multicast = leave_group,
                             .poll = poll_all,
                             .reply = reply_to,
                             .peer_addr = peer_addr_of,
                             .sendto = send_from,
                             .close = close_port,
                             .joined_group = group_on};

PROTOCORE_END_DECLS
