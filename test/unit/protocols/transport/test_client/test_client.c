// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/network/network.h"
#include "network_drivers/transport/tcp/client/client.h"
#include "network_drivers/transport/tcp/tcp.h"
#include <string.h>
#include <unity.h>

#define HOST "192.168.1.10"

void setUp()
{
    protocore_net_host_reset();
    tcp_capture_reset();
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
    uint32_t ip = 0;
    TEST_ASSERT_NOT_NULL(network.dns);
    TEST_ASSERT_NOT_NULL(network.dns->resolver);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_DNS_READY, network.dns->resolver->resolve(HOST, &ip));
    TEST_ASSERT_EQUAL_HEX32(0xC0A8010Au, ip);
}

void test_open_connects_and_reports_the_slot()
{
    TcpClient.dial.host = HOST;
    TcpClient.dial.port = 80;
    TcpClient.dial.timeout_ms = 1000;
    TcpClient.open(TcpClient.internal);
    int cid = TcpClient.i32;
    TEST_ASSERT_EQUAL_INT(0, cid);
    TcpClient.cid = cid;
    TcpClient.connected(TcpClient.internal);
    TEST_ASSERT_TRUE(TcpClient.ok);
    TcpClient.cid = cid;
    TcpClient.is_closed(TcpClient.internal);
    TEST_ASSERT_FALSE(TcpClient.ok);

    protocore_pcb *p = dialed(80);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT16(80, p->remote_port);

    TcpClient.cid = cid;
    TcpClient.close(TcpClient.internal);
}

void test_open_refuses_a_bad_host_and_a_refused_connect()
{
    TcpClient.dial.host = NULL;
    TcpClient.dial.port = 80;
    TcpClient.dial.timeout_ms = 1000;
    TcpClient.open(TcpClient.internal);
    TEST_ASSERT_TRUE(TcpClient.i32 < 0);

    TcpClient.dial.host = "no.such.host";
    TcpClient.dial.port = 80;
    TcpClient.dial.timeout_ms = 10;
    TcpClient.open(TcpClient.internal);
    int bad = TcpClient.i32;
    TEST_ASSERT_TRUE(bad >= 0);
    TcpClient.cid = bad;
    TcpClient.connected(TcpClient.internal);
    TEST_ASSERT_FALSE(TcpClient.ok);
    set_millis(millis() + 11);
    TcpClient.cid = bad;
    TcpClient.is_closed(TcpClient.internal);
    TEST_ASSERT_TRUE(TcpClient.ok);
    TcpClient.cid = bad;
    TcpClient.close(TcpClient.internal);

    mock_connect_fail_once();
    TcpClient.dial.host = HOST;
    TcpClient.dial.port = 80;
    TcpClient.dial.timeout_ms = 100;
    TcpClient.open(TcpClient.internal);
    int refused = TcpClient.i32;
    TEST_ASSERT_TRUE(refused >= 0);
    TcpClient.cid = refused;
    TcpClient.connected(TcpClient.internal);
    TEST_ASSERT_FALSE(TcpClient.ok);
    TcpClient.cid = refused;
    TcpClient.is_closed(TcpClient.internal);
    TEST_ASSERT_TRUE(TcpClient.ok);
    TcpClient.cid = refused;
    TcpClient.close(TcpClient.internal);

    TcpClient.dial.host = HOST;
    TcpClient.dial.port = 80;
    TcpClient.dial.timeout_ms = 1000;
    TcpClient.open(TcpClient.internal);
    int cid = TcpClient.i32;
    TEST_ASSERT_TRUE(cid >= 0);
    TcpClient.cid = cid;
    TcpClient.connected(TcpClient.internal);
    TEST_ASSERT_TRUE(TcpClient.ok);
    TcpClient.cid = cid;
    TcpClient.close(TcpClient.internal);
}

void test_send_reaches_the_wire()
{
    TcpClient.dial.host = HOST;
    TcpClient.dial.port = 80;
    TcpClient.dial.timeout_ms = 1000;
    TcpClient.open(TcpClient.internal);
    int cid = TcpClient.i32;
    TEST_ASSERT_TRUE(cid >= 0);

    TcpClient.cid = cid;
    TcpClient.io.data = "GET / HTTP/1.1\r\n";
    TcpClient.io.len = 16;
    TcpClient.send(TcpClient.internal);
    TEST_ASSERT_TRUE(TcpClient.ok);
    size_t n = 0;
    const uint8_t *sent = protocore_net_host_sent(&n);
    TEST_ASSERT_EQUAL_size_t(16, n);
    TEST_ASSERT_EQUAL_INT(0, memcmp("GET / HTTP/1.1\r\n", sent, 16));

    TcpClient.cid = -1;
    TcpClient.io.data = "x";
    TcpClient.io.len = 1;
    TcpClient.send(TcpClient.internal);
    TEST_ASSERT_FALSE(TcpClient.ok);
    TcpClient.cid = cid;
    TcpClient.io.data = NULL;
    TcpClient.io.len = 4;
    TcpClient.send(TcpClient.internal);
    TEST_ASSERT_FALSE(TcpClient.ok);
    protocore_net_host_sent(&n);
    TEST_ASSERT_EQUAL_size_t(16, n);

    TcpClient.cid = cid;
    TcpClient.close(TcpClient.internal);
}

void test_received_bytes_buffer_and_drain()
{
    TcpClient.dial.host = HOST;
    TcpClient.dial.port = 80;
    TcpClient.dial.timeout_ms = 1000;
    TcpClient.open(TcpClient.internal);
    int cid = TcpClient.i32;
    TEST_ASSERT_TRUE(cid >= 0);
    TcpClient.cid = cid;
    TcpClient.available(TcpClient.internal);
    TEST_ASSERT_EQUAL_size_t(0, TcpClient.n);

    protocore_pcb *p = dialed(80);
    TEST_ASSERT_NOT_NULL(p);
    char body[] = "HTTP/1.1 200 OK";
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, protocore_net_host_deliver(p, body, (uint16_t)(sizeof(body) - 1)));
    TcpClient.cid = cid;
    TcpClient.available(TcpClient.internal);
    TEST_ASSERT_EQUAL_size_t(sizeof(body) - 1, TcpClient.n);

    uint8_t buf[32];
    TcpClient.cid = cid;
    TcpClient.io.buf = buf;
    TcpClient.io.cap = sizeof(buf);
    TcpClient.read(TcpClient.internal);
    size_t got = TcpClient.n;
    TEST_ASSERT_EQUAL_size_t(sizeof(body) - 1, got);
    TEST_ASSERT_EQUAL_INT(0, memcmp(body, buf, got));
    TcpClient.cid = cid;
    TcpClient.available(TcpClient.internal);
    TEST_ASSERT_EQUAL_size_t(0, TcpClient.n);

    TcpClient.cid = cid;
    TcpClient.close(TcpClient.internal);
}

void test_a_peer_fin_closes_the_slot()
{
    TcpClient.dial.host = HOST;
    TcpClient.dial.port = 80;
    TcpClient.dial.timeout_ms = 1000;
    TcpClient.open(TcpClient.internal);
    int cid = TcpClient.i32;
    TEST_ASSERT_TRUE(cid >= 0);
    TcpClient.cid = cid;
    TcpClient.is_closed(TcpClient.internal);
    TEST_ASSERT_FALSE(TcpClient.ok);

    protocore_pcb *p = dialed(80);
    TEST_ASSERT_NOT_NULL(p);
    protocore_net_host_close_peer(p);
    TcpClient.cid = cid;
    TcpClient.is_closed(TcpClient.internal);
    TEST_ASSERT_TRUE(TcpClient.ok);

    TcpClient.cid = cid;
    TcpClient.close(TcpClient.internal);
}

void test_guards_reject_ids_outside_the_pool()
{
    TcpClient.cid = -1;
    TcpClient.connected(TcpClient.internal);
    TEST_ASSERT_FALSE(TcpClient.ok);
    TcpClient.cid = PROTOCORE_CLIENT_CONNS;
    TcpClient.connected(TcpClient.internal);
    TEST_ASSERT_FALSE(TcpClient.ok);
    TcpClient.cid = -1;
    TcpClient.is_closed(TcpClient.internal);
    TEST_ASSERT_TRUE(TcpClient.ok);
    TcpClient.cid = PROTOCORE_CLIENT_CONNS;
    TcpClient.is_closed(TcpClient.internal);
    TEST_ASSERT_TRUE(TcpClient.ok);
    TcpClient.cid = -1;
    TcpClient.available(TcpClient.internal);
    TEST_ASSERT_EQUAL_size_t(0, TcpClient.n);
    uint8_t buf[8];
    TcpClient.cid = -1;
    TcpClient.io.buf = buf;
    TcpClient.io.cap = sizeof(buf);
    TcpClient.read(TcpClient.internal);
    TEST_ASSERT_EQUAL_size_t(0, TcpClient.n);
    TcpClient.cid = 0;
    TcpClient.io.buf = NULL;
    TcpClient.io.cap = 0;
    TcpClient.read(TcpClient.internal);
    TEST_ASSERT_EQUAL_size_t(0, TcpClient.n);
    TcpClient.cid = -1;
    TcpClient.close(TcpClient.internal);
    TcpClient.cid = PROTOCORE_CLIENT_CONNS;
    TcpClient.close(TcpClient.internal);
}

void test_pool_exhaustion_refuses_a_further_open()
{
    int open_ids[PROTOCORE_CLIENT_CONNS];
    for (int i = 0; i < PROTOCORE_CLIENT_CONNS; i++)
    {
        TcpClient.dial.host = HOST;
        TcpClient.dial.port = (uint16_t)(9000 + i);
        TcpClient.dial.timeout_ms = 1000;
        TcpClient.open(TcpClient.internal);
        open_ids[i] = TcpClient.i32;
        TEST_ASSERT_TRUE(open_ids[i] >= 0);
    }
    TcpClient.dial.host = HOST;
    TcpClient.dial.port = 9999;
    TcpClient.dial.timeout_ms = 1000;
    TcpClient.open(TcpClient.internal);
    TEST_ASSERT_TRUE(TcpClient.i32 < 0);

    TcpClient.cid = open_ids[0];
    TcpClient.close(TcpClient.internal);
    TcpClient.dial.host = HOST;
    TcpClient.dial.port = 9999;
    TcpClient.dial.timeout_ms = 1000;
    TcpClient.open(TcpClient.internal);
    int again = TcpClient.i32;
    TEST_ASSERT_TRUE(again >= 0);

    TcpClient.cid = again;
    TcpClient.close(TcpClient.internal);
    for (int i = 1; i < PROTOCORE_CLIENT_CONNS; i++)
    {
        TcpClient.cid = open_ids[i];
        TcpClient.close(TcpClient.internal);
    }
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_the_dial_resolves_a_literal);
    RUN_TEST(test_open_connects_and_reports_the_slot);
    RUN_TEST(test_open_refuses_a_bad_host_and_a_refused_connect);
    RUN_TEST(test_send_reaches_the_wire);
    RUN_TEST(test_received_bytes_buffer_and_drain);
    RUN_TEST(test_a_peer_fin_closes_the_slot);
    RUN_TEST(test_guards_reject_ids_outside_the_pool);
    RUN_TEST(test_pool_exhaustion_refuses_a_further_open);
    return UNITY_END();
}
