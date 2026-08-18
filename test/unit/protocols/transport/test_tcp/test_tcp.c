// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/transport/tcp/common.h"
#include "network_drivers/transport/tcp/protocol/protocol.h"
#include "network_drivers/transport/tcp/server/server.h"
#include "network_drivers/transport/tcp/tcp.h"

#include "protocore_net_host.h"

#include <string.h>
#include <unity.h>

#define PORT 8080

void setUp()
{
    protocore_net_host_reset();
    ConnPool.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(protocore_conn_pool_span());
    TcpListener.idx = 0;
    TcpListener.bind.port = PORT;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(protocore_tcp_listener_span());
}

void tearDown()
{
    TcpListener.stop_all(protocore_tcp_listener_span());
}

void test_accept_wires_every_callback_on_the_pcb()
{
    protocore_pcb *peer = protocore_net_new(PROTOCORE_NET_TYPE_ANY);
    TEST_ASSERT_NOT_NULL(peer);

    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, peer, PROTOCORE_NET_OK));

    TEST_ASSERT_NOT_NULL(peer->on_recv);
    TEST_ASSERT_NOT_NULL(peer->on_sent);
    TEST_ASSERT_NOT_NULL(peer->on_err);
    TEST_ASSERT_EQUAL_PTR(peer, conn_pool[0].pcb);
}

void test_delivery_through_the_wired_callback_fills_the_ring()
{
    protocore_pcb *peer = protocore_net_new(PROTOCORE_NET_TYPE_ANY);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, peer, PROTOCORE_NET_OK));

    char seg[] = "GET / HTTP/1.1\r\n";
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, protocore_net_host_deliver(peer, seg, (uint16_t)(sizeof(seg) - 1)));

    TEST_ASSERT_EQUAL_UINT32(sizeof(seg) - 1, (uint32_t)conn_pool[0].rx_head);
    TEST_ASSERT_EQUAL_MEMORY(seg, conn_pool[0].rx_buffer, sizeof(seg) - 1);
}

void test_peer_fin_through_the_wired_callback_closes_the_slot()
{
    protocore_pcb *peer = protocore_net_new(PROTOCORE_NET_TYPE_ANY);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, peer, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);

    (void)protocore_net_host_close_peer(peer);

    TEST_ASSERT_NOT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

void test_marshaled_send_reaches_the_capture()
{
    protocore_pcb *peer = protocore_net_new(PROTOCORE_NET_TYPE_ANY);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, peer, PROTOCORE_NET_OK));

    tcp_capture_reset();
    ConnPool.slot = 0;
    ConnPool.io.data = "PONG";
    ConnPool.io.len = 4;
    ConnPool.send_flush(protocore_conn_pool_span());
    TEST_ASSERT_TRUE(ConnPool.ok);

    TEST_ASSERT_EQUAL_UINT32(4, (uint32_t)tcp_captured_len());
    TEST_ASSERT_EQUAL_MEMORY("PONG", tcp_captured(), 4);
}

void test_marshaled_close_releases_the_slot()
{
    protocore_pcb *peer = protocore_net_new(PROTOCORE_NET_TYPE_ANY);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, peer, PROTOCORE_NET_OK));

    ConnPool.slot = 0;
    ConnPool.close(protocore_conn_pool_span());

    TEST_ASSERT_NOT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_accept_wires_every_callback_on_the_pcb);
    RUN_TEST(test_delivery_through_the_wired_callback_fills_the_ring);
    RUN_TEST(test_peer_fin_through_the_wired_callback_closes_the_slot);
    RUN_TEST(test_marshaled_send_reaches_the_capture);
    RUN_TEST(test_marshaled_close_releases_the_slot);
    return UNITY_END();
}
