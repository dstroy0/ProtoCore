// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file udp.cpp
 * @brief Layer 4 UDP datagram service - the only place lwIP UDP is touched.
 */

#include "network_drivers/transport/udp.h"

#include <string.h> // memcpy (both the lwIP and host builds)

#if PROTOCORE_HOT

#include "diffserv.h" // DiffServ DSCP marking for outbound datagrams (compiles out when off)

// A small fixed pool of bound UDP ports (e.g. SNMP :161 + captive DNS :53). No
// heap: the pool and the shared receive scratch live in BSS.
typedef struct
{
    pc_udp_pcb *pcb;
    pc_udp_handler handler;
    void *ctx;
    pc_net_ip group;  // multicast group joined by this listener (mcast only)
    proto_bool mcast; // true if this slot joined a group and must leave on teardown
    proto_bool used;
} UdpListener;

// All UDP transport state, owned by one instance (internal linkage): the listener table, the
// shared single-datagram rx buffer, the shared outbound PCB, and the tcpip-thread reentrancy
// flag. One named owner, unreachable from any other translation unit.
//
// in_tcpip_thread is true while a pc_net_udp_recv trampoline (or a marshaled op) is running, i.e. while
// we are already inside tcpip_thread. A handler replying from the trampoline then sends directly
// instead of re-marshaling (which would deadlock on the tcpip mailbox) - the UDP mirror of
// tcp.cpp's TransportCtx::in_tcpip_thread.
typedef struct
{
    UdpListener listeners[PC_MAX_UDP_LISTENERS];
    uint8_t rx[PC_UDP_RX_BUF_SIZE]; // shared: lwIP delivers one datagram at a time
    pc_udp_pcb *out;                // one shared outbound PCB for pc_udp_sendto()
    volatile proto_bool in_tcpip_thread;
} UdpCtx;
static UdpCtx s_udp;

// Concrete peer: lwIP source address/port plus the receiving PCB to reply on.
typedef struct pc_udp_peer
{
    const pc_net_ip *addr;
    proto_u16 port;
    pc_udp_pcb *pcb;
} pc_udp_peer;

// Raw send (alloc + copy + sendto + free). Only ever called in tcpip_thread.
static proto_bool udp_pbuf_send(pc_udp_pcb *pcb, const pc_net_ip *addr, proto_u16 port, const uint8_t *data, size_t len)
{
    pc_pbuf *p = pc_net_pbuf_alloc(PC_NET_PBUF_TRANSPORT, (proto_u16)len, PC_NET_PBUF_RAM);
    if (!p)
    {
        return PROTO_FALSE;
    }
    memcpy(p->payload, data, len);
    pc_net_err e = pc_net_udp_sendto(pcb, p, addr, port);
    pc_net_pbuf_free(p);
    return e == PC_NET_OK;
}

// lwIP pc_net_udp_recv trampoline: copy the (possibly chained) pbuf into a contiguous
// scratch buffer and hand it to the registered handler. Runs in tcpip_thread, so a
// reply the handler sends is already in-thread (flagged for the send helpers).
static void udp_trampoline(void *arg, pc_udp_pcb *pcb, pc_pbuf *p, const pc_net_ip *addr, proto_u16 port)
{
    UdpListener *l = (UdpListener *)arg;
    if (!p)
    {
        return;
    }
    proto_u16 n = (p->tot_len < sizeof(s_udp.rx)) ? p->tot_len : (proto_u16)sizeof(s_udp.rx);
    pc_net_pbuf_copy(p, s_udp.rx, n, 0);
    pc_net_pbuf_free(p);
    if (l && l->handler)
    {
        struct pc_udp_peer peer = {addr, port, pcb};
        proto_bool prev = s_udp.in_tcpip_thread;
        s_udp.in_tcpip_thread = PROTO_TRUE;
        l->handler(s_udp.rx, n, &peer, l->ctx);
        s_udp.in_tcpip_thread = prev;
    }
}

// Raw lwIP UDP must run in tcpip_thread: with lwIP core-locking (arduino-esp32 3.x /
// IDF 5.x) a pc_net_udp_new/bind/recv/sendto from any other task asserts ("Required to lock
// TCPIP core functionality"), and without it, it races the stack. pc_udp_* therefore
// marshal these ops via pc_net_call_marshal(), the same as the TCP transport.
typedef enum PROTO_ENUM_PACKED
{
    UDP_OP_LISTEN,       // pc_net_udp_new + bind + arm recv on s_udp.listeners[slot]
    UDP_OP_LISTEN_MCAST, // as LISTEN, plus SO_REUSEADDR + pc_net_igmp_join
    UDP_OP_LEAVE_MCAST,  // pc_net_igmp_leave + pc_net_udp_remove
    UDP_OP_SEND,         // send to addr:port on an existing pcb
    UDP_OP_SEND_OUT      // send to addr:port on the shared lazy outbound pcb
} pc_udp_op;

typedef struct
{
    pc_net_call base;
    pc_udp_op op;
    int slot;            // LISTEN: index into s_udp.listeners
    pc_udp_pcb *pcb;     // SEND: target pcb
    pc_net_ip addr;      // SEND / SEND_OUT: destination (by value - caller's may be transient)
    proto_u16 port;      // LISTEN: bind port; SEND / SEND_OUT: destination port
    const uint8_t *data; // SEND / SEND_OUT
    size_t len;          // SEND / SEND_OUT
    proto_bool result;
} pc_udp_call;

// Stamp a UDP pcb with the configured default DSCP (DiffServ) so its outbound datagrams carry the class.
// No-op when marking is off or the DSCP is 0 (best-effort). Applied per outbound send so a live
// pc_udp_set_dscp() change takes effect on the next datagram - useful for network testing.
static inline void apply_udp_dscp(pc_udp_pcb *pcb)
{
#if PC_ENABLE_DIFFSERV
    uint8_t dscp = pc_diffserv_udp_dscp();
    if (pcb && dscp)
    {
        pcb->tos = pc_dscp_to_tos(dscp);
    }
#else
    (void)pcb;
#endif
}

// Runs in tcpip_thread via pc_net_call_marshal.
static pc_net_err udp_do(pc_net_call *c)
{
    pc_udp_call *k = (pc_udp_call *)c;
    proto_bool prev = s_udp.in_tcpip_thread;
    s_udp.in_tcpip_thread = PROTO_TRUE;
    k->result = PROTO_FALSE;
    switch (k->op)
    {
    case UDP_OP_LISTEN: {
        pc_udp_pcb *pcb = pc_net_udp_new();
        if (pcb)
        {
            if (pc_net_udp_bind(pcb, PC_NET_ADDR_ANY, k->port) == PC_NET_OK)
            {
                s_udp.listeners[k->slot].pcb = pcb;
                apply_udp_dscp(pcb); // reply datagrams from this listener carry the configured DSCP
                pc_net_udp_recv(pcb, udp_trampoline, &s_udp.listeners[k->slot]);
                k->result = PROTO_TRUE;
            }
            else
            {
                pc_net_udp_remove(pcb);
            }
        }
        break;
    }
    case UDP_OP_LISTEN_MCAST: {
#if PC_NET_HAS_IGMP
        pc_udp_pcb *pcb = pc_net_udp_new();
        if (pcb)
        {
            // A well-known multicast port is usually already bound by whoever implements that
            // protocol (the ESP-IDF mdns component holds 5353), so co-bind rather than fail.
            // Bind IPv4-only, NOT PC_NET_ADDR_ANY: a dual-stack ANY pcb also matches the IPv4
            // datagrams the other responder's own socket is waiting on, and lwIP hands each
            // datagram to the first matching pcb - which silently stops that responder answering
            // queries even though its announcements still go out.
            pc_net_opt_set(pcb, PC_NET_OPT_REUSEADDR);
            if (pc_net_udp_bind(pcb, PC_NET_ADDR_ANY4, k->port) == PC_NET_OK &&
                pc_net_igmp_join(PC_NET_ADDR_ANY4_P, pc_net_ip_as_v4(&k->addr)) == PC_NET_OK)
            {
                s_udp.listeners[k->slot].pcb = pcb;
                s_udp.listeners[k->slot].group = k->addr;
                s_udp.listeners[k->slot].mcast = PROTO_TRUE;
                pc_net_udp_recv(pcb, udp_trampoline, &s_udp.listeners[k->slot]);
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
        UdpListener *l = &s_udp.listeners[k->slot];
        pc_net_igmp_leave(PC_NET_ADDR_ANY4_P, pc_net_ip_as_v4(&l->group));
        if (l->pcb)
        {
            pc_net_udp_remove(l->pcb);
        }
        l->pcb = NULL;
        l->mcast = PROTO_FALSE;
        k->result = PROTO_TRUE;
#endif
        break;
    }
    case UDP_OP_SEND:
        k->result = udp_pbuf_send(k->pcb, &k->addr, k->port, k->data, k->len);
        break;
    case UDP_OP_SEND_OUT:
        if (!s_udp.out)
        {
            s_udp.out = pc_net_udp_new();
        }
        if (s_udp.out)
        {
            apply_udp_dscp(s_udp.out); // per-send: a live pc_udp_set_dscp() change takes effect immediately
            k->result = udp_pbuf_send(s_udp.out, &k->addr, k->port, k->data, k->len);
        }
        break;
    }
    s_udp.in_tcpip_thread = prev;
    return PC_NET_OK;
}

proto_bool pc_udp_listen(uint16_t port, pc_udp_handler handler, void *ctx)
{
    for (int i = 0; i < PC_MAX_UDP_LISTENERS; i++)
    {
        if (s_udp.listeners[i].used)
        {
            continue;
        }
        // The trampoline reads handler/ctx once recv is armed, so set them first.
        s_udp.listeners[i].handler = handler;
        s_udp.listeners[i].ctx = ctx;
        s_udp.listeners[i].pcb = NULL;
        pc_udp_call k;
        memset(&k, 0, sizeof(k));
        k.op = UDP_OP_LISTEN;
        k.slot = i;
        k.port = port;
        pc_net_call_marshal(udp_do, &k.base); // always called off tcpip_thread (service begin())
        if (!k.result)
        {
            s_udp.listeners[i].handler = NULL;
            return PROTO_FALSE;
        }
        s_udp.listeners[i].used = PROTO_TRUE;
        return PROTO_TRUE;
    }
    return PROTO_FALSE; // pool exhausted
}

proto_bool pc_udp_listen_multicast(const char *group_ip, uint16_t port, pc_udp_handler handler, void *ctx)
{
#if PC_NET_HAS_IGMP
    if (!group_ip)
    {
        return PROTO_FALSE;
    }
    pc_net_ip group;
    if (!pc_net_ip_parse(group_ip, &group) || !pc_net_ip_is_v4(&group))
    {
        return PROTO_FALSE;
    }
    // 224.0.0.0/4 - joining a unicast address would silently never deliver.
    if (!pc_net_ip4_is_multicast(pc_net_ip_as_v4(&group)))
    {
        return PROTO_FALSE;
    }

    for (int i = 0; i < PC_MAX_UDP_LISTENERS; i++)
    {
        if (s_udp.listeners[i].used)
        {
            continue;
        }
        // The trampoline reads handler/ctx once recv is armed, so set them first.
        s_udp.listeners[i].handler = handler;
        s_udp.listeners[i].ctx = ctx;
        s_udp.listeners[i].pcb = NULL;
        s_udp.listeners[i].mcast = PROTO_FALSE;
        pc_udp_call k;
        memset(&k, 0, sizeof(k));
        k.op = UDP_OP_LISTEN_MCAST;
        k.slot = i;
        k.port = port;
        k.addr = group;
        pc_net_call_marshal(udp_do, &k.base); // always called off tcpip_thread (service begin())
        if (!k.result)
        {
            s_udp.listeners[i].handler = NULL;
            return PROTO_FALSE;
        }
        s_udp.listeners[i].used = PROTO_TRUE;
        return PROTO_TRUE;
    }
    return PROTO_FALSE; // pool exhausted
#else
    (void)group_ip;
    (void)port;
    (void)handler;
    (void)ctx;
    return PROTO_FALSE; // lwIP built without IGMP
#endif
}

proto_bool pc_udp_leave_multicast(uint16_t port)
{
#if PC_NET_HAS_IGMP
    for (int i = 0; i < PC_MAX_UDP_LISTENERS; i++)
    {
        if (!s_udp.listeners[i].used || !s_udp.listeners[i].mcast || !s_udp.listeners[i].pcb ||
            s_udp.listeners[i].pcb->local_port != port)
        {
            continue;
        }
        pc_udp_call k;
        memset(&k, 0, sizeof(k));
        k.op = UDP_OP_LEAVE_MCAST;
        k.slot = i;
        pc_net_call_marshal(udp_do, &k.base);
        s_udp.listeners[i].used = PROTO_FALSE;
        s_udp.listeners[i].handler = NULL;
        s_udp.listeners[i].ctx = NULL;
        return k.result;
    }
    return PROTO_FALSE;
#else
    (void)port;
    return PROTO_FALSE;
#endif
}

proto_bool pc_udp_send(const struct pc_udp_peer *peer, const uint8_t *data, size_t len)
{
    if (!peer || !peer->pcb || !peer->addr || !data || len == 0)
    {
        return PROTO_FALSE;
    }
    if (s_udp.in_tcpip_thread) // replying from a handler (already in tcpip_thread)
    {
        return udp_pbuf_send(peer->pcb, peer->addr, peer->port, data, len);
    }
    pc_udp_call k;
    memset(&k, 0, sizeof(k));
    k.op = UDP_OP_SEND;
    k.pcb = peer->pcb;
    k.addr = *peer->addr;
    k.port = peer->port;
    k.data = data;
    k.len = len;
    pc_net_call_marshal(udp_do, &k.base);
    return k.result;
}

proto_bool pc_udp_sendto(const char *dst_ip, uint16_t dst_port, const uint8_t *data, size_t len)
{
    if (!dst_ip || !data || len == 0)
    {
        return PROTO_FALSE;
    }
    pc_net_ip dst;
    if (!pc_net_ip_parse(dst_ip, &dst))
    {
        return PROTO_FALSE;
    }
    if (s_udp.in_tcpip_thread)
    {
        if (!s_udp.out)
        {
            s_udp.out = pc_net_udp_new();
            if (!s_udp.out)
            {
                return PROTO_FALSE;
            }
        }
        apply_udp_dscp(s_udp.out);
        return udp_pbuf_send(s_udp.out, &dst, dst_port, data, len);
    }
    pc_udp_call k;
    memset(&k, 0, sizeof(k));
    k.op = UDP_OP_SEND_OUT;
    k.addr = dst;
    k.port = dst_port;
    k.data = data;
    k.len = len;
    pc_net_call_marshal(udp_do, &k.base);
    return k.result;
}

proto_bool pc_udp_peer_addr(const struct pc_udp_peer *peer, char *ip_out, size_t ip_cap, uint16_t *port_out)
{
    if (!peer || !peer->addr || !ip_out || ip_cap < 8)
    {
        return PROTO_FALSE;
    }
    pc_net_ip_print(peer->addr, ip_out, (int)ip_cap);
    if (port_out)
    {
        *port_out = peer->port;
    }
    return PROTO_TRUE;
}

proto_bool pc_udp_listener_sendto(uint16_t listen_port, const char *dst_ip, uint16_t dst_port, const uint8_t *data,
                                  size_t len)
{
    if (!dst_ip || !data || len == 0)
    {
        return PROTO_FALSE;
    }
    pc_net_ip dst;
    if (!pc_net_ip_parse(dst_ip, &dst))
    {
        return PROTO_FALSE;
    }
    pc_udp_pcb *pcb = NULL;
    for (int i = 0; i < PC_MAX_UDP_LISTENERS; i++)
    {
        if (s_udp.listeners[i].used && s_udp.listeners[i].pcb && s_udp.listeners[i].pcb->local_port == listen_port)
        {
            pcb = s_udp.listeners[i].pcb;
            break;
        }
    }
    if (!pcb)
    {
        return PROTO_FALSE;
    }
    if (s_udp.in_tcpip_thread)
    {
        return udp_pbuf_send(pcb, &dst, dst_port, data, len);
    }
    pc_udp_call k;
    memset(&k, 0, sizeof(k));
    k.op = UDP_OP_SEND;
    k.pcb = pcb;
    k.addr = dst;
    k.port = dst_port;
    k.data = data;
    k.len = len;
    pc_net_call_marshal(udp_do, &k.base);
    return k.result;
}

#else // host build: no lwIP. A test-injectable UDP mock keeps UDP-using services host-testable.

// Concrete host peer: the source address/port of an injected datagram, which a service's handler
// reads back via pc_udp_peer_addr() to reply (or, for CoAP Observe, to key a registration).
typedef struct pc_udp_peer
{
    char ip[16];
    uint16_t port;
} pc_udp_peer;

// Host UDP mock state, owned by one instance (internal linkage): the bound listeners, a capture of
// the last datagram sent (shared by pc_udp_send/sendto/listener_sendto), and the listener_sendto
// result knob. A test drives the receive path with pc_udp_inject() and reads replies via
// pc_udp_captured(). One named owner, unreachable from any other translation unit.
typedef struct
{
    uint16_t port;
    pc_udp_handler handler;
    void *ctx;
    char group[16]; // multicast group joined on this port ("" for a plain listener)
    proto_bool mcast;
    proto_bool used;
} HostUdpListener;
typedef struct
{
    HostUdpListener lst[PC_MAX_UDP_LISTENERS];
    proto_bool cap_on;
    uint8_t cap_buf[2048];
    size_t cap_len;
    proto_bool listener_sendto_ok;
} HostUdpCtx;
static HostUdpCtx s_udp = {{{0}}, PROTO_FALSE, {0}, 0, PROTO_TRUE};

// Capture one outbound datagram if capture is enabled; return whether it was captured.
static proto_bool host_capture(const uint8_t *data, size_t len)
{
    if (s_udp.cap_on && data && len && len <= sizeof(s_udp.cap_buf))
    {
        memcpy(s_udp.cap_buf, data, len);
        s_udp.cap_len = len;
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

proto_bool pc_udp_listen(uint16_t port, pc_udp_handler handler, void *ctx)
{
    // Rebind an existing port, else take a free slot, else evict slot 0 (host tests only).
    HostUdpListener *slot = NULL;
    for (int i = 0; i < PC_MAX_UDP_LISTENERS; i++)
    {
        if (s_udp.lst[i].used && s_udp.lst[i].port == port)
        {
            slot = &s_udp.lst[i];
        }
    }
    for (int i = 0; i < PC_MAX_UDP_LISTENERS && !slot; i++)
    {
        if (!s_udp.lst[i].used)
        {
            slot = &s_udp.lst[i];
        }
    }
    if (!slot)
    {
        slot = &s_udp.lst[0];
    }
    slot->port = port;
    slot->handler = handler;
    slot->ctx = ctx;
    slot->used = PROTO_TRUE;
    return PROTO_TRUE;
}

// Validate a dotted-quad IPv4 multicast group (224.0.0.0/4) and report its length.
//
// Mirrors the device-side validation so a host test catches a bad group before hardware does.
// Returning the length is what lets the caller copy it with a bound it just proved, instead of
// trusting a scan that happened several branches earlier.
static proto_bool parse_mcast_group(const char *ip, size_t *len_out)
{
    if (!ip)
    {
        return PROTO_FALSE;
    }
    uint32_t octet = 0;
    uint32_t first = 0;
    int count = 0;
    proto_bool have_digit = PROTO_FALSE;
    const char *p = ip;
    for (;; p++)
    {
        if (*p >= '0' && *p <= '9')
        {
            octet = octet * 10 + (uint32_t)(*p - '0');
            have_digit = PROTO_TRUE;
            if (octet > 255)
            {
                return PROTO_FALSE;
            }
        }
        else if (*p == '.' || *p == '\0')
        {
            if (!have_digit || count == 4)
            {
                return PROTO_FALSE;
            }
            if (count == 0)
            {
                first = octet;
            }
            count++;
            octet = 0;
            have_digit = PROTO_FALSE;
            if (*p == '\0')
            {
                break;
            }
        }
        else
        {
            return PROTO_FALSE;
        }
    }
    if (count != 4 || first < 224 || first > 239) // 224.0.0.0/4
    {
        return PROTO_FALSE;
    }
    *len_out = (size_t)(p - ip);
    return PROTO_TRUE;
}

proto_bool pc_udp_listen_multicast(const char *group_ip, uint16_t port, pc_udp_handler handler, void *ctx)
{
    size_t glen = 0;
    if (!parse_mcast_group(group_ip, &glen))
    {
        return PROTO_FALSE;
    }
    // A normal dotted quad is at most 15 chars, but parse_mcast_group() only bounds each octet's
    // VALUE (<=255), not its digit count - a run of leading zeros ("224.000000000000000.0.0") keeps
    // an octet at 0 indefinitely while still growing glen past this buffer (see
    // test_multicast_group_too_long_for_buffer_rejected), so this check is genuinely reachable, not
    // just defensive. Failing here before claiming a listener slot means a truncated group leaks
    // nothing.
    if (glen >= sizeof(s_udp.lst[0].group))
    {
        return PROTO_FALSE;
    }

    // pc_udp_listen()'s host stub (this file, #else branch above) always returns true - rebind,
    // free-slot, or evict-slot-0 all succeed unconditionally, with no failure path of its own - so
    // this guard, and the "not found" fallthrough below, only exist for the real ESP32
    // implementation (which can fail: no free pcb, a bind conflict). Neither is reachable on native.
    if (!pc_udp_listen(port, handler, ctx)) // GCOVR_EXCL_BR_LINE - pc_udp_listen() host stub never fails
    {
        return PROTO_FALSE; // GCOVR_EXCL_LINE - unreachable: see above
    }
    for (int i = 0; i < PC_MAX_UDP_LISTENERS; i++) // GCOVR_EXCL_BR_LINE - loop always finds the slot pc_udp_listen()
                                                   // just claimed (lowest-free-first, so every lower index is
                                                   // already used); natural exhaustion is unreachable
    {
        if (s_udp.lst[i].used && s_udp.lst[i].port == port)
        {
            memcpy(s_udp.lst[i].group, group_ip, glen + 1); // + NUL; length bounded above
            s_udp.lst[i].mcast = PROTO_TRUE;
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE; // GCOVR_EXCL_LINE - unreachable: the loop above always finds the just-bound slot
}

proto_bool pc_udp_leave_multicast(uint16_t port)
{
    for (int i = 0; i < PC_MAX_UDP_LISTENERS; i++)
    {
        if (s_udp.lst[i].used && s_udp.lst[i].mcast && s_udp.lst[i].port == port)
        {
            s_udp.lst[i] = (HostUdpListener){0};
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

const char *pc_udp_joined_group(uint16_t port)
{
    for (int i = 0; i < PC_MAX_UDP_LISTENERS; i++)
    {
        if (s_udp.lst[i].used && s_udp.lst[i].mcast && s_udp.lst[i].port == port)
        {
            return s_udp.lst[i].group;
        }
    }
    return NULL;
}

void pc_udp_inject(uint16_t listen_port, const char *src_ip, uint16_t src_port, const uint8_t *data, size_t len)
{
    for (int i = 0; i < PC_MAX_UDP_LISTENERS; i++)
    {
        if (s_udp.lst[i].used && s_udp.lst[i].port == listen_port && s_udp.lst[i].handler)
        {
            struct pc_udp_peer peer;
            strncpy(peer.ip, src_ip ? src_ip : "", sizeof(peer.ip) - 1);
            peer.ip[sizeof(peer.ip) - 1] = '\0';
            peer.port = src_port;
            s_udp.lst[i].handler(data, len, &peer, s_udp.lst[i].ctx);
            return;
        }
    }
}

void pc_udp_set_listener_sendto_result(proto_bool ok)
{
    s_udp.listener_sendto_ok = ok;
}

void pc_udp_reset_listeners(void)
{
    for (int i = 0; i < PC_MAX_UDP_LISTENERS; i++)
    {
        s_udp.lst[i] = (HostUdpListener){0};
    }
    s_udp.listener_sendto_ok = PROTO_TRUE;
}

proto_bool pc_udp_send(const struct pc_udp_peer *peer, const uint8_t *data, size_t len)
{
    (void)peer;
    return host_capture(data, len);
}

void pc_udp_capture_enable(void)
{
    s_udp.cap_on = PROTO_TRUE;
    s_udp.cap_len = 0;
}
void pc_udp_capture_reset(void)
{
    s_udp.cap_len = 0;
}
const uint8_t *pc_udp_captured(void)
{
    return s_udp.cap_len ? s_udp.cap_buf : NULL;
}
size_t pc_udp_captured_len(void)
{
    return s_udp.cap_len;
}

proto_bool pc_udp_sendto(const char *dst_ip, uint16_t dst_port, const uint8_t *data, size_t len)
{
    (void)dst_ip;
    (void)dst_port;
    return host_capture(data, len); // captured "send" succeeds so the caller's success path runs
}

proto_bool pc_udp_peer_addr(const struct pc_udp_peer *peer, char *ip_out, size_t ip_cap, uint16_t *port_out)
{
    if (!peer)
    {
        return PROTO_FALSE;
    }
    if (ip_out && ip_cap)
    {
        strncpy(ip_out, peer->ip, ip_cap - 1);
        ip_out[ip_cap - 1] = '\0';
    }
    if (port_out)
    {
        *port_out = peer->port;
    }
    return PROTO_TRUE;
}

proto_bool pc_udp_listener_sendto(uint16_t listen_port, const char *dst_ip, uint16_t dst_port, const uint8_t *data,
                                  size_t len)
{
    (void)listen_port;
    (void)dst_ip;
    (void)dst_port;
    if (!s_udp.listener_sendto_ok)
    {
        return PROTO_FALSE; // test knob: model an unreachable peer (drops the observer)
    }
    host_capture(data, len);
    return PROTO_TRUE;
}

#endif // PROTOCORE_HOT
