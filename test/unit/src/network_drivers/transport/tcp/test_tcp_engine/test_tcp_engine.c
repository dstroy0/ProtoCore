// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

// The TCP engine on the idemIP stack, over the fake PHY of idemIP. The suite is the peer. It puts each
// segment on the wire with its checksums, runs one tick of the stack, and reads back each segment that
// the engine sent. Pool entry 0 has rings, and no other entry has them.

#include "network_drivers/transport/tcp/engine/engine.h"

#include "src/netif/dma.h"         // Dma.pinned: the receive descriptors that a unit keeps
#include "test/support/fake_phy.h" // the link driver of idemIP for the host, from include/idemIP

#include <string.h>
#include <unity.h>

IDEMIP_FAKE_PHY_STORAGE;
IDEMIP_FAKE_PHY_DRIVER(0);

static EMBED_ALIGN(64) uint8_t s_rx_bufs[IDEMIP_RX_DESCRIPTORS * IDEMIP_DMA_BUF_STRIDE];
static EMBED_ALIGN(64) uint8_t s_tx_bufs[IDEMIP_TX_DESCRIPTORS * IDEMIP_DMA_BUF_STRIDE];

static const uint8_t MAC_OWN[IDEMIP_MAC_LEN] = {0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x02u};
static const uint8_t MAC_PEER[IDEMIP_MAC_LEN] = {0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x09u};
static const uint8_t ISN_SECRET[IDEMIP_TCP_ISN_SECRET_BYTES] = {0x11u, 0x22u, 0x33u, 0x44u};

#define IP_OWN 0xC0A80102u  // 192.168.1.2
#define IP_PEER 0xC0A80109u // 192.168.1.9
#define IP_MASK 0xFFFFFF00u
#define PORT_OWN 80u
#define PEER_ISS 1000u

#define TCP_FIN 0x01u
#define TCP_SYN 0x02u
#define TCP_RST 0x04u
#define TCP_PSH 0x08u
#define TCP_ACK 0x10u

// The receive ring of pool entry 0, and its window: the ring keeps one octet back.
#define RECEIVE_BYTES 1024u
#define WINDOW (RECEIVE_BYTES - 1u)
// The transmit ring of pool entry 0: PROTOCORE_TCP_ENGINE_SEGMENTS segments of 128 octets, each with its head.
#define TRANSMIT_BYTES (PROTOCORE_TCP_ENGINE_SEGMENTS * 128u)
#define SEGMENT_TEXT (128u - PROTOCORE_TCP_ENGINE_SEGMENT_HEAD)

static EMBED_ALIGN(64) uint8_t s_receive_bytes[RECEIVE_BYTES];
static EMBED_ALIGN(64) uint8_t s_transmit_bytes[TRANSMIT_BYTES];
static mmgr_ring s_receive;
static mmgr_ring s_transmit;

static uint32_t s_now_ms;
static proto_bool s_up;

static void put16(uint8_t *at, uint16_t value)
{
    at[0] = (uint8_t)(value >> 8);
    at[1] = (uint8_t)value;
}

static void put32(uint8_t *at, uint32_t value)
{
    put16(at, (uint16_t)(value >> 16));
    put16(at + 2, (uint16_t)value);
}

static uint16_t get16(const uint8_t *at)
{
    return (uint16_t)((at[0] << 8) | at[1]);
}

static uint32_t get32(const uint8_t *at)
{
    return ((uint32_t)get16(at) << 16) | get16(at + 2);
}

// The RFC 1071 sum of len octets, folded and not complemented, from a start value.
static uint32_t sum_add(uint32_t sum, const uint8_t *data, size_t len)
{
    for (size_t at = 0u; at + 1u < len; at += 2u)
    {
        sum += get16(data + at);
    }
    if ((len & 1u) != 0u)
    {
        sum += (uint32_t)data[len - 1u] << 8;
    }
    return sum;
}

static uint16_t sum_fold(uint32_t sum)
{
    while ((sum >> 16) != 0u)
    {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

// The RFC 9293 sec 3.1 checksum of a segment over its IPv4 pseudo-header.
static uint16_t tcp_checksum(const uint8_t *ip, const uint8_t *segment, size_t len)
{
    uint8_t pseudo[12] = {0};
    memcpy(pseudo, ip + 12, 8u);
    pseudo[9] = 6u;
    put16(pseudo + 10, (uint16_t)len);
    return sum_fold(sum_add(sum_add(0u, pseudo, sizeof pseudo), segment, len));
}

/** @brief One segment that the peer sends. */
typedef struct
{
    uint32_t seq;
    uint32_t ack;
    const char *text;
    uint16_t port;    ///< the port of the peer
    uint16_t to_port; ///< the local port, 0 for PORT_OWN
    uint16_t mss;     ///< the value of an MSS Option, 0 for none
    uint8_t flags;
} PeerSegment;

// Puts one segment of the peer on the wire, to the address of the interface.
static void peer_send(const PeerSegment *segment)
{
    uint8_t frame[IDEMIP_DMA_FRAME_MAX] = {0};
    const size_t text_len = (segment->text != NULL) ? strlen(segment->text) : 0u;
    const size_t options = (segment->mss != 0u) ? 4u : 0u;
    const size_t tcp_len = 20u + options + text_len;
    memcpy(frame, MAC_OWN, IDEMIP_MAC_LEN);
    memcpy(frame + IDEMIP_MAC_LEN, MAC_PEER, IDEMIP_MAC_LEN);
    put16(frame + 12, 0x0800u);
    uint8_t *const ip = frame + 14;
    ip[0] = 0x45u;
    put16(ip + 2, (uint16_t)(20u + tcp_len));
    ip[8] = 64u;
    ip[9] = 6u;
    put32(ip + 12, IP_PEER);
    put32(ip + 16, IP_OWN);
    put16(ip + 10, sum_fold(sum_add(0u, ip, 20u)));
    uint8_t *const tcp = ip + 20;
    put16(tcp, segment->port);
    put16(tcp + 2, (segment->to_port != 0u) ? segment->to_port : PORT_OWN);
    put32(tcp + 4, segment->seq);
    put32(tcp + 8, segment->ack);
    tcp[12] = (uint8_t)(((20u + options) / 4u) << 4);
    tcp[13] = segment->flags;
    put16(tcp + 14, 8192u);
    if (options != 0u)
    {
        tcp[20] = 2u;
        tcp[21] = 4u;
        put16(tcp + 22, segment->mss);
    }
    if (text_len != 0u)
    {
        memcpy(tcp + 20u + options, segment->text, text_len);
    }
    put16(tcp + 16, tcp_checksum(ip, tcp, tcp_len));
    const size_t frame_len = (14u + 20u + tcp_len < 60u) ? 60u : 14u + 20u + tcp_len;
    TEST_ASSERT_TRUE(idemip_fake_phy_feed(&g_idemip_fake_phy[0], frame, frame_len));
}

// One pass: the engine lands each frame, the stack drains and answers, and the engine completes the
// transmission.
static void pass(void)
{
    idemip_fake_phy_poll(&g_idemip_fake_phy[0]);
    s_now_ms += 10u;
    StackV.tick_args.now_ms = s_now_ms;
    StackV.tick_args.random_word = 0x5A5Au;
    Stack.tick(protocore_stack_span());
    TEST_ASSERT_TRUE(StackV.ok);
    idemip_fake_phy_poll(&g_idemip_fake_phy[0]);
}

/** @brief One segment that the engine sent. */
typedef struct
{
    uint32_t seq;
    uint32_t ack;
    uint8_t text[IDEMIP_DMA_FRAME_MAX];
    uint16_t text_len;
    uint16_t window;
    uint16_t mss;        ///< the value of its MSS Option, 0 for none
    uint16_t local_port; ///< its source port
    uint16_t port;       ///< its destination port, the port of the peer
    uint8_t flags;
} EngineSegment;

// Takes the oldest frame that the engine sent, checks its addresses and both checksums, and reads its
// segment.
static EngineSegment engine_sent(void)
{
    EngineSegment segment = {0};
    size_t len = 0u;
    const uint8_t *const frame = idemip_fake_phy_tx_peek(&g_idemip_fake_phy[0], &len);
    TEST_ASSERT_NOT_NULL_MESSAGE(frame, "the engine sent no frame");
    TEST_ASSERT_EQUAL_MEMORY(MAC_PEER, frame, IDEMIP_MAC_LEN);
    TEST_ASSERT_EQUAL_MEMORY(MAC_OWN, frame + IDEMIP_MAC_LEN, IDEMIP_MAC_LEN);
    TEST_ASSERT_EQUAL_HEX16(0x0800u, get16(frame + 12));
    const uint8_t *const ip = frame + 14;
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0u, sum_fold(sum_add(0u, ip, 20u)), "the IPv4 header checksum does not hold");
    TEST_ASSERT_EQUAL_UINT8(6u, ip[9]);
    TEST_ASSERT_EQUAL_HEX32(IP_OWN, get32(ip + 12));
    TEST_ASSERT_EQUAL_HEX32(IP_PEER, get32(ip + 16));
    const size_t tcp_len = get16(ip + 2) - 20u;
    const uint8_t *const tcp = ip + 20;
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0u, tcp_checksum(ip, tcp, tcp_len), "the TCP checksum does not hold");
    segment.local_port = get16(tcp);
    segment.port = get16(tcp + 2);
    segment.seq = get32(tcp + 4);
    segment.ack = get32(tcp + 8);
    segment.flags = tcp[13];
    segment.window = get16(tcp + 14);
    const size_t header_len = (size_t)(tcp[12] >> 4) * 4u;
    if (header_len >= 24u && tcp[20] == 2u)
    {
        segment.mss = get16(tcp + 22);
    }
    segment.text_len = (uint16_t)(tcp_len - header_len);
    memcpy(segment.text, tcp + header_len, segment.text_len);
    idemip_fake_phy_tx_drop(&g_idemip_fake_phy[0]);
    return segment;
}

static size_t sent_count(void)
{
    return idemip_fake_phy_tx_count(&g_idemip_fake_phy[0]);
}

// Takes the next event of the engine, of the type that the case expects.
static TcpEvt event_expect(EvtType type)
{
    TcpEngine.event_take(protocore_tcp_engine_span());
    TEST_ASSERT_TRUE_MESSAGE(TcpEngineV.ok, "the engine posted no event");
    TEST_ASSERT_EQUAL_INT(type, TcpEngineV.event.type);
    TEST_ASSERT_EQUAL_UINT8(0u, TcpEngineV.event.slot_id);
    return TcpEngineV.event;
}

static void event_none(void)
{
    TcpEngine.event_take(protocore_tcp_engine_span());
    TEST_ASSERT_FALSE_MESSAGE(TcpEngineV.ok, "the engine posted an event that the case did not expect");
}

// The owner reads the receive ring of pool entry 0.
static size_t owner_read(uint8_t *into, size_t cap)
{
    return EMBED_CALL(anularis.read, AnularisCfg, .ring = &s_receive, .dst = into, .bytes = cap);
}

static size_t owner_write(const char *text)
{
    const TcpEngineWriteArgs args = {.data = (const uint8_t *)text, .len = strlen(text), .index = 0u};
    return protocore_tcp_engine_write(&args);
}

static proto_bool owner_close(void)
{
    const TcpEngineCloseArgs args = {.index = 0u};
    return protocore_tcp_engine_close(&args);
}

// RFC 9293 sec 3.5 Figure 6: the three-way handshake from a peer port. The SYN carries an MSS Option of 536
// octets. Returns the ISS of the engine, after EVT_CONNECT.
static uint32_t handshake(uint16_t port)
{
    peer_send(&(PeerSegment){.seq = PEER_ISS, .port = port, .mss = 536u, .flags = TCP_SYN});
    pass();
    TEST_ASSERT_EQUAL(1u, sent_count());
    const EngineSegment syn_ack = engine_sent();
    TEST_ASSERT_EQUAL_HEX8(TCP_SYN | TCP_ACK, syn_ack.flags);
    TEST_ASSERT_EQUAL_UINT32(PEER_ISS + 1u, syn_ack.ack);
    TEST_ASSERT_EQUAL_UINT16(PORT_OWN, syn_ack.local_port);
    TEST_ASSERT_EQUAL_UINT16(port, syn_ack.port);
    const uint32_t iss = syn_ack.seq;
    peer_send(&(PeerSegment){.seq = PEER_ISS + 1u, .ack = iss + 1u, .port = port, .flags = TCP_ACK});
    pass();
    (void)event_expect(EVT_CONNECT);
    return iss;
}

// The peer resets the connection, and the owner closes the pool entry. The entry is then free for the next
// case.
static void reset_and_release(uint16_t port, uint32_t peer_seq)
{
    peer_send(&(PeerSegment){.seq = peer_seq, .port = port, .flags = TCP_RST});
    pass();
    (void)event_expect(EVT_ERROR);
    TEST_ASSERT_TRUE(owner_close());
    pass();
}

void setUp(void)
{
    if (s_up)
    {
        return;
    }
    s_now_ms = 1000u;
    idemip_fake_phy_attach(&g_idemip_fake_phy[0], s_rx_bufs, s_tx_bufs, MAC_OWN);
    StackV.start_args.isn_secret = ISN_SECRET;
    Stack.start(protocore_stack_span());
    TEST_ASSERT_TRUE_MESSAGE(StackV.ok, "the stack did not start");
    StackV.bind_args.driver = &idemip_fake_phy_drv_0;
    StackV.bind_args.mac = MAC_OWN;
    StackV.bind_args.rx_base = s_rx_bufs;
    StackV.bind_args.tx_base = s_tx_bufs;
    StackV.bind_args.phy_addr = 1u;
    StackV.bind_args.index = 0u;
    Stack.bind(protocore_stack_span());
    TEST_ASSERT_TRUE_MESSAGE(StackV.ok, "the interface did not bind");
    StackV.addr4_args.address = IP_OWN;
    StackV.addr4_args.mask = IP_MASK;
    StackV.addr4_args.index = 0u;
    Stack.set_addr4(protocore_stack_span());
    TEST_ASSERT_TRUE_MESSAGE(StackV.ok, "the interface did not take its address");

    TcpEngineV.start_args.stack = protocore_stack_span();
    TcpEngineV.start_args.window = WINDOW;
    TcpEngine.start(protocore_tcp_engine_span());
    TEST_ASSERT_TRUE_MESSAGE(TcpEngineV.ok, "the engine did not start");
    TEST_ASSERT_TRUE(EMBED_CALL(anularis.init, AnularisCfg, .ring = &s_receive, .buf = s_receive_bytes,
                                .capacity = RECEIVE_BYTES, .segment_count = 1u));
    TcpEngineV.slot_args.receive = &s_receive;
    TcpEngineV.slot_args.transmit = &s_transmit;
    TcpEngineV.slot_args.transmit_buffer = s_transmit_bytes;
    TcpEngineV.slot_args.transmit_bytes = TRANSMIT_BYTES;
    TcpEngineV.slot_args.index = 0u;
    TcpEngine.slot_bind(protocore_tcp_engine_span());
    TEST_ASSERT_TRUE_MESSAGE(TcpEngineV.ok, "slot 0 did not take its rings");
    TcpEngineV.listen_args.port = PORT_OWN;
    TcpEngineV.listen_args.ip_version = 4u;
    TcpEngineV.listen_args.backlog = 2u;
    TcpEngine.listen(protocore_tcp_engine_span());
    TEST_ASSERT_TRUE_MESSAGE(TcpEngineV.ok, "the listener did not open");
    s_up = PROTO_TRUE;
}

void tearDown(void)
{
    while (sent_count() != 0u)
    {
        idemip_fake_phy_tx_drop(&g_idemip_fake_phy[0]);
    }
}

// RFC 9293 sec 3.10.7.2: a SYN to the listener gets <SEQ=ISS><ACK=RCV.NXT><CTL=SYN,ACK>. The window is the
// receive ring of the pool entry, and the MSS Option gives the receive MSS of the interface (sec 3.7.1 MUST-14).
void test_a_syn_to_the_listener_gets_a_syn_ack(void)
{
    peer_send(&(PeerSegment){.seq = PEER_ISS, .port = 40001u, .mss = 536u, .flags = TCP_SYN});
    pass();
    TEST_ASSERT_EQUAL(1u, sent_count());
    const EngineSegment syn_ack = engine_sent();
    TEST_ASSERT_EQUAL_HEX8(TCP_SYN | TCP_ACK, syn_ack.flags);
    TEST_ASSERT_EQUAL_UINT32(PEER_ISS + 1u, syn_ack.ack);
    TEST_ASSERT_EQUAL_UINT16(WINDOW, syn_ack.window);
    TEST_ASSERT_EQUAL_UINT16(1460u, syn_ack.mss);
    TEST_ASSERT_EQUAL_UINT16(0u, syn_ack.text_len);
    event_none();

    peer_send(&(PeerSegment){.seq = PEER_ISS + 1u, .ack = syn_ack.seq + 1u, .port = 40001u, .flags = TCP_ACK});
    pass();
    (void)event_expect(EVT_CONNECT);
    TEST_ASSERT_EQUAL(0u, sent_count());
    reset_and_release(40001u, PEER_ISS + 1u);
}

// RFC 9293 sec 3.10.7.4 seventh: the text lands in the receive ring of the pool entry, and one aggregate ACK
// covers it. RCV.WND gets smaller by the octets that the owner did not read yet.
void test_text_lands_in_the_receive_ring_and_draws_one_ack(void)
{
    const uint32_t iss = handshake(40002u);
    peer_send(&(PeerSegment){.seq = PEER_ISS + 1u, .ack = iss + 1u, .port = 40002u, .text = "hello",
                             .flags = TCP_ACK | TCP_PSH});
    pass();
    const TcpEvt data = event_expect(EVT_DATA);
    TEST_ASSERT_EQUAL_UINT(5u, data.data_len);
    uint8_t read[16] = {0};
    TEST_ASSERT_EQUAL_UINT(5u, owner_read(read, sizeof read));
    TEST_ASSERT_EQUAL_MEMORY("hello", read, 5u);
    TEST_ASSERT_EQUAL(1u, sent_count());
    const EngineSegment ack = engine_sent();
    TEST_ASSERT_EQUAL_HEX8(TCP_ACK, ack.flags);
    TEST_ASSERT_EQUAL_UINT32(iss + 1u, ack.seq);
    TEST_ASSERT_EQUAL_UINT32(PEER_ISS + 6u, ack.ack);
    TEST_ASSERT_EQUAL_UINT16(WINDOW - 5u, ack.window);
    reset_and_release(40002u, PEER_ISS + 6u);
}

// RFC 9293 sec 3.8.6.2.1: the octets of the transmit ring go out at SND.NXT. RFC 9293 sec 3.10.7.4 fifth:
// the ACK of the peer takes their segment off the ring. The owner can then fill each segment again.
void test_the_transmit_ring_goes_out_and_its_acknowledged_octets_leave_it(void)
{
    const uint32_t iss = handshake(40003u);
    TEST_ASSERT_EQUAL_UINT(5u, owner_write("world"));
    pass();
    TEST_ASSERT_EQUAL(1u, sent_count());
    const EngineSegment text = engine_sent();
    TEST_ASSERT_EQUAL_HEX8(TCP_ACK | TCP_PSH, text.flags);
    TEST_ASSERT_EQUAL_UINT32(iss + 1u, text.seq);
    TEST_ASSERT_EQUAL_UINT32(PEER_ISS + 1u, text.ack);
    TEST_ASSERT_EQUAL_UINT16(5u, text.text_len);
    TEST_ASSERT_EQUAL_MEMORY("world", text.text, 5u);

    // The ring has three free segments, and the segment of "world" waits for its ACK.
    char fill[(PROTOCORE_TCP_ENGINE_SEGMENTS * SEGMENT_TEXT) + 1u];
    memset(fill, 'x', sizeof fill - 1u);
    fill[sizeof fill - 1u] = '\0';
    TEST_ASSERT_EQUAL_UINT((PROTOCORE_TCP_ENGINE_SEGMENTS - 1u) * SEGMENT_TEXT, owner_write(fill));
    peer_send(&(PeerSegment){.seq = PEER_ISS + 1u, .ack = iss + 6u, .port = 40003u, .flags = TCP_ACK});
    pass();
    // The ACK freed the segment of "world". The three segments of the fill went out behind it.
    uint32_t next = iss + 6u;
    while (sent_count() != 0u)
    {
        const EngineSegment segment = engine_sent();
        TEST_ASSERT_EQUAL_UINT32(next, segment.seq);
        next += segment.text_len;
    }
    TEST_ASSERT_EQUAL_UINT32(iss + 6u + ((PROTOCORE_TCP_ENGINE_SEGMENTS - 1u) * SEGMENT_TEXT), next);
    TEST_ASSERT_EQUAL_UINT(SEGMENT_TEXT, owner_write(fill));
    reset_and_release(40003u, PEER_ISS + 1u);
}

// RFC 9293 sec 3.10.7.4 eighth: a FIN from the peer is EVT_DISCONNECT. Then the owner closes. The FIN of the
// engine goes out after the octets in front of the close mark. The ACK of that FIN ends the TCB in LAST-ACK.
void test_a_close_from_the_peer_and_then_the_owner(void)
{
    const uint32_t iss = handshake(40004u);
    peer_send(&(PeerSegment){.seq = PEER_ISS + 1u, .ack = iss + 1u, .port = 40004u, .flags = TCP_ACK | TCP_FIN});
    pass();
    (void)event_expect(EVT_DISCONNECT);
    TEST_ASSERT_EQUAL(1u, sent_count());
    const EngineSegment ack = engine_sent();
    TEST_ASSERT_EQUAL_UINT32(PEER_ISS + 2u, ack.ack);

    TEST_ASSERT_EQUAL_UINT(3u, owner_write("bye"));
    TEST_ASSERT_TRUE(owner_close());
    pass();
    TEST_ASSERT_EQUAL(2u, sent_count());
    const EngineSegment text = engine_sent();
    TEST_ASSERT_EQUAL_MEMORY("bye", text.text, 3u);
    const EngineSegment fin = engine_sent();
    TEST_ASSERT_EQUAL_HEX8(TCP_FIN | TCP_ACK, fin.flags);
    TEST_ASSERT_EQUAL_UINT32(iss + 4u, fin.seq);

    peer_send(&(PeerSegment){.seq = PEER_ISS + 2u, .ack = iss + 5u, .port = 40004u, .flags = TCP_ACK});
    pass();
    event_none();
    // The pool entry is free again: a new connection takes it.
    (void)handshake(40005u);
    reset_and_release(40005u, PEER_ISS + 1u);
}

// RFC 9293 sec 3.10.4 CLOSE from the owner first: ESTABLISHED to FIN-WAIT-1, the ACK of the FIN to FIN-WAIT-2,
// which frees the pool entry. The FIN of the peer then moves the TCB to TIME-WAIT, and 2 MSL later the TCB
// closes.
void test_a_close_from_the_owner_first_ends_in_time_wait(void)
{
    const uint32_t iss = handshake(40006u);
    TEST_ASSERT_TRUE(owner_close());
    pass();
    TEST_ASSERT_EQUAL(1u, sent_count());
    const EngineSegment fin = engine_sent();
    TEST_ASSERT_EQUAL_HEX8(TCP_FIN | TCP_ACK, fin.flags);
    TEST_ASSERT_EQUAL_UINT32(iss + 1u, fin.seq);
    peer_send(&(PeerSegment){.seq = PEER_ISS + 1u, .ack = iss + 2u, .port = 40006u, .flags = TCP_ACK | TCP_FIN});
    pass();
    TEST_ASSERT_EQUAL(1u, sent_count());
    const EngineSegment ack = engine_sent();
    TEST_ASSERT_EQUAL_UINT32(PEER_ISS + 2u, ack.ack);
    event_none();

    // TIME-WAIT keeps the TCB for 2 MSL, and then the TCB closes. A SYN from the same port then opens a new
    // connection.
    s_now_ms += 2u * IDEMIP_TCP_MSL_MS;
    pass();
    (void)handshake(40006u);
    reset_and_release(40006u, PEER_ISS + 1u);
}

// RFC 9293 sec 3.10.7.1: a segment to a port with no listener gets a reset. A SYN gets
// <SEQ=0><ACK=SEG.SEQ+SEG.LEN><CTL=RST,ACK>, from the port that it went to.
void test_a_syn_to_a_closed_port_gets_a_reset(void)
{
    peer_send(&(PeerSegment){.seq = PEER_ISS, .port = 40007u, .to_port = 81u, .flags = TCP_SYN});
    pass();
    TEST_ASSERT_EQUAL(1u, sent_count());
    const EngineSegment reset = engine_sent();
    TEST_ASSERT_EQUAL_HEX8(TCP_RST | TCP_ACK, reset.flags);
    TEST_ASSERT_EQUAL_UINT32(0u, reset.seq);
    TEST_ASSERT_EQUAL_UINT32(PEER_ISS + 1u, reset.ack);
    TEST_ASSERT_EQUAL_UINT16(81u, reset.local_port);
    TEST_ASSERT_EQUAL_UINT16(40007u, reset.port);
    event_none();
}

// RFC 9293 sec 3.8.6.2.2: once the owner reads the receive ring, the engine opens the window again with an
// ACK. Until then the window shows the octets that the ring still holds.
void test_the_window_opens_once_the_owner_reads_the_ring(void)
{
    const uint32_t iss = handshake(40008u);
    char text[601];
    memset(text, 'r', sizeof text - 1u);
    text[sizeof text - 1u] = '\0';
    peer_send(&(PeerSegment){.seq = PEER_ISS + 1u, .ack = iss + 1u, .port = 40008u, .text = text,
                             .flags = TCP_ACK | TCP_PSH});
    pass();
    (void)event_expect(EVT_DATA);
    TEST_ASSERT_EQUAL(1u, sent_count());
    TEST_ASSERT_EQUAL_UINT16(WINDOW - 600u, engine_sent().window);
    pass();
    TEST_ASSERT_EQUAL_MESSAGE(0u, sent_count(), "the window opened before the owner read the ring");

    uint8_t read[600];
    TEST_ASSERT_EQUAL_UINT(600u, owner_read(read, sizeof read));
    pass();
    TEST_ASSERT_EQUAL(1u, sent_count());
    const EngineSegment update = engine_sent();
    TEST_ASSERT_EQUAL_HEX8(TCP_ACK, update.flags);
    TEST_ASSERT_EQUAL_UINT32(PEER_ISS + 601u, update.ack);
    TEST_ASSERT_EQUAL_UINT16(WINDOW, update.window);
    reset_and_release(40008u, PEER_ISS + 601u);
}

// A second SYN finds no free pool entry while entry 0 holds a connection. The TCB of that SYN closes, and no
// SYN,ACK answers it. The peer sends the SYN again later.
void test_a_syn_finds_no_free_slot_and_gets_no_answer(void)
{
    (void)handshake(40009u);
    peer_send(&(PeerSegment){.seq = PEER_ISS, .port = 40010u, .flags = TCP_SYN});
    pass();
    TEST_ASSERT_EQUAL(0u, sent_count());
    event_none();
    reset_and_release(40009u, PEER_ISS + 1u);
    // Pool entry 0 is free now, and the SYN that the peer sends again gets its SYN,ACK.
    (void)handshake(40010u);
    reset_and_release(40010u, PEER_ISS + 1u);
}

// RFC 9293 sec 3.10.7.4 SHLD-31: a segment past RCV.NXT waits in its receive descriptor. The segment in front
// of it then fills the gap. Both texts land in the receive ring in order, one ACK covers both, and no receive
// descriptor stays pinned.
void test_a_segment_past_rcv_nxt_waits_and_lands_after_the_gap(void)
{
    const uint32_t iss = handshake(40011u);
    peer_send(&(PeerSegment){.seq = PEER_ISS + 6u, .ack = iss + 1u, .port = 40011u, .text = "world",
                             .flags = TCP_ACK | TCP_PSH});
    pass();
    event_none();
    TEST_ASSERT_EQUAL(1u, sent_count());
    TEST_ASSERT_EQUAL_UINT32(PEER_ISS + 1u, engine_sent().ack);

    peer_send(&(PeerSegment){.seq = PEER_ISS + 1u, .ack = iss + 1u, .port = 40011u, .text = "hello",
                             .flags = TCP_ACK | TCP_PSH});
    pass();
    TEST_ASSERT_EQUAL_UINT(5u, event_expect(EVT_DATA).data_len);
    TEST_ASSERT_EQUAL_UINT(5u, event_expect(EVT_DATA).data_len);
    uint8_t read[16] = {0};
    TEST_ASSERT_EQUAL_UINT(10u, owner_read(read, sizeof read));
    TEST_ASSERT_EQUAL_MEMORY("helloworld", read, 10u);
    TEST_ASSERT_EQUAL(1u, sent_count());
    const EngineSegment ack = engine_sent();
    TEST_ASSERT_EQUAL_UINT32(PEER_ISS + 11u, ack.ack);
    TEST_ASSERT_EQUAL_UINT16(WINDOW - 10u, ack.window);

    uint8_t *const dma = protocore_stack_units(protocore_stack_span())->dma[0];
    Dma.pinned(dma);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0u, IDEMIP_DMA_IO(dma)->pinned, "a receive descriptor stayed pinned");
    reset_and_release(40011u, PEER_ISS + 11u);
}
