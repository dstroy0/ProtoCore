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
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from

#include "config/platform/platform.h" // the stack's UDP, under our names
#include "mmgr/rawmemcpy/rawmemcpy.h"                               // raw.read: the caller's bytes into the pbuf
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

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define UDP_CLIENT_OFF_CTX 0u
static_assert(UDP_CLIENT_OFF_CTX + sizeof(struct UdpClientStorage) <= PROTOCORE_UDP_CLIENT_BORROW,
              "PROTOCORE_UDP_CLIENT_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define UDP_CLIENT_CTX(w) ((struct UdpClientStorage *)(void *)((w) + UDP_CLIENT_OFF_CTX))

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
// that owns it. The stack fixes this signature, so there is no borrow to take and the module's own
// span is what it reads.
static protocore_net_err send_do(protocore_net_call *c)
{
    protocore_udp_send_call *k = (protocore_udp_send_call *)c;
    uint8_t *work = protocore_udp_client_span();
    struct UdpClientStorage *st = UDP_CLIENT_CTX(work);
    if (st->out == NULL)
    {
        st->out = protocore_net_udp_new();
    }
    if (st->out == NULL)
    {
        return PROTOCORE_NET_OK; // no control block: k->ok stays false and the caller still holds its bytes
    }
    apply_dscp(st->out);
    k->ok = wire_send(st->out, k->dst, k->port, k->data, k->len);
    return PROTOCORE_NET_OK;
}

// ---------------------------------------------------------------------------
// The bodies behind the table
// ---------------------------------------------------------------------------

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_UDP_CLIENT_BORROW persistent bytes, or null while the pool was short
} UdpClientOwnCtx;
static UdpClientOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_udp_client_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_plaintext_persist_span(PROTOCORE_UDP_CLIENT_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

static void send_to(uint8_t *restrict work)
{
    (void)work;
    if (UdpClient.data == NULL || UdpClient.len == 0 || UdpClient.len > PROTOCORE_UDP_RX_BUF_SIZE ||
        UdpClient.dst == NULL || UdpClient.dst->family == PROTOCORE_IP_NONE)
    {
        UdpClient.ok = PROTO_FALSE;
        return;
    }
    // The marshal is synchronous, so this outlives the call and carries its answer back.
    protocore_udp_send_call k = {{0}, UdpClient.dst, UdpClient.data, UdpClient.len, UdpClient.dst_port, PROTO_FALSE};
    (void)protocore_net_call_marshal(send_do, &k.base);
    UdpClient.ok = k.ok;
}

// Designated, so a member's position in the struct does not decide what it binds to.
UdpClientNs UdpClient = {.sendto = send_to};

PROTOCORE_END_DECLS
