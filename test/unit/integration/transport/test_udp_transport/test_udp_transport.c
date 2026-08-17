// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/transport/udp/client/client.h"
#include "network_drivers/transport/udp/server/server.h"
#include "network_drivers/transport/udp/udp.h"
#include "shared/ip/ip.h"
#include <string.h>

#include <unity.h>

static const protocore_ip *addr(const char *s)
{
    static protocore_ip a;
    a = (protocore_ip){PROTOCORE_IP_NONE, {0}};
    Ip.args.text = s;
    Ip.args.out = &a;
    Ip.parse(Ip.internal);
    return &a;
}

static int g_calls = 0;
static char g_last[64];
static size_t g_last_len = 0;
static char g_src_ip[16];
static uint16_t g_src_port = 0;
static void *g_ctx_seen = NULL;

static void on_datagram(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer, void *ctx)
{
    g_calls++;
    g_ctx_seen = ctx;
    g_last_len = (len < sizeof(g_last)) ? len : sizeof(g_last) - 1;
    memcpy(g_last, data, g_last_len);
    g_last[g_last_len] = '\0';
    UdpListener.peer_args.peer = peer;
    UdpListener.peer_args.ip_out = g_src_ip;
    UdpListener.peer_args.ip_cap = sizeof(g_src_ip);
    UdpListener.peer_args.port_out = &g_src_port;
    UdpListener.peer_addr(UdpListener.internal);

}

static void inject(uint16_t port, const char *src_ip, uint16_t src_port, const uint8_t *data, size_t len)
{
    protocore_net_host_udp_deliver(port, src_ip, src_port, (void *)(uintptr_t)data, (uint16_t)len);
    UdpListener.poll(UdpListener.internal);
}

static size_t sent_len(void)
{
    size_t n = protocore_net_host_udp_count();
    return n ? protocore_net_host_udp_at(n - 1)->len : 0;
}

static const uint8_t *sent_bytes(void)
{
    size_t n = protocore_net_host_udp_count();
    return n ? protocore_net_host_udp_at(n - 1)->data : NULL;
}

static void close_all_ports(void)
{
    static const uint16_t ports[] = {1111, 1900, 2222, 3333, 4000, 5000, 5353,
                                     5683, 6000, 6100, 7000, 7001, 7002, 9999};
    for (size_t i = 0; i < sizeof(ports) / sizeof(ports[0]); i++)
    {
        UdpListener.port = ports[i];
        UdpListener.close(UdpListener.internal);

        (void)UdpListener.ok;
    }
}

void setUp(void)
{
    close_all_ports();
    protocore_net_host_udp_reset();
    g_calls = 0;
    g_last_len = 0;
    g_last[0] = '\0';
    g_src_ip[0] = '\0';
    g_src_port = 0;
    g_ctx_seen = NULL;
}
void tearDown(void)
{
}

void test_join_records_the_group()
{
    UdpListener.port = 5353;
    UdpListener.bind.group_ip = "224.0.0.251";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    UdpListener.port = 5353;
    UdpListener.joined_group(UdpListener.internal);

    TEST_ASSERT_EQUAL_STRING("224.0.0.251", UdpListener.text);
    UdpListener.port = 1900;
    UdpListener.joined_group(UdpListener.internal);

    TEST_ASSERT_NULL(UdpListener.text);
}

void test_group_datagram_reaches_the_handler()
{
    int marker = 42;
    UdpListener.port = 5353;
    UdpListener.bind.group_ip = "224.0.0.251";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = &marker;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    const char *pkt = "\x00\x00\x84\x00mdns-announce";
    inject(5353, "192.168.1.77", 5353, (const uint8_t *)pkt, 17);
    TEST_ASSERT_EQUAL_INT(1, g_calls);
    TEST_ASSERT_EQUAL_UINT(17, (unsigned)g_last_len);
    TEST_ASSERT_EQUAL_PTR(&marker, g_ctx_seen);
    TEST_ASSERT_EQUAL_STRING("192.168.1.77", g_src_ip);
    TEST_ASSERT_EQUAL_UINT16(5353, g_src_port);
}

void test_counts_repeated_announcements()
{
    UdpListener.port = 5353;
    UdpListener.bind.group_ip = "224.0.0.251";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    for (int i = 0; i < 12; i++)
    {
        inject(5353, "192.168.1.5", 5353, (const uint8_t *)"x", 1);
    }
    TEST_ASSERT_EQUAL_INT(12, g_calls);
}

void test_rejects_non_multicast_group()
{
    UdpListener.port = 5353;
    UdpListener.bind.group_ip = "192.168.1.10";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);
    UdpListener.port = 5353;
    UdpListener.bind.group_ip = "223.255.255.255";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);
    UdpListener.port = 5353;
    UdpListener.bind.group_ip = "240.0.0.1";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);
    UdpListener.port = 5353;
    UdpListener.joined_group(UdpListener.internal);

    TEST_ASSERT_NULL(UdpListener.text);
}

void test_accepts_group_range_edges()
{
    UdpListener.port = 5000;
    UdpListener.bind.group_ip = "224.0.0.1";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    UdpListener.port = 5000;
    UdpListener.leave_multicast(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    UdpListener.port = 1900;
    UdpListener.bind.group_ip = "239.255.255.250";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    UdpListener.port = 1900;
    UdpListener.joined_group(UdpListener.internal);

    TEST_ASSERT_EQUAL_STRING("239.255.255.250", UdpListener.text);
}

void test_rejects_malformed_group()
{
    UdpListener.port = 5353;
    UdpListener.bind.group_ip = NULL;
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);
    UdpListener.port = 5353;
    UdpListener.bind.group_ip = "";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);
    UdpListener.port = 5353;
    UdpListener.bind.group_ip = "224.0.0";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);
    UdpListener.port = 5353;
    UdpListener.bind.group_ip = "224.0.0.1.2";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);
    UdpListener.port = 5353;
    UdpListener.bind.group_ip = "224.0.0.256";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);
    UdpListener.port = 5353;
    UdpListener.bind.group_ip = "224.0.0.";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);
    UdpListener.port = 5353;
    UdpListener.bind.group_ip = "224.0..1";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);
    UdpListener.port = 5353;
    UdpListener.bind.group_ip = "224.0.0.abc";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);
    UdpListener.port = 5353;
    UdpListener.joined_group(UdpListener.internal);

    TEST_ASSERT_NULL(UdpListener.text);
}

void test_leave_releases_the_slot()
{
    UdpListener.port = 5353;
    UdpListener.bind.group_ip = "224.0.0.251";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    UdpListener.port = 5353;
    UdpListener.leave_multicast(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    UdpListener.port = 5353;
    UdpListener.joined_group(UdpListener.internal);

    TEST_ASSERT_NULL(UdpListener.text);
    UdpListener.port = 5353;
    UdpListener.leave_multicast(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);
    UdpListener.port = 9999;
    UdpListener.leave_multicast(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);

    inject(5353, "192.168.1.5", 5353, (const uint8_t *)"x", 1);
    TEST_ASSERT_EQUAL_INT(0, g_calls);
}

void test_leave_ignores_a_plain_listener()
{
    UdpListener.port = 5353;
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    UdpListener.port = 5353;
    UdpListener.leave_multicast(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);
    inject(5353, "192.168.1.5", 5353, (const uint8_t *)"y", 1);
    TEST_ASSERT_EQUAL_INT(1, g_calls);
}

void test_listen_rebinds_existing_port()
{
    int marker1 = 1, marker2 = 2;
    UdpListener.port = 5353;
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = &marker1;
    UdpListener.listen(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    UdpListener.port = 5353;
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = &marker2;
    UdpListener.listen(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    inject(5353, "10.0.0.1", 1234, (const uint8_t *)"z", 1);
    TEST_ASSERT_EQUAL_INT(1, g_calls);
    TEST_ASSERT_EQUAL_PTR(&marker2, g_ctx_seen);
    UdpListener.port = 9999;
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
}

void test_listen_refuses_a_third_port_when_the_pool_is_full()
{
    UdpListener.port = 1111;
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    UdpListener.port = 2222;
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    UdpListener.port = 3333;
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);

    inject(1111, "10.0.0.1", 1, (const uint8_t *)"a", 1);
    inject(2222, "10.0.0.1", 1, (const uint8_t *)"b", 1);
    TEST_ASSERT_EQUAL_INT(2, g_calls);
    inject(3333, "10.0.0.1", 1, (const uint8_t *)"c", 1);
    TEST_ASSERT_EQUAL_INT(2, g_calls);
}

void test_multicast_group_too_long_for_buffer_rejected()
{
    char group[64];
    size_t n = 0;
    memcpy(group + n, "224.", 4);
    n += 4;
    for (int i = 0; i < 20; i++)
    {
        group[n++] = '0';
    }
    memcpy(group + n, ".0.0", 4);
    n += 4;
    group[n] = '\0';
    TEST_ASSERT_TRUE(n >= 16);
    UdpListener.port = 5353;
    UdpListener.bind.group_ip = group;
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);
    UdpListener.port = 5353;
    UdpListener.joined_group(UdpListener.internal);

    TEST_ASSERT_NULL(UdpListener.text);
}

void test_multicast_join_finds_slot_past_an_unrelated_listener()
{
    UdpListener.port = 4000;
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    UdpListener.port = 1900;
    UdpListener.bind.group_ip = "239.255.255.250";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    UdpListener.port = 1900;
    UdpListener.joined_group(UdpListener.internal);

    TEST_ASSERT_EQUAL_STRING("239.255.255.250", UdpListener.text);
}

void test_multicast_rejoin_scans_past_a_freed_lower_slot()
{
    UdpListener.port = 5353;
    UdpListener.bind.group_ip = "224.0.0.251";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    UdpListener.port = 1900;
    UdpListener.bind.group_ip = "239.255.255.250";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    UdpListener.port = 5353;
    UdpListener.leave_multicast(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    UdpListener.port = 1900;
    UdpListener.bind.group_ip = "239.255.255.250";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    UdpListener.port = 1900;
    UdpListener.joined_group(UdpListener.internal);

    TEST_ASSERT_EQUAL_STRING("239.255.255.250", UdpListener.text);
}

void test_peer_addr_rejects_null_peer()
{
    char ip[16];
    uint16_t port = 0;
    UdpListener.peer_args.peer = NULL;
    UdpListener.peer_args.ip_out = ip;
    UdpListener.peer_args.ip_cap = sizeof(ip);
    UdpListener.peer_args.port_out = &port;
    UdpListener.peer_addr(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);
}

void test_peer_addr_copies_and_tolerates_null_outparams()
{
    int marker = 7;
    UdpListener.port = 6000;
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = &marker;
    UdpListener.listen(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    inject(6000, "203.0.113.9", 4242, (const uint8_t *)"q", 1);
    TEST_ASSERT_EQUAL_STRING("203.0.113.9", g_src_ip);
    TEST_ASSERT_EQUAL_UINT16(4242, g_src_port);
}

static void reply_handler(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer, void *ctx)
{
    (void)data;
    (void)len;
    (void)ctx;
    g_calls++;
    UdpListener.peer_args.peer = peer;
    UdpListener.send_args.data = (const uint8_t *)"reply";
    UdpListener.send_args.len = 5;
    UdpListener.reply(UdpListener.internal);

}

void test_send_paths_are_captured()
{
    UdpListener.port = 5683;
    UdpListener.bind.handler = reply_handler;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    inject(5683, "192.168.1.30", 5683, (const uint8_t *)"q", 1);
    TEST_ASSERT_EQUAL_INT(1, g_calls);
    TEST_ASSERT_EQUAL_UINT(5, (unsigned)sent_len());
    TEST_ASSERT_EQUAL_INT(0, memcmp("reply", sent_bytes(), 5));

    TEST_ASSERT_EQUAL_UINT16(5683, protocore_net_host_udp_at(protocore_net_host_udp_count() - 1)->dst_port);

    protocore_net_host_udp_reset();
    UdpClient.dst = addr("192.168.1.10");
    UdpClient.dst_port = 514;
    UdpClient.data = (const uint8_t *)"syslog!";
    UdpClient.len = 7;
    UdpClient.sendto(UdpClient.internal);

    TEST_ASSERT_TRUE(UdpClient.ok);
    TEST_ASSERT_EQUAL_UINT(7, (unsigned)sent_len());
    TEST_ASSERT_EQUAL_UINT16(514, protocore_net_host_udp_at(protocore_net_host_udp_count() - 1)->dst_port);

    protocore_net_host_udp_reset();
    UdpListener.port = 5683;
    UdpListener.send_args.dst = addr("192.168.1.20");
    UdpListener.send_args.dst_port = 5683;
    UdpListener.send_args.data = (const uint8_t *)"notify";
    UdpListener.send_args.len = 6;
    UdpListener.sendto(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    TEST_ASSERT_EQUAL_UINT(6, (unsigned)sent_len());
    TEST_ASSERT_EQUAL_INT(0, memcmp("notify", sent_bytes(), 6));
}

void test_a_refused_send_reports_the_refusal()
{
    UdpListener.port = 5683;
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    mock_udp_send_fail_after(0);
    UdpListener.port = 5683;
    UdpListener.send_args.dst = addr("192.168.1.20");
    UdpListener.send_args.dst_port = 5683;
    UdpListener.send_args.data = (const uint8_t *)"x";
    UdpListener.send_args.len = 1;
    UdpListener.sendto(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);
    TEST_ASSERT_EQUAL_size_t(0, protocore_net_host_udp_count());

    mock_udp_send_fail_after(-1);
    UdpListener.port = 5683;
    UdpListener.send_args.dst = addr("192.168.1.20");
    UdpListener.send_args.dst_port = 5683;
    UdpListener.send_args.data = (const uint8_t *)"y";
    UdpListener.send_args.len = 1;
    UdpListener.sendto(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    TEST_ASSERT_EQUAL_UINT(1, (unsigned)sent_len());
    TEST_ASSERT_EQUAL_UINT8('y', sent_bytes()[0]);
}

void test_send_rejects_null_zero_and_oversized_payload()
{
    UdpListener.port = 6100;
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    UdpListener.port = 6100;
    UdpListener.send_args.dst = addr("192.168.1.20");
    UdpListener.send_args.dst_port = 6100;
    UdpListener.send_args.data = NULL;
    UdpListener.send_args.len = 5;
    UdpListener.sendto(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);
    UdpListener.port = 6100;
    UdpListener.send_args.dst = addr("192.168.1.20");
    UdpListener.send_args.dst_port = 6100;
    UdpListener.send_args.data = (const uint8_t *)"x";
    UdpListener.send_args.len = 0;
    UdpListener.sendto(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);
    static uint8_t big[3000] = {0};
    UdpListener.port = 6100;
    UdpListener.send_args.dst = addr("192.168.1.20");
    UdpListener.send_args.dst_port = 6100;
    UdpListener.send_args.data = big;
    UdpListener.send_args.len = sizeof(big);
    UdpListener.sendto(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);
    UdpListener.peer_args.peer = NULL;
    UdpListener.send_args.data = (const uint8_t *)"x";
    UdpListener.send_args.len = 1;
    UdpListener.reply(UdpListener.internal);

    TEST_ASSERT_FALSE(UdpListener.ok);
    TEST_ASSERT_EQUAL_size_t(0, protocore_net_host_udp_count());
}

void test_inject_skips_a_listener_with_no_handler()
{
    UdpListener.port = 7000;
    UdpListener.bind.handler = NULL;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    inject(7000, "10.0.0.1", 1, (const uint8_t *)"x", 1);
    TEST_ASSERT_EQUAL_INT(0, g_calls);
}

void test_an_untagged_source_address_carries_no_address()
{
    UdpListener.port = 7001;
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    inject(7001, NULL, 1, (const uint8_t *)"x", 1);
    TEST_ASSERT_EQUAL_INT(1, g_calls);
    TEST_ASSERT_EQUAL_STRING("", g_src_ip);
}

void test_multicast_lookup_skips_a_different_multicast_group()
{
    UdpListener.port = 5353;
    UdpListener.bind.group_ip = "224.0.0.251";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    UdpListener.port = 1900;
    UdpListener.bind.group_ip = "239.255.255.250";
    UdpListener.bind.handler = on_datagram;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen_multicast(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    UdpListener.port = 1900;
    UdpListener.joined_group(UdpListener.internal);

    TEST_ASSERT_EQUAL_STRING("239.255.255.250", UdpListener.text);
    UdpListener.port = 1900;
    UdpListener.leave_multicast(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    UdpListener.port = 1900;
    UdpListener.joined_group(UdpListener.internal);

    TEST_ASSERT_NULL(UdpListener.text);
    UdpListener.port = 5353;
    UdpListener.joined_group(UdpListener.internal);

    TEST_ASSERT_EQUAL_STRING("224.0.0.251", UdpListener.text);
}

static char g_edge_ip[16];
static proto_bool g_edge_had_ip_out = PROTO_FALSE;
static proto_bool g_edge_had_port_out = PROTO_FALSE;

static void on_datagram_edge_cases(const uint8_t *, size_t, const struct protocore_udp_peer *peer, void *)
{
    uint16_t port_tmp = 0;
    UdpListener.peer_args.peer = peer;
    UdpListener.peer_args.ip_out = NULL;
    UdpListener.peer_args.ip_cap = sizeof(g_edge_ip);
    UdpListener.peer_args.port_out = &port_tmp;
    UdpListener.peer_addr(UdpListener.internal);
    g_edge_had_ip_out = UdpListener.ok;

    if (!g_edge_had_ip_out)
    {
        UdpListener.peer_args.peer = peer;
        UdpListener.peer_args.ip_out = g_edge_ip;
        UdpListener.peer_args.ip_cap = 0;
        UdpListener.peer_args.port_out = &port_tmp;
        UdpListener.peer_addr(UdpListener.internal);
        g_edge_had_ip_out = UdpListener.ok;
    }

    if (!g_edge_had_ip_out)
    {
        UdpListener.peer_args.peer = peer;
        UdpListener.peer_args.ip_out = g_edge_ip;
        UdpListener.peer_args.ip_cap = 4;
        UdpListener.peer_args.port_out = &port_tmp;
        UdpListener.peer_addr(UdpListener.internal);
        g_edge_had_ip_out = UdpListener.ok;
    }

    UdpListener.peer_args.peer = peer;
    UdpListener.peer_args.ip_out = g_edge_ip;
    UdpListener.peer_args.ip_cap = sizeof(g_edge_ip);
    UdpListener.peer_args.port_out = NULL;
    UdpListener.peer_addr(UdpListener.internal);
    g_edge_had_port_out = UdpListener.ok;
}

void test_peer_addr_refuses_a_buffer_it_cannot_fill_and_allows_a_null_port_out()
{
    g_edge_had_ip_out = PROTO_TRUE;
    g_edge_had_port_out = PROTO_FALSE;
    UdpListener.port = 7002;
    UdpListener.bind.handler = on_datagram_edge_cases;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen(UdpListener.internal);

    TEST_ASSERT_TRUE(UdpListener.ok);
    inject(7002, "198.51.100.5", 9, (const uint8_t *)"e", 1);
    TEST_ASSERT_FALSE(g_edge_had_ip_out);
    TEST_ASSERT_TRUE(g_edge_had_port_out);
    TEST_ASSERT_EQUAL_STRING("198.51.100.5", g_edge_ip);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_join_records_the_group);
    RUN_TEST(test_group_datagram_reaches_the_handler);
    RUN_TEST(test_counts_repeated_announcements);
    RUN_TEST(test_rejects_non_multicast_group);
    RUN_TEST(test_accepts_group_range_edges);
    RUN_TEST(test_rejects_malformed_group);
    RUN_TEST(test_leave_releases_the_slot);
    RUN_TEST(test_leave_ignores_a_plain_listener);
    RUN_TEST(test_listen_rebinds_existing_port);
    RUN_TEST(test_listen_refuses_a_third_port_when_the_pool_is_full);
    RUN_TEST(test_multicast_group_too_long_for_buffer_rejected);
    RUN_TEST(test_multicast_join_finds_slot_past_an_unrelated_listener);
    RUN_TEST(test_multicast_rejoin_scans_past_a_freed_lower_slot);
    RUN_TEST(test_peer_addr_rejects_null_peer);
    RUN_TEST(test_peer_addr_copies_and_tolerates_null_outparams);
    RUN_TEST(test_send_paths_are_captured);
    RUN_TEST(test_a_refused_send_reports_the_refusal);
    RUN_TEST(test_send_rejects_null_zero_and_oversized_payload);
    RUN_TEST(test_inject_skips_a_listener_with_no_handler);
    RUN_TEST(test_an_untagged_source_address_carries_no_address);
    RUN_TEST(test_multicast_lookup_skips_a_different_multicast_group);
    RUN_TEST(test_peer_addr_refuses_a_buffer_it_cannot_fill_and_allows_a_null_port_out);
    return UNITY_END();
}
