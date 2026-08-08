// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The TCP target path, run on the host. The env (native_tcp_hot) builds with PROTOCORE_HOT_FORCE,
// so pc_platform.h selects PC_VENDOR_MOCK and PROTOCORE_HOT is 1: tcp_conn.c and tcp_listener.c
// compile the same lines that ship to silicon, driven against test/mocks/pc_net_host.h.
//
// Two things live here that the plain native_transport env cannot reach.
//
// The marshaled arm. Every send, output, close, abort and recved on the target goes through
// pc_tcp_marshal into the stack's own context, and none of that code is compiled by any other
// environment. Four fixed bugs in docs/BUGS.md live inside it.
//
// The accept wiring. Every other TCP suite reaches the connection callbacks by naming
// lowlevel_recv_cb / lowlevel_sent_cb / lowlevel_err_cb directly, so nothing observes whether
// listener_accept_cb ever installed them on the pcb. Deleting the registrations leaves those suites
// green and every accepted connection on the target deaf. Here a segment is delivered THROUGH the
// pcb the accept path wired, so the wiring is what carries the test.

#include "network_drivers/transport/tcp.h"
#include "network_drivers/transport/tcp/tcp_conn.h"
#include "network_drivers/transport/tcp/tcp_listener.h"

#include "pc_net_host.h"

#include <string.h>
#include <unity.h>

#if !PROTOCORE_HOT
#error "native_tcp_hot must build the target path: check base native_hot_base in test_matrix.json"
#endif

#define PORT 8080

void setUp()
{
    pc_net_host_reset();
    Tcp.conn->init(NULL);
    Tcp.listener->add(0, PORT, PROTO_HTTP, PROTO_FALSE);
}

void tearDown()
{
    Tcp.listener->stop_all();
}

// Accept claims a slot and installs the four callbacks on the accepted pcb. Asserted here rather
// than in test_transport because only the pcb carries them, and only the mock keeps them.
void test_accept_wires_every_callback_on_the_pcb()
{
    pc_pcb *peer = pc_net_new(PC_NET_TYPE_ANY);
    TEST_ASSERT_NOT_NULL(peer);

    TEST_ASSERT_EQUAL_INT(PC_NET_OK, listener_accept_cb((void *)(uintptr_t)0, peer, PC_NET_OK));

    TEST_ASSERT_NOT_NULL(peer->on_recv);
    TEST_ASSERT_NOT_NULL(peer->on_sent);
    TEST_ASSERT_NOT_NULL(peer->on_err);
    TEST_ASSERT_EQUAL_PTR(peer, conn_pool[0].pcb);
}

// A segment delivered through the wired recv callback reaches the slot's ring. Nothing here names
// lowlevel_recv_cb, so an unwired pcb fails at pc_net_host_deliver rather than passing silently.
void test_delivery_through_the_wired_callback_fills_the_ring()
{
    pc_pcb *peer = pc_net_new(PC_NET_TYPE_ANY);
    TEST_ASSERT_EQUAL_INT(PC_NET_OK, listener_accept_cb((void *)(uintptr_t)0, peer, PC_NET_OK));

    char seg[] = "GET / HTTP/1.1\r\n";
    TEST_ASSERT_EQUAL_INT(PC_NET_OK, pc_net_host_deliver(peer, seg, (uint16_t)(sizeof(seg) - 1)));

    TEST_ASSERT_EQUAL_UINT32(sizeof(seg) - 1, (uint32_t)conn_pool[0].rx_head);
    TEST_ASSERT_EQUAL_MEMORY(seg, conn_pool[0].rx_buffer, sizeof(seg) - 1);
}

// A peer FIN arrives as a null pbuf on the same wired callback and takes the slot out of ACTIVE.
void test_peer_fin_through_the_wired_callback_closes_the_slot()
{
    pc_pcb *peer = pc_net_new(PC_NET_TYPE_ANY);
    TEST_ASSERT_EQUAL_INT(PC_NET_OK, listener_accept_cb((void *)(uintptr_t)0, peer, PC_NET_OK));
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);

    (void)pc_net_host_close_peer(peer);

    TEST_ASSERT_NOT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

// The marshaled send arm: on the target a send from the app task is routed into the stack context
// by pc_tcp_marshal. This is the only env that compiles that path.
void test_marshaled_send_reaches_the_capture()
{
    pc_pcb *peer = pc_net_new(PC_NET_TYPE_ANY);
    TEST_ASSERT_EQUAL_INT(PC_NET_OK, listener_accept_cb((void *)(uintptr_t)0, peer, PC_NET_OK));

    tcp_capture_reset();
    TEST_ASSERT_TRUE(Tcp.conn->send_flush(0, "PONG", 4));

    TEST_ASSERT_EQUAL_UINT32(4, (uint32_t)tcp_captured_len());
    TEST_ASSERT_EQUAL_MEMORY("PONG", tcp_captured(), 4);
}

// Closing through the marshaled arm releases the slot rather than leaving it ACTIVE with a dead pcb.
void test_marshaled_close_releases_the_slot()
{
    pc_pcb *peer = pc_net_new(PC_NET_TYPE_ANY);
    TEST_ASSERT_EQUAL_INT(PC_NET_OK, listener_accept_cb((void *)(uintptr_t)0, peer, PC_NET_OK));

    Tcp.conn->close(0);

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
