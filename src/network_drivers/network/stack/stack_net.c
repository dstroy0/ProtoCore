// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file stack_net.c
 * @brief The raw API of lwIP over the stack module. See stack_net.h.
 *
 * Each TCP control block that carries a connection holds one entry of the TCP of the stack, and this file holds
 * the two rings of each entry. An event of an entry becomes a callback: STACK_TCP_CONNECT the accept of the
 * listener, STACK_TCP_DATA the receive, STACK_TCP_DISCONNECT the receive of a NULL buffer, and STACK_TCP_ERROR
 * the error. The text of an entry stays on its receive ring until protocore_net_recved, so the window opens
 * as the transport takes the text, as lwIP does.
 *
 * protocore_stack_net_pass runs one pass of the stack under the lock, and the session of worker 0 calls it.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the knobs of the stack

#if PROTOCORE_ENABLE_IDEMIP

#include "network_drivers/network/stack/stack_net.h"

#include "memoria_anularis/memoria_anularis.h"       // the rings of each entry
#include "network_drivers/network/stack/stack.h"     // Stack.tick, and the borrow of the stack
#include "network_drivers/network/stack/stack_tcp.h" // the entries, their events and their rings
#include "proximus_operor/proximus_operor.h"         // proxim.read
#include "src/idemip.h"                              // the units of idemIP that the UDP, ARP and IGMP paths call

EMBED_BEGIN_DECLS

// --- the storage --------------------------------------------------------------------------------

/** @brief The control blocks: one for each entry, and the listeners. */
#define STACK_NET_PCBS (PROTOCORE_STACK_TCP_ENTRIES + PROTOCORE_STACK_NET_LISTENERS)
/** @brief The octets of the transmit ring of each entry. */
#define STACK_NET_TX_BYTES (PROTOCORE_STACK_TCP_SEGMENTS * PROTOCORE_STACK_NET_SEGMENT_BYTES)

static_assert(MMGR_RING_POW2(PROTOCORE_STACK_NET_RX_BYTES),
              "PROTOCORE_STACK_NET_RX_BYTES must be a power of two: a ring index wraps with a mask");
static_assert(
    PROTOCORE_STACK_NET_RX_BYTES - 1u <= 0xFFFFu,
    "the receive window of an entry is PROTOCORE_STACK_NET_RX_BYTES less one, and RFC 9293 holds it in 16 bits");

/** @brief The two rings of one entry, and what the transport did with them. */
typedef struct
{
    uint8_t rx_buffer[PROTOCORE_STACK_NET_RX_BYTES];
    EMBED_ALIGN(4) uint8_t tx_buffer[STACK_NET_TX_BYTES];
    mmgr_ring rx;        ///< the receive ring: the TCP of the stack writes it, the transport reads it
    mmgr_ring tx;        ///< the transmit ring: the transport writes it, the TCP of the stack sends it
    protocore_pcb *pcb;  ///< the control block of the connection, NULL for none
    uint32_t delivered;  ///< the octets at the front of the receive ring that went to the transport
    uint32_t released;   ///< the octets that the peer acknowledged, as the last pass saw them
    embed_bool fin_owed; ///< the peer sent its FIN, and the receive of a NULL buffer waits on the text
} StackNetEntry;

/** @brief One packet buffer of the pool, and its octets. */
typedef struct
{
    protocore_pbuf pbuf;
    uint8_t bytes[PROTOCORE_STACK_NET_PBUF_BYTES];
    embed_bool taken;
} StackNetPbuf;

/** @brief The state of the raw API. */
typedef struct
{
    StackNetEntry entries[PROTOCORE_STACK_TCP_ENTRIES];
    protocore_pcb pcbs[STACK_NET_PCBS];
    StackNetPbuf pbufs[PROTOCORE_STACK_NET_PBUFS];
    protocore_udp_pcb udp[PROTOCORE_STACK_NET_UDP_PCBS];
    protocore_platform_mutex_ctrl lock_ctrl;
    protocore_platform_mutex lock;  ///< the lock of the stack, NULL until the first call
    protocore_platform_task holder; ///< the task that holds the lock
    uint32_t depth;                 ///< the takes of the lock by its holder
    embed_bool started;             ///< the TCP of the stack started, and each entry has its rings
} StackNetCtx;

static StackNetCtx s_net;

const protocore_net_ip protocore_stack_net_any = {PROTOCORE_NET_TYPE_ANY, {0}};

static protocore_net_err stack_net_connect_try(protocore_pcb *pcb, uint32_t now_ms);

// --- the lock ------------------------------------------------------------------------------------

// The lock of the stack. A holder takes it again from a callback, and only the last give lets it go. The first
// call creates it, before a second task reaches the stack.
static void stack_net_lock(void)
{
    if (s_net.lock == NULL)
    {
        s_net.lock = protocore_platform_mutex_create(&s_net.lock_ctrl);
    }
    const protocore_platform_task self = protocore_platform_task_self();
    if (s_net.depth != 0u && s_net.holder == self)
    {
        s_net.depth++;
        return;
    }
    (void)protocore_platform_mutex_take(s_net.lock, PROTOCORE_PLATFORM_WAIT_FOREVER);
    s_net.holder = self;
    s_net.depth = 1u;
}

static void stack_net_unlock(void)
{
    s_net.depth--;
    if (s_net.depth == 0u)
    {
        (void)protocore_platform_mutex_give(s_net.lock);
    }
}

protocore_net_err protocore_net_call_marshal(protocore_net_err (*fn)(protocore_net_call *), protocore_net_call *c)
{
    if (fn == NULL)
    {
        return PROTOCORE_NET_OK;
    }
    stack_net_lock();
    const protocore_net_err result = fn(c);
    stack_net_unlock();
    return result;
}

// --- addresses -----------------------------------------------------------------------------------

uint32_t protocore_net_ip4_u32(const protocore_net_ip *a)
{
    uint32_t word = 0u;
    if (a != NULL)
    {
        EMBED_CALL(proxim.read, ProximusCfg, .dst = (uint8_t *)&word, .at = a->bytes, .size = 4u);
    }
    return word;
}

void protocore_net_ip4_set(protocore_net_ip *a, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3)
{
    if (a == NULL)
    {
        return;
    }
    *a = (protocore_net_ip){PROTOCORE_NET_TYPE_V4, {b0, b1, b2, b3}};
}

int protocore_net_ip4_is_multicast(const protocore_net_ip *a)
{
    return (a != NULL && a->type == PROTOCORE_NET_TYPE_V4 && (a->bytes[0] & 0xF0u) == 0xE0u) ? 1 : 0;
}

int protocore_net_ip_parse(const char *s, protocore_net_ip *out)
{
    if (s == NULL || out == NULL)
    {
        return 0;
    }
    uint8_t octets[4] = {0};
    for (uint8_t part = 0u; part < 4u; part++)
    {
        uint32_t value = 0u;
        uint8_t digits = 0u;
        while (*s >= '0' && *s <= '9' && digits < 3u)
        {
            value = (value * 10u) + (uint32_t)(*s - '0');
            s++;
            digits++;
        }
        if (digits == 0u || value > 255u || (part < 3u && *s != '.') || (part == 3u && *s != '\0'))
        {
            return 0;
        }
        // Explicit cast narrows the octet, which the test above holds at 255 or below
        octets[part] = (uint8_t)value;
        if (part < 3u)
        {
            s++;
        }
    }
    protocore_net_ip4_set(out, octets[0], octets[1], octets[2], octets[3]);
    return 1;
}

char *protocore_net_ip_print(const protocore_net_ip *a, char *buf, int cap)
{
    if (buf == NULL || cap <= 0)
    {
        return buf;
    }
    int at = 0;
    for (uint8_t part = 0u; part < 4u && a != NULL; part++)
    {
        const uint8_t octet = a->bytes[part];
        char digits[3];
        int count = 0;
        uint8_t rest = octet;
        do
        {
            // Explicit cast narrows the digit, which the remainder holds below 10
            digits[count] = (char)('0' + (rest % 10u));
            count++;
            rest /= 10u;
        } while (rest != 0u);
        while (count > 0 && at < cap - 1)
        {
            count--;
            buf[at] = digits[count];
            at++;
        }
        if (part < 3u && at < cap - 1)
        {
            buf[at] = '.';
            at++;
        }
    }
    buf[at] = '\0';
    return buf;
}

protocore_net_err protocore_net_dns_resolve(const char *host, protocore_net_ip *addr, protocore_net_dns_found_fn found,
                                            void *arg)
{
    (void)found;
    (void)arg;
    return (protocore_net_ip_parse(host, addr) != 0) ? PROTOCORE_NET_OK : PROTOCORE_NET_ERR_VAL;
}

// --- packet buffers --------------------------------------------------------------------------------

protocore_pbuf *protocore_net_pbuf_alloc(int layer, uint16_t len, int type)
{
    (void)layer;
    (void)type;
    if (len > PROTOCORE_STACK_NET_PBUF_BYTES)
    {
        return NULL;
    }
    for (size_t index = 0u; index < PROTOCORE_STACK_NET_PBUFS; index++)
    {
        StackNetPbuf *const slot = &s_net.pbufs[index];
        if (!slot->taken)
        {
            slot->taken = EMBED_TRUE;
            slot->pbuf = (protocore_pbuf){NULL, slot->bytes, len, len};
            return &slot->pbuf;
        }
    }
    return NULL;
}

uint8_t protocore_net_pbuf_free(protocore_pbuf *p)
{
    for (size_t index = 0u; index < PROTOCORE_STACK_NET_PBUFS; index++)
    {
        if (p == &s_net.pbufs[index].pbuf)
        {
            s_net.pbufs[index].taken = EMBED_FALSE;
            return 1u;
        }
    }
    return 0u;
}

uint16_t protocore_net_pbuf_copy(const protocore_pbuf *p, void *dst, uint16_t len, uint16_t off)
{
    if (p == NULL || dst == NULL || p->payload == NULL || off >= p->len)
    {
        return 0u;
    }
    // Explicit cast narrows the octets after off, which p->len holds in 16 bits
    const uint16_t left = (uint16_t)(p->len - off);
    const uint16_t count = (len < left) ? len : left;
    EMBED_CALL(proxim.read, ProximusCfg, .dst = (uint8_t *)dst, .at = (const uint8_t *)p->payload + off, .size = count);
    return count;
}

// --- TCP control blocks ------------------------------------------------------------------------------

// A free control block, cleared, or NULL when each one is taken.
static protocore_pcb *stack_net_pcb_take(void)
{
    for (size_t index = 0u; index < STACK_NET_PCBS; index++)
    {
        protocore_pcb *const pcb = &s_net.pcbs[index];
        if (!pcb->in_use)
        {
            *pcb = (protocore_pcb){0};
            pcb->in_use = EMBED_TRUE;
            pcb->entry = PROTOCORE_STACK_NET_NONE;
            pcb->ttl = 64u;
            return pcb;
        }
    }
    return NULL;
}

// Give a control block back. Its entry, where it holds one, loses the control block.
static void stack_net_pcb_give(protocore_pcb *pcb)
{
    if (pcb->entry < PROTOCORE_STACK_TCP_ENTRIES && s_net.entries[pcb->entry].pcb == pcb)
    {
        s_net.entries[pcb->entry].pcb = NULL;
    }
    pcb->in_use = EMBED_FALSE;
    pcb->entry = PROTOCORE_STACK_NET_NONE;
}

protocore_pcb *protocore_net_new(int type)
{
    (void)type;
    return stack_net_pcb_take();
}

protocore_net_err protocore_net_bind(protocore_pcb *p, const protocore_net_ip *a, uint16_t port)
{
    if (p == NULL || port == 0u)
    {
        return PROTOCORE_NET_ERR_VAL;
    }
    for (size_t index = 0u; index < STACK_NET_PCBS; index++)
    {
        const protocore_pcb *const other = &s_net.pcbs[index];
        if (other != p && other->in_use && other->listen && other->local_port == port)
        {
            return PROTOCORE_NET_ERR_USE;
        }
    }
    p->local_port = port;
    p->local_ip = (a != NULL) ? *a : protocore_stack_net_any;
    return PROTOCORE_NET_OK;
}

// Start the TCP of the stack, give each entry its rings, and open each listener that waits. The stack has to
// run first, and the pass tries again until it does.
static void stack_net_start(void)
{
    uint8_t *const stack = protocore_stack_span();
    uint8_t *const tcp = protocore_stack_tcp_span();
    if (s_net.started || stack == NULL || tcp == NULL)
    {
        return;
    }
    StackTcpV.start_args.stack = stack;
    StackTcpV.start_args.window = PROTOCORE_STACK_NET_RX_BYTES - 1u;
    StackTcp.start(tcp);
    if (!StackTcpV.ok)
    {
        return;
    }
    for (uint8_t index = 0u; index < PROTOCORE_STACK_TCP_ENTRIES; index++)
    {
        StackNetEntry *const entry = &s_net.entries[index];
        (void)EMBED_CALL(anularis.init, AnularisCfg, .ring = &entry->rx, .buf = entry->rx_buffer,
                         .capacity = PROTOCORE_STACK_NET_RX_BYTES, .segment_count = 1u);
        StackTcpV.slot_args.receive = &entry->rx;
        StackTcpV.slot_args.transmit = &entry->tx;
        StackTcpV.slot_args.transmit_buffer = entry->tx_buffer;
        StackTcpV.slot_args.transmit_bytes = STACK_NET_TX_BYTES;
        StackTcpV.slot_args.index = index;
        StackTcp.slot_bind(tcp);
        entry->pcb = NULL;
    }
    s_net.started = EMBED_TRUE;
    for (size_t index = 0u; index < STACK_NET_PCBS; index++)
    {
        const protocore_pcb *const pcb = &s_net.pcbs[index];
        if (pcb->in_use && pcb->listen)
        {
            StackTcpV.listen_args.port = pcb->local_port;
            StackTcpV.listen_args.ip_version = 4u;
            StackTcpV.listen_args.backlog = 4u;
            StackTcp.listen(tcp);
        }
    }
}

protocore_pcb *protocore_net_listen(protocore_pcb *p, uint8_t backlog)
{
    if (p == NULL || p->local_port == 0u)
    {
        return NULL;
    }
    p->listen = EMBED_TRUE;
    p->state = 1u;
    if (s_net.started)
    {
        StackTcpV.listen_args.port = p->local_port;
        StackTcpV.listen_args.ip_version = 4u;
        StackTcpV.listen_args.backlog = (backlog == 0u) ? 1u : backlog;
        StackTcp.listen(protocore_stack_tcp_span());
        if (!StackTcpV.ok)
        {
            p->listen = EMBED_FALSE;
            return NULL;
        }
    }
    return p;
}

protocore_net_err protocore_net_connect(protocore_pcb *p, const protocore_net_ip *a, uint16_t port,
                                        protocore_net_connect_fn connected)
{
    if (p == NULL || !p->in_use || p->listen || p->connecting || p->entry < PROTOCORE_STACK_TCP_ENTRIES ||
        !protocore_net_ip_is_v4(a) || port == 0u)
    {
        return PROTOCORE_NET_ERR_VAL;
    }
    if (!s_net.started)
    {
        return PROTOCORE_NET_ERR_CONN;
    }
    const uint32_t now_ms = protocore_platform_millis();
    p->on_connect = connected;
    p->remote_ip = *a;
    p->remote_port = port;
    p->connecting = EMBED_TRUE;
    p->connect_deadline = now_ms + PROTOCORE_STACK_NET_ARP_WAIT_MS;
    p->arp_sent_ms = now_ms;
    return stack_net_connect_try(p, now_ms);
}

protocore_net_err protocore_net_close(protocore_pcb *p)
{
    if (p == NULL || !p->in_use)
    {
        return PROTOCORE_NET_ERR_ARG;
    }
    if (p->entry < PROTOCORE_STACK_TCP_ENTRIES)
    {
        const StackTcpCloseArgs args = {.index = p->entry};
        if (!protocore_stack_tcp_close(&args))
        {
            return PROTOCORE_NET_ERR_MEM; // the transmit ring has no segment for the mark yet; the transport tries
                                          // again
        }
    }
    stack_net_pcb_give(p);
    return PROTOCORE_NET_OK;
}

void protocore_net_abort(protocore_pcb *p)
{
    if (p == NULL || !p->in_use)
    {
        return;
    }
    if (p->entry < PROTOCORE_STACK_TCP_ENTRIES)
    {
        protocore_stack_tcp_abort(p->entry);
    }
    protocore_net_err_fn on_err = p->on_err;
    void *const arg = p->arg;
    stack_net_pcb_give(p);
    // tcp_abort of lwIP reports ERR_ABRT to the error callback, after the control block went back.
    if (on_err != NULL)
    {
        on_err(arg, PROTOCORE_NET_ERR_ABRT);
    }
}

void protocore_net_arg(protocore_pcb *p, void *arg)
{
    if (p != NULL)
    {
        p->arg = arg;
    }
}

void protocore_net_on_recv(protocore_pcb *p, protocore_net_recv_fn fn)
{
    if (p != NULL)
    {
        p->on_recv = fn;
    }
}

void protocore_net_on_sent(protocore_pcb *p, protocore_net_sent_fn fn)
{
    if (p != NULL)
    {
        p->on_sent = fn;
    }
}

void protocore_net_on_accept(protocore_pcb *p, protocore_net_accept_fn fn)
{
    if (p != NULL)
    {
        p->on_accept = fn;
    }
}

void protocore_net_on_err(protocore_pcb *p, protocore_net_err_fn fn)
{
    if (p != NULL)
    {
        p->on_err = fn;
    }
}

protocore_net_err protocore_net_write(protocore_pcb *p, const void *data, uint16_t len, uint8_t flags)
{
    (void)flags;
    if (p == NULL || p->entry >= PROTOCORE_STACK_TCP_ENTRIES || data == NULL)
    {
        return PROTOCORE_NET_ERR_CONN;
    }
    // tcp_write of lwIP takes the whole write or none of it.
    if (len > protocore_stack_tcp_room(p->entry))
    {
        return PROTOCORE_NET_ERR_MEM;
    }
    const StackTcpWriteArgs args = {.data = (const uint8_t *)data, .len = len, .index = p->entry};
    (void)protocore_stack_tcp_write(&args);
    // Explicit cast narrows the count, which PROTOCORE_STACK_TCP_SEGMENTS holds below 2^16
    p->snd_queuelen = (uint16_t)protocore_stack_tcp_queued(p->entry);
    return PROTOCORE_NET_OK;
}

// The TCP of the stack sends its transmit rings in each pass.
protocore_net_err protocore_net_output(protocore_pcb *p)
{
    (void)p;
    return PROTOCORE_NET_OK;
}

void protocore_net_recved(protocore_pcb *p, uint16_t len)
{
    if (p == NULL || p->entry >= PROTOCORE_STACK_TCP_ENTRIES)
    {
        return;
    }
    StackNetEntry *const entry = &s_net.entries[p->entry];
    const uint32_t take = (len < entry->delivered) ? len : entry->delivered;
    // The octets leave the receive ring here, and the TCP of the stack opens the window on its next pass.
    EMBED_CALL(anularis.consume, AnularisCfg, .ring = &entry->rx, .bytes = take);
    entry->delivered -= take;
}

uint16_t protocore_net_sndbuf(protocore_pcb *p)
{
    if (p == NULL || p->entry >= PROTOCORE_STACK_TCP_ENTRIES)
    {
        return 0u;
    }
    const size_t room = protocore_stack_tcp_room(p->entry);
    // Explicit cast narrows the room, which the test holds at 65535 or below
    return (uint16_t)((room > 0xFFFFu) ? 0xFFFFu : room);
}

// The TCP of the stack sends each write whole, and it opens the window from the receive ring. These two have
// nothing to set.
void protocore_net_nagle_disable(protocore_pcb *p)
{
    (void)p;
}

void protocore_net_rcv_wnd_update(protocore_pcb *p, uint16_t len)
{
    (void)p;
    (void)len;
}

// The control blocks of this stack take no socket option.
void protocore_net_opt_set(void *p, uint32_t opt)
{
    (void)p;
    (void)opt;
}

// --- UDP and IGMP --------------------------------------------------------------------------------------

/** @brief RFC 791 sec 3.1 Protocol number of IGMP, RFC 1112 appendix I. */
#define STACK_NET_PROTO_IGMP 2u
/** @brief RFC 2236 sec 2.1 message types. */
#define STACK_NET_IGMP_V1_REPORT 0x12u
#define STACK_NET_IGMP_V2_REPORT 0x16u
#define STACK_NET_IGMP_LEAVE 0x17u
/** @brief The octets of an IPv4 header that carries the RFC 2113 Router Alert option. */
#define STACK_NET_IP4_ALERT_HDR_LEN 24u
/** @brief The interface that each datagram of the raw API goes out on. */
#define STACK_NET_NETIF 0u

static_assert(PROTOCORE_STACK_NET_UDP_PCBS <= IDEMIP_UDP_PCBS,
              "PROTOCORE_STACK_NET_UDP_PCBS is above the UdpPcb entries of idemIP");

// The units of the stack, or NULL before the stack runs.
static const StackUnits *stack_net_units(void)
{
    const uint8_t *const stack = protocore_stack_span();
    return (stack == NULL) ? NULL : protocore_stack_units(stack);
}

// Reads interface STACK_NET_NETIF into the operand block of Netif. False where the interface has no entry.
static embed_bool stack_net_netif(const StackUnits *units)
{
    NetifIo *const netif_io = IDEMIP_NETIF_IO(units->netif);
    netif_io->if_args.index = STACK_NET_NETIF;
    Netif.get(units->netif);
    return netif_io->status == IDEMIP_OK;
}

// RFC 1112 sec 6.4: the hardware address of an IPv4 host group is 01-00-5E and the low 23 bits of the group.
static void stack_net_group_mac(uint32_t group, uint8_t *mac)
{
    mac[0] = 0x01u;
    mac[1] = 0x00u;
    mac[2] = 0x5Eu;
    // Explicit casts narrow each octet of the group, which the masks hold in eight bits
    mac[3] = (uint8_t)((group >> 16) & 0x7Fu);
    mac[4] = (uint8_t)((group >> 8) & 0xFFu);
    mac[5] = (uint8_t)(group & 0xFFu);
}

// Sends one RFC 826 request for @p target on the interface. The datagram that needs the answer goes no
// further, and the next datagram to the same next hop finds the row.
static void stack_net_arp_request(const StackUnits *units, uint32_t target)
{
    const NetifIo *const netif_io = IDEMIP_NETIF_IO(units->netif);
    StackV.transmit_args.index = STACK_NET_NETIF;
    Stack.transmit_hold(protocore_stack_span());
    if (!StackV.ok)
    {
        return;
    }
    uint8_t *const frame = StackV.transmit_frame;
    static const uint8_t broadcast[IDEMIP_MAC_LEN] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
    idemip_arp_build_request(frame + IDEMIP_ETH_HDR_LEN, netif_io->hwaddr, netif_io->addr, target);
    idemip_eth_build(frame, broadcast, netif_io->hwaddr, IDEMIP_ETHERTYPE_ARP);
    StackV.transmit_args.index = STACK_NET_NETIF;
    StackV.transmit_args.len = IDEMIP_ETH_HDR_LEN + idemip_eth_pad(frame, IDEMIP_ARP_LEN);
    Stack.transmit_post(protocore_stack_span());
}

// RFC 1122 sec 3.3.1.1: the hardware address of the next hop to @p dst, in @p mac. A host group and a
// broadcast map without ARP. An address off the link goes to the gateway. False where ARP holds no row, and
// a request then went out.
static embed_bool stack_net_next_hop(const StackUnits *units, uint32_t dst, uint8_t *mac)
{
    const NetifIo *const netif_io = IDEMIP_NETIF_IO(units->netif);
    if ((dst & 0xF0000000u) == 0xE0000000u)
    {
        stack_net_group_mac(dst, mac);
        return EMBED_TRUE;
    }
    if (dst == 0xFFFFFFFFu || (netif_io->mask != 0u && (dst | netif_io->mask) == 0xFFFFFFFFu))
    {
        for (uint8_t octet = 0u; octet < IDEMIP_MAC_LEN; octet++)
        {
            mac[octet] = 0xFFu;
        }
        return EMBED_TRUE;
    }
    const embed_bool on_link = (dst & netif_io->mask) == (netif_io->addr & netif_io->mask);
    const uint32_t hop = on_link ? dst : netif_io->gw;
    ArpTableIo *const arp_io = IDEMIP_ARP_IO(units->arp);
    arp_io->find_args.spa = hop;
    arp_io->find_args.pro = (uint16_t)IDEMIP_ARP_PRO_IPV4;
    ArpTable.find(units->arp);
    if (arp_io->status != IDEMIP_OK || arp_io->mac == NULL)
    {
        stack_net_arp_request(units, hop);
        return EMBED_FALSE;
    }
    EMBED_CALL(proxim.read, ProximusCfg, .dst = mac, .at = arp_io->mac, .size = IDEMIP_MAC_LEN);
    return EMBED_TRUE;
}

// The IGMP message that the last call of Igmp asked for: the Report to the group, or the Leave to
// ALL-ROUTERS. RFC 2236 sec 2 sends each one with TTL 1 and the RFC 2113 Router Alert option.
static void stack_net_igmp_send(const StackUnits *units)
{
    const IgmpIo *const igmp_io = IDEMIP_IGMP_IO(units->igmp);
    if (!(igmp_io->send_report || igmp_io->send_leave) || !stack_net_netif(units))
    {
        return;
    }
    const NetifIo *const netif_io = IDEMIP_NETIF_IO(units->netif);
    const uint32_t dst = igmp_io->send_leave ? IDEMIP_IGMP_ALL_ROUTERS : igmp_io->group;
    uint8_t type = igmp_io->report_v1 ? STACK_NET_IGMP_V1_REPORT : STACK_NET_IGMP_V2_REPORT;
    if (igmp_io->send_leave)
    {
        type = STACK_NET_IGMP_LEAVE;
    }
    StackV.transmit_args.index = STACK_NET_NETIF;
    Stack.transmit_hold(protocore_stack_span());
    if (!StackV.ok)
    {
        return;
    }
    uint8_t *const frame = StackV.transmit_frame;
    uint8_t *const ip = frame + IDEMIP_ETH_HDR_LEN;
    uint8_t *const message = ip + STACK_NET_IP4_ALERT_HDR_LEN;
    message[0] = type;
    message[1] = 0u;
    idemip_wr16(message + 2, 0u);
    idemip_wr32(message + 4, igmp_io->group);
    idemip_wr16(message + 2, idemip_cksum(message, IDEMIP_IGMP_MSG_LEN));
    // RFC 791 sec 3.1: version 4 and an IHL of six words, for the four octets of the option
    ip[0] = 0x46u;
    ip[1] = 0xC0u; // RFC 2474 sec 4.2.2.2: the Class Selector of network control
    idemip_wr16(ip + 2, (uint16_t)(STACK_NET_IP4_ALERT_HDR_LEN + IDEMIP_IGMP_MSG_LEN));
    idemip_wr16(ip + 4, 0u);
    idemip_wr16(ip + 6, (uint16_t)IDEMIP_IP4_FLAG_DF);
    ip[8] = 1u;
    ip[9] = STACK_NET_PROTO_IGMP;
    idemip_wr16(ip + 10, 0u);
    idemip_wr32(ip + 12, netif_io->addr);
    idemip_wr32(ip + 16, dst);
    // RFC 2113 sec 2.1: the Router Alert option, copied, class 0, number 20, length 4, value 0
    ip[20] = 0x94u;
    ip[21] = 0x04u;
    ip[22] = 0x00u;
    ip[23] = 0x00u;
    idemip_wr16(ip + 10, idemip_cksum(ip, STACK_NET_IP4_ALERT_HDR_LEN));
    uint8_t mac[IDEMIP_MAC_LEN];
    stack_net_group_mac(dst, mac);
    idemip_eth_build(frame, mac, netif_io->hwaddr, IDEMIP_ETHERTYPE_IPV4);
    StackV.transmit_args.index = STACK_NET_NETIF;
    StackV.transmit_args.len =
        IDEMIP_ETH_HDR_LEN + idemip_eth_pad(frame, STACK_NET_IP4_ALERT_HDR_LEN + IDEMIP_IGMP_MSG_LEN);
    Stack.transmit_post(protocore_stack_span());
}

// RFC 2236 sec 6 "timer expired": each call of Igmp.tick fires one due group, and its Report goes out.
static void stack_net_igmp_tick(const StackUnits *units, uint32_t now_ms)
{
    IgmpIo *const igmp_io = IDEMIP_IGMP_IO(units->igmp);
    for (uint8_t round = 0u; round < IDEMIP_IGMP_GROUPS; round++)
    {
        igmp_io->tick_args.now_ms = now_ms;
        Igmp.tick(units->igmp);
        if (igmp_io->status != IDEMIP_OK || igmp_io->expired == 0u)
        {
            return;
        }
        stack_net_igmp_send(units);
    }
}

// Opens and binds the UdpPcb entry of a control block. False where idemIP refuses the open or the bind.
static embed_bool stack_net_udp_open(const StackUnits *units, protocore_udp_pcb *pcb)
{
    UdpPcbIo *const udp_io = IDEMIP_UDP_PCB_IO(units->udp_pcb);
    udp_io->open_args.ip_version = 4u;
    udp_io->open_args.lite = EMBED_FALSE;
    UdpPcb.open(units->udp_pcb);
    if (udp_io->status != IDEMIP_OK)
    {
        return EMBED_FALSE;
    }
    const uint16_t entry = udp_io->index;
    static const uint8_t any[4] = {0u, 0u, 0u, 0u};
    udp_io->bind_args = (UdpPcbAddrArgs){0};
    udp_io->bind_args.ip = any;
    udp_io->bind_args.index = entry;
    udp_io->bind_args.port = pcb->local_port;
    UdpPcb.bind(units->udp_pcb);
    if (udp_io->status != IDEMIP_OK)
    {
        udp_io->pcb_args.index = entry;
        UdpPcb.close(units->udp_pcb);
        return EMBED_FALSE;
    }
    pcb->entry = entry;
    pcb->local_port = udp_io->port;
    pcb->opened = EMBED_TRUE;
    return EMBED_TRUE;
}

// The control blocks that bound before the stack ran open now.
static void stack_net_udp_start(void)
{
    const StackUnits *const units = stack_net_units();
    if (units == NULL)
    {
        return;
    }
    for (size_t index = 0u; index < PROTOCORE_STACK_NET_UDP_PCBS; index++)
    {
        protocore_udp_pcb *const pcb = &s_net.udp[index];
        if (pcb->in_use && pcb->local_port != 0u && !pcb->opened)
        {
            (void)stack_net_udp_open(units, pcb);
        }
    }
}

protocore_udp_pcb *protocore_net_udp_new(void)
{
    for (size_t index = 0u; index < PROTOCORE_STACK_NET_UDP_PCBS; index++)
    {
        protocore_udp_pcb *const pcb = &s_net.udp[index];
        if (!pcb->in_use)
        {
            *pcb = (protocore_udp_pcb){0};
            pcb->in_use = EMBED_TRUE;
            pcb->entry = IDEMIP_UDP_PCB_NONE;
            return pcb;
        }
    }
    return NULL;
}

protocore_net_err protocore_net_udp_bind(protocore_udp_pcb *p, const protocore_net_ip *a, uint16_t port)
{
    (void)a;
    if (p == NULL || !p->in_use || p->opened)
    {
        return PROTOCORE_NET_ERR_VAL;
    }
    p->local_port = port;
    const StackUnits *const units = stack_net_units();
    if (units != NULL && !stack_net_udp_open(units, p))
    {
        p->local_port = 0u;
        return PROTOCORE_NET_ERR_USE;
    }
    return PROTOCORE_NET_OK;
}

void protocore_net_udp_recv(protocore_udp_pcb *p, protocore_net_udp_recv_fn fn, void *arg)
{
    if (p != NULL)
    {
        p->on_recv = fn;
        p->arg = arg;
    }
}

protocore_net_err protocore_net_udp_sendto(protocore_udp_pcb *p, protocore_pbuf *b, const protocore_net_ip *a,
                                           uint16_t port)
{
    const StackUnits *const units = stack_net_units();
    if (p == NULL || b == NULL || b->payload == NULL || !protocore_net_ip_is_v4(a) || units == NULL)
    {
        return PROTOCORE_NET_ERR_VAL;
    }
    const size_t room = IDEMIP_DMA_FRAME_MAX - IDEMIP_ETH_HDR_LEN - IDEMIP_IPV4_HDR_LEN - IDEMIP_UDP_HDR_LEN;
    if (b->len > room || !stack_net_netif(units))
    {
        return PROTOCORE_NET_ERR_VAL;
    }
    const uint32_t dst = idemip_rd32(a->bytes);
    uint8_t mac[IDEMIP_MAC_LEN];
    if (!stack_net_next_hop(units, dst, mac))
    {
        return PROTOCORE_NET_ERR_MEM; // ARP holds no row yet, and the request went out
    }
    StackV.transmit_args.index = STACK_NET_NETIF;
    Stack.transmit_hold(protocore_stack_span());
    if (!StackV.ok)
    {
        return PROTOCORE_NET_ERR_MEM;
    }
    const NetifIo *const netif_io = IDEMIP_NETIF_IO(units->netif);
    uint8_t *const frame = StackV.transmit_frame;
    uint8_t *const udp = frame + IDEMIP_ETH_HDR_LEN + IDEMIP_IPV4_HDR_LEN;
    // Explicit cast narrows the length, which the room test above holds below IDEMIP_DMA_FRAME_MAX
    const uint16_t udp_len = (uint16_t)(IDEMIP_UDP_HDR_LEN + b->len);
    idemip_udp_build(udp, p->local_port, port, udp_len);
    EMBED_CALL(proxim.read, ProximusCfg, .dst = udp + IDEMIP_UDP_HDR_LEN, .at = (const uint8_t *)b->payload,
               .size = b->len);
    // RFC 768: the checksum covers the pseudo header, the header and the data. A sum of zero goes out as ones.
    uint8_t pseudo[12];
    idemip_wr32(pseudo, netif_io->addr);
    idemip_wr32(pseudo + 4, dst);
    pseudo[8] = 0u;
    pseudo[9] = IDEMIP_IP4_PROTO_UDP;
    idemip_wr16(pseudo + 10, udp_len);
    const uint32_t sum = idemip_cksum_accum(idemip_cksum_accum(0u, pseudo, sizeof(pseudo)), udp, udp_len);
    const uint16_t check = idemip_cksum_final(sum);
    idemip_wr16(udp + 6, (check == 0u) ? 0xFFFFu : check);
    IdemIpIp4Fields fields = {0};
    fields.tos = p->tos;
    // Explicit cast narrows the length, which the room test above holds below IDEMIP_DMA_FRAME_MAX
    fields.total_len = (uint16_t)(IDEMIP_IPV4_HDR_LEN + udp_len);
    fields.flags_frag = IDEMIP_IP4_FLAG_DF;
    // RFC 1112 sec 6.1: a datagram to a host group goes out with a TTL of 1, unless the sender asks for more
    fields.ttl = ((dst & 0xF0000000u) == 0xE0000000u) ? 1u : IDEMIP_IP_DEFAULT_TTL;
    fields.proto = IDEMIP_IP4_PROTO_UDP;
    fields.src = netif_io->addr;
    fields.dst = dst;
    idemip_ip4_build(frame + IDEMIP_ETH_HDR_LEN, &fields);
    idemip_eth_build(frame, mac, netif_io->hwaddr, IDEMIP_ETHERTYPE_IPV4);
    StackV.transmit_args.index = STACK_NET_NETIF;
    StackV.transmit_args.len = IDEMIP_ETH_HDR_LEN + idemip_eth_pad(frame, IDEMIP_IPV4_HDR_LEN + udp_len);
    Stack.transmit_post(protocore_stack_span());
    return StackV.ok ? PROTOCORE_NET_OK : PROTOCORE_NET_ERR_MEM;
}

void protocore_net_udp_remove(protocore_udp_pcb *p)
{
    if (p == NULL || !p->in_use)
    {
        return;
    }
    const StackUnits *const units = stack_net_units();
    if (p->opened && units != NULL)
    {
        UdpPcbIo *const udp_io = IDEMIP_UDP_PCB_IO(units->udp_pcb);
        udp_io->pcb_args.index = p->entry;
        UdpPcb.close(units->udp_pcb);
    }
    *p = (protocore_udp_pcb){0};
}

void protocore_stack_net_udp_input(const uint8_t *frame, uint16_t entry, size_t ip_off, size_t payload_off,
                                   size_t payload_len)
{
    if (payload_len < IDEMIP_UDP_HDR_LEN)
    {
        return;
    }
    for (size_t index = 0u; index < PROTOCORE_STACK_NET_UDP_PCBS; index++)
    {
        protocore_udp_pcb *const pcb = &s_net.udp[index];
        if (!pcb->in_use || !pcb->opened || pcb->entry != entry || pcb->on_recv == NULL)
        {
            continue;
        }
        const size_t data_len = payload_len - IDEMIP_UDP_HDR_LEN;
        // Explicit cast narrows the length, which the pool test below holds at PROTOCORE_STACK_NET_PBUF_BYTES
        protocore_pbuf *const pbuf =
            (data_len <= PROTOCORE_STACK_NET_PBUF_BYTES)
                ? protocore_net_pbuf_alloc(PROTOCORE_NET_PBUF_TRANSPORT, (uint16_t)data_len, PROTOCORE_NET_PBUF_RAM)
                : NULL;
        if (pbuf == NULL)
        {
            return;
        }
        EMBED_CALL(proxim.read, ProximusCfg, .dst = (uint8_t *)pbuf->payload,
                   .at = frame + payload_off + IDEMIP_UDP_HDR_LEN, .size = data_len);
        const uint8_t *const source = frame + ip_off + 12u;
        const protocore_net_ip from = {PROTOCORE_NET_TYPE_V4, {source[0], source[1], source[2], source[3]}};
        // The callback owns the buffer, and frees it, as lwIP has it.
        pcb->on_recv(pcb->arg, pcb, pbuf, &from, idemip_rd16(frame + payload_off));
        return;
    }
}

// RFC 2236 sec 6 "join group" and "leave group", and the message that each one sends.
static protocore_net_err stack_net_igmp(const protocore_net_ip *grp, embed_bool join)
{
    const StackUnits *const units = stack_net_units();
    if (!protocore_net_ip_is_v4(grp) || units == NULL)
    {
        return PROTOCORE_NET_ERR_VAL;
    }
    IgmpIo *const igmp_io = IDEMIP_IGMP_IO(units->igmp);
    igmp_io->group_args.group = idemip_rd32(grp->bytes);
    igmp_io->group_args.netif = STACK_NET_NETIF;
    igmp_io->group_args.now_ms = protocore_platform_millis();
    igmp_io->group_args.rand = 0u;
    if (join)
    {
        Igmp.join(units->igmp);
    }
    else
    {
        Igmp.leave(units->igmp);
    }
    if (igmp_io->status != IDEMIP_OK)
    {
        return PROTOCORE_NET_ERR_VAL;
    }
    stack_net_igmp_send(units);
    return PROTOCORE_NET_OK;
}

protocore_net_err protocore_net_igmp_join(const protocore_net_ip *nif, const protocore_net_ip *grp)
{
    (void)nif;
    return stack_net_igmp(grp, EMBED_TRUE);
}

protocore_net_err protocore_net_igmp_leave(const protocore_net_ip *nif, const protocore_net_ip *grp)
{
    (void)nif;
    return stack_net_igmp(grp, EMBED_FALSE);
}

// --- the pass ----------------------------------------------------------------------------------------

// The listener of a local port, or NULL.
static protocore_pcb *stack_net_listener(uint16_t port)
{
    for (size_t index = 0u; index < STACK_NET_PCBS; index++)
    {
        protocore_pcb *const pcb = &s_net.pcbs[index];
        if (pcb->in_use && pcb->listen && pcb->local_port == port)
        {
            return pcb;
        }
    }
    return NULL;
}

// The active open of a control block, once ARP holds the next hop: the SYN goes out through the TCP of the stack.
// While ARP holds no row, the control block keeps waiting, and the request went out.
static protocore_net_err stack_net_connect_try(protocore_pcb *pcb, uint32_t now_ms)
{
    const StackUnits *const units = stack_net_units();
    if (units == NULL || !stack_net_netif(units))
    {
        pcb->connecting = EMBED_FALSE;
        return PROTOCORE_NET_ERR_CONN;
    }
    uint8_t mac[IDEMIP_MAC_LEN];
    if (!stack_net_next_hop(units, idemip_rd32(pcb->remote_ip.bytes), mac))
    {
        pcb->arp_sent_ms = now_ms;
        return PROTOCORE_NET_OK;
    }
    pcb->connecting = EMBED_FALSE;
    uint8_t local_ip[4];
    idemip_wr32(local_ip, IDEMIP_NETIF_IO(units->netif)->addr);
    const StackTcpConnectArgs args = {
        .local_ip = local_ip,
        .remote_ip = pcb->remote_ip.bytes,
        .next_hop = mac,
        .now_ms = now_ms,
        .random_word = protocore_platform_rand_u32(),
        .remote_port = pcb->remote_port,
        .netif = STACK_NET_NETIF,
    };
    const uint8_t index = protocore_stack_tcp_connect(&args);
    if (index >= PROTOCORE_STACK_TCP_ENTRIES)
    {
        return PROTOCORE_NET_ERR_MEM;
    }
    StackNetEntry *const entry = &s_net.entries[index];
    entry->pcb = pcb;
    entry->delivered = 0u;
    entry->released = protocore_stack_tcp_released(index);
    entry->fin_owed = EMBED_FALSE;
    pcb->entry = index;
    StackTcpEnds ends;
    if (protocore_stack_tcp_ends(index, &ends))
    {
        pcb->local_port = ends.local_port;
        pcb->local_ip = (protocore_net_ip){PROTOCORE_NET_TYPE_V4,
                                           {ends.local_ip[0], ends.local_ip[1], ends.local_ip[2], ends.local_ip[3]}};
    }
    return PROTOCORE_NET_OK;
}

// Each active open that waits for ARP: a request again each PROTOCORE_STACK_NET_ARP_RETRY_MS, and the end at its
// deadline. The control block then goes back, and its error callback gets ERR_ABRT, as lwIP has it.
static void stack_net_connect_wait(uint32_t now_ms)
{
    for (size_t index = 0u; index < STACK_NET_PCBS; index++)
    {
        protocore_pcb *const pcb = &s_net.pcbs[index];
        if (!pcb->in_use || !pcb->connecting)
        {
            continue;
        }
        if ((int32_t)(now_ms - pcb->connect_deadline) >= 0)
        {
            protocore_net_err_fn on_err = pcb->on_err;
            void *const arg = pcb->arg;
            stack_net_pcb_give(pcb);
            if (on_err != NULL)
            {
                on_err(arg, PROTOCORE_NET_ERR_ABRT);
            }
        }
        else if (now_ms - pcb->arp_sent_ms >= PROTOCORE_STACK_NET_ARP_RETRY_MS)
        {
            (void)stack_net_connect_try(pcb, now_ms);
        }
    }
}

// STACK_TCP_CONNECT of an active open: the connect callback of its control block. A callback that refuses ends
// the connection.
static void stack_net_connected(protocore_pcb *pcb)
{
    pcb->state = 4u;
    const protocore_net_err result =
        (pcb->on_connect != NULL) ? pcb->on_connect(pcb->arg, pcb, PROTOCORE_NET_OK) : PROTOCORE_NET_OK;
    if (result != PROTOCORE_NET_OK && result != PROTOCORE_NET_ERR_ABRT && pcb->in_use)
    {
        protocore_net_abort(pcb);
    }
}

// STACK_TCP_CONNECT: a control block for the connection, and the accept of the listener of its port. No
// listener, no free control block, or an accept that refuses ends the connection.
static void stack_net_accept(uint8_t index)
{
    if (s_net.entries[index].pcb != NULL)
    {
        stack_net_connected(s_net.entries[index].pcb);
        return;
    }
    StackTcpEnds ends;
    if (!protocore_stack_tcp_ends(index, &ends))
    {
        return;
    }
    protocore_pcb *const listener = stack_net_listener(ends.local_port);
    protocore_pcb *const pcb = (listener != NULL) ? stack_net_pcb_take() : NULL;
    if (pcb == NULL)
    {
        protocore_stack_tcp_abort(index);
        return;
    }
    StackNetEntry *const entry = &s_net.entries[index];
    entry->pcb = pcb;
    entry->delivered = 0u;
    entry->released = protocore_stack_tcp_released(index);
    entry->fin_owed = EMBED_FALSE;
    pcb->entry = index;
    pcb->state = 4u;
    pcb->local_port = ends.local_port;
    pcb->remote_port = ends.remote_port;
    pcb->local_ip = (protocore_net_ip){PROTOCORE_NET_TYPE_V4,
                                       {ends.local_ip[0], ends.local_ip[1], ends.local_ip[2], ends.local_ip[3]}};
    pcb->remote_ip = (protocore_net_ip){PROTOCORE_NET_TYPE_V4,
                                        {ends.remote_ip[0], ends.remote_ip[1], ends.remote_ip[2], ends.remote_ip[3]}};
    pcb->tos = listener->tos;
    pcb->ttl = listener->ttl;
    const protocore_net_err result = (listener->on_accept != NULL)
                                         ? listener->on_accept(listener->arg, pcb, PROTOCORE_NET_OK)
                                         : PROTOCORE_NET_ERR_VAL;
    // ERR_ABRT means the accept aborted the control block itself.
    if (result != PROTOCORE_NET_OK && result != PROTOCORE_NET_ERR_ABRT && pcb->in_use)
    {
        protocore_net_abort(pcb);
    }
}

// The text of an entry that the transport did not get yet, in packet buffers of the pool. Then the FIN, once
// each octet in front of it went to the transport.
static void stack_net_deliver(uint8_t index)
{
    StackNetEntry *const entry = &s_net.entries[index];
    while (entry->pcb != NULL)
    {
        const size_t waiting = EMBED_CALL(anularis.available, AnularisCfg, .ring = &entry->rx) - entry->delivered;
        if (waiting == 0u)
        {
            break;
        }
        const uint16_t take =
            (waiting < PROTOCORE_STACK_NET_PBUF_BYTES) ? (uint16_t)waiting : (uint16_t)PROTOCORE_STACK_NET_PBUF_BYTES;
        protocore_pbuf *const pbuf =
            protocore_net_pbuf_alloc(PROTOCORE_NET_PBUF_TRANSPORT, take, PROTOCORE_NET_PBUF_RAM);
        if (pbuf == NULL)
        {
            return; // the pool is spent; the next pass tries again
        }
        EMBED_CALL(anularis.peek, AnularisCfg, .ring = &entry->rx, .dst = (uint8_t *)pbuf->payload, .bytes = take,
                   .offset = entry->delivered);
        entry->delivered += take;
        protocore_pcb *const pcb = entry->pcb;
        if (pcb->on_recv == NULL || pcb->on_recv(pcb->arg, pcb, pbuf, PROTOCORE_NET_OK) != PROTOCORE_NET_OK)
        {
            (void)protocore_net_pbuf_free(pbuf);
            return;
        }
    }
    if (entry->pcb != NULL && entry->fin_owed &&
        entry->delivered == EMBED_CALL(anularis.available, AnularisCfg, .ring = &entry->rx))
    {
        entry->fin_owed = EMBED_FALSE;
        protocore_pcb *const pcb = entry->pcb;
        if (pcb->on_recv != NULL)
        {
            (void)pcb->on_recv(pcb->arg, pcb, NULL, PROTOCORE_NET_OK);
        }
    }
}

// STACK_TCP_ERROR: the connection ended. The control block goes back, and then its error callback runs, as
// lwIP runs it. The close mark frees the dead entry.
static void stack_net_error(uint8_t index)
{
    StackNetEntry *const entry = &s_net.entries[index];
    protocore_pcb *const pcb = entry->pcb;
    const StackTcpCloseArgs args = {.index = index};
    (void)protocore_stack_tcp_close(&args);
    EMBED_CALL(anularis.consume, AnularisCfg, .ring = &entry->rx,
               .bytes = EMBED_CALL(anularis.available, AnularisCfg, .ring = &entry->rx));
    entry->delivered = 0u;
    entry->fin_owed = EMBED_FALSE;
    if (pcb == NULL)
    {
        return;
    }
    protocore_net_err_fn on_err = pcb->on_err;
    void *const arg = pcb->arg;
    stack_net_pcb_give(pcb);
    if (on_err != NULL)
    {
        on_err(arg, PROTOCORE_NET_ERR_RST);
    }
}

// The octets that the peer acknowledged since the last pass, as the sent callback.
static void stack_net_sent(uint8_t index)
{
    StackNetEntry *const entry = &s_net.entries[index];
    protocore_pcb *const pcb = entry->pcb;
    if (pcb == NULL)
    {
        return;
    }
    const uint32_t released = protocore_stack_tcp_released(index);
    const uint32_t acknowledged = released - entry->released;
    entry->released = released;
    // Explicit cast narrows the count, which PROTOCORE_STACK_TCP_SEGMENTS holds below 2^16
    pcb->snd_queuelen = (uint16_t)protocore_stack_tcp_queued(index);
    if (acknowledged != 0u && pcb->on_sent != NULL)
    {
        // Explicit cast narrows the count, which the test holds at 65535 or below
        (void)pcb->on_sent(pcb->arg, pcb, (uint16_t)((acknowledged > 0xFFFFu) ? 0xFFFFu : acknowledged));
    }
}

void protocore_stack_net_pass(uint32_t now_ms, uint32_t random_word)
{
    stack_net_lock();
    stack_net_start();
    uint8_t *const stack = protocore_stack_span();
    if (stack != NULL)
    {
        StackV.tick_args.now_ms = now_ms;
        StackV.tick_args.random_word = random_word;
        Stack.tick(stack);
        stack_net_udp_start();
        stack_net_igmp_tick(protocore_stack_units(stack), now_ms);
    }
    if (s_net.started)
    {
        stack_net_connect_wait(now_ms);
        uint8_t *const tcp = protocore_stack_tcp_span();
        StackTcp.event_take(tcp);
        while (StackTcpV.ok)
        {
            const StackTcpEvent event = StackTcpV.event;
            if (event.slot_id < PROTOCORE_STACK_TCP_ENTRIES)
            {
                if (event.type == STACK_TCP_CONNECT)
                {
                    stack_net_accept(event.slot_id);
                }
                else if (event.type == STACK_TCP_DISCONNECT)
                {
                    s_net.entries[event.slot_id].fin_owed = EMBED_TRUE;
                }
                else if (event.type == STACK_TCP_ERROR)
                {
                    stack_net_error(event.slot_id);
                }
            }
            StackTcp.event_take(tcp);
        }
        for (uint8_t index = 0u; index < PROTOCORE_STACK_TCP_ENTRIES; index++)
        {
            stack_net_sent(index);
            stack_net_deliver(index);
        }
    }
    stack_net_unlock();
}

// The task of the stack: one pass, then the rest of the tick goes to the other tasks.
static void stack_net_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        protocore_stack_net_pass(protocore_platform_millis(), protocore_platform_rand_u32());
        protocore_platform_task_delay(1);
    }
}

int protocore_stack_net_start(void)
{
    static protocore_platform_task task;
    if (task != NULL)
    {
        return 1;
    }
    stack_net_lock();
    stack_net_unlock();
    return protocore_platform_task_start(stack_net_task, "protocore_stack", PROTOCORE_STACK_NET_TASK_STACK, NULL,
                                         PROTOCORE_STACK_NET_TASK_PRIORITY, &task,
                                         PROTOCORE_STACK_NET_TASK_CORE) == PROTOCORE_PLATFORM_PASS
               ? 1
               : 0;
}

EMBED_END_DECLS

#endif // PROTOCORE_ENABLE_IDEMIP
