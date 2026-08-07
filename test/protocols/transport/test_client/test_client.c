// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the outbound TCP client transport (network_drivers/transport/tcp/tcp_client.c).
// These are the lines that ship to silicon: the stack underneath is test/mocks/pc_net_host.h, so a
// connect completes inline, a send is read out of the driver's capture, and received bytes arrive
// through the recv callback the client armed.

#include "network_drivers/network/network.h" // network.dns->resolver: what the dial resolves through
#include "network_drivers/transport/tcp.h"
#include <string.h>
#include <unity.h>

// A dotted quad never reaches DNS, so an open resolves from the literal.
#define HOST "192.168.1.10"

void setUp()
{
    pc_net_host_reset();
    tcp_capture_reset();
}
void tearDown()
{
}

// The pcb the client dialed out on: the last one the pool handed out that carries our remote port.
static pc_pcb *dialed(uint16_t port)
{
    for (int i = 0; i < PC_NET_HOST_PCBS; i++)
    {
        if (pc_net_host_pcbs[i].in_use && pc_net_host_pcbs[i].remote_port == port)
        {
            return &pc_net_host_pcbs[i];
        }
    }
    return NULL;
}

// The dial resolves through the shared DNS owner, so a dotted quad must come straight back out of
// it: if this fails, every open below fails for that reason and not for a transport one.
void test_the_dial_resolves_a_literal()
{
    uint32_t ip = 0;
    TEST_ASSERT_NOT_NULL(network.dns);
    TEST_ASSERT_NOT_NULL(network.dns->resolver);
    TEST_ASSERT_TRUE(network.dns->resolver->resolve(HOST, &ip));
    TEST_ASSERT_EQUAL_HEX32(0xC0A8010Au, ip); // 192.168.1.10, host order
}

void test_open_connects_and_reports_the_slot()
{
    int cid = Tcp.client->open(HOST, 80, 1000);
    TEST_ASSERT_EQUAL_INT(0, cid); // the first dial takes the lowest free slot
    TEST_ASSERT_TRUE(Tcp.client->connected(cid));
    TEST_ASSERT_FALSE(Tcp.client->is_closed(cid));

    pc_pcb *p = dialed(80);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT16(80, p->remote_port);

    Tcp.client->close(cid);
}

void test_open_refuses_a_bad_host_and_a_refused_connect()
{
    TEST_ASSERT_TRUE(Tcp.client->open(NULL, 80, 1000) < 0); // nothing to resolve
    TEST_ASSERT_TRUE(Tcp.client->open("", 80, 1000) < 0);   // nor an empty name
    TEST_ASSERT_TRUE(Tcp.client->open("no.such.host", 80, 10) < 0);

    mock_connect_fail_once(); // the stack refuses the SYN
    TEST_ASSERT_TRUE(Tcp.client->open(HOST, 80, 100) < 0);

    // A refused open leaves no slot behind: the next one succeeds.
    int cid = Tcp.client->open(HOST, 80, 1000);
    TEST_ASSERT_TRUE(cid >= 0);
    Tcp.client->close(cid);
}

void test_send_reaches_the_wire()
{
    int cid = Tcp.client->open(HOST, 80, 1000);
    TEST_ASSERT_TRUE(cid >= 0);

    TEST_ASSERT_TRUE(Tcp.client->send(cid, "GET / HTTP/1.1\r\n", 16));
    size_t n = 0;
    const uint8_t *sent = pc_net_host_sent(&n);
    TEST_ASSERT_EQUAL_size_t(16, n);
    TEST_ASSERT_EQUAL_INT(0, memcmp("GET / HTTP/1.1\r\n", sent, 16));

    // A slot that was never opened, and a null payload, are refused without touching the wire.
    TEST_ASSERT_FALSE(Tcp.client->send(-1, "x", 1));
    TEST_ASSERT_FALSE(Tcp.client->send(cid, NULL, 4));
    pc_net_host_sent(&n);
    TEST_ASSERT_EQUAL_size_t(16, n);

    Tcp.client->close(cid);
}

void test_received_bytes_buffer_and_drain()
{
    int cid = Tcp.client->open(HOST, 80, 1000);
    TEST_ASSERT_TRUE(cid >= 0);
    TEST_ASSERT_EQUAL_size_t(0, Tcp.client->available(cid));

    pc_pcb *p = dialed(80);
    TEST_ASSERT_NOT_NULL(p);
    char body[] = "HTTP/1.1 200 OK";
    TEST_ASSERT_EQUAL_INT(PC_NET_OK, pc_net_host_deliver(p, body, (uint16_t)(sizeof(body) - 1)));
    TEST_ASSERT_EQUAL_size_t(sizeof(body) - 1, Tcp.client->available(cid));

    uint8_t buf[32];
    size_t got = Tcp.client->read(cid, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(sizeof(body) - 1, got);
    TEST_ASSERT_EQUAL_INT(0, memcmp(body, buf, got));
    TEST_ASSERT_EQUAL_size_t(0, Tcp.client->available(cid)); // drained

    Tcp.client->close(cid);
}

void test_a_peer_fin_closes_the_slot()
{
    int cid = Tcp.client->open(HOST, 80, 1000);
    TEST_ASSERT_TRUE(cid >= 0);
    TEST_ASSERT_FALSE(Tcp.client->is_closed(cid));

    pc_pcb *p = dialed(80);
    TEST_ASSERT_NOT_NULL(p);
    pc_net_host_close_peer(p); // a null pbuf is lwIP's FIN
    TEST_ASSERT_TRUE(Tcp.client->is_closed(cid));

    Tcp.client->close(cid);
}

// Out-of-range and unopened ids are reported, never dereferenced.
void test_guards_reject_ids_outside_the_pool()
{
    TEST_ASSERT_FALSE(Tcp.client->connected(-1));
    TEST_ASSERT_FALSE(Tcp.client->connected(PC_CLIENT_CONNS));
    TEST_ASSERT_TRUE(Tcp.client->is_closed(-1));
    TEST_ASSERT_TRUE(Tcp.client->is_closed(PC_CLIENT_CONNS));
    TEST_ASSERT_EQUAL_size_t(0, Tcp.client->available(-1));
    uint8_t buf[8];
    TEST_ASSERT_EQUAL_size_t(0, Tcp.client->read(-1, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0, Tcp.client->read(0, NULL, 0));
    Tcp.client->close(-1); // must not crash
    Tcp.client->close(PC_CLIENT_CONNS);
}

// The pool is finite: once every slot is open, the next dial is refused rather than evicting one.
void test_pool_exhaustion_refuses_a_further_open()
{
    int open_ids[PC_CLIENT_CONNS];
    for (int i = 0; i < PC_CLIENT_CONNS; i++)
    {
        open_ids[i] = Tcp.client->open(HOST, (uint16_t)(9000 + i), 1000);
        TEST_ASSERT_TRUE(open_ids[i] >= 0);
    }
    TEST_ASSERT_TRUE(Tcp.client->open(HOST, 9999, 1000) < 0);

    Tcp.client->close(open_ids[0]);
    int again = Tcp.client->open(HOST, 9999, 1000); // the freed slot is reusable
    TEST_ASSERT_TRUE(again >= 0);

    Tcp.client->close(again);
    for (int i = 1; i < PC_CLIENT_CONNS; i++)
    {
        Tcp.client->close(open_ids[i]);
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
