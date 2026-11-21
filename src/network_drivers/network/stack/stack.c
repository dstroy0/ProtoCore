// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file stack.c
 * @brief Layer 3 (Network) - the idemIP stack. See stack.h.
 *
 * The context of this module holds one pointer to the cell of each unit. Each unit runs in its own cell,
 * which counts to the owner of its unit.
 *
 * Dispatch builds each reply in a transmit buffer of Dma, and no copy moves it. Each buffer starts on
 * the alignment of a DMA buffer. A reply of ARP is a whole frame, at the start of the buffer. A reply of
 * ICMP is a message, after the room for its IP header and its Ethernet header. Those two headers then go
 * in front of it, in the same buffer.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_IDEMIP

#include "network_drivers/network/stack/stack.h"

#include "src/idemip.h" // every unit of idemIP, its borrow and its operand block

#include "server/core/worker/worker.h" // the owners of the units, mmgr_zero_buf, and the context of this module

PROTOCORE_BEGIN_DECLS

/**
 * @brief The context of the stack: the unit cells and the interfaces that ::StackNs::bind bound. Each
 *        interface also keeps one transmit buffer for its next reply.
 */
struct StackStorage
{
    StackUnits units;
    uint8_t *transmit[IDEMIP_NETIF_COUNT];      ///< the buffer for the next reply, NULL where the interface holds none
    uint8_t transmit_index[IDEMIP_NETIF_COUNT]; ///< the descriptor of that buffer
    uint32_t bound;                             ///< one bit for each interface that bind bound
    proto_bool started;                         ///< ::StackNs::start ran
};

#define STACK_OFF_CONTEXT 0u
static_assert(STACK_OFF_CONTEXT + sizeof(struct StackStorage) <= PROTOCORE_STACK_BORROW,
              "PROTOCORE_STACK_BORROW is short of the stack context - raise it in buffer_sizing.h");
static_assert(IDEMIP_NETIF_COUNT <= 32u, "StackStorage::bound holds one bit for each of 32 interfaces");

#define STACK_CONTEXT(work_) ((struct StackStorage *)(void *)((work_) + STACK_OFF_CONTEXT))

// --- the cells -------------------------------------------------------------

static_assert(MMGR_CARCER_ALIGN >= IDEMIP_ALIGN,
              "a cell of MMgr starts on MMGR_CARCER_ALIGN, and each unit of idemIP needs IDEMIP_ALIGN");

// The cellblock of the units, apart from the plaintext arena. See PROTOCORE_STACK_CELLBLOCK_BYTES.
ParsMemoriaeInternae(stack_cells, PROTOCORE_STACK_CELLBLOCK_BYTES);
LocusCarcerum(stack_site, MMGR_MINIMUM_SECURITY(stack_cells));

// One zeroed cell. A cell starts on the grain of MMgr, which the assert above holds at IDEMIP_ALIGN.
static uint8_t *stack_take(size_t bytes, const CellOwner *owner)
{
    uint8_t *const cell = (uint8_t *)stack_site.stack_cells.persistent_buf_alloc(bytes, owner);
    if (cell == NULL)
    {
        return NULL;
    }
    mmgr_zero_buf(cell, bytes);
    return cell;
}

// Takes the cell of each unit. False where the cellblock cannot hold one of them.
static proto_bool stack_take_all(StackUnits *units)
{
    units->netif = stack_take(IDEMIP_NETIF_BORROW, idemip_owner_netif);
    units->loopif = stack_take(IDEMIP_LOOPIF_BORROW, idemip_owner_loopif);
    units->arp = stack_take(IDEMIP_ARP_BORROW, idemip_owner_arp);
    units->ip4_route = stack_take(IDEMIP_IP4_ROUTE_BORROW, idemip_owner_ip4_route);
    units->ip4_forward = stack_take(IDEMIP_IP4_FORWARD_BORROW, idemip_owner_ip4_forward);
    units->ip6_forward = stack_take(IDEMIP_IP6_FORWARD_BORROW, idemip_owner_ip6_forward);
    units->ip4_reass = stack_take(IDEMIP_IP4_REASS_BORROW, idemip_owner_ip4_reass);
    units->ip4_frag = stack_take(IDEMIP_IP4_FRAG_BORROW, idemip_owner_ip4_frag);
    units->igmp = stack_take(IDEMIP_IGMP_BORROW, idemip_owner_igmp);
    units->ip6_reass = stack_take(IDEMIP_IP6_REASS_BORROW, idemip_owner_ip6_reass);
    units->ip6_frag = stack_take(IDEMIP_IP6_FRAG_BORROW, idemip_owner_ip6_frag);
    units->mld6 = stack_take(IDEMIP_MLD6_BORROW, idemip_owner_mld6);
    units->icmp_in = stack_take(IDEMIP_ICMP_IN_BORROW, idemip_owner_icmp_in);
    units->icmp6_in = stack_take(IDEMIP_ICMP6_IN_BORROW, idemip_owner_icmp6_in);
    units->raw_pcb = stack_take(IDEMIP_RAW_PCB_BORROW, idemip_owner_raw_pcb);
    units->udp_pcb = stack_take(IDEMIP_UDP_PCB_BORROW, idemip_owner_udp_pcb);
    units->udplite = stack_take(IDEMIP_UDPLITE_BORROW, idemip_owner_udplite);
    units->tcp_pcb = stack_take(IDEMIP_TCP_PCB_BORROW, idemip_owner_tcp_pcb);
    units->tcp_in = stack_take(IDEMIP_TCP_IN_BORROW, idemip_owner_tcp_in);
    units->tcp_out = stack_take(IDEMIP_TCP_OUT_BORROW, idemip_owner_tcp_out);
    units->tcp_isn = stack_take(IDEMIP_TCP_ISN_BORROW, idemip_owner_tcp_isn);
    units->ip4_addr = stack_take(IDEMIP_IP4_ADDR_BORROW, idemip_owner_ip4_addr);
    units->ip6_addr = stack_take(IDEMIP_IP6_ADDR_BORROW, idemip_owner_ip6_addr);
    units->ip6_select = stack_take(IDEMIP_IP6_SELECT_BORROW, idemip_owner_ip6_select);
    units->pmtu4 = stack_take(IDEMIP_PMTU4_BORROW, idemip_owner_pmtu4);
    units->dns = stack_take(IDEMIP_DNS_BORROW, idemip_owner_dns);
    units->timeouts = stack_take(IDEMIP_TIMEOUTS_BORROW, idemip_owner_timeouts);
    units->tick = stack_take(IDEMIP_TICK_BORROW, idemip_owner_tick);
    units->dispatch = stack_take(IDEMIP_DISPATCH_BORROW, idemip_owner_dispatch);
    units->stats = stack_take(IDEMIP_STATS_BORROW, idemip_owner_stats);
    units->vlan = stack_take(IDEMIP_VLAN_BORROW, idemip_owner_vlan);
    units->ethip6 = stack_take(IDEMIP_ETHIP6_BORROW, idemip_owner_ethip6);
    proto_bool taken = PROTO_TRUE;
    for (uint8_t index = 0u; index < IDEMIP_NETIF_COUNT; index++)
    {
        units->phy[index] = stack_take(IDEMIP_PHY_BORROW, idemip_owner_phy);
        units->dma[index] = stack_take(IDEMIP_DMA_BORROW, idemip_owner_dma);
        units->nd6[index] = stack_take(IDEMIP_ND6_BORROW, idemip_owner_nd6);
        units->acd[index] = stack_take(IDEMIP_ACD_BORROW, idemip_owner_acd);
        units->autoip[index] = stack_take(IDEMIP_AUTOIP_BORROW, idemip_owner_autoip);
        units->pmtu6[index] = stack_take(IDEMIP_PMTU6_BORROW, idemip_owner_pmtu6);
        units->dhcp4[index] = stack_take(IDEMIP_DHCP4_BORROW, idemip_owner_dhcp4);
        units->dhcp6[index] = stack_take(IDEMIP_DHCP6_BORROW, idemip_owner_dhcp6);
        units->dad[index] = stack_take(IDEMIP_DAD_BORROW, idemip_owner_dad);
        units->slaac[index] = stack_take(IDEMIP_SLAAC_BORROW, idemip_owner_slaac);
        units->rdnss[index] = stack_take(IDEMIP_RDNSS_BORROW, idemip_owner_rdnss);
        taken = taken && units->phy[index] && units->dma[index] && units->nd6[index] && units->acd[index] &&
                units->autoip[index] && units->pmtu6[index] && units->dhcp4[index] && units->dhcp6[index] &&
                units->dad[index] && units->slaac[index] && units->rdnss[index];
    }
    return taken && units->netif && units->loopif && units->arp && units->ip4_route && units->ip4_forward &&
           units->ip6_forward && units->ip4_reass && units->ip4_frag && units->igmp && units->ip6_reass &&
           units->ip6_frag && units->mld6 && units->icmp_in && units->icmp6_in && units->raw_pcb &&
           units->udp_pcb && units->udplite && units->tcp_pcb && units->tcp_in && units->tcp_out &&
           units->tcp_isn && units->ip4_addr && units->ip6_addr && units->ip6_select && units->pmtu4 &&
           units->dns && units->timeouts && units->tick && units->dispatch && units->stats && units->vlan &&
           units->ethip6;
}

// Clears each unit. Each entry of a unit refuses a borrow that its clear did not run on. The PHY unit
// has no clear entry, and the zero of its cell in stack_take stands in for one.
static void stack_clear_all(const StackUnits *units)
{
    Netif.clear(units->netif);
    Loopif.clear(units->loopif);
    ArpTable.clear(units->arp);
    Ip4Route.clear(units->ip4_route);
    Ip4Forward.clear(units->ip4_forward);
    Ip6Forward.clear(units->ip6_forward);
    Ip4Reass.clear(units->ip4_reass);
    Ip4Frag.clear(units->ip4_frag);
    Igmp.clear(units->igmp);
    Ip6Reass.clear(units->ip6_reass);
    Ip6Frag.clear(units->ip6_frag);
    Mld6.clear(units->mld6);
    IcmpIn.clear(units->icmp_in);
    Icmp6In.clear(units->icmp6_in);
    RawPcb.clear(units->raw_pcb);
    UdpPcb.clear(units->udp_pcb);
    UdpLite.clear(units->udplite);
    TcpPcb.clear(units->tcp_pcb);
    TcpIn.clear(units->tcp_in);
    TcpOut.clear(units->tcp_out);
    TcpIsn.reset(units->tcp_isn);
    Ip4Addr.clear(units->ip4_addr);
    Ip6Addr.clear(units->ip6_addr);
    Ip6Select.clear(units->ip6_select);
    Pmtu4.clear(units->pmtu4);
    Dns.clear(units->dns);
    Timeouts.clear(units->timeouts);
    Tick.clear(units->tick);
    Dispatch.clear(units->dispatch);
    Stats.clear(units->stats);
    Vlan.clear(units->vlan);
    Ethip6.clear(units->ethip6);
    for (uint8_t index = 0u; index < IDEMIP_NETIF_COUNT; index++)
    {
        Dma.clear(units->dma[index]);
        Nd6.clear(units->nd6[index]);
        Acd.clear(units->acd[index]);
        AutoIp.clear(units->autoip[index]);
        Pmtu6.clear(units->pmtu6[index]);
        Dhcp4.clear(units->dhcp4[index]);
        Dhcp6.clear(units->dhcp6[index]);
        Dad.clear(units->dad[index]);
        Slaac.clear(units->slaac[index]);
        Rdnss.clear(units->rdnss[index]);
    }
}

// Binds the units that every interface shares: the receive path and the scheduler.
static proto_bool stack_bind_shared(const StackUnits *units)
{
    DispatchIo *const dispatch_io = IDEMIP_DISPATCH_IO(units->dispatch);
    dispatch_io->bind_args.stats = units->stats;
    dispatch_io->bind_args.netif = units->netif;
    dispatch_io->bind_args.loopif = units->loopif;
    dispatch_io->bind_args.vlan = units->vlan;
    dispatch_io->bind_args.arp = units->arp;
    dispatch_io->bind_args.ip4_addr = units->ip4_addr;
    dispatch_io->bind_args.ip4_reass = units->ip4_reass;
    dispatch_io->bind_args.icmp_in = units->icmp_in;
    dispatch_io->bind_args.igmp = units->igmp;
    dispatch_io->bind_args.ip6_addr = units->ip6_addr;
    dispatch_io->bind_args.ip6_reass = units->ip6_reass;
    dispatch_io->bind_args.icmp6_in = units->icmp6_in;
    dispatch_io->bind_args.mld6 = units->mld6;
    dispatch_io->bind_args.raw_pcb = units->raw_pcb;
    dispatch_io->bind_args.udp_pcb = units->udp_pcb;
    dispatch_io->bind_args.udplite = units->udplite;
    dispatch_io->bind_args.tcp_pcb = units->tcp_pcb;
    dispatch_io->bind_args.tcp_in = units->tcp_in;
    dispatch_io->bind_args.tcp_isn = units->tcp_isn;
    Dispatch.bind(units->dispatch);
    if (dispatch_io->status != IDEMIP_OK)
    {
        return PROTO_FALSE;
    }
    TickIo *const tick_io = IDEMIP_TICK_IO(units->tick);
    tick_io->bind_args.dispatch = units->dispatch;
    tick_io->bind_args.timeouts = units->timeouts;
    tick_io->bind_args.stats = units->stats;
    tick_io->bind_args.arp = units->arp;
    tick_io->bind_args.ip4_reass = units->ip4_reass;
    tick_io->bind_args.igmp = units->igmp;
    tick_io->bind_args.ip6_reass = units->ip6_reass;
    tick_io->bind_args.mld6 = units->mld6;
    tick_io->bind_args.netif = units->netif;
    Tick.bind(units->tick);
    return tick_io->status == IDEMIP_OK;
}

// --- the entries -----------------------------------------------------------

void protocore_stack_start(uint8_t *work)
{
    struct StackStorage *const context = STACK_CONTEXT(work);
    StackV.ok = PROTO_FALSE;
    if (context->started || StackV.start_args.isn_secret == NULL || !stack_take_all(&context->units))
    {
        return;
    }
    stack_clear_all(&context->units);
    TcpIsnIo *const isn_io = IDEMIP_TCP_ISN_IO(context->units.tcp_isn);
    isn_io->seed_args.key = StackV.start_args.isn_secret;
    isn_io->seed_args.key_len = IDEMIP_TCP_ISN_SECRET_BYTES;
    isn_io->seed_args.base = 0u;
    TcpIsn.seed(context->units.tcp_isn);
    if (isn_io->status != IDEMIP_OK || !stack_bind_shared(&context->units))
    {
        return;
    }
    context->started = PROTO_TRUE;
    StackV.ok = PROTO_TRUE;
}

void protocore_stack_bind(uint8_t *work)
{
    struct StackStorage *const context = STACK_CONTEXT(work);
    const StackBindArgs *const args = &StackV.bind_args;
    StackV.ok = PROTO_FALSE;
    if (!context->started || args->index >= IDEMIP_NETIF_COUNT || args->driver == NULL || args->mac == NULL)
    {
        return;
    }
    const uint8_t index = args->index;
    PhyIo *const phy_io = IDEMIP_PHY_IO(context->units.phy[index]);
    phy_io->bind_args.drv = args->driver;
    phy_io->bind_args.addr = args->phy_addr;
    Phy.bind(context->units.phy[index]);
    DmaIo *const dma_io = IDEMIP_DMA_IO(context->units.dma[index]);
    dma_io->bind_args.drv = args->driver;
    dma_io->bind_args.rx_base = args->rx_base;
    dma_io->bind_args.tx_base = args->tx_base;
    Dma.bind(context->units.dma[index]);
    NetifIo *const netif_io = IDEMIP_NETIF_IO(context->units.netif);
    netif_io->bind_args.index = index;
    netif_io->bind_args.phy = context->units.phy[index];
    netif_io->bind_args.hwaddr = args->mac;
    netif_io->bind_args.mtu = IDEMIP_ETH_MAX_PAYLOAD;
    Netif.bind(context->units.netif);
    if (phy_io->status != IDEMIP_OK || dma_io->status != IDEMIP_OK || netif_io->status != IDEMIP_OK)
    {
        return;
    }
    // An untagged interface accepts each frame. Its VLAN ID is thus the default VLAN of IEEE 802.1Q.
    DispatchIo *const dispatch_io = IDEMIP_DISPATCH_IO(context->units.dispatch);
    dispatch_io->if_args.dma = context->units.dma[index];
    dispatch_io->if_args.vid = 1u;
    dispatch_io->if_args.tagged = EMBED_FALSE;
    dispatch_io->if_args.index = index;
    Dispatch.if_bind(context->units.dispatch);
    // The row of the scheduler takes no reply buffer. This module drains each ring itself, and the drain
    // step of the scheduler finds each ring empty.
    TickIo *const tick_io = IDEMIP_TICK_IO(context->units.tick);
    tick_io->if_args.dma = context->units.dma[index];
    tick_io->if_args.nd6 = context->units.nd6[index];
    tick_io->if_args.out = NULL;
    tick_io->if_args.out_cap = 0u;
    tick_io->if_args.index = index;
    Tick.if_bind(context->units.tick);
    if (dispatch_io->status != IDEMIP_OK || tick_io->status != IDEMIP_OK)
    {
        return;
    }
    context->bound |= 1u << index;
    StackV.ok = PROTO_TRUE;
}

void protocore_stack_set_addr4(uint8_t *work)
{
    struct StackStorage *const context = STACK_CONTEXT(work);
    StackV.ok = PROTO_FALSE;
    if (!context->started)
    {
        return;
    }
    NetifIo *const netif_io = IDEMIP_NETIF_IO(context->units.netif);
    netif_io->addr4_args.index = StackV.addr4_args.index;
    netif_io->addr4_args.addr = StackV.addr4_args.address;
    netif_io->addr4_args.mask = StackV.addr4_args.mask;
    netif_io->addr4_args.gw = StackV.addr4_args.gateway;
    Netif.set_addr4(context->units.netif);
    StackV.ok = netif_io->status == IDEMIP_OK;
}

// The octets in front of the reply that Dispatch builds, from the EtherType of the request. The reply to
// an ARP request is a whole frame, at the start of the buffer. An IPv4 request can get an ICMP message,
// after one Ethernet header and one IPv4 header. An IPv6 request takes the IPv6 header in its place.
static size_t stack_reply_offset(const uint8_t *request)
{
    const uint16_t type = idemip_eth_type(request);
    if (type == IDEMIP_ETHERTYPE_IPV4)
    {
        return IDEMIP_ETH_HDR_LEN + IDEMIP_IPV4_HDR_LEN;
    }
    if (type == IDEMIP_ETHERTYPE_IPV6)
    {
        return IDEMIP_ETH_HDR_LEN + IDEMIP_IPV6_HDR_LEN;
    }
    return 0u;
}

// Holds one transmit buffer for the next reply of an interface. The interface keeps the buffer until a
// reply goes out in it. False where the ring has no free buffer.
static proto_bool stack_transmit_hold(struct StackStorage *context, uint8_t index)
{
    if (context->transmit[index] != NULL)
    {
        return PROTO_TRUE;
    }
    const DmaIo *const dma_io = IDEMIP_DMA_IO(context->units.dma[index]);
    Dma.tx_take(context->units.dma[index]);
    if (dma_io->status != IDEMIP_OK)
    {
        return PROTO_FALSE;
    }
    context->transmit[index] = dma_io->buf;
    context->transmit_index[index] = dma_io->index;
    return PROTO_TRUE;
}

// Writes the IP header and the Ethernet header of an ICMP reply in front of its message. The two go in
// the reply buffer of the interface, and the call gives the length of the frame. The frame goes to the link source of the request. That
// address is the next hop back to the requester. Zero where Netif has no record of the interface.
static size_t stack_icmp_frame(const struct StackStorage *context, uint8_t index, const uint8_t *request)
{
    NetifIo *const netif_io = IDEMIP_NETIF_IO(context->units.netif);
    netif_io->if_args.index = index;
    Netif.get(context->units.netif);
    if (netif_io->status != IDEMIP_OK)
    {
        return 0u;
    }
    uint8_t *const frame = context->transmit[index];
    size_t payload_len = 0u;
    uint16_t type = 0u;
    if (IDEMIP_DISPATCH_IO(context->units.dispatch)->ip_version == 4u)
    {
        // RFC 6864 sec 4.1: the source of an atomic datagram MAY set the Identification to any value.
        const IcmpInIo *const icmp_io = IDEMIP_ICMP_IN_IO(context->units.icmp_in);
        payload_len = IDEMIP_IPV4_HDR_LEN + icmp_io->out_len;
        IdemIpIp4Fields fields = {0};
        // Explicit cast narrows the length, which the request frame holds below IDEMIP_DMA_FRAME_MAX
        fields.total_len = (uint16_t)payload_len;
        fields.flags_frag = IDEMIP_IP4_FLAG_DF;
        fields.ttl = IDEMIP_IP_DEFAULT_TTL;
        fields.proto = IDEMIP_IP4_PROTO_ICMP;
        fields.src = icmp_io->src;
        fields.dst = icmp_io->dst;
        idemip_ip4_build(frame + IDEMIP_ETH_HDR_LEN, &fields);
        type = IDEMIP_ETHERTYPE_IPV4;
    }
    else
    {
        const Icmp6InIo *const icmp6_io = IDEMIP_ICMP6_IN_IO(context->units.icmp6_in);
        payload_len = IDEMIP_IPV6_HDR_LEN + icmp6_io->out_len;
        IdemIpIp6BuildArgs fields = {0};
        fields.src = icmp6_io->src;
        fields.dst = icmp6_io->dst;
        // Explicit cast narrows the length, which the request frame holds below IDEMIP_DMA_FRAME_MAX
        fields.payload_len = (uint16_t)icmp6_io->out_len;
        fields.next_hdr = IDEMIP_IP6_NH_ICMPV6;
        fields.hop_limit = IDEMIP_IP_DEFAULT_TTL;
        idemip_ip6_build(frame + IDEMIP_ETH_HDR_LEN, &fields);
        type = IDEMIP_ETHERTYPE_IPV6;
    }
    idemip_eth_build(frame, idemip_eth_src(request), netif_io->hwaddr, type);
    return IDEMIP_ETH_HDR_LEN + idemip_eth_pad(frame, payload_len);
}

// Sends the reply that the last dispatch built in the reply buffer. False where Dispatch asks for no send,
// or where the reply is not of the kind that its offset has room for. False also where the ring takes no
// frame. The interface then keeps the buffer for its next reply.
static proto_bool stack_answer(struct StackStorage *context, uint8_t index, const uint8_t *request, size_t offset)
{
    const DispatchIo *const dispatch_io = IDEMIP_DISPATCH_IO(context->units.dispatch);
    if ((dispatch_io->act & IDEMIP_DISPATCH_ACT_SEND) == 0u)
    {
        return PROTO_FALSE;
    }
    size_t len = 0u;
    if (dispatch_io->pcb_kind == IDEMIP_DISPATCH_PCB_ARP && offset == 0u)
    {
        len = dispatch_io->out_len;
    }
    else if (dispatch_io->pcb_kind == IDEMIP_DISPATCH_PCB_ICMP && offset != 0u)
    {
        len = stack_icmp_frame(context, index, request);
    }
    if (len == 0u)
    {
        return PROTO_FALSE;
    }
    DmaIo *const dma_io = IDEMIP_DMA_IO(context->units.dma[index]);
    dma_io->desc_args.index = context->transmit_index[index];
    // Explicit cast narrows the length, which IDEMIP_DMA_FRAME_MAX holds below 65536
    dma_io->desc_args.len = (uint16_t)len;
    Dma.tx_post(context->units.dma[index]);
    if (dma_io->status != IDEMIP_OK)
    {
        return PROTO_FALSE;
    }
    context->transmit[index] = NULL;
    return PROTO_TRUE;
}

// Drains the receive ring of one interface. Each frame goes through Dispatch, its reply goes out, and its
// descriptor goes back to the engine. Each descriptor goes back, and a unit that pins the frame keeps
// it. The post ends the claim of this pass, and the last unpin gives the descriptor back.
static void stack_drain(struct StackStorage *context, uint8_t index, uint32_t now_ms, uint32_t random_word)
{
    DmaIo *const dma_io = IDEMIP_DMA_IO(context->units.dma[index]);
    DispatchIo *const dispatch_io = IDEMIP_DISPATCH_IO(context->units.dispatch);
    Dma.rx_take(context->units.dma[index]);
    while (dma_io->status == IDEMIP_OK)
    {
        // The take of a reply buffer below writes the same operand block. This loop thus reads the request first.
        const uint8_t descriptor = dma_io->index;
        const uint8_t *const request = dma_io->buf;
        const size_t request_len = dma_io->len;
        const size_t offset = stack_reply_offset(request);
        // Without a reply buffer, Dispatch has nowhere to build a reply and reports the frame BUSY.
        const proto_bool held = stack_transmit_hold(context, index);
        dispatch_io->input_args.frame = request;
        dispatch_io->input_args.len = request_len;
        dispatch_io->input_args.out = held ? context->transmit[index] + offset : NULL;
        dispatch_io->input_args.out_cap = held ? IDEMIP_DMA_FRAME_MAX - offset : 0u;
        dispatch_io->input_args.now_ms = now_ms;
        dispatch_io->input_args.rand = random_word;
        dispatch_io->input_args.desc = descriptor;
        dispatch_io->input_args.netif = index;
        Dispatch.input(context->units.dispatch);
        StackV.frames++;
        if (stack_answer(context, index, request, offset))
        {
            StackV.sent++;
        }
        dma_io->desc_args.index = descriptor;
        Dma.rx_post(context->units.dma[index]);
        Dma.rx_take(context->units.dma[index]);
    }
}

void protocore_stack_tick(uint8_t *work)
{
    struct StackStorage *const context = STACK_CONTEXT(work);
    StackV.ok = PROTO_FALSE;
    StackV.frames = 0u;
    StackV.sent = 0u;
    if (!context->started)
    {
        return;
    }
    TickIo *const tick_io = IDEMIP_TICK_IO(context->units.tick);
    tick_io->open_args.now_ms = StackV.tick_args.now_ms;
    tick_io->open_args.rand = StackV.tick_args.random_word;
    Tick.open(context->units.tick);
    for (uint8_t index = 0u; index < IDEMIP_NETIF_COUNT; index++)
    {
        if ((context->bound & (1u << index)) != 0u)
        {
            stack_drain(context, index, StackV.tick_args.now_ms, StackV.tick_args.random_word);
        }
    }
    // The rings are empty. The first drain step thus moves the phase on to the services.
    Tick.drain(context->units.tick);
    while (tick_io->status == IDEMIP_OK)
    {
        Tick.drain(context->units.tick);
    }
    Tick.service(context->units.tick);
    while (tick_io->status == IDEMIP_OK)
    {
        Tick.service(context->units.tick);
    }
    // This module opens no connection and holds no frame on ARP. No step of the flush thus hands a frame
    // back to it.
    Tick.flush(context->units.tick);
    while (tick_io->status == IDEMIP_OK)
    {
        Tick.flush(context->units.tick);
    }
    for (uint8_t index = 0u; index < IDEMIP_NETIF_COUNT; index++)
    {
        if ((context->bound & (1u << index)) != 0u)
        {
            Dma.tx_reap(context->units.dma[index]);
        }
    }
    StackV.ok = PROTO_TRUE;
}

const StackUnits *protocore_stack_units(const uint8_t *work)
{
    return &((const struct StackStorage *)(const void *)(work + STACK_OFF_CONTEXT))->units;
}

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for itself.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_STACK_BORROW persistent bytes
} StackOwnContext;
static StackOwnContext s_own;

uint8_t *protocore_stack_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = (uint8_t *)protocore_plain_persist(PROTOCORE_STACK_BORROW);
    }
    return s_own.span;
}

/** @brief The operands and the outcome. */
StackVars StackV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_IDEMIP
