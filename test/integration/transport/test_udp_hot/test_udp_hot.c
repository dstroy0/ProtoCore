// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The UDP target path, run on the host. The env (native_udp_hot) builds with PROTOCORE_HOT_FORCE,
// so pc_platform.h selects PC_VENDOR_MOCK and PROTOCORE_HOT is 1: these are the same lines that
// ship to silicon, driven against test/mocks/pc_net_host.h instead of lwIP.
//
// What it covers: the listener's RX ring and its send ring, the client's send ring, NetAddr
// carrying v4 and v6 in both directions, the pbuf pool refusing, and every datagram the core
// handed to the stack read back as a DLT_RAW libpcap capture (test/mocks/pc_net_pcap.h) whose
// IP and UDP headers are checked field by field.

#include "network_drivers/transport/udp/udp_client.h"
#include "network_drivers/transport/udp/udp_datagram.h" // PC_UDP_DGRAM_HDR: the ring entry width
#include "network_drivers/transport/udp/udp_listener.h"
#include "shared_primitives/ip.h" // Ip.parse, for the v6 address the mock cannot spell

#include "pc_net_pcap.h"

#include <string.h>

#include <unity.h>

// The sends take an address; a test spelling a literal turns it into one here.
static const pc_ip *addr(const char *s)
{
    static pc_ip a;
    a = (pc_ip){PC_IP_NONE, {0}};
    Ip.parse(s, &a);
    return &a;
}

#if !PROTOCORE_HOT
#error "native_udp_hot must build the target path: check base native_hot_base in test_matrix.json"
#endif

#define PORT 5353

static int g_calls;
static uint8_t g_buf[8][64];
static size_t g_len[8];
static uint16_t g_peer_port[8];
static char g_peer_ip[8][48];

static uint8_t g_pcap[16384];

// Only the send log and the pbuf pool are cleared between tests. The pcb table is not: the
// listener holds the pcb it bound, and there is no unlisten on the target path.
void setUp()
{
    pc_net_host_udp_reset();
    g_calls = 0;
    memset(g_len, 0, sizeof(g_len));
}
void tearDown()
{
}

static void on_dgram(const uint8_t *d, size_t n, const struct pc_udp_peer *peer, void *ctx)
{
    (void)ctx;
    if (g_calls < 8)
    {
        size_t c = n < sizeof(g_buf[0]) ? n : sizeof(g_buf[0]);
        memcpy(g_buf[g_calls], d, c);
        g_len[g_calls] = n;
        UdpListener.peer_addr(peer, g_peer_ip[g_calls], sizeof(g_peer_ip[0]), &g_peer_port[g_calls]);
    }
    g_calls++;
}

// One listener for the whole suite: the port stays bound once it is open.
static void ensure_listening(void)
{
    static int open = 0;
    if (!open)
    {
        TEST_ASSERT_TRUE(UdpListener.listen(PORT, on_dgram, NULL));
        open = 1;
    }
}

// The bound pcb the listener opened, found the way a stack would reach it.
static pc_udp_pcb *bound_pcb(uint16_t port)
{
    for (int i = 0; i < PC_NET_HOST_PCBS; i++)
    {
        if (pc_net_host_udp_pcbs[i].in_use && pc_net_host_udp_pcbs[i].local_port == port)
        {
            return &pc_net_host_udp_pcbs[i];
        }
    }
    return NULL;
}

// Deliver one datagram through the recv callback, which is what lwIP calls on the target.
static void deliver(const char *src_ip, uint16_t src_port, const uint8_t *data, uint16_t len)
{
    pc_udp_pcb *p = bound_pcb(PORT);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_NOT_NULL(p->on_recv);
    pc_net_ip src;
    memset(&src, 0, sizeof(src));
    TEST_ASSERT_TRUE(pc_net_ip_parse(src_ip, &src));
    pc_pbuf b;
    memset(&b, 0, sizeof(b));
    b.payload = (void *)(uintptr_t)data;
    b.len = len;
    b.tot_len = len;
    p->on_recv(p->arg, p, &b, &src, src_port);
}

void test_listener_delivers_in_order_with_boundaries()
{
    ensure_listening();
    const uint8_t a[] = {1, 2, 3};
    const uint8_t b[] = {9, 8, 7, 6, 5};
    deliver("10.0.0.5", 1234, a, sizeof(a));
    deliver("10.0.0.6", 4321, b, sizeof(b));
    UdpListener.poll();

    TEST_ASSERT_EQUAL_INT(2, g_calls);
    TEST_ASSERT_EQUAL_UINT(3, g_len[0]);
    TEST_ASSERT_EQUAL_MEMORY(a, g_buf[0], 3);
    TEST_ASSERT_EQUAL_UINT(5, g_len[1]);
    TEST_ASSERT_EQUAL_MEMORY(b, g_buf[1], 5);
    TEST_ASSERT_EQUAL_UINT16(1234, g_peer_port[0]);
    TEST_ASSERT_EQUAL_UINT16(4321, g_peer_port[1]);
    TEST_ASSERT_EQUAL_STRING("10.0.0.5", g_peer_ip[0]);
    TEST_ASSERT_EQUAL_STRING("10.0.0.6", g_peer_ip[1]);

    // Nothing queued: a second poll delivers nothing.
    g_calls = 0;
    UdpListener.poll();
    TEST_ASSERT_EQUAL_INT(0, g_calls);
}

void test_listener_peer_carries_v6()
{
    ensure_listening();
    pc_udp_pcb *p = bound_pcb(PORT);
    TEST_ASSERT_NOT_NULL(p);

    // A v6 source the mock's dotted-quad parser cannot build, so it is set through the v6 seam.
    pc_net_ip src;
    memset(&src, 0, sizeof(src));
    pc_net_ip6_mark(&src);
    pc_ip parsed;
    memset(&parsed, 0, sizeof(parsed));
    TEST_ASSERT_TRUE(Ip.parse("2001:db8::dead:beef", &parsed));
    memcpy(pc_net_ip6_wbytes(&src), parsed.bytes, 16);

    const uint8_t d[] = {0xEE};
    pc_pbuf b;
    memset(&b, 0, sizeof(b));
    b.payload = (void *)(uintptr_t)d;
    b.len = 1;
    b.tot_len = 1;
    p->on_recv(p->arg, p, &b, &src, 7777);
    UdpListener.poll();

    TEST_ASSERT_EQUAL_INT(1, g_calls);
    TEST_ASSERT_EQUAL_UINT16(7777, g_peer_port[0]);
    TEST_ASSERT_EQUAL_STRING("2001:db8::dead:beef", g_peer_ip[0]);
}

void test_listener_send_ring_fills_and_drains()
{
    ensure_listening();
    size_t before = UdpListener.sndbuf(PORT);
    TEST_ASSERT_GREATER_THAN_UINT(0, before);

    uint8_t pay[64];
    memset(pay, 0xAA, sizeof(pay));
    TEST_ASSERT_TRUE(UdpListener.sendto(PORT, addr("10.0.0.9"), 99, pay, sizeof(pay)));
    TEST_ASSERT_EQUAL_UINT(before - (PC_UDP_DGRAM_HDR + sizeof(pay)), UdpListener.sndbuf(PORT));

    int queued = 1;
    while (UdpListener.sendto(PORT, addr("10.0.0.9"), 99, pay, sizeof(pay)))
    {
        queued++;
        TEST_ASSERT_LESS_THAN_INT(4096, queued);
    }
    TEST_ASSERT_LESS_THAN_UINT(sizeof(pay), UdpListener.sndbuf(PORT));

    UdpListener.poll();
    TEST_ASSERT_EQUAL_UINT(before, UdpListener.sndbuf(PORT));
    TEST_ASSERT_EQUAL_UINT((size_t)queued, pc_net_host_udp_sent());
}

void test_client_refuses_a_malformed_address_without_touching_the_ring()
{
    size_t before = UdpClient.sndbuf();
    uint8_t pay[8];
    memset(pay, 0x11, sizeof(pay));
    pc_ip none = {PC_IP_NONE, {0}};
    TEST_ASSERT_FALSE(UdpClient.sendto(&none, 99, pay, sizeof(pay))); // an address it never got
    TEST_ASSERT_FALSE(UdpClient.sendto(NULL, 99, pay, sizeof(pay)));
    TEST_ASSERT_EQUAL_UINT(before, UdpClient.sndbuf());
    UdpClient.poll();
    TEST_ASSERT_EQUAL_UINT(0, pc_net_host_udp_sent());
}

void test_client_send_ring_accounts_both_families()
{
    size_t before = UdpClient.sndbuf();
    uint8_t pay[64];
    memset(pay, 0xBB, sizeof(pay));
    TEST_ASSERT_TRUE(UdpClient.sendto(addr("10.0.0.9"), 99, pay, sizeof(pay)));
    TEST_ASSERT_TRUE(UdpClient.sendto(addr("2001:db8::1"), 99, pay, sizeof(pay)));
    // The entry is the same width for either family: the address travels as sixteen bytes.
    TEST_ASSERT_EQUAL_UINT(before - 2u * (PC_UDP_DGRAM_HDR + sizeof(pay)), UdpClient.sndbuf());

    UdpClient.poll();
    TEST_ASSERT_EQUAL_UINT(before, UdpClient.sndbuf());
    TEST_ASSERT_EQUAL_UINT(2, pc_net_host_udp_count());
    TEST_ASSERT_EQUAL_UINT8(PC_NET_TYPE_V4, pc_net_host_udp_at(0)->type);
    TEST_ASSERT_EQUAL_UINT8(PC_NET_TYPE_V6, pc_net_host_udp_at(1)->type);
}

void test_a_spent_pbuf_pool_drops_the_datagram()
{
    uint8_t pay[8];
    memset(pay, 0x22, sizeof(pay));
    TEST_ASSERT_TRUE(UdpClient.sendto(addr("10.0.0.9"), 99, pay, sizeof(pay)));
    mock_pbuf_fail_once();
    UdpClient.poll();
    // The ring still drained; the stack never saw it.
    TEST_ASSERT_EQUAL_UINT(0, pc_net_host_udp_sent());

    // The pool is available again, so the next send reaches the stack.
    TEST_ASSERT_TRUE(UdpClient.sendto(addr("10.0.0.9"), 99, pay, sizeof(pay)));
    UdpClient.poll();
    TEST_ASSERT_EQUAL_UINT(1, pc_net_host_udp_sent());
}

void test_every_send_returns_its_pbuf()
{
    uint8_t pay[8];
    memset(pay, 0x33, sizeof(pay));
    for (int i = 0; i < PC_NET_HOST_PBUFS * 3; i++)
    {
        TEST_ASSERT_TRUE(UdpClient.sendto(addr("10.0.0.9"), 99, pay, sizeof(pay)));
        UdpClient.poll();
    }
    TEST_ASSERT_EQUAL_UINT(PC_NET_HOST_PBUFS * 3, pc_net_host_udp_sent());
    // A leaked pbuf would have spent the pool by now.
    TEST_ASSERT_NOT_NULL(pc_net_pbuf_alloc(PC_NET_PBUF_TRANSPORT, 8, PC_NET_PBUF_RAM));
}

void test_capture_renders_a_v4_datagram()
{
    uint8_t pay[64];
    for (int i = 0; i < 64; i++)
    {
        pay[i] = (uint8_t)(0x40 + i);
    }
    TEST_ASSERT_TRUE(UdpClient.sendto(addr("10.0.0.9"), 9999, pay, sizeof(pay)));
    UdpClient.poll();
    TEST_ASSERT_EQUAL_UINT(1, pc_net_host_udp_count());

    size_t n = pc_net_pcap_render(g_pcap, sizeof(g_pcap));
    TEST_ASSERT_EQUAL_UINT(PC_PCAP_GLOBAL_HDR_LEN + PC_PCAP_REC_HDR_LEN + 20 + 8 + 64, n);
    TEST_ASSERT_EQUAL_HEX8(PC_DLT_RAW, g_pcap[20]); // the link type the records are in

    const uint8_t *ip = g_pcap + PC_PCAP_GLOBAL_HDR_LEN + PC_PCAP_REC_HDR_LEN;
    TEST_ASSERT_EQUAL_HEX8(0x45, ip[0]);                         // IPv4, 5-word header
    TEST_ASSERT_EQUAL_UINT16(20 + 8 + 64, (ip[2] << 8) | ip[3]); // total length
    TEST_ASSERT_EQUAL_HEX8(PC_NET_PCAP_TTL, ip[8]);              // TTL
    TEST_ASSERT_EQUAL_HEX8(PC_NET_PCAP_PROTO_UDP, ip[9]);        // protocol 17
    TEST_ASSERT_EQUAL_HEX8(10, ip[16]);                          // destination 10.0.0.9
    TEST_ASSERT_EQUAL_HEX8(9, ip[19]);
    // A header carrying its own checksum folds to zero over the whole header.
    TEST_ASSERT_EQUAL_HEX16(0, pc_net_pcap_fold(pc_net_pcap_sum(0, ip, 20)));

    const uint8_t *udp = ip + 20;
    TEST_ASSERT_EQUAL_UINT16(9999, (udp[2] << 8) | udp[3]); // destination port
    TEST_ASSERT_EQUAL_UINT16(8 + 64, (udp[4] << 8) | udp[5]);
    TEST_ASSERT_EQUAL_MEMORY(pay, udp + 8, 64);
}

void test_capture_renders_a_v6_datagram()
{
    uint8_t pay[17]; // odd length: the checksum pads its tail
    memset(pay, 0x5A, sizeof(pay));
    TEST_ASSERT_TRUE(UdpClient.sendto(addr("2001:db8::1"), 5353, pay, sizeof(pay)));
    UdpClient.poll();

    size_t n = pc_net_pcap_render(g_pcap, sizeof(g_pcap));
    TEST_ASSERT_EQUAL_UINT(PC_PCAP_GLOBAL_HDR_LEN + PC_PCAP_REC_HDR_LEN + 40 + 8 + 17, n);

    const uint8_t *ip = g_pcap + PC_PCAP_GLOBAL_HDR_LEN + PC_PCAP_REC_HDR_LEN;
    TEST_ASSERT_EQUAL_HEX8(0x60, ip[0] & 0xF0);             // version 6
    TEST_ASSERT_EQUAL_UINT16(8 + 17, (ip[4] << 8) | ip[5]); // payload length
    TEST_ASSERT_EQUAL_HEX8(PC_NET_PCAP_PROTO_UDP, ip[6]);   // next header
    TEST_ASSERT_EQUAL_HEX8(PC_NET_PCAP_TTL, ip[7]);         // hop limit
    TEST_ASSERT_EQUAL_HEX8(0x20, ip[24]);                   // destination 2001:db8::1
    TEST_ASSERT_EQUAL_HEX8(0x01, ip[24 + 15]);

    const uint8_t *udp = ip + 40;
    TEST_ASSERT_EQUAL_UINT16(5353, (udp[2] << 8) | udp[3]);
    TEST_ASSERT_TRUE(((udp[6] << 8) | udp[7]) != 0); // v6 has no zero-checksum exemption
    TEST_ASSERT_EQUAL_MEMORY(pay, udp + 8, 17);
}

void test_capture_refuses_a_buffer_that_cannot_hold_it()
{
    uint8_t pay[8];
    memset(pay, 0x44, sizeof(pay));
    TEST_ASSERT_TRUE(UdpClient.sendto(addr("10.0.0.9"), 99, pay, sizeof(pay)));
    UdpClient.poll();
    TEST_ASSERT_EQUAL_UINT(0, pc_net_pcap_render(g_pcap, PC_PCAP_GLOBAL_HDR_LEN));
    TEST_ASSERT_EQUAL_UINT(0, pc_net_pcap_render(g_pcap, 8));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_listener_delivers_in_order_with_boundaries);
    RUN_TEST(test_listener_peer_carries_v6);
    RUN_TEST(test_listener_send_ring_fills_and_drains);
    RUN_TEST(test_client_refuses_a_malformed_address_without_touching_the_ring);
    RUN_TEST(test_client_send_ring_accounts_both_families);
    RUN_TEST(test_a_spent_pbuf_pool_drops_the_datagram);
    RUN_TEST(test_every_send_returns_its_pbuf);
    RUN_TEST(test_capture_renders_a_v4_datagram);
    RUN_TEST(test_capture_renders_a_v6_datagram);
    RUN_TEST(test_capture_refuses_a_buffer_that_cannot_hold_it);
    return UNITY_END();
}
