// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/transport/udp/client/client.h"
#include "network_drivers/transport/udp/server/server.h"
#include "shared/ip/ip.h"

#include "protocore_net_pcap.h"

#include <string.h>

#include <unity.h>

static uint8_t ip_work[16]; // the borrow an entry takes; Ip never reads it

static const protocore_ip *addr(const char *s)
{
    static protocore_ip a;
    a = (protocore_ip){PROTOCORE_IP_NONE, {0}};
    Ip.args.text = s;
    Ip.args.out = &a;
    Ip.parse(ip_work);
    return &a;
}

#define PORT 5353

static int g_calls;
static uint8_t g_buf[8][64];
static size_t g_len[8];
static uint16_t g_peer_port[8];
static char g_peer_ip[8][48];

static uint8_t g_pcap[16384];

void setUp()
{
    protocore_net_host_udp_reset();
    g_calls = 0;
    memset(g_len, 0, sizeof(g_len));
}
void tearDown()
{
}

static void on_dgram(const uint8_t *d, size_t n, const struct protocore_udp_peer *peer, void *ctx)
{
    (void)ctx;
    if (g_calls < 8)
    {
        size_t c = n < sizeof(g_buf[0]) ? n : sizeof(g_buf[0]);
        memcpy(g_buf[g_calls], d, c);
        g_len[g_calls] = n;
        UdpListener.peer_args.peer = peer;
        UdpListener.peer_args.ip_out = g_peer_ip[g_calls];
        UdpListener.peer_args.ip_cap = sizeof(g_peer_ip[0]);
        UdpListener.peer_args.port_out = &g_peer_port[g_calls];
        UdpListener.peer_addr(protocore_udp_listener_span());
    }
    g_calls++;
}

static void ensure_listening(void)
{
    static int open = 0;
    if (!open)
    {
        UdpListener.port = PORT;
        UdpListener.bind.handler = on_dgram;
        UdpListener.bind.handler_ctx = NULL;
        UdpListener.listen(protocore_udp_listener_span());
        TEST_ASSERT_TRUE(UdpListener.ok);
        open = 1;
    }
}

static void deliver(const char *src_ip, uint16_t src_port, const uint8_t *data, uint16_t len)
{
    TEST_ASSERT_TRUE(protocore_net_host_udp_deliver(PORT, src_ip, src_port, (void *)(uintptr_t)data, len));
}

void test_listener_delivers_in_order_with_boundaries()
{
    ensure_listening();
    const uint8_t a[] = {1, 2, 3};
    const uint8_t b[] = {9, 8, 7, 6, 5};
    deliver("10.0.0.5", 1234, a, sizeof(a));
    deliver("10.0.0.6", 4321, b, sizeof(b));
    UdpListener.poll(protocore_udp_listener_span());

    TEST_ASSERT_EQUAL_INT(2, g_calls);
    TEST_ASSERT_EQUAL_UINT(3, g_len[0]);
    TEST_ASSERT_EQUAL_MEMORY(a, g_buf[0], 3);
    TEST_ASSERT_EQUAL_UINT(5, g_len[1]);
    TEST_ASSERT_EQUAL_MEMORY(b, g_buf[1], 5);
    TEST_ASSERT_EQUAL_UINT16(1234, g_peer_port[0]);
    TEST_ASSERT_EQUAL_UINT16(4321, g_peer_port[1]);
    TEST_ASSERT_EQUAL_STRING("10.0.0.5", g_peer_ip[0]);
    TEST_ASSERT_EQUAL_STRING("10.0.0.6", g_peer_ip[1]);

    g_calls = 0;
    UdpListener.poll(protocore_udp_listener_span());
    TEST_ASSERT_EQUAL_INT(0, g_calls);
}

void test_listener_peer_carries_v6()
{
    ensure_listening();
    protocore_udp_pcb *p = protocore_net_host_udp_pcb(PORT);
    TEST_ASSERT_NOT_NULL(p);

    protocore_net_ip src;
    memset(&src, 0, sizeof(src));
    protocore_net_ip6_mark(&src);
    protocore_ip parsed;
    memset(&parsed, 0, sizeof(parsed));
    Ip.args.text = "2001:db8::dead:beef";
    Ip.args.out = &parsed;
    Ip.parse(ip_work);
    TEST_ASSERT_TRUE(Ip.ok);
    memcpy(protocore_net_ip6_wbytes(&src), parsed.bytes, 16);

    const uint8_t d[] = {0xEE};
    protocore_pbuf b;
    memset(&b, 0, sizeof(b));
    b.payload = (void *)(uintptr_t)d;
    b.len = 1;
    b.tot_len = 1;
    p->on_recv(p->arg, p, &b, &src, 7777);
    UdpListener.poll(protocore_udp_listener_span());

    TEST_ASSERT_EQUAL_INT(1, g_calls);
    TEST_ASSERT_EQUAL_UINT16(7777, g_peer_port[0]);
    TEST_ASSERT_EQUAL_STRING("2001:db8::dead:beef", g_peer_ip[0]);
}

void test_listener_sends_from_a_bound_port()
{
    ensure_listening();

    uint8_t pay[64];
    memset(pay, 0xAA, sizeof(pay));

    for (size_t i = 1; i <= 8; i++)
    {
        UdpListener.port = PORT;
        UdpListener.send_args.dst = addr("10.0.0.9");
        UdpListener.send_args.dst_port = 99;
        UdpListener.send_args.data = pay;
        UdpListener.send_args.len = sizeof(pay);
        UdpListener.sendto(protocore_udp_listener_span());
        TEST_ASSERT_TRUE(UdpListener.ok);
        TEST_ASSERT_EQUAL_UINT(i, protocore_net_host_udp_sent());
    }

    UdpListener.port = PORT + 1;
    UdpListener.send_args.dst = addr("10.0.0.9");
    UdpListener.send_args.dst_port = 99;
    UdpListener.send_args.data = pay;
    UdpListener.send_args.len = sizeof(pay);
    UdpListener.sendto(protocore_udp_listener_span());
    TEST_ASSERT_FALSE(UdpListener.ok);
    TEST_ASSERT_EQUAL_UINT(8, protocore_net_host_udp_sent());
}

void test_client_refuses_a_malformed_address_without_sending()
{
    uint8_t pay[8];
    memset(pay, 0x11, sizeof(pay));
    protocore_ip none = {PROTOCORE_IP_NONE, {0}};
    UdpClient.dst = &none;
    UdpClient.dst_port = 99;
    UdpClient.data = pay;
    UdpClient.len = sizeof(pay);
    UdpClient.sendto(protocore_udp_client_span());
    TEST_ASSERT_FALSE(UdpClient.ok);
    UdpClient.dst = NULL;
    UdpClient.dst_port = 99;
    UdpClient.data = pay;
    UdpClient.len = sizeof(pay);
    UdpClient.sendto(protocore_udp_client_span());
    TEST_ASSERT_FALSE(UdpClient.ok);
    TEST_ASSERT_EQUAL_UINT(0, protocore_net_host_udp_sent());
}

void test_client_sends_both_families()
{
    uint8_t pay[64];
    memset(pay, 0xBB, sizeof(pay));

    UdpClient.dst = addr("10.0.0.9");
    UdpClient.dst_port = 99;
    UdpClient.data = pay;
    UdpClient.len = sizeof(pay);
    UdpClient.sendto(protocore_udp_client_span());
    TEST_ASSERT_TRUE(UdpClient.ok);
    UdpClient.dst = addr("2001:db8::1");
    UdpClient.dst_port = 99;
    UdpClient.data = pay;
    UdpClient.len = sizeof(pay);
    UdpClient.sendto(protocore_udp_client_span());
    TEST_ASSERT_TRUE(UdpClient.ok);
    TEST_ASSERT_EQUAL_UINT(2, protocore_net_host_udp_count());
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_NET_TYPE_V4, protocore_net_host_udp_at(0)->type);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_NET_TYPE_V6, protocore_net_host_udp_at(1)->type);
}

void test_a_spent_pbuf_pool_drops_the_datagram()
{
    uint8_t pay[8];
    memset(pay, 0x22, sizeof(pay));

    mock_pbuf_fail_once();
    UdpClient.dst = addr("10.0.0.9");
    UdpClient.dst_port = 99;
    UdpClient.data = pay;
    UdpClient.len = sizeof(pay);
    UdpClient.sendto(protocore_udp_client_span());
    TEST_ASSERT_FALSE(UdpClient.ok);
    TEST_ASSERT_EQUAL_UINT(0, protocore_net_host_udp_sent());

    UdpClient.dst = addr("10.0.0.9");
    UdpClient.dst_port = 99;
    UdpClient.data = pay;
    UdpClient.len = sizeof(pay);
    UdpClient.sendto(protocore_udp_client_span());
    TEST_ASSERT_TRUE(UdpClient.ok);
    TEST_ASSERT_EQUAL_UINT(1, protocore_net_host_udp_sent());
}

void test_every_send_returns_its_pbuf()
{
    uint8_t pay[8];
    memset(pay, 0x33, sizeof(pay));
    for (int i = 0; i < PROTOCORE_NET_HOST_PBUFS * 3; i++)
    {
        UdpClient.dst = addr("10.0.0.9");
        UdpClient.dst_port = 99;
        UdpClient.data = pay;
        UdpClient.len = sizeof(pay);
        UdpClient.sendto(protocore_udp_client_span());
        TEST_ASSERT_TRUE(UdpClient.ok);
    }
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_NET_HOST_PBUFS * 3, protocore_net_host_udp_sent());

    TEST_ASSERT_NOT_NULL(protocore_net_pbuf_alloc(PROTOCORE_NET_PBUF_TRANSPORT, 8, PROTOCORE_NET_PBUF_RAM));
}

void test_capture_renders_a_v4_datagram()
{
    uint8_t pay[64];
    for (int i = 0; i < 64; i++)
    {
        pay[i] = (uint8_t)(0x40 + i);
    }
    UdpClient.dst = addr("10.0.0.9");
    UdpClient.dst_port = 9999;
    UdpClient.data = pay;
    UdpClient.len = sizeof(pay);
    UdpClient.sendto(protocore_udp_client_span());
    TEST_ASSERT_TRUE(UdpClient.ok);
    TEST_ASSERT_EQUAL_UINT(1, protocore_net_host_udp_count());

    size_t n = protocore_net_pcap_render(g_pcap, sizeof(g_pcap));
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_PCAP_GLOBAL_HDR_LEN + PROTOCORE_PCAP_REC_HDR_LEN + 20 + 8 + 64, n);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_DLT_RAW, g_pcap[20]);

    const uint8_t *ip = g_pcap + PROTOCORE_PCAP_GLOBAL_HDR_LEN + PROTOCORE_PCAP_REC_HDR_LEN;
    TEST_ASSERT_EQUAL_HEX8(0x45, ip[0]);
    TEST_ASSERT_EQUAL_UINT16(20 + 8 + 64, (ip[2] << 8) | ip[3]);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_NET_PCAP_TTL, ip[8]);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_NET_PCAP_PROTO_UDP, ip[9]);
    TEST_ASSERT_EQUAL_HEX8(10, ip[16]);
    TEST_ASSERT_EQUAL_HEX8(9, ip[19]);

    TEST_ASSERT_EQUAL_HEX16(0, protocore_net_pcap_fold(protocore_net_pcap_sum(0, ip, 20)));

    const uint8_t *udp = ip + 20;
    TEST_ASSERT_EQUAL_UINT16(9999, (udp[2] << 8) | udp[3]);
    TEST_ASSERT_EQUAL_UINT16(8 + 64, (udp[4] << 8) | udp[5]);
    TEST_ASSERT_EQUAL_MEMORY(pay, udp + 8, 64);
}

void test_capture_renders_a_v6_datagram()
{
    uint8_t pay[17];
    memset(pay, 0x5A, sizeof(pay));
    UdpClient.dst = addr("2001:db8::1");
    UdpClient.dst_port = 5353;
    UdpClient.data = pay;
    UdpClient.len = sizeof(pay);
    UdpClient.sendto(protocore_udp_client_span());
    TEST_ASSERT_TRUE(UdpClient.ok);

    size_t n = protocore_net_pcap_render(g_pcap, sizeof(g_pcap));
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_PCAP_GLOBAL_HDR_LEN + PROTOCORE_PCAP_REC_HDR_LEN + 40 + 8 + 17, n);

    const uint8_t *ip = g_pcap + PROTOCORE_PCAP_GLOBAL_HDR_LEN + PROTOCORE_PCAP_REC_HDR_LEN;
    TEST_ASSERT_EQUAL_HEX8(0x60, ip[0] & 0xF0);
    TEST_ASSERT_EQUAL_UINT16(8 + 17, (ip[4] << 8) | ip[5]);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_NET_PCAP_PROTO_UDP, ip[6]);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_NET_PCAP_TTL, ip[7]);
    TEST_ASSERT_EQUAL_HEX8(0x20, ip[24]);
    TEST_ASSERT_EQUAL_HEX8(0x01, ip[24 + 15]);

    const uint8_t *udp = ip + 40;
    TEST_ASSERT_EQUAL_UINT16(5353, (udp[2] << 8) | udp[3]);
    TEST_ASSERT_TRUE(((udp[6] << 8) | udp[7]) != 0);
    TEST_ASSERT_EQUAL_MEMORY(pay, udp + 8, 17);
}

void test_capture_refuses_a_buffer_that_cannot_hold_it()
{
    uint8_t pay[8];
    memset(pay, 0x44, sizeof(pay));
    UdpClient.dst = addr("10.0.0.9");
    UdpClient.dst_port = 99;
    UdpClient.data = pay;
    UdpClient.len = sizeof(pay);
    UdpClient.sendto(protocore_udp_client_span());
    TEST_ASSERT_TRUE(UdpClient.ok);
    TEST_ASSERT_EQUAL_UINT(0, protocore_net_pcap_render(g_pcap, PROTOCORE_PCAP_GLOBAL_HDR_LEN));
    TEST_ASSERT_EQUAL_UINT(0, protocore_net_pcap_render(g_pcap, 8));
}

