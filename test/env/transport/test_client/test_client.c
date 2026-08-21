// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/network/dns/dns_resolver/dns_resolver.h"
#include "network_drivers/network/network.h"
#include "network_drivers/transport/tcp/client/client.h"
#include "network_drivers/transport/tcp/tcp.h"
#include "server/clock/clock.h"
#include <string.h>
#include <unity.h>

#define HOST "192.168.1.10"

// Moves the virtual clock and takes the reading the library compares against: Clock.ms is a stamp
// Clock.millis leaves behind, not a read of the source.
static void advance_ms(uint32_t by)
{
    set_millis(millis() + by);
    Clock.millis(Clock.internal);
}

void setUp()
{
    protocore_net_host_reset();
    tcp_capture_reset();
    Clock.millis(Clock.internal);
}
void tearDown()
{
}

static protocore_pcb *dialed(uint16_t port)
{
    for (int i = 0; i < PROTOCORE_NET_HOST_PCBS; i++)
    {
        if (protocore_net_host_pcbs[i].in_use && protocore_net_host_pcbs[i].remote_port == port)
        {
            return &protocore_net_host_pcbs[i];
        }
    }
    return NULL;
}

void test_the_dial_resolves_a_literal()
{
    TEST_ASSERT_NOT_NULL(networkV.dns);
    TEST_ASSERT_NOT_NULL(networkV.dns->resolver);

    // The wiring is checked by what the stored resolver DOES, not by its address. A namespace table
    // is `static const` in its header, so every translation unit that takes its address gets its
    // own copy of an identical object: `&Resolver` here is not the pointer dns.c stored, and
    // comparing the two asserts an identity the shape deliberately does not provide. Calling
    // through the stored pointer proves the same wiring and survives the difference.
    ResolverV.query.host = HOST;
    networkV.dns->resolver->resolve(protocore_dns_resolver_span());
    TEST_ASSERT_EQUAL_INT(PROTOCORE_DNS_READY, ResolverV.state);
    TEST_ASSERT_EQUAL_HEX32(0xC0A8010Au, ResolverV.u32);
}

void test_open_connects_and_reports_the_slot()
{
    TcpClientV.dial.host = HOST;
    TcpClientV.dial.port = 80;
    TcpClientV.dial.timeout_ms = 1000;
    TcpClient.open(protocore_tcp_client_span());
    int cid = TcpClientV.i32;
    TEST_ASSERT_EQUAL_INT(0, cid);
    TcpClientV.cid = cid;
    TcpClient.connected(protocore_tcp_client_span());
    TEST_ASSERT_TRUE(TcpClientV.ok);
    TcpClientV.cid = cid;
    TcpClient.is_closed(protocore_tcp_client_span());
    TEST_ASSERT_FALSE(TcpClientV.ok);

    protocore_pcb *p = dialed(80);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT16(80, p->remote_port);

    TcpClientV.cid = cid;
    TcpClient.close(protocore_tcp_client_span());
}

void test_open_refuses_a_bad_host_and_a_refused_connect()
{
    TcpClientV.dial.host = NULL;
    TcpClientV.dial.port = 80;
    TcpClientV.dial.timeout_ms = 1000;
    TcpClient.open(protocore_tcp_client_span());
    TEST_ASSERT_TRUE(TcpClientV.i32 < 0);

    TcpClientV.dial.host = "no.such.host";
    TcpClientV.dial.port = 80;
    TcpClientV.dial.timeout_ms = 10;
    TcpClient.open(protocore_tcp_client_span());
    int bad = TcpClientV.i32;
    TEST_ASSERT_TRUE(bad >= 0);
    TcpClientV.cid = bad;
    TcpClient.connected(protocore_tcp_client_span());
    TEST_ASSERT_FALSE(TcpClientV.ok);
    advance_ms(11);
    TcpClientV.cid = bad;
    TcpClient.is_closed(protocore_tcp_client_span());
    TEST_ASSERT_TRUE(TcpClientV.ok);
    TcpClientV.cid = bad;
    TcpClient.close(protocore_tcp_client_span());

    mock_connect_fail_once();
    TcpClientV.dial.host = HOST;
    TcpClientV.dial.port = 80;
    TcpClientV.dial.timeout_ms = 100;
    TcpClient.open(protocore_tcp_client_span());
    int refused = TcpClientV.i32;
    TEST_ASSERT_TRUE(refused >= 0);
    TcpClientV.cid = refused;
    TcpClient.connected(protocore_tcp_client_span());
    TEST_ASSERT_FALSE(TcpClientV.ok);
    TcpClientV.cid = refused;
    TcpClient.is_closed(protocore_tcp_client_span());
    TEST_ASSERT_TRUE(TcpClientV.ok);
    TcpClientV.cid = refused;
    TcpClient.close(protocore_tcp_client_span());

    TcpClientV.dial.host = HOST;
    TcpClientV.dial.port = 80;
    TcpClientV.dial.timeout_ms = 1000;
    TcpClient.open(protocore_tcp_client_span());
    int cid = TcpClientV.i32;
    TEST_ASSERT_TRUE(cid >= 0);
    TcpClientV.cid = cid;
    TcpClient.connected(protocore_tcp_client_span());
    TEST_ASSERT_TRUE(TcpClientV.ok);
    TcpClientV.cid = cid;
    TcpClient.close(protocore_tcp_client_span());
}

void test_send_reaches_the_wire()
{
    TcpClientV.dial.host = HOST;
    TcpClientV.dial.port = 80;
    TcpClientV.dial.timeout_ms = 1000;
    TcpClient.open(protocore_tcp_client_span());
    int cid = TcpClientV.i32;
    TEST_ASSERT_TRUE(cid >= 0);

    TcpClientV.cid = cid;
    TcpClientV.io.data = "GET / HTTP/1.1\r\n";
    TcpClientV.io.len = 16;
    TcpClient.send(protocore_tcp_client_span());
    TEST_ASSERT_TRUE(TcpClientV.ok);
    size_t n = 0;
    const uint8_t *sent = protocore_net_host_sent(&n);
    TEST_ASSERT_EQUAL_size_t(16, n);
    TEST_ASSERT_EQUAL_INT(0, memcmp("GET / HTTP/1.1\r\n", sent, 16));

    TcpClientV.cid = -1;
    TcpClientV.io.data = "x";
    TcpClientV.io.len = 1;
    TcpClient.send(protocore_tcp_client_span());
    TEST_ASSERT_FALSE(TcpClientV.ok);
    TcpClientV.cid = cid;
    TcpClientV.io.data = NULL;
    TcpClientV.io.len = 4;
    TcpClient.send(protocore_tcp_client_span());
    TEST_ASSERT_FALSE(TcpClientV.ok);
    protocore_net_host_sent(&n);
    TEST_ASSERT_EQUAL_size_t(16, n);

    TcpClientV.cid = cid;
    TcpClient.close(protocore_tcp_client_span());
}

void test_received_bytes_buffer_and_drain()
{
    TcpClientV.dial.host = HOST;
    TcpClientV.dial.port = 80;
    TcpClientV.dial.timeout_ms = 1000;
    TcpClient.open(protocore_tcp_client_span());
    int cid = TcpClientV.i32;
    TEST_ASSERT_TRUE(cid >= 0);
    TcpClientV.cid = cid;
    TcpClient.available(protocore_tcp_client_span());
    TEST_ASSERT_EQUAL_size_t(0, TcpClientV.n);

    protocore_pcb *p = dialed(80);
    TEST_ASSERT_NOT_NULL(p);
    char body[] = "HTTP/1.1 200 OK";
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, protocore_net_host_deliver(p, body, (uint16_t)(sizeof(body) - 1)));
    TcpClientV.cid = cid;
    TcpClient.available(protocore_tcp_client_span());
    TEST_ASSERT_EQUAL_size_t(sizeof(body) - 1, TcpClientV.n);

    uint8_t buf[32];
    TcpClientV.cid = cid;
    TcpClientV.io.buf = buf;
    TcpClientV.io.cap = sizeof(buf);
    TcpClient.read(protocore_tcp_client_span());
    size_t got = TcpClientV.n;
    TEST_ASSERT_EQUAL_size_t(sizeof(body) - 1, got);
    TEST_ASSERT_EQUAL_INT(0, memcmp(body, buf, got));
    TcpClientV.cid = cid;
    TcpClient.available(protocore_tcp_client_span());
    TEST_ASSERT_EQUAL_size_t(0, TcpClientV.n);

    TcpClientV.cid = cid;
    TcpClient.close(protocore_tcp_client_span());
}

void test_a_peer_fin_closes_the_slot()
{
    TcpClientV.dial.host = HOST;
    TcpClientV.dial.port = 80;
    TcpClientV.dial.timeout_ms = 1000;
    TcpClient.open(protocore_tcp_client_span());
    int cid = TcpClientV.i32;
    TEST_ASSERT_TRUE(cid >= 0);
    TcpClientV.cid = cid;
    TcpClient.is_closed(protocore_tcp_client_span());
    TEST_ASSERT_FALSE(TcpClientV.ok);

    protocore_pcb *p = dialed(80);
    TEST_ASSERT_NOT_NULL(p);
    protocore_net_host_close_peer(p);
    TcpClientV.cid = cid;
    TcpClient.is_closed(protocore_tcp_client_span());
    TEST_ASSERT_TRUE(TcpClientV.ok);

    TcpClientV.cid = cid;
    TcpClient.close(protocore_tcp_client_span());
}

void test_guards_reject_ids_outside_the_pool()
{
    TcpClientV.cid = -1;
    TcpClient.connected(protocore_tcp_client_span());
    TEST_ASSERT_FALSE(TcpClientV.ok);
    TcpClientV.cid = PROTOCORE_CLIENT_CONNS;
    TcpClient.connected(protocore_tcp_client_span());
    TEST_ASSERT_FALSE(TcpClientV.ok);
    TcpClientV.cid = -1;
    TcpClient.is_closed(protocore_tcp_client_span());
    TEST_ASSERT_TRUE(TcpClientV.ok);
    TcpClientV.cid = PROTOCORE_CLIENT_CONNS;
    TcpClient.is_closed(protocore_tcp_client_span());
    TEST_ASSERT_TRUE(TcpClientV.ok);
    TcpClientV.cid = -1;
    TcpClient.available(protocore_tcp_client_span());
    TEST_ASSERT_EQUAL_size_t(0, TcpClientV.n);
    uint8_t buf[8];
    TcpClientV.cid = -1;
    TcpClientV.io.buf = buf;
    TcpClientV.io.cap = sizeof(buf);
    TcpClient.read(protocore_tcp_client_span());
    TEST_ASSERT_EQUAL_size_t(0, TcpClientV.n);
    TcpClientV.cid = 0;
    TcpClientV.io.buf = NULL;
    TcpClientV.io.cap = 0;
    TcpClient.read(protocore_tcp_client_span());
    TEST_ASSERT_EQUAL_size_t(0, TcpClientV.n);
    TcpClientV.cid = -1;
    TcpClient.close(protocore_tcp_client_span());
    TcpClientV.cid = PROTOCORE_CLIENT_CONNS;
    TcpClient.close(protocore_tcp_client_span());
}

void test_pool_exhaustion_refuses_a_further_open()
{
    int open_ids[PROTOCORE_CLIENT_CONNS];
    for (int i = 0; i < PROTOCORE_CLIENT_CONNS; i++)
    {
        TcpClientV.dial.host = HOST;
        TcpClientV.dial.port = (uint16_t)(9000 + i);
        TcpClientV.dial.timeout_ms = 1000;
        TcpClient.open(protocore_tcp_client_span());
        open_ids[i] = TcpClientV.i32;
        TEST_ASSERT_TRUE(open_ids[i] >= 0);
    }
    TcpClientV.dial.host = HOST;
    TcpClientV.dial.port = 9999;
    TcpClientV.dial.timeout_ms = 1000;
    TcpClient.open(protocore_tcp_client_span());
    TEST_ASSERT_TRUE(TcpClientV.i32 < 0);

    TcpClientV.cid = open_ids[0];
    TcpClient.close(protocore_tcp_client_span());
    TcpClientV.dial.host = HOST;
    TcpClientV.dial.port = 9999;
    TcpClientV.dial.timeout_ms = 1000;
    TcpClient.open(protocore_tcp_client_span());
    int again = TcpClientV.i32;
    TEST_ASSERT_TRUE(again >= 0);

    TcpClientV.cid = again;
    TcpClient.close(protocore_tcp_client_span());
    for (int i = 1; i < PROTOCORE_CLIENT_CONNS; i++)
    {
        TcpClientV.cid = open_ids[i];
        TcpClient.close(protocore_tcp_client_span());
    }
}
