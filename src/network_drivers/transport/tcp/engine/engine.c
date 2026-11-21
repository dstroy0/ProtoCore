// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file engine.c
 * @brief Layer 4 (Transport) - the TCP engine over idemIP. See engine.h.
 *
 * The context holds one row for each TCB of TcpPcb, and one record for each pool entry. A row keeps the
 * link source of the SYN and the pool entry of the connection. The link source is the next hop back to the
 * peer. A pool entry keeps its two rings and the counts of the text octets that went out and came back
 * acknowledged.
 *
 * Each transmit segment starts with a count of its text octets. The engine names the text where the ring
 * holds it, and TcpOut.build copies it into the frame. A segment stays on the ring until the peer
 * acknowledges each octet of it. RFC 9293 sec 3.10.7.4 fifth moves SND.UNA, and the engine frees each
 * segment that SND.UNA passes.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_IDEMIP

#include "network_drivers/transport/tcp/engine/engine.h"

#include "src/idemip.h" // every unit of idemIP, its borrow and its operand block

#include "server/core/worker/worker.h" // protocore_plain_persist, and mmgr_zero_buf

PROTOCORE_BEGIN_DECLS

// --- the event ring ----------------------------------------------------------

/** @brief The octets of the event ring. The ring keeps one octet back, and so it holds one record fewer. */
#define TCP_ENGINE_EVENT_BYTES (PROTOCORE_TCP_ENGINE_EVENTS * sizeof(TcpEvt))

static_assert((sizeof(TcpEvt) & (sizeof(TcpEvt) - 1u)) == 0u,
              "sizeof(TcpEvt) must be a power of two: the event ring holds whole records in a power of two");
static_assert((PROTOCORE_TCP_ENGINE_EVENTS & (PROTOCORE_TCP_ENGINE_EVENTS - 1u)) == 0u,
              "PROTOCORE_TCP_ENGINE_EVENTS must be a power of two: the byte ring of MMgr wraps with a mask");

ParsMemoriaeInternae(tcp_engine_events, TCP_ENGINE_EVENT_BYTES);
MemoriaAnularis(s_events, tcp_engine_events);

// --- the context ---------------------------------------------------------------

/** @brief The life of a pool entry. */
typedef enum PROTO_ENUM_PACKED
{
    TCP_ENGINE_SLOT_UNBOUND = 0, ///< ::TcpEngineNs::slot_bind did not run on the entry
    TCP_ENGINE_SLOT_FREE,        ///< bound, and no connection holds it
    TCP_ENGINE_SLOT_OPENING,     ///< a SYN took it, and the handshake goes on
    TCP_ENGINE_SLOT_OPEN,        ///< EVT_CONNECT went to the owner
    TCP_ENGINE_SLOT_DEAD,        ///< the connection ended, and the close mark of the owner frees the entry
} TcpEngineSlotState;

/** @brief One row for each TCB. */
typedef struct
{
    uint8_t next_hop[IDEMIP_MAC_LEN]; ///< the link source of the SYN: the next hop back to the peer
    uint8_t netif;                    ///< the interface of the connection
    uint8_t slot;                     ///< the pool entry of the connection, or PROTOCORE_TCP_ENGINE_SLOT_NONE
    proto_bool open;                  ///< the TCB is open, and the engine took it
    proto_bool timing;         ///< an RFC 6298 sample runs, and the ACK of TcpPcbCtl::rtseq ends it
    uint32_t timed_ms;         ///< the millisecond that the timed segment went out
    uint32_t peer_window;      ///< SEG.WND of the last segment of the peer, for the RFC 5681 sec 2 test
    uint32_t persist_interval; ///< the interval to the next zero-window probe, 0 while no probe runs
    uint32_t linger_deadline;  ///< the millisecond that FIN-WAIT-2 ends at, 0 while no deadline runs
} TcpEngineRow;

/** @brief One record for each pool entry. */
typedef struct
{
    mmgr_ring *receive;          ///< the receive ring, which the owner reads
    mmgr_ring *transmit;         ///< the transmit ring, which the owner writes
    uint32_t text_bytes;         ///< the text octets that one transmit segment holds
    uint32_t released;           ///< the text octets that the peer acknowledged and the ring freed
    uint32_t sent;               ///< the text octets that went out
    uint32_t front_acknowledged; ///< the text octets of the front segment that the peer acknowledged
    uint16_t tcb;                ///< the TCB of the connection, or IDEMIP_TCP_PCB_NONE
    TcpEngineSlotState state;
    proto_bool closed;   ///< the close mark reached the engine
    proto_bool fin_sent; ///< the FIN went out
} TcpEngineSlot;

/** @brief The context of the engine. */
struct TcpEngineStorage
{
    TcpEngineRow rows[IDEMIP_TCP_PCBS];
    TcpEngineSlot slots[PROTOCORE_TCP_ENGINE_SLOTS];
    uint8_t *stack; ///< the borrow of the stack, which holds the transmit buffer of each interface
    const StackUnits *units;
    uint32_t window;
    proto_bool started;
};

static_assert(sizeof(struct TcpEngineStorage) <= PROTOCORE_TCP_ENGINE_BORROW,
              "PROTOCORE_TCP_ENGINE_BORROW is short of the engine context - raise it in buffer_sizing.h");
static_assert(IDEMIP_TCP_PCBS < IDEMIP_TCP_PCB_NONE, "a TCB index fits the 16 bits of TcpEngineSlot::tcb");

#define TCP_ENGINE_CONTEXT(work_) ((struct TcpEngineStorage *)(void *)(work_))

/** @brief Where a segment goes: the two ends of the connection and the next hop. */
typedef struct
{
    const uint8_t *local_ip;  ///< IDEMIP_TCP_PCB_ADDR_BYTES octets
    const uint8_t *remote_ip; ///< IDEMIP_TCP_PCB_ADDR_BYTES octets
    const uint8_t *next_hop;  ///< the hardware address that the frame goes to
    uint16_t local_port;
    uint16_t remote_port;
    uint8_t ip_version;
    uint8_t netif;
} TcpEngineEndpoint;

/** @brief One segment to send. */
typedef struct
{
    const uint8_t *text; ///< the text octets, NULL for a segment of control bits alone
    uint32_t seq;
    uint32_t ack;
    uint32_t window;
    uint16_t len;     ///< the text octets
    uint16_t options; ///< the IDEMIP_TCP_OUT_OPT_* set
    uint8_t flags;
} TcpEngineOutbound;

/** @brief The next text that a pool entry sends, from the front of its transmit ring. */
typedef struct
{
    const uint8_t *text; ///< the first text octet that did not go out
    uint32_t len;        ///< the octets after it in the same segment
    uint32_t queued;     ///< the octets after it on the whole ring, D of RFC 9293 sec 3.8.6.2.1
    proto_bool mark;     ///< the close mark is next, and each octet in front of it went out
} TcpEngineCursor;

// --- the octets ------------------------------------------------------------------

// The text count at the head of a transmit segment. slot_bind holds each segment on a word boundary.
static uint32_t engine_head_read(const uint8_t *segment)
{
    // Explicit cast reads the head as the 32-bit count that the write stored, on the word boundary that slot_bind holds
    return *(const uint32_t *)(const void *)segment;
}

static void engine_head_write(uint8_t *segment, uint32_t count)
{
    // Explicit cast writes the head as a 32-bit count, on the word boundary that slot_bind holds
    *(uint32_t *)(void *)segment = count;
}

// The receive MSS of the interface: one Ethernet payload less the IP header and the TCP header.
static uint16_t engine_receive_mss(uint8_t ip_version)
{
    const uint32_t ip_len = (ip_version == 6u) ? IDEMIP_IPV6_HDR_LEN : IDEMIP_IPV4_HDR_LEN;
    // Explicit cast narrows the count, which IDEMIP_ETH_MAX_PAYLOAD holds below 65536
    return (uint16_t)(IDEMIP_ETH_MAX_PAYLOAD - ip_len - IDEMIP_TCP_HDR_LEN);
}

// The RFC 9293 sec 3.7.1 Eff.snd.MSS of a loaded TCB. TcpIn takes the send MSS from the SYN. The frame
// of the interface sets the ceiling.
static uint32_t engine_send_mss(const TcpPcbIo *tcb_io)
{
    const uint32_t ceiling = engine_receive_mss(tcb_io->info.ip_version);
    const uint32_t mss = tcb_io->ctl.mss;
    return (mss != 0u && mss < ceiling) ? mss : ceiling;
}

// --- the timers ------------------------------------------------------------------

// A deadline interval milliseconds out. Zero names the deadline that does not run. A deadline that lands on
// zero thus takes the next millisecond.
static uint32_t engine_deadline(uint32_t now_ms, uint32_t interval)
{
    const uint32_t at = now_ms + interval;
    return (at == 0u) ? 1u : at;
}

// True where a deadline runs and the clock reached it. RFC 9293 sec 3.4 arithmetic modulo 2^32 makes the
// distance signed.
static proto_bool engine_passed(uint32_t now_ms, uint32_t deadline)
{
    // Explicit cast reads the distance as signed, which arithmetic modulo 2^32 allows
    return deadline != 0u && (int32_t)(now_ms - deadline) >= 0;
}

// Copies the loaded TCB into the operand block of TcpOut for a timer or congestion entry. FlightSize is
// SND.NXT less SND.UNA.
static void engine_out_load(const struct TcpEngineStorage *context, uint32_t now_ms)
{
    const TcpPcbIo *const tcb_io = IDEMIP_TCP_PCB_IO(context->units->tcp_pcb);
    TcpOutIo *const out_io = IDEMIP_TCP_OUT_IO(context->units->tcp_out);
    out_io->vars = tcb_io->vars;
    out_io->ctl = tcb_io->ctl;
    out_io->state = tcb_io->state;
    out_io->timer_args = (TcpOutTimerArgs){0};
    out_io->timer_args.now_ms = now_ms;
    out_io->timer_args.flight = tcb_io->vars.snd_nxt - tcb_io->vars.snd_una;
    out_io->timer_args.smss = engine_send_mss(tcb_io);
}

// RFC 6298 (5.1): a segment that holds data, a SYN or a FIN starts the retransmission timer, where the timer
// does not run. The caller stores the TCB.
static void engine_timer_arm(const struct TcpEngineStorage *context, uint32_t now_ms)
{
    engine_out_load(context, now_ms);
    TcpOut.rtx_arm(context->units->tcp_out);
    IDEMIP_TCP_PCB_IO(context->units->tcp_pcb)->ctl = IDEMIP_TCP_OUT_IO(context->units->tcp_out)->ctl;
}

// --- the events ------------------------------------------------------------------

static void engine_event_post(EvtType type, uint8_t slot, size_t data_len)
{
    const TcpEvt record = {.type = type, .slot_id = slot, .data_len = data_len};
    // Explicit cast reads the record as its octets, which the ring copies whole
    if (!EMBED_CALL(anularis.put, AnularisCfg, .ring = &s_events, .src = (const uint8_t *)(const void *)&record,
                    .bytes = sizeof record))
    {
        TcpEngineV.lost_events++;
    }
}

// --- the frames ------------------------------------------------------------------

// Builds one segment and its IP header and its Ethernet header in the transmit buffer of the interface. The
// stack holds that buffer. Then the frame goes on the transmit ring. False where the ring has no buffer or
// takes no frame. The stack then keeps the buffer for the next frame of the interface.
static proto_bool engine_emit(const struct TcpEngineStorage *context, const TcpEngineEndpoint *endpoint,
                              const TcpEngineOutbound *outbound)
{
    const uint8_t index = endpoint->netif;
    StackV.transmit_args.index = index;
    Stack.transmit_hold(context->stack);
    if (!StackV.ok)
    {
        return PROTO_FALSE;
    }
    NetifIo *const netif_io = IDEMIP_NETIF_IO(context->units->netif);
    netif_io->if_args.index = index;
    Netif.get(context->units->netif);
    if (netif_io->status != IDEMIP_OK)
    {
        return PROTO_FALSE;
    }
    uint8_t *const frame = StackV.transmit_frame;
    const proto_bool version6 = endpoint->ip_version == 6u;
    const size_t ip_len = version6 ? IDEMIP_IPV6_HDR_LEN : IDEMIP_IPV4_HDR_LEN;
    TcpOutIo *const out_io = IDEMIP_TCP_OUT_IO(context->units->tcp_out);
    out_io->build_args = (TcpOutBuildArgs){0};
    out_io->build_args.buf = frame + IDEMIP_ETH_HDR_LEN + ip_len;
    out_io->build_args.data = outbound->text;
    out_io->build_args.local_ip = endpoint->local_ip;
    out_io->build_args.remote_ip = endpoint->remote_ip;
    out_io->build_args.seq = outbound->seq;
    out_io->build_args.ack = outbound->ack;
    out_io->build_args.wnd = outbound->window;
    // Explicit cast narrows the room, which IDEMIP_DMA_FRAME_MAX holds below 65536
    out_io->build_args.cap = (uint16_t)(IDEMIP_DMA_FRAME_MAX - IDEMIP_ETH_HDR_LEN - ip_len);
    out_io->build_args.len = outbound->len;
    out_io->build_args.mss = engine_receive_mss(endpoint->ip_version);
    out_io->build_args.local_port = endpoint->local_port;
    out_io->build_args.remote_port = endpoint->remote_port;
    out_io->build_args.opts = outbound->options;
    out_io->build_args.flags = outbound->flags;
    out_io->build_args.ip_version = endpoint->ip_version;
    TcpOut.build(context->units->tcp_out);
    if (out_io->status != IDEMIP_OK)
    {
        return PROTO_FALSE;
    }
    const size_t segment_len = out_io->res.built;
    if (version6)
    {
        IdemIpIp6BuildArgs fields = {0};
        fields.src = endpoint->local_ip;
        fields.dst = endpoint->remote_ip;
        // Explicit cast narrows the length, which the frame holds below IDEMIP_DMA_FRAME_MAX
        fields.payload_len = (uint16_t)segment_len;
        fields.next_hdr = IDEMIP_IP6_NH_TCP;
        fields.hop_limit = IDEMIP_IP_DEFAULT_TTL;
        idemip_ip6_build(frame + IDEMIP_ETH_HDR_LEN, &fields);
    }
    else
    {
        // RFC 6864 sec 4.1: the source of an atomic datagram MAY set the Identification to any value.
        IdemIpIp4Fields fields = {0};
        // Explicit cast narrows the length, which the frame holds below IDEMIP_DMA_FRAME_MAX
        fields.total_len = (uint16_t)(ip_len + segment_len);
        fields.flags_frag = IDEMIP_IP4_FLAG_DF;
        fields.ttl = IDEMIP_IP_DEFAULT_TTL;
        fields.proto = IDEMIP_IP4_PROTO_TCP;
        fields.src = idemip_rd32(endpoint->local_ip);
        fields.dst = idemip_rd32(endpoint->remote_ip);
        idemip_ip4_build(frame + IDEMIP_ETH_HDR_LEN, &fields);
    }
    idemip_eth_build(frame, endpoint->next_hop, netif_io->hwaddr,
                     version6 ? IDEMIP_ETHERTYPE_IPV6 : IDEMIP_ETHERTYPE_IPV4);
    StackV.transmit_args.index = index;
    StackV.transmit_args.len = IDEMIP_ETH_HDR_LEN + idemip_eth_pad(frame, ip_len + segment_len);
    Stack.transmit_post(context->stack);
    if (!StackV.ok)
    {
        return PROTO_FALSE;
    }
    TcpEngineV.sent++;
    return PROTO_TRUE;
}

// Loads a TCB, and the two ends and the next hop of its connection. False where TcpPcb has no such TCB.
static proto_bool engine_endpoint_of_tcb(const struct TcpEngineStorage *context, uint16_t tcb,
                                         TcpEngineEndpoint *endpoint)
{
    TcpPcbIo *const tcb_io = IDEMIP_TCP_PCB_IO(context->units->tcp_pcb);
    tcb_io->pcb_args.index = tcb;
    TcpPcb.load(context->units->tcp_pcb);
    if (tcb_io->status != IDEMIP_OK)
    {
        return PROTO_FALSE;
    }
    const TcpEngineRow *const row = &context->rows[tcb];
    endpoint->local_ip = tcb_io->info.local_ip;
    endpoint->remote_ip = tcb_io->info.remote_ip;
    endpoint->next_hop = row->next_hop;
    endpoint->local_port = tcb_io->info.local_port;
    endpoint->remote_port = tcb_io->info.remote_port;
    endpoint->ip_version = tcb_io->info.ip_version;
    endpoint->netif = row->netif;
    return PROTO_TRUE;
}

// Writes the TCB of the last load back. False where TcpPcb refuses the state.
static proto_bool engine_tcb_store(const struct TcpEngineStorage *context, uint16_t tcb)
{
    TcpPcbIo *const tcb_io = IDEMIP_TCP_PCB_IO(context->units->tcp_pcb);
    tcb_io->pcb_args.index = tcb;
    TcpPcb.store(context->units->tcp_pcb);
    return tcb_io->status == IDEMIP_OK;
}

// RFC 9293 sec 3.10.7.1 and sec 3.10.7.2: the reset that answers a segment with no TCB. The two ends and the
// next hop come from the frame itself.
static void engine_answer_frame(struct TcpEngineStorage *context, const TcpEngineSegmentArgs *args)
{
    const DispatchIo *const dispatch_io = IDEMIP_DISPATCH_IO(context->units->dispatch);
    if ((dispatch_io->tcp_act & IDEMIP_TCP_IN_ACT_RST) == 0u)
    {
        return;
    }
    uint8_t local_ip[IDEMIP_TCP_PCB_ADDR_BYTES] = {0};
    uint8_t remote_ip[IDEMIP_TCP_PCB_ADDR_BYTES] = {0};
    const uint8_t *const ip = args->frame + dispatch_io->ip_off;
    const proto_bool version6 = dispatch_io->ip_version == 6u;
    const size_t address_len = version6 ? IDEMIP_TCP_PCB_ADDR_BYTES : sizeof(uint32_t);
    EMBED_CALL(proxim.read, ProximusCfg, .dst = local_ip,
               .at = ip + (version6 ? IDEMIP_IP6_OFF_DST : IDEMIP_IP4_OFF_DST), .size = address_len);
    EMBED_CALL(proxim.read, ProximusCfg, .dst = remote_ip,
               .at = ip + (version6 ? IDEMIP_IP6_OFF_SRC : IDEMIP_IP4_OFF_SRC), .size = address_len);
    const uint8_t *const segment = args->frame + dispatch_io->payload_off;
    const TcpEngineEndpoint endpoint = {
        .local_ip = local_ip,
        .remote_ip = remote_ip,
        .next_hop = idemip_eth_src(args->frame),
        .local_port = idemip_tcp_dst_port(segment),
        .remote_port = idemip_tcp_src_port(segment),
        .ip_version = dispatch_io->ip_version,
        .netif = args->netif,
    };
    const TcpEngineOutbound outbound = {
        .seq = dispatch_io->reply.seq,
        .ack = dispatch_io->reply.ack,
        // Explicit cast narrows the control bits, which RFC 9293 sec 3.1 holds in the low eight bits
        .flags = (uint8_t)dispatch_io->reply.flags,
    };
    (void)engine_emit(context, &endpoint, &outbound);
}

// --- the pool entries ------------------------------------------------------------

// A free pool entry with an empty receive ring, for a passive OPEN. PROTOCORE_TCP_ENGINE_SLOT_NONE where none is.
static uint8_t engine_slot_take(const struct TcpEngineStorage *context)
{
    for (uint8_t index = 0u; index < PROTOCORE_TCP_ENGINE_SLOTS; index++)
    {
        const TcpEngineSlot *const slot = &context->slots[index];
        if (slot->state == TCP_ENGINE_SLOT_FREE &&
            EMBED_CALL(anularis.vacant, AnularisCfg, .ring = slot->receive) >= context->window)
        {
            return index;
        }
    }
    return PROTOCORE_TCP_ENGINE_SLOT_NONE;
}

// Gives a pool entry back. Each transmit segment leaves the ring, the close mark too, and the row of the TCB
// lets go of the entry.
static void engine_slot_free(struct TcpEngineStorage *context, uint8_t index)
{
    TcpEngineSlot *const slot = &context->slots[index];
    while (EMBED_CALL(anularis.seg_inflight, AnularisCfg, .ring = slot->transmit) != 0u)
    {
        EMBED_CALL(anularis.seg_release, AnularisCfg, .ring = slot->transmit);
    }
    if (slot->tcb < IDEMIP_TCP_PCBS && context->rows[slot->tcb].slot == index)
    {
        context->rows[slot->tcb].slot = PROTOCORE_TCP_ENGINE_SLOT_NONE;
    }
    slot->released = 0u;
    slot->sent = 0u;
    slot->front_acknowledged = 0u;
    slot->tcb = IDEMIP_TCP_PCB_NONE;
    slot->closed = PROTO_FALSE;
    slot->fin_sent = PROTO_FALSE;
    slot->state = TCP_ENGINE_SLOT_FREE;
}

// The transmit ring of a dead pool entry: no octet on it goes out. Each segment leaves the ring, up to the close
// mark and with it. The owner thus always has room for the mark. True where the mark left the ring.
static proto_bool engine_dead_drain(TcpEngineSlot *slot)
{
    for (uint32_t segment = 0u; segment < PROTOCORE_TCP_ENGINE_SEGMENTS; segment++)
    {
        size_t front = 0u;
        if (!EMBED_CALL(anularis.seg_front, AnularisCfg, .ring = slot->transmit, .out_index = &front))
        {
            return PROTO_FALSE;
        }
        const uint32_t len = engine_head_read(EMBED_CALL(anularis.seg_at, AnularisCfg, .ring = slot->transmit,
                                                         .index = front));
        EMBED_CALL(anularis.seg_release, AnularisCfg, .ring = slot->transmit);
        if (len == 0u)
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

// The connection of a pool entry ended. An entry that sent no EVT_CONNECT, or whose owner closed it, goes
// back now. The owner of an open entry gets EVT_ERROR, and its close mark frees the entry.
static void engine_slot_end(struct TcpEngineStorage *context, uint8_t index)
{
    TcpEngineSlot *const slot = &context->slots[index];
    if (slot->state == TCP_ENGINE_SLOT_OPENING || (slot->state == TCP_ENGINE_SLOT_OPEN && slot->closed))
    {
        engine_slot_free(context, index);
        return;
    }
    if (slot->state == TCP_ENGINE_SLOT_OPEN)
    {
        slot->state = TCP_ENGINE_SLOT_DEAD;
        slot->released = 0u;
        slot->sent = 0u;
        slot->front_acknowledged = 0u;
        engine_event_post(EVT_ERROR, index, 0u);
        if (engine_dead_drain(slot))
        {
            engine_slot_free(context, index);
        }
    }
}

// Closes a TCB. Each segment of its out-of-order queue first gives its receive descriptor back, because
// TcpPcb.close refuses a TCB that keeps one.
static void engine_tcb_close(struct TcpEngineStorage *context, uint16_t tcb)
{
    TcpPcbIo *const tcb_io = IDEMIP_TCP_PCB_IO(context->units->tcp_pcb);
    for (uint16_t held = 0u; held <= IDEMIP_TCP_OOSEQ_SEGS; held++)
    {
        tcb_io->pcb_args.index = tcb;
        TcpPcb.load(context->units->tcp_pcb);
        if (tcb_io->status != IDEMIP_OK || tcb_io->info.ooseq == IDEMIP_TCP_PCB_NONE)
        {
            break;
        }
        const uint16_t head = tcb_io->info.ooseq;
        tcb_io->oos_args.index = head;
        TcpPcb.oos_load(context->units->tcp_pcb);
        const uint16_t handle = tcb_io->oos.desc;
        const uint8_t netif = IDEMIP_DISPATCH_DESC_NETIF(handle);
        if (netif < IDEMIP_NETIF_COUNT)
        {
            DmaIo *const dma_io = IDEMIP_DMA_IO(context->units->dma[netif]);
            dma_io->desc_args.index = IDEMIP_DISPATCH_DESC_INDEX(handle);
            Dma.unpin(context->units->dma[netif]);
        }
        tcb_io->oos_args.index = head;
        tcb_io->oos_args.pcb = tcb;
        TcpPcb.oos_free(context->units->tcp_pcb);
    }
    tcb_io->pcb_args.index = tcb;
    TcpPcb.close(context->units->tcp_pcb);
    TcpEngineRow *const row = &context->rows[tcb];
    if (row->slot < PROTOCORE_TCP_ENGINE_SLOTS && context->slots[row->slot].tcb == tcb)
    {
        context->slots[row->slot].tcb = IDEMIP_TCP_PCB_NONE;
    }
    row->slot = PROTOCORE_TCP_ENGINE_SLOT_NONE;
    row->open = PROTO_FALSE;
}

// RFC 9293 sec 3.10.7.2: Dispatch opened this TCB for a SYN at a listener. The TCB takes a free pool entry,
// and the link source of the SYN is its next hop. Without a free entry the TCB closes, and the SYN gets no
// SYN,ACK.
// The peer then sends the SYN again.
static proto_bool engine_open_passive(struct TcpEngineStorage *context, const TcpEngineSegmentArgs *args,
                                      uint16_t tcb)
{
    const uint8_t index = engine_slot_take(context);
    if (index == PROTOCORE_TCP_ENGINE_SLOT_NONE)
    {
        engine_tcb_close(context, tcb);
        return PROTO_FALSE;
    }
    TcpEngineRow *const row = &context->rows[tcb];
    *row = (TcpEngineRow){0};
    EMBED_CALL(proxim.read, ProximusCfg, .dst = row->next_hop, .at = idemip_eth_src(args->frame),
               .size = IDEMIP_MAC_LEN);
    row->netif = args->netif;
    row->slot = index;
    row->open = PROTO_TRUE;
    TcpEngineSlot *const slot = &context->slots[index];
    slot->tcb = tcb;
    slot->state = TCP_ENGINE_SLOT_OPENING;
    return PROTO_TRUE;
}

// RFC 9293 sec 3.10.7.4 fifth: the handshake reached ESTABLISHED. RFC 5681 sec 3.1 starts the congestion
// window, and the owner gets EVT_CONNECT.
static void engine_slot_connect(struct TcpEngineStorage *context, uint8_t index)
{
    TcpEngineSlot *const slot = &context->slots[index];
    TcpPcbIo *const tcb_io = IDEMIP_TCP_PCB_IO(context->units->tcp_pcb);
    TcpOutIo *const out_io = IDEMIP_TCP_OUT_IO(context->units->tcp_out);
    out_io->timer_args = (TcpOutTimerArgs){0};
    out_io->timer_args.smss = engine_send_mss(tcb_io);
    out_io->ctl = tcb_io->ctl;
    TcpOut.cc_init(context->units->tcp_out);
    tcb_io->ctl = out_io->ctl;
    (void)engine_tcb_store(context, slot->tcb);
    slot->state = TCP_ENGINE_SLOT_OPEN;
    engine_event_post(EVT_CONNECT, index, 0u);
}

// Puts text on the receive ring of a pool entry. RCV.WND never passes the vacant octets of the ring, and so
// the text fits. An entry that is not open, or whose owner closed it, takes no text. TcpIn acknowledged the text,
// and the engine drops it.
static void engine_text(TcpEngineSlot *slot, uint8_t index, const uint8_t *text, uint16_t len)
{
    if (slot == NULL || slot->state != TCP_ENGINE_SLOT_OPEN || slot->closed || len == 0u)
    {
        return;
    }
    if (EMBED_CALL(anularis.put, AnularisCfg, .ring = slot->receive, .src = text, .bytes = len))
    {
        engine_event_post(EVT_DATA, index, len);
    }
}

// RFC 9293 sec 3.10.7.4 SHLD-31: the kept segments that RCV.NXT reached. Each delivery keeps its receive
// descriptor pinned until the next call. A second pin reports the frame, and the text comes off it. The last
// call is BUSY, and it gives the last descriptor back.
static void engine_redeliver(struct TcpEngineStorage *context, uint16_t tcb, uint32_t now_ms)
{
    DispatchIo *const dispatch_io = IDEMIP_DISPATCH_IO(context->units->dispatch);
    const TcpEngineRow *const row = &context->rows[tcb];
    TcpEngineSlot *const slot = (row->slot < PROTOCORE_TCP_ENGINE_SLOTS) ? &context->slots[row->slot] : NULL;
    for (uint16_t delivery = 0u; delivery <= IDEMIP_TCP_OOSEQ_SEGS; delivery++)
    {
        dispatch_io->tcp_args.pcb = tcb;
        dispatch_io->tcp_args.now_ms = now_ms;
        Dispatch.tcp_deliver(context->units->dispatch);
        if (dispatch_io->status != IDEMIP_OK)
        {
            return;
        }
        const uint8_t netif = IDEMIP_DISPATCH_DESC_NETIF(dispatch_io->desc);
        if ((dispatch_io->act & IDEMIP_DISPATCH_ACT_DELIVER) == 0u || netif >= IDEMIP_NETIF_COUNT)
        {
            continue;
        }
        DmaIo *const dma_io = IDEMIP_DMA_IO(context->units->dma[netif]);
        dma_io->desc_args.index = IDEMIP_DISPATCH_DESC_INDEX(dispatch_io->desc);
        Dma.pin(context->units->dma[netif]);
        if (dma_io->status != IDEMIP_OK)
        {
            continue;
        }
        engine_text(slot, row->slot, dma_io->buf + dispatch_io->text_off, dispatch_io->text_len);
        Dma.unpin(context->units->dma[netif]);
    }
}

// RFC 9293 sec 3.10.7.4 fifth: SND.UNA moved. Each transmit segment that SND.UNA passed leaves the ring. The
// text starts one past ISS, behind the SYN, and the FIN stands after the last text octet. The count of new
// text octets is thus at most the octets in flight.
static void engine_release(TcpEngineSlot *slot, uint32_t snd_una, uint32_t iss)
{
    uint32_t acknowledged = (snd_una - iss - 1u) - slot->released;
    const uint32_t in_flight = slot->sent - slot->released;
    if (acknowledged > in_flight)
    {
        acknowledged = in_flight;
    }
    for (uint32_t segment = 0u; segment < PROTOCORE_TCP_ENGINE_SEGMENTS && acknowledged != 0u; segment++)
    {
        size_t front = 0u;
        if (!EMBED_CALL(anularis.seg_front, AnularisCfg, .ring = slot->transmit, .out_index = &front))
        {
            return;
        }
        const uint32_t len = engine_head_read(EMBED_CALL(anularis.seg_at, AnularisCfg, .ring = slot->transmit,
                                                         .index = front));
        // The close mark holds no text, and the free of the entry takes it off the ring.
        if (len == 0u)
        {
            return;
        }
        const uint32_t rest = len - slot->front_acknowledged;
        if (acknowledged < rest)
        {
            slot->front_acknowledged += acknowledged;
            slot->released += acknowledged;
            return;
        }
        EMBED_CALL(anularis.seg_release, AnularisCfg, .ring = slot->transmit);
        slot->released += rest;
        slot->front_acknowledged = 0u;
        acknowledged -= rest;
    }
}

// An ACK of new data, on the TCB of a pool entry. The acknowledged segments leave the transmit ring. The ACK
// of the timed segment gives an RFC 6298 sample, which TcpOut.rtt refuses after a resend (Karn). RFC 6298
// (5.2) stops the timer once the peer acknowledges each octet, and (5.3) restarts it on new data otherwise. RFC
// 5681 sec 3.2 step 6 ends fast recovery. Otherwise sec 3.1 opens the congestion window, and RFC 3465 counts
// the octets of the ACK.
static void engine_acknowledged(const struct TcpEngineStorage *context, TcpEngineRow *row, TcpEngineSlot *slot,
                                uint32_t acked, uint32_t now_ms)
{
    TcpPcbIo *const tcb_io = IDEMIP_TCP_PCB_IO(context->units->tcp_pcb);
    TcpOutIo *const out_io = IDEMIP_TCP_OUT_IO(context->units->tcp_out);
    engine_release(slot, tcb_io->vars.snd_una, tcb_io->vars.iss);
    engine_out_load(context, now_ms);
    if (row->timing && (tcb_io->vars.snd_una - tcb_io->ctl.rtseq) < 0x80000000u)
    {
        const uint32_t sample = now_ms - row->timed_ms;
        out_io->timer_args.sample_ms = (sample != 0u) ? sample : 1u;
        TcpOut.rtt(context->units->tcp_out);
        row->timing = PROTO_FALSE;
    }
    if (tcb_io->vars.snd_una == tcb_io->vars.snd_nxt)
    {
        TcpOut.rtx_stop(context->units->tcp_out);
    }
    else
    {
        TcpOut.rtx_restart(context->units->tcp_out);
    }
    out_io->timer_args.acked = acked;
    if (out_io->ctl.dupacks < (uint8_t)IDEMIP_TCP_DUPACK_THRESH)
    {
        TcpOut.cc_ack(context->units->tcp_out);
    }
    // cc_recover deflates cwnd after fast recovery, and it sets the duplicate count back to zero in each case.
    TcpOut.cc_recover(context->units->tcp_out);
    tcb_io->ctl = out_io->ctl;
    (void)engine_tcb_store(context, slot->tcb);
}

// RFC 6298 (5.4) and RFC 5681 sec 3.2 step 2: the earliest segment that the peer did not acknowledge goes out
// again. In SYN-RECEIVED that is the SYN,ACK. Then it is the text at SND.UNA, one MSS at most and within one
// transmit segment. Last it is the FIN. False where nothing waits or the frame does not go out.
static proto_bool engine_resend(const struct TcpEngineStorage *context, const TcpEngineSlot *slot,
                                const TcpEngineEndpoint *endpoint)
{
    const TcpPcbIo *const tcb_io = IDEMIP_TCP_PCB_IO(context->units->tcp_pcb);
    TcpEngineOutbound outbound = {
        .seq = tcb_io->vars.snd_una,
        .ack = tcb_io->vars.rcv_nxt,
        .window = tcb_io->vars.rcv_wnd,
    };
    size_t front = 0u;
    if (tcb_io->state == IDEMIP_TCP_STATE_SYN_RECEIVED)
    {
        outbound.seq = tcb_io->vars.iss;
        outbound.options = (uint16_t)IDEMIP_TCP_OUT_OPT_MSS;
        // Explicit cast narrows the control bits, which RFC 9293 sec 3.1 holds in the low eight bits
        outbound.flags = (uint8_t)(IDEMIP_TCP_SYN | IDEMIP_TCP_ACK);
    }
    else if (slot->sent != slot->released &&
             EMBED_CALL(anularis.seg_front, AnularisCfg, .ring = slot->transmit, .out_index = &front))
    {
        const uint8_t *const segment = EMBED_CALL(anularis.seg_at, AnularisCfg, .ring = slot->transmit, .index = front);
        uint32_t len = engine_head_read(segment) - slot->front_acknowledged;
        const uint32_t in_flight = slot->sent - slot->released;
        const uint32_t mss = engine_send_mss(tcb_io);
        len = (len < in_flight) ? len : in_flight;
        len = (len < mss) ? len : mss;
        outbound.text = segment + PROTOCORE_TCP_ENGINE_SEGMENT_HEAD + slot->front_acknowledged;
        // Explicit cast narrows the count, which the send MSS holds below 65536
        outbound.len = (uint16_t)len;
        // Explicit cast narrows the control bits, which RFC 9293 sec 3.1 holds in the low eight bits
        outbound.flags = (uint8_t)(IDEMIP_TCP_ACK | IDEMIP_TCP_PSH);
    }
    else if (slot->fin_sent && tcb_io->vars.snd_una != tcb_io->vars.snd_nxt)
    {
        // Explicit cast narrows the control bits, which RFC 9293 sec 3.1 holds in the low eight bits
        outbound.flags = (uint8_t)(IDEMIP_TCP_FIN | IDEMIP_TCP_ACK);
    }
    else
    {
        return PROTO_FALSE;
    }
    return engine_emit(context, endpoint, &outbound);
}

// RFC 5681 sec 2 DUPLICATE ACKNOWLEDGMENT. Data is in flight, and SEG.ACK is SND.UNA. The segment carries
// no text, and it carries neither SYN nor FIN. Its window is the window of the last segment. A closed window
// is the probe of RFC 9293 sec 3.8.6.1 and no loss. The third duplicate sends the segment at SND.UNA again:
// RFC 5681 sec 3.2 step 2, fast retransmit.
static void engine_duplicate(const struct TcpEngineStorage *context, TcpEngineRow *row, const TcpEngineSlot *slot,
                             const TcpEngineEndpoint *endpoint, uint32_t now_ms)
{
    const TcpInIo *const in_io = IDEMIP_TCP_IN_IO(context->units->tcp_in);
    TcpPcbIo *const tcb_io = IDEMIP_TCP_PCB_IO(context->units->tcp_pcb);
    if (tcb_io->vars.snd_nxt == tcb_io->vars.snd_una || in_io->seg.data_len != 0u ||
        (in_io->seg.flags & (IDEMIP_TCP_SYN | IDEMIP_TCP_FIN)) != 0u || in_io->seg.ack != tcb_io->vars.snd_una ||
        in_io->seg.wnd != row->peer_window || tcb_io->vars.snd_wnd == 0u)
    {
        return;
    }
    engine_out_load(context, now_ms);
    TcpOut.cc_dupack(context->units->tcp_out);
    tcb_io->ctl = IDEMIP_TCP_OUT_IO(context->units->tcp_out)->ctl;
    (void)engine_tcb_store(context, slot->tcb);
    if (tcb_io->ctl.dupacks == (uint8_t)IDEMIP_TCP_DUPACK_THRESH)
    {
        row->timing = PROTO_FALSE;
        (void)engine_resend(context, slot, endpoint);
    }
}

// What the last Dispatch.input decided about a segment on an open TCB.
static void engine_tcb_segment(struct TcpEngineStorage *context, const TcpEngineSegmentArgs *args, uint16_t tcb)
{
    const DispatchIo *const dispatch_io = IDEMIP_DISPATCH_IO(context->units->dispatch);
    // The redelivery below writes the same operand block. This function thus reads the decision first.
    const uint32_t act = dispatch_io->tcp_act;
    const TcpInReply reply = dispatch_io->reply;
    const uint32_t acked = dispatch_io->acked;
    const uint16_t text_off = dispatch_io->text_off;
    const uint16_t text_len = dispatch_io->text_len;
    TcpEngineRow *const row = &context->rows[tcb];
    if (!row->open && !engine_open_passive(context, args, tcb))
    {
        return;
    }
    TcpEngineEndpoint endpoint;
    if (!engine_endpoint_of_tcb(context, tcb, &endpoint))
    {
        return;
    }
    const TcpPcbIo *const tcb_io = IDEMIP_TCP_PCB_IO(context->units->tcp_pcb);
    // Dispatch aggregates a plain ACK and sends none of the others. A SYN,ACK carries the MSS Option, which
    // RFC 9293 sec 3.7.1 MUST-14 sends.
    if ((act & (IDEMIP_TCP_IN_ACT_ACK | IDEMIP_TCP_IN_ACT_RST)) != 0u)
    {
        const TcpEngineOutbound outbound = {
            .seq = reply.seq,
            .ack = reply.ack,
            .window = tcb_io->vars.rcv_wnd,
            .options = ((reply.flags & IDEMIP_TCP_SYN) != 0u) ? (uint16_t)IDEMIP_TCP_OUT_OPT_MSS : 0u,
            // Explicit cast narrows the control bits, which RFC 9293 sec 3.1 holds in the low eight bits
            .flags = (uint8_t)reply.flags,
        };
        // RFC 6298 (5.1): the SYN,ACK starts the retransmission timer, which sends it again where no ACK comes.
        if (engine_emit(context, &endpoint, &outbound) && (reply.flags & IDEMIP_TCP_SYN) != 0u)
        {
            engine_timer_arm(context, args->now_ms);
            (void)engine_tcb_store(context, tcb);
        }
    }
    const uint8_t index = row->slot;
    TcpEngineSlot *const slot = (index < PROTOCORE_TCP_ENGINE_SLOTS) ? &context->slots[index] : NULL;
    if ((act & IDEMIP_TCP_IN_ACT_ESTABLISHED) != 0u && slot != NULL && slot->state == TCP_ENGINE_SLOT_OPENING)
    {
        engine_slot_connect(context, index);
    }
    if ((act & IDEMIP_TCP_IN_ACT_ACKED) != 0u && slot != NULL)
    {
        engine_acknowledged(context, row, slot, acked, args->now_ms);
    }
    if ((act & IDEMIP_TCP_IN_ACT_DUPACK) != 0u && slot != NULL)
    {
        engine_duplicate(context, row, slot, &endpoint, args->now_ms);
    }
    row->peer_window = IDEMIP_TCP_IN_IO(context->units->tcp_in)->seg.wnd;
    if ((act & IDEMIP_TCP_IN_ACT_TEXT) != 0u)
    {
        engine_text(slot, index, args->frame + text_off, text_len);
        engine_redeliver(context, tcb, args->now_ms);
    }
    if ((act & IDEMIP_TCP_IN_ACT_CLOSING) != 0u && slot != NULL && slot->state == TCP_ENGINE_SLOT_OPEN &&
        !slot->closed)
    {
        engine_event_post(EVT_DISCONNECT, index, 0u);
    }
    const uint32_t ended = IDEMIP_TCP_IN_ACT_RESET | IDEMIP_TCP_IN_ACT_REFUSED | IDEMIP_TCP_IN_ACT_DELETE |
                           IDEMIP_TCP_IN_ACT_LISTEN;
    if ((act & ended) != 0u && slot != NULL)
    {
        engine_slot_end(context, index);
    }
    // RFC 9293 sec 3.10.7.4 second and fourth return a passive TCB to LISTEN. The listener still holds the
    // passive OPEN, and so this TCB closes.
    if ((act & (IDEMIP_TCP_IN_ACT_DELETE | IDEMIP_TCP_IN_ACT_LISTEN)) != 0u)
    {
        engine_tcb_close(context, tcb);
    }
}

// --- the transmit rings ------------------------------------------------------------

// The next text of a pool entry. The walk starts at the front segment, past the octets that the peer acknowledged
// and the octets that went out.
static TcpEngineCursor engine_cursor(const TcpEngineSlot *slot)
{
    TcpEngineCursor cursor = {0};
    size_t front = 0u;
    if (!EMBED_CALL(anularis.seg_front, AnularisCfg, .ring = slot->transmit, .out_index = &front))
    {
        return cursor;
    }
    const size_t in_flight = EMBED_CALL(anularis.seg_inflight, AnularisCfg, .ring = slot->transmit);
    uint32_t position = slot->front_acknowledged + (slot->sent - slot->released);
    for (size_t step = 0u; step < in_flight && step < PROTOCORE_TCP_ENGINE_SEGMENTS; step++)
    {
        const size_t index = (front + step) & (PROTOCORE_TCP_ENGINE_SEGMENTS - 1u);
        const uint8_t *const segment = EMBED_CALL(anularis.seg_at, AnularisCfg, .ring = slot->transmit, .index = index);
        const uint32_t len = engine_head_read(segment);
        if (len == 0u)
        {
            cursor.mark = (cursor.text == NULL && position == 0u) ? PROTO_TRUE : PROTO_FALSE;
            return cursor;
        }
        if (cursor.text == NULL && position < len)
        {
            cursor.text = segment + PROTOCORE_TCP_ENGINE_SEGMENT_HEAD + position;
            cursor.len = len - position;
            cursor.queued = cursor.len;
            position = 0u;
            continue;
        }
        if (cursor.text != NULL)
        {
            cursor.queued += len;
            continue;
        }
        position -= len;
    }
    return cursor;
}

// RFC 9293 sec 3.10.4 CLOSE: the FIN goes out after the last text octet. ESTABLISHED moves to FIN-WAIT-1, and
// CLOSE-WAIT to LAST-ACK. The FIN starts the retransmission timer.
static void engine_fin_send(struct TcpEngineStorage *context, TcpEngineSlot *slot, const TcpEngineEndpoint *endpoint,
                            uint32_t now_ms)
{
    TcpPcbIo *const tcb_io = IDEMIP_TCP_PCB_IO(context->units->tcp_pcb);
    const TcpEngineOutbound outbound = {
        .seq = tcb_io->vars.snd_nxt,
        .ack = tcb_io->vars.rcv_nxt,
        .window = tcb_io->vars.rcv_wnd,
        // Explicit cast narrows the control bits, which RFC 9293 sec 3.1 holds in the low eight bits
        .flags = (uint8_t)(IDEMIP_TCP_FIN | IDEMIP_TCP_ACK),
    };
    if (!engine_emit(context, endpoint, &outbound))
    {
        return;
    }
    tcb_io->vars.snd_nxt++;
    tcb_io->state = (tcb_io->state == IDEMIP_TCP_STATE_CLOSE_WAIT) ? IDEMIP_TCP_STATE_LAST_ACK
                                                                   : IDEMIP_TCP_STATE_FIN_WAIT_1;
    engine_timer_arm(context, now_ms);
    slot->fin_sent = engine_tcb_store(context, slot->tcb);
}

// Sends up to budget text octets of a pool entry. Each run of octets in one transmit segment goes out in its
// own segment, one MSS at most. Returns the octets that went out. Each segment advances SND.NXT and starts
// the retransmission timer, and the TCB goes back to TcpPcb after each one. Where no RFC 6298 sample runs,
// the first segment starts one.
static uint32_t engine_text_send(struct TcpEngineStorage *context, TcpEngineSlot *slot,
                                 const TcpEngineEndpoint *endpoint, uint32_t budget, uint32_t now_ms)
{
    TcpPcbIo *const tcb_io = IDEMIP_TCP_PCB_IO(context->units->tcp_pcb);
    TcpEngineRow *const row = &context->rows[slot->tcb];
    const uint32_t mss = engine_send_mss(tcb_io);
    uint32_t sent = 0u;
    for (uint32_t step = 0u; step <= PROTOCORE_TCP_ENGINE_SEGMENTS && sent < budget; step++)
    {
        const TcpEngineCursor cursor = engine_cursor(slot);
        if (cursor.text == NULL)
        {
            break;
        }
        uint32_t len = budget - sent;
        len = (len < cursor.len) ? len : cursor.len;
        len = (len < mss) ? len : mss;
        const TcpEngineOutbound outbound = {
            .text = cursor.text,
            .seq = tcb_io->vars.snd_nxt,
            .ack = tcb_io->vars.rcv_nxt,
            .window = tcb_io->vars.rcv_wnd,
            // Explicit cast narrows the count, which the send MSS holds below 65536
            .len = (uint16_t)len,
            // Explicit cast narrows the control bits, which RFC 9293 sec 3.1 holds in the low eight bits
            .flags = (uint8_t)(IDEMIP_TCP_ACK | IDEMIP_TCP_PSH),
        };
        if (!engine_emit(context, endpoint, &outbound))
        {
            break;
        }
        tcb_io->ctl.last_send_ms = now_ms;
        tcb_io->vars.snd_nxt += len;
        if (!row->timing)
        {
            row->timing = PROTO_TRUE;
            row->timed_ms = now_ms;
            tcb_io->ctl.rtseq = tcb_io->vars.snd_nxt;
        }
        slot->sent += len;
        sent += len;
        engine_timer_arm(context, now_ms);
        (void)engine_tcb_store(context, slot->tcb);
    }
    return sent;
}

// RFC 9293 sec 3.8.6.1: the peer closed its window, and text waits. Where nothing is in flight, the persist
// timer starts at RTO. Each expiry sends one text octet as a probe and doubles the interval, up to the RTO
// ceiling. A probe that the peer did not take goes out again at SND.UNA. MUST-29 keeps the connection open
// while the peer acknowledges the probes. The probes thus never reach the retransmission timer, which ends
// the connection after R2.
static void engine_persist(struct TcpEngineStorage *context, TcpEngineSlot *slot, const TcpEngineEndpoint *endpoint,
                           const TcpEngineCursor *cursor, uint32_t now_ms)
{
    TcpPcbIo *const tcb_io = IDEMIP_TCP_PCB_IO(context->units->tcp_pcb);
    TcpEngineRow *const row = &context->rows[slot->tcb];
    const proto_bool idle = tcb_io->vars.snd_una == tcb_io->vars.snd_nxt;
    if (tcb_io->vars.snd_wnd != 0u || (!idle && row->persist_interval == 0u))
    {
        return;
    }
    if (row->persist_interval == 0u)
    {
        row->persist_interval = (tcb_io->ctl.rto != 0u) ? tcb_io->ctl.rto : IDEMIP_TCP_RTO_INIT_MS;
        tcb_io->ctl.persist_deadline = engine_deadline(now_ms, row->persist_interval);
        (void)engine_tcb_store(context, slot->tcb);
        return;
    }
    if (!engine_passed(now_ms, tcb_io->ctl.persist_deadline))
    {
        return;
    }
    if (idle)
    {
        const TcpEngineOutbound outbound = {
            .text = cursor->text,
            .seq = tcb_io->vars.snd_nxt,
            .ack = tcb_io->vars.rcv_nxt,
            .window = tcb_io->vars.rcv_wnd,
            .len = 1u,
            // Explicit cast narrows the control bits, which RFC 9293 sec 3.1 holds in the low eight bits
            .flags = (uint8_t)(IDEMIP_TCP_ACK | IDEMIP_TCP_PSH),
        };
        if (engine_emit(context, endpoint, &outbound))
        {
            tcb_io->vars.snd_nxt++;
            slot->sent++;
        }
    }
    else
    {
        (void)engine_resend(context, slot, endpoint);
    }
    const uint32_t doubled = row->persist_interval << 1;
    row->persist_interval = (doubled < IDEMIP_TCP_RTO_MAX_MS) ? doubled : IDEMIP_TCP_RTO_MAX_MS;
    tcb_io->ctl.persist_deadline = engine_deadline(now_ms, row->persist_interval);
    (void)engine_tcb_store(context, slot->tcb);
}

// The peer opened its window again, and the probes end. A probe octet that is still in flight now waits on
// the retransmission timer.
static void engine_persist_end(struct TcpEngineStorage *context, const TcpEngineSlot *slot, uint32_t now_ms)
{
    TcpPcbIo *const tcb_io = IDEMIP_TCP_PCB_IO(context->units->tcp_pcb);
    TcpEngineRow *const row = &context->rows[slot->tcb];
    if (tcb_io->vars.snd_wnd == 0u || row->persist_interval == 0u)
    {
        return;
    }
    row->persist_interval = 0u;
    tcb_io->ctl.persist_deadline = 0u;
    if (tcb_io->vars.snd_una != tcb_io->vars.snd_nxt)
    {
        engine_timer_arm(context, now_ms);
    }
    (void)engine_tcb_store(context, slot->tcb);
}

// RFC 9293 sec 3.8.6.2.1: TcpOut.send decides how many text octets of a pool entry go out now. The usable
// window, the congestion window and the sec 3.7.4 Nagle condition set that count. Each octet of one decision goes out
// before the next decision. A transmit segment boundary thus never holds back text that the decision
// sends. The close mark sends the FIN after the last text octet.
static void engine_transmit(struct TcpEngineStorage *context, TcpEngineSlot *slot, const TcpEngineEndpoint *endpoint,
                            uint32_t now_ms)
{
    TcpPcbIo *const tcb_io = IDEMIP_TCP_PCB_IO(context->units->tcp_pcb);
    TcpOutIo *const out_io = IDEMIP_TCP_OUT_IO(context->units->tcp_out);
    engine_persist_end(context, slot, now_ms);
    for (uint32_t decision = 0u; decision <= PROTOCORE_TCP_ENGINE_SEGMENTS; decision++)
    {
        const TcpEngineCursor cursor = engine_cursor(slot);
        if (cursor.mark)
        {
            slot->closed = PROTO_TRUE;
            if (!slot->fin_sent)
            {
                engine_fin_send(context, slot, endpoint, now_ms);
            }
            return;
        }
        if (cursor.text == NULL)
        {
            return;
        }
        out_io->vars = tcb_io->vars;
        out_io->ctl = tcb_io->ctl;
        out_io->state = tcb_io->state;
        out_io->send_args = (TcpOutSendArgs){0};
        out_io->send_args.queued = cursor.queued;
        out_io->send_args.eff_snd_mss = engine_send_mss(tcb_io);
        out_io->send_args.now_ms = now_ms;
        out_io->send_args.push = PROTO_TRUE;
        TcpOut.send(context->units->tcp_out);
        if (out_io->status != IDEMIP_OK || !out_io->res.send_now)
        {
            engine_persist(context, slot, endpoint, &cursor, now_ms);
            return;
        }
        // RFC 5681 sec 4.1 can shrink cwnd after an idle interval, and the decision carries it.
        tcb_io->ctl = out_io->ctl;
        if (engine_text_send(context, slot, endpoint, out_io->res.send_len, now_ms) == 0u)
        {
            return;
        }
    }
}

// RFC 9293 sec 3.8.6.2.2: the receiver opens the window once the owner read enough of the receive ring. The
// step is the smaller of half the window and one MSS. The window never passes the vacant octets of the ring,
// and never passes the window of ::TcpEngineStartArgs.
static void engine_window_open(struct TcpEngineStorage *context, const TcpEngineSlot *slot,
                               const TcpEngineEndpoint *endpoint)
{
    TcpPcbIo *const tcb_io = IDEMIP_TCP_PCB_IO(context->units->tcp_pcb);
    const size_t vacant = EMBED_CALL(anularis.vacant, AnularisCfg, .ring = slot->receive);
    // Explicit cast narrows the room, which the window of ::TcpEngineStartArgs holds below 65536
    const uint32_t room = (vacant < context->window) ? (uint32_t)vacant : context->window;
    const uint32_t mss = engine_send_mss(tcb_io);
    const uint32_t half = context->window / 2u;
    const uint32_t step = (half < mss) ? half : mss;
    if (room <= tcb_io->vars.rcv_wnd || (room - tcb_io->vars.rcv_wnd) < step)
    {
        return;
    }
    tcb_io->vars.rcv_wnd = room;
    if (!engine_tcb_store(context, slot->tcb))
    {
        return;
    }
    const TcpEngineOutbound outbound = {
        .seq = tcb_io->vars.snd_nxt,
        .ack = tcb_io->vars.rcv_nxt,
        .window = room,
        // Explicit cast narrows the control bit, which RFC 9293 sec 3.1 holds in the low eight bits
        .flags = (uint8_t)IDEMIP_TCP_ACK,
    };
    (void)engine_emit(context, endpoint, &outbound);
}

// RFC 6298 (5.4) through (5.6): the retransmission timer of a pool entry expired. TcpOut.rtx_expire doubles
// RTO, starts the timer again, and sets RFC 5681 sec 3.1 cwnd to one segment. The earliest unacknowledged
// segment then goes out again. RFC 9293 sec 3.8.3 (c) closes the connection after R2 transmissions. The
// sec 3.10.4 ABORT sends <SEQ=SND.NXT><CTL=RST>, and the owner gets EVT_ERROR. False where the connection
// ended.
static proto_bool engine_expire(struct TcpEngineStorage *context, uint8_t index, const TcpEngineEndpoint *endpoint,
                                uint32_t now_ms)
{
    TcpEngineSlot *const slot = &context->slots[index];
    TcpEngineRow *const row = &context->rows[slot->tcb];
    TcpPcbIo *const tcb_io = IDEMIP_TCP_PCB_IO(context->units->tcp_pcb);
    TcpOutIo *const out_io = IDEMIP_TCP_OUT_IO(context->units->tcp_out);
    engine_out_load(context, now_ms);
    out_io->timer_args.resent = tcb_io->ctl.nrtx != 0u;
    TcpOut.rtx_expire(context->units->tcp_out);
    tcb_io->ctl = out_io->ctl;
    row->timing = PROTO_FALSE;
    if (out_io->res.r2)
    {
        const uint16_t tcb = slot->tcb;
        const TcpEngineOutbound outbound = {
            .seq = tcb_io->vars.snd_nxt,
            // Explicit cast narrows the control bit, which RFC 9293 sec 3.1 holds in the low eight bits
            .flags = (uint8_t)IDEMIP_TCP_RST,
        };
        (void)engine_emit(context, endpoint, &outbound);
        engine_slot_end(context, index);
        engine_tcb_close(context, tcb);
        return PROTO_FALSE;
    }
    (void)engine_tcb_store(context, slot->tcb);
    (void)engine_resend(context, slot, endpoint);
    return PROTO_TRUE;
}

// One pool entry with a connection: its retransmission timer, its window, its text, its FIN. The entry goes
// back once the peer acknowledged the FIN, and with it each text octet in front of the FIN.
static void engine_slot_service(struct TcpEngineStorage *context, uint8_t index, uint32_t now_ms)
{
    TcpEngineSlot *const slot = &context->slots[index];
    TcpEngineEndpoint endpoint;
    if (slot->tcb >= IDEMIP_TCP_PCBS || !engine_endpoint_of_tcb(context, slot->tcb, &endpoint))
    {
        return;
    }
    const TcpPcbIo *const tcb_io = IDEMIP_TCP_PCB_IO(context->units->tcp_pcb);
    if (engine_passed(now_ms, tcb_io->ctl.rtx_deadline) && !engine_expire(context, index, &endpoint, now_ms))
    {
        return;
    }
    if (slot->state != TCP_ENGINE_SLOT_OPEN)
    {
        return;
    }
    const IdemIpTcpState state = tcb_io->state;
    if (!slot->closed && (state == IDEMIP_TCP_STATE_ESTABLISHED || state == IDEMIP_TCP_STATE_FIN_WAIT_1 ||
                          state == IDEMIP_TCP_STATE_FIN_WAIT_2))
    {
        engine_window_open(context, slot, &endpoint);
    }
    if (state == IDEMIP_TCP_STATE_ESTABLISHED || state == IDEMIP_TCP_STATE_CLOSE_WAIT)
    {
        engine_transmit(context, slot, &endpoint, now_ms);
    }
    if (slot->fin_sent && tcb_io->vars.snd_una == tcb_io->vars.snd_nxt)
    {
        engine_slot_free(context, index);
    }
}

// The two waits at the end of a connection. RFC 9293 sec 3.6 MUST-13: a TIME-WAIT TCB closes once 2 MSL
// passed, and TcpIn sets that deadline. A FIN-WAIT-2 TCB with no pool entry closes after
// PROTOCORE_TCP_ENGINE_FIN_WAIT_2_MS, because its peer sent no FIN.
static void engine_end_waits(struct TcpEngineStorage *context, uint32_t now_ms)
{
    TcpPcbIo *const tcb_io = IDEMIP_TCP_PCB_IO(context->units->tcp_pcb);
    for (uint16_t tcb = 0u; tcb < IDEMIP_TCP_PCBS; tcb++)
    {
        TcpEngineRow *const row = &context->rows[tcb];
        if (!row->open)
        {
            continue;
        }
        tcb_io->pcb_args.index = tcb;
        TcpPcb.load(context->units->tcp_pcb);
        if (tcb_io->status != IDEMIP_OK)
        {
            continue;
        }
        if (tcb_io->state == IDEMIP_TCP_STATE_TIME_WAIT && engine_passed(now_ms, tcb_io->ctl.time_wait_deadline))
        {
            engine_tcb_close(context, tcb);
        }
        else if (tcb_io->state == IDEMIP_TCP_STATE_FIN_WAIT_2 && row->slot == PROTOCORE_TCP_ENGINE_SLOT_NONE)
        {
            if (row->linger_deadline == 0u)
            {
                row->linger_deadline = engine_deadline(now_ms, PROTOCORE_TCP_ENGINE_FIN_WAIT_2_MS);
            }
            else if (engine_passed(now_ms, row->linger_deadline))
            {
                engine_tcb_close(context, tcb);
            }
        }
    }
}

// --- the entries -----------------------------------------------------------------

void protocore_tcp_engine_start(uint8_t *work)
{
    struct TcpEngineStorage *const context = TCP_ENGINE_CONTEXT(work);
    const TcpEngineStartArgs *const args = &TcpEngineV.start_args;
    TcpEngineV.ok = PROTO_FALSE;
    if (args->stack == NULL || args->window == 0u || args->window > 0xFFFFu)
    {
        return;
    }
    mmgr_zero_buf(work, sizeof(struct TcpEngineStorage));
    for (uint16_t tcb = 0u; tcb < IDEMIP_TCP_PCBS; tcb++)
    {
        context->rows[tcb].slot = PROTOCORE_TCP_ENGINE_SLOT_NONE;
    }
    for (uint8_t index = 0u; index < PROTOCORE_TCP_ENGINE_SLOTS; index++)
    {
        context->slots[index].tcb = IDEMIP_TCP_PCB_NONE;
    }
    context->stack = args->stack;
    context->units = protocore_stack_units(args->stack);
    context->window = args->window;
    context->started = EMBED_CALL(anularis.init, AnularisCfg, .ring = &s_events, .buf = mmgr_pars_storage_tcp_engine_events,
                                  .capacity = TCP_ENGINE_EVENT_BYTES, .segment_count = 1u);
    TcpEngineV.sent = 0u;
    TcpEngineV.lost_events = 0u;
    TcpEngineV.ok = context->started;
}

void protocore_tcp_engine_listen(uint8_t *work)
{
    // Sixteen zero octets: RFC 9293 sec 3.9.1.1 takes the unspecified local address as each address of the host.
    static const uint8_t any_address[IDEMIP_TCP_PCB_ADDR_BYTES] = {0};
    const struct TcpEngineStorage *const context = TCP_ENGINE_CONTEXT(work);
    const TcpEngineListenArgs *const args = &TcpEngineV.listen_args;
    TcpEngineV.ok = PROTO_FALSE;
    if (!context->started)
    {
        return;
    }
    TcpPcbIo *const tcb_io = IDEMIP_TCP_PCB_IO(context->units->tcp_pcb);
    tcb_io->listen_args = (TcpPcbListenArgs){0};
    tcb_io->listen_args.ip = any_address;
    tcb_io->listen_args.port = args->port;
    tcb_io->listen_args.backlog = args->backlog;
    tcb_io->listen_args.ip_version = args->ip_version;
    tcb_io->listen_args.rcv_wnd = context->window;
    TcpPcb.listen(context->units->tcp_pcb);
    TcpEngineV.ok = tcb_io->status == IDEMIP_OK;
}

void protocore_tcp_engine_slot_bind(uint8_t *work)
{
    struct TcpEngineStorage *const context = TCP_ENGINE_CONTEXT(work);
    const TcpEngineSlotArgs *const args = &TcpEngineV.slot_args;
    TcpEngineV.ok = PROTO_FALSE;
    const size_t segment_bytes = args->transmit_bytes / PROTOCORE_TCP_ENGINE_SEGMENTS;
    // Explicit cast reads the address as an integer, where the test for word alignment is arithmetic
    const proto_bool aligned = ((uintptr_t)args->transmit_buffer & (uintptr_t)(sizeof(uint32_t) - 1u)) == 0u;
    if (!context->started || args->index >= PROTOCORE_TCP_ENGINE_SLOTS || args->receive == NULL ||
        args->transmit == NULL || args->transmit_buffer == NULL || !aligned ||
        segment_bytes <= PROTOCORE_TCP_ENGINE_SEGMENT_HEAD || (segment_bytes % sizeof(uint32_t)) != 0u ||
        context->slots[args->index].state > TCP_ENGINE_SLOT_FREE ||
        EMBED_CALL(anularis.vacant, AnularisCfg, .ring = args->receive) < context->window)
    {
        return;
    }
    if (!EMBED_CALL(anularis.init, AnularisCfg, .ring = args->transmit, .buf = args->transmit_buffer,
                    .capacity = args->transmit_bytes, .segment_count = PROTOCORE_TCP_ENGINE_SEGMENTS,
                    .segment_bytes = segment_bytes))
    {
        return;
    }
    TcpEngineSlot *const slot = &context->slots[args->index];
    slot->receive = args->receive;
    slot->transmit = args->transmit;
    // Explicit cast narrows the count, which the octets of one segment hold below 2^32
    slot->text_bytes = (uint32_t)(segment_bytes - PROTOCORE_TCP_ENGINE_SEGMENT_HEAD);
    engine_slot_free(context, args->index);
    TcpEngineV.ok = PROTO_TRUE;
}

void protocore_tcp_engine_event_take(uint8_t *work)
{
    const struct TcpEngineStorage *const context = TCP_ENGINE_CONTEXT(work);
    TcpEngineV.ok = PROTO_FALSE;
    if (!context->started || EMBED_CALL(anularis.available, AnularisCfg, .ring = &s_events) < sizeof(TcpEvt))
    {
        return;
    }
    // Explicit cast writes the record as its octets, which the ring copies whole
    (void)EMBED_CALL(anularis.read, AnularisCfg, .ring = &s_events, .dst = (uint8_t *)(void *)&TcpEngineV.event,
                     .bytes = sizeof(TcpEvt));
    TcpEngineV.ok = PROTO_TRUE;
}

void protocore_tcp_engine_segment(uint8_t *work)
{
    struct TcpEngineStorage *const context = TCP_ENGINE_CONTEXT(work);
    const TcpEngineSegmentArgs *const args = &TcpEngineV.segment_args;
    TcpEngineV.ok = PROTO_FALSE;
    if (!context->started)
    {
        return;
    }
    const DispatchIo *const dispatch_io = IDEMIP_DISPATCH_IO(context->units->dispatch);
    if ((dispatch_io->act & IDEMIP_DISPATCH_ACT_TCP) == 0u)
    {
        return;
    }
    if (dispatch_io->pcb_kind == IDEMIP_DISPATCH_PCB_TCP && dispatch_io->pcb < IDEMIP_TCP_PCBS)
    {
        engine_tcb_segment(context, args, dispatch_io->pcb);
    }
    else
    {
        engine_answer_frame(context, args);
    }
    TcpEngineV.ok = PROTO_TRUE;
}

void protocore_tcp_engine_acknowledge(uint8_t *work)
{
    struct TcpEngineStorage *const context = TCP_ENGINE_CONTEXT(work);
    TcpEngineV.ok = PROTO_FALSE;
    if (!context->started)
    {
        return;
    }
    const DispatchIo *const dispatch_io = IDEMIP_DISPATCH_IO(context->units->dispatch);
    const uint16_t tcb = dispatch_io->pcb;
    const TcpInReply reply = dispatch_io->reply;
    TcpEngineEndpoint endpoint;
    if (tcb >= IDEMIP_TCP_PCBS || !context->rows[tcb].open || !engine_endpoint_of_tcb(context, tcb, &endpoint))
    {
        return;
    }
    const TcpEngineOutbound outbound = {
        .seq = reply.seq,
        .ack = reply.ack,
        .window = IDEMIP_TCP_PCB_IO(context->units->tcp_pcb)->vars.rcv_wnd,
        // Explicit cast narrows the control bits, which RFC 9293 sec 3.1 holds in the low eight bits
        .flags = (uint8_t)reply.flags,
    };
    TcpEngineV.ok = engine_emit(context, &endpoint, &outbound);
}

void protocore_tcp_engine_service(uint8_t *work)
{
    struct TcpEngineStorage *const context = TCP_ENGINE_CONTEXT(work);
    const uint32_t now_ms = TcpEngineV.service_args.now_ms;
    TcpEngineV.ok = PROTO_FALSE;
    if (!context->started)
    {
        return;
    }
    for (uint8_t index = 0u; index < PROTOCORE_TCP_ENGINE_SLOTS; index++)
    {
        TcpEngineSlot *const slot = &context->slots[index];
        if (slot->state == TCP_ENGINE_SLOT_OPEN || slot->state == TCP_ENGINE_SLOT_OPENING)
        {
            engine_slot_service(context, index, now_ms);
        }
        else if (slot->state == TCP_ENGINE_SLOT_DEAD && engine_dead_drain(slot))
        {
            engine_slot_free(context, index);
        }
    }
    engine_end_waits(context, now_ms);
    TcpEngineV.ok = PROTO_TRUE;
}

// --- the owner of a pool entry -----------------------------------------------------

// The record of a pool entry, for the owner of the entry. NULL where the engine did not start, or where the
// entry has no rings.
static TcpEngineSlot *engine_owner_slot(uint8_t index)
{
    struct TcpEngineStorage *const context = TCP_ENGINE_CONTEXT(protocore_tcp_engine_span());
    if (context == NULL || !context->started || index >= PROTOCORE_TCP_ENGINE_SLOTS ||
        context->slots[index].state == TCP_ENGINE_SLOT_UNBOUND)
    {
        return NULL;
    }
    return &context->slots[index];
}

size_t protocore_tcp_engine_write(const TcpEngineWriteArgs *args)
{
    const TcpEngineSlot *const slot = engine_owner_slot(args->index);
    if (slot == NULL)
    {
        return 0u;
    }
    size_t written = 0u;
    while (written < args->len)
    {
        size_t index = 0u;
        if (!EMBED_CALL(anularis.seg_next, AnularisCfg, .ring = slot->transmit, .out_index = &index))
        {
            break;
        }
        uint8_t *const segment = EMBED_CALL(anularis.seg_at, AnularisCfg, .ring = slot->transmit, .index = index);
        const size_t rest = args->len - written;
        const size_t take = (rest < slot->text_bytes) ? rest : slot->text_bytes;
        // Explicit cast narrows the count, which TcpEngineSlot::text_bytes holds below 2^32
        engine_head_write(segment, (uint32_t)take);
        EMBED_CALL(proxim.read, ProximusCfg, .dst = segment + PROTOCORE_TCP_ENGINE_SEGMENT_HEAD,
                   .at = args->data + written, .size = take);
        EMBED_CALL(anularis.seg_publish, AnularisCfg, .ring = slot->transmit);
        written += take;
    }
    return written;
}

proto_bool protocore_tcp_engine_close(const TcpEngineCloseArgs *args)
{
    const TcpEngineSlot *const slot = engine_owner_slot(args->index);
    size_t index = 0u;
    if (slot == NULL || !EMBED_CALL(anularis.seg_next, AnularisCfg, .ring = slot->transmit, .out_index = &index))
    {
        return PROTO_FALSE;
    }
    engine_head_write(EMBED_CALL(anularis.seg_at, AnularisCfg, .ring = slot->transmit, .index = index), 0u);
    EMBED_CALL(anularis.seg_publish, AnularisCfg, .ring = slot->transmit);
    return PROTO_TRUE;
}

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for itself.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_TCP_ENGINE_BORROW persistent bytes
} TcpEngineOwnContext;
static TcpEngineOwnContext s_own;

uint8_t *protocore_tcp_engine_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = (uint8_t *)protocore_plain_persist(PROTOCORE_TCP_ENGINE_BORROW);
    }
    return s_own.span;
}

/** @brief The operands and the outcome. */
TcpEngineVars TcpEngineV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_IDEMIP
