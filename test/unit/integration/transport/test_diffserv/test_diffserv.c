// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for DiffServ QoS marking (RFC 2474): the DSCP -> TOS encode, the two server-wide
// defaults, the per-listener override, and what the accept callback stamps a new pcb with.
//
// Two levels of control and no third. RFC 9293 sec 3.9.2 SHLD-23: an application should not change
// the Diffserv field during a connection, so the bound port is the finest granularity and ConnPool
// carries no per-connection setter. The mark reaches a connection at accept, through
// listener_accept_cb, which is what the last four cases drive.

#include "network_drivers/transport/diffserv/diffserv.h"
#include "network_drivers/transport/tcp/common.h"
#include "network_drivers/transport/tcp/protocol/protocol.h"
#include "network_drivers/transport/tcp/server/server.h"
#include "network_drivers/transport/tcp/tcp.h"
#include <unity.h>

// The bytes the two defaults live in. An entry takes a borrow; this is where a caller's comes from.
static uint8_t *g_ds;

void setUp()
{
    g_ds = protocore_diffserv_span();
    TEST_ASSERT_NOT_NULL(g_ds);
    DiffServV.dscp = 0;
    DiffServ.set_default(g_ds);
    DiffServV.dscp = 0;
    DiffServ.set_udp(g_ds);
    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i].pcb = NULL;
    }
}

void tearDown()
{
}

void test_dscp_to_tos_encode()
{
    TEST_ASSERT_EQUAL_UINT8(0, protocore_dscp_to_tos(PROTOCORE_DSCP_CS0));
    TEST_ASSERT_EQUAL_UINT8(0xB8, protocore_dscp_to_tos(PROTOCORE_DSCP_EF));
    TEST_ASSERT_EQUAL_UINT8(0xC0, protocore_dscp_to_tos(PROTOCORE_DSCP_CS6));
    TEST_ASSERT_EQUAL_UINT8(0x88, protocore_dscp_to_tos(PROTOCORE_DSCP_AF41));
    TEST_ASSERT_EQUAL_UINT8(0xFC, protocore_dscp_to_tos(63));
    TEST_ASSERT_EQUAL_UINT8(0x04, protocore_dscp_to_tos(0x41));
}

void test_default_dscp_roundtrip()
{
    DiffServ.default_dscp(g_ds);
    TEST_ASSERT_EQUAL_UINT8(0, DiffServV.u8);

    DiffServV.dscp = PROTOCORE_DSCP_EF;
    DiffServ.set_default(g_ds);
    DiffServ.default_dscp(g_ds);
    TEST_ASSERT_EQUAL_UINT8(46, DiffServV.u8);

    // Masked to six bits on write, so a caller cannot spill into the two ECN bits.
    DiffServV.dscp = 0xFF;
    DiffServ.set_default(g_ds);
    DiffServ.default_dscp(g_ds);
    TEST_ASSERT_EQUAL_UINT8(63, DiffServV.u8);
}

void test_udp_dscp_roundtrip()
{
    DiffServ.udp_dscp(g_ds);
    TEST_ASSERT_EQUAL_UINT8(0, DiffServV.u8);

    DiffServV.dscp = PROTOCORE_DSCP_AF31;
    DiffServ.set_udp(g_ds);
    DiffServ.udp_dscp(g_ds);
    TEST_ASSERT_EQUAL_UINT8(26, DiffServV.u8);

    DiffServV.dscp = 0;
    DiffServ.set_udp(g_ds);
    DiffServ.udp_dscp(g_ds);
    TEST_ASSERT_EQUAL_UINT8(0, DiffServV.u8);
}

// The two marks are one pair of bytes, so a write through one entry is what the other reads.
void test_the_two_defaults_are_separate_marks()
{
    DiffServV.dscp = PROTOCORE_DSCP_EF;
    DiffServ.set_default(g_ds);
    DiffServV.dscp = PROTOCORE_DSCP_AF31;
    DiffServ.set_udp(g_ds);

    DiffServ.default_dscp(g_ds);
    TEST_ASSERT_EQUAL_UINT8(46, DiffServV.u8);
    DiffServ.udp_dscp(g_ds);
    TEST_ASSERT_EQUAL_UINT8(26, DiffServV.u8);
}

// The accept path takes no borrow, so it reads the marks through the module's own span. These are
// the calls listener_accept_cb and the UDP send path actually make.
void test_the_flat_readers_report_what_the_entries_wrote()
{
    DiffServV.dscp = PROTOCORE_DSCP_CS6;
    DiffServ.set_default(g_ds);
    DiffServV.dscp = PROTOCORE_DSCP_AF41;
    DiffServ.set_udp(g_ds);

    TEST_ASSERT_EQUAL_UINT8(48, protocore_diffserv_default_dscp());
    TEST_ASSERT_EQUAL_UINT8(34, protocore_diffserv_udp_dscp());
}

void test_listen_set_dscp_override_and_sentinel()
{
    TcpListener.idx = 0;
    TcpListener.bind.port = 8080;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(protocore_tcp_listener_span());
    TEST_ASSERT_EQUAL(1, TcpListener.i32);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_DSCP_UNSET, listener_pool[0].dscp);

    // set_dscp names the port, not the row: it walks the pool for the active listener bound to
    // bind.port. idx is what add and stop take, and this call ignores it.
    TcpListener.bind.port = 8080;
    TcpListener.bind.dscp = PROTOCORE_DSCP_EF;
    TcpListener.set_dscp(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
    TEST_ASSERT_EQUAL_UINT8(46, listener_pool[0].dscp);

    TcpListener.bind.dscp = 0x7E;
    TcpListener.set_dscp(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
    TEST_ASSERT_EQUAL_UINT8(62, listener_pool[0].dscp);

    // The sentinel is preserved rather than masked: it is not a code point.
    TcpListener.bind.dscp = PROTOCORE_DSCP_UNSET;
    TcpListener.set_dscp(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_DSCP_UNSET, listener_pool[0].dscp);

    TcpListener.bind.port = 9999;
    TcpListener.bind.dscp = PROTOCORE_DSCP_EF;
    TcpListener.set_dscp(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.idx = 0;
    TcpListener.stop(protocore_tcp_listener_span());
}

void test_accept_cb_applies_per_listener_dscp_override()
{
    ConnPool.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(protocore_conn_pool_span());
    TcpListener.idx = 0;
    TcpListener.bind.port = 8080;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(protocore_tcp_listener_span());
    TEST_ASSERT_EQUAL(1, TcpListener.i32);
    TcpListener.bind.dscp = PROTOCORE_DSCP_EF;
    TcpListener.set_dscp(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);

    protocore_pcb pcb;
    pcb.tos = 0;
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_UINT8(0xB8, pcb.tos);
    TcpListener.idx = 0;
    TcpListener.stop(protocore_tcp_listener_span());
}

void test_accept_cb_falls_back_to_server_default_dscp()
{
    ConnPool.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(protocore_conn_pool_span());
    TcpListener.idx = 0;
    TcpListener.bind.port = 8080;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(protocore_tcp_listener_span());
    TEST_ASSERT_EQUAL(1, TcpListener.i32);
    DiffServV.dscp = PROTOCORE_DSCP_AF41;
    DiffServ.set_default(g_ds);

    protocore_pcb pcb;
    pcb.tos = 0;
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_UINT8(0x88, pcb.tos);
    TcpListener.idx = 0;
    TcpListener.stop(protocore_tcp_listener_span());
}

void test_accept_cb_skips_tos_write_at_best_effort()
{
    ConnPool.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(protocore_conn_pool_span());
    TcpListener.idx = 0;
    TcpListener.bind.port = 8080;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(protocore_tcp_listener_span());
    TEST_ASSERT_EQUAL(1, TcpListener.i32);

    protocore_pcb pcb;
    pcb.tos = 0x77;
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_UINT8(0x77, pcb.tos);
    TcpListener.idx = 0;
    TcpListener.stop(protocore_tcp_listener_span());
}

// A forwarded port is a plaintext bridge started from a running task, and takes the sentinel, so it
// marks with whatever the server-wide default is at accept rather than a mark fixed at add time.
void test_dynamic_listener_inherits_default_dscp()
{
    TcpListener.idx = 1;
    TcpListener.bind.port = 2222;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add_dynamic(protocore_tcp_listener_span());
    TEST_ASSERT_EQUAL_INT32(1, TcpListener.i32);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_DSCP_UNSET, listener_pool[1].dscp);
    TcpListener.idx = 1;
    TcpListener.stop_dynamic(protocore_tcp_listener_span());
}
