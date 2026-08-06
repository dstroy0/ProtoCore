// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file udp_client.c
 * @brief Layer 4 UDP sending side. See udp_client.h.
 *
 * One send ring and one outbound control block. sendto() is the ring's sole producer and poll() its
 * sole consumer, so the two run in different tasks and the ring is the only thing between them.
 */

#include "network_drivers/transport/udp/udp_client.h"
#include "network_drivers/transport/udp/udp_datagram.h" // the wire layout the ring carries

#if PROTOCORE_HOT
#include "board_drivers/board_profiles/pc_platform.h" // the target's UDP, under our names
#include "network_drivers/transport/diffserv.h"       // DSCP marking; compiles out when off
#include "network_drivers/transport/net_addr.h"       // NetAddr: the stack's address as a pc_ip
#endif

PROTO_BEGIN_DECLS

/**
 * @brief All sending-side UDP state, owned by one instance.
 *
 * Each stage belongs to one end of the ring: sendto() writes the header stage on the way in, the
 * drain reads its own on the way out, and the payload stage is held for the length of one send.
 */
typedef struct
{
    uint8_t tx[PC_UDP_TX_RING];
    _Atomic size_t tx_head;            ///< Producer: sendto().
    _Atomic size_t tx_tail;            ///< Consumer: poll().
    uint8_t stage[PC_UDP_RX_BUF_SIZE]; ///< Contiguous payload handed to the wire.
    uint8_t whdr[PC_UDP_DGRAM_HDR];    ///< Header staged by the producer.
    uint8_t rhdr[PC_UDP_DGRAM_HDR];    ///< Header staged by the consumer.
#if PROTOCORE_HOT
    pc_udp_pcb *out; ///< Shared outbound control block, created on first flush.
#else
    proto_bool cap_on;
    uint8_t cap_buf[PC_UDP_RX_BUF_SIZE];
    size_t cap_len;
#endif
} UdpClientCtx;
static UdpClientCtx s_cli;

// ---------------------------------------------------------------------------
// The seam: where a queued datagram goes when it leaves
// ---------------------------------------------------------------------------

#if PROTOCORE_HOT

typedef struct
{
    pc_net_call base;
} pc_udp_flush_call;

// Stamp the control block with the configured UDP DSCP, applied per flush so a DiffServ.set_udp()
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

// Drain the ring to the wire. Marshaled, so this is the sole consumer and it runs in the stack's
// thread. The control block is created here on first use, in the thread that owns it.
static pc_net_err flush_do(pc_net_call *c)
{
    (void)c;
    if (s_cli.out == NULL)
    {
        s_cli.out = pc_net_udp_new();
    }
    if (s_cli.out == NULL)
    {
        return PC_NET_OK;
    }
    apply_dscp(s_cli.out);
    pc_udp_dgram d = {{PC_IP_NONE, {0}}, 0, 0};
    while (pc_udp_dgram_take(s_cli.tx, PC_UDP_TX_RING, &s_cli.tx_head, &s_cli.tx_tail, s_cli.rhdr, &d, s_cli.stage,
                             sizeof(s_cli.stage)))
    {
        (void)wire_send(s_cli.out, &d.addr, d.port, s_cli.stage, d.len);
    }
    return PC_NET_OK;
}

static void drain(void)
{
    pc_udp_flush_call k = {{0}};
    (void)pc_net_call_marshal(flush_do, &k.base);
}

#else // host build: the wire is a capture a test reads back

static void drain(void)
{
    pc_udp_dgram d = {{PC_IP_NONE, {0}}, 0, 0};
    while (pc_udp_dgram_take(s_cli.tx, PC_UDP_TX_RING, &s_cli.tx_head, &s_cli.tx_tail, s_cli.rhdr, &d, s_cli.stage,
                             sizeof(s_cli.stage)))
    {
        if (s_cli.cap_on && d.len > 0 && d.len <= sizeof(s_cli.cap_buf))
        {
            proto_raw_read(s_cli.cap_buf, s_cli.stage, d.len);
            s_cli.cap_len = d.len;
        }
    }
}

#endif // PROTOCORE_HOT

// ---------------------------------------------------------------------------
// The bodies behind the table
// ---------------------------------------------------------------------------

static proto_bool send_to(const pc_ip *dst, uint16_t dst_port, const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0 || len > PC_UDP_RX_BUF_SIZE || dst == NULL || dst->type == PC_IP_NONE)
    {
        return PROTO_FALSE;
    }
    pc_udp_dgram d = {*dst, dst_port, (uint16_t)len};
    return pc_udp_dgram_put(s_cli.tx, PC_UDP_TX_RING, &s_cli.tx_head, &s_cli.tx_tail, s_cli.whdr, &d, data, len);
}

static void poll_out(void)
{
    if (pc_ring_available(&s_cli.tx_head, &s_cli.tx_tail, PC_UDP_TX_RING) > 0)
    {
        drain();
    }
}

static size_t sndbuf_of(void)
{
    return pc_udp_dgram_room(&s_cli.tx_head, &s_cli.tx_tail, PC_UDP_TX_RING);
}

#if !PROTOCORE_HOT
static void capture_enable(void)
{
    s_cli.cap_on = PROTO_TRUE;
    s_cli.cap_len = 0;
}

static void capture_reset(void)
{
    s_cli.cap_len = 0;
}

static const uint8_t *captured(void)
{
    if (s_cli.cap_len == 0)
    {
        return NULL;
    }
    return s_cli.cap_buf;
}

static size_t captured_len(void)
{
    return s_cli.cap_len;
}
#endif // !PROTOCORE_HOT

const UdpClientNs UdpClient = {send_to,        poll_out,      sndbuf_of,
#if !PROTOCORE_HOT
                               capture_enable, capture_reset, captured,
                               captured_len
#endif
};

PROTO_END_DECLS
