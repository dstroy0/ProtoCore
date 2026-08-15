// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file client.c
 * @brief Layer 4 UDP sending side. See client.h.
 *
 * One outbound control block, created on first send. sendto() marshals onto the stack's thread and
 * sends the caller's bytes from where they already are.
 */

#include "network_drivers/transport/udp/client/client.h"

#include "core_setup/board_profiles/protocore_platform.h" // the stack's UDP, under our names
#include "mmgr/rawmemcpy.h"                               // raw.read: the caller's bytes into the pbuf
#include "network_drivers/transport/diffserv/diffserv.h"  // DSCP marking; compiles out when off
#include "network_drivers/transport/net_addr/net_addr.h"  // NetAddr: the stack's address as a protocore_ip

PROTOCORE_BEGIN_DECLS

/**
 * @brief The sending side's compile-time storage: the one control block every datagram leaves
 *        through.
 *
 * Opened on first send, in the thread that owns it.
 */
struct UdpClientStorage
{
    protocore_udp_pcb *out;
};

/**
 * @brief The sending side's state and the call that reaches it - what UdpClientNs points at.
 *
 * RFC 768 names the datagram's destination by address and port, which is the whole of what a send
 * needs; there is no connection to hold, so the only state is the shared outbound control block.
 *
 * @var UdpClientInternal::store  the shared control block every datagram leaves through
 * @var UdpClientInternal::ns     the handle a caller sets the call's members on
 */
struct UdpClientInternal
{
    struct UdpClientStorage *store;
    UdpClientNs *ns;
};

static struct UdpClientStorage s_store;

static struct UdpClientInternal s_cli = {.store = &s_store, .ns = &UdpClient};

// The datagram, carried to the stack's thread by pointer. The caller's buffer is the send buffer,
// so nothing is copied into this and it holds no storage of its own.
typedef struct
{
    protocore_net_call base;
    const protocore_ip *dst;
    const uint8_t *data;
    size_t len;
    uint16_t port;
    proto_bool ok;
} protocore_udp_send_call;

// Stamp the control block with the configured UDP DSCP, applied per send so a DiffServ.set_udp()
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

// Take a pbuf, hand the bytes over, release it. Runs in the stack's thread only.
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

// The send, on the stack's thread. The control block is created here on first use, in the thread
// that owns it.
static protocore_net_err send_do(protocore_net_call *c)
{
    protocore_udp_send_call *k = (protocore_udp_send_call *)c;
    if (s_store.out == NULL)
    {
        s_store.out = protocore_net_udp_new();
    }
    if (s_store.out == NULL)
    {
        return PROTOCORE_NET_OK; // no control block: k->ok stays false and the caller still holds its bytes
    }
    apply_dscp(s_store.out);
    k->ok = wire_send(s_store.out, k->dst, k->port, k->data, k->len);
    return PROTOCORE_NET_OK;
}

// ---------------------------------------------------------------------------
// The bodies behind the table
// ---------------------------------------------------------------------------

static void send_to(struct UdpClientInternal *restrict ctx)
{
    if (ctx->ns->data == NULL || ctx->ns->len == 0 || ctx->ns->len > PROTOCORE_UDP_RX_BUF_SIZE ||
        ctx->ns->dst == NULL || ctx->ns->dst->family == PROTOCORE_IP_NONE)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    // The marshal is synchronous, so this outlives the call and carries its answer back.
    protocore_udp_send_call k = {{0}, ctx->ns->dst, ctx->ns->data, ctx->ns->len, ctx->ns->dst_port, PROTO_FALSE};
    (void)protocore_net_call_marshal(send_do, &k.base);
    ctx->ns->ok = k.ok;
}

// Designated, so a member's position in the struct does not decide what it binds to.
UdpClientNs UdpClient = {.sendto = send_to, .internal = &s_cli};

PROTOCORE_END_DECLS
