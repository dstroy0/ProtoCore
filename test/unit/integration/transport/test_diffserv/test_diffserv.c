// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "network_drivers/transport/diffserv/diffserv.h"
#include "network_drivers/transport/tcp/common.h"
#include "network_drivers/transport/tcp/protocol/protocol.h"
#include "network_drivers/transport/tcp/server/server.h"
#include "network_drivers/transport/tcp/tcp.h"
#include <unity.h>

void setUp()
{
    DiffServ.set_default(0);
    DiffServ.set_udp(0);
    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i].pcb = NULL;
    }
}

void tearDown()
{
}

static void test_dscp_to_tos_encode()
{
    TEST_ASSERT_EQUAL_UINT8(0, protocore_dscp_to_tos(PROTOCORE_DSCP_CS0));
    TEST_ASSERT_EQUAL_UINT8(0xB8, protocore_dscp_to_tos(PROTOCORE_DSCP_EF));
    TEST_ASSERT_EQUAL_UINT8(0xC0, protocore_dscp_to_tos(PROTOCORE_DSCP_CS6));
    TEST_ASSERT_EQUAL_UINT8(0x88, protocore_dscp_to_tos(PROTOCORE_DSCP_AF41));
    TEST_ASSERT_EQUAL_UINT8(0xFC, protocore_dscp_to_tos(63));
    TEST_ASSERT_EQUAL_UINT8(0x04, protocore_dscp_to_tos(0x41));
}

static void test_default_dscp_roundtrip()
{
    TEST_ASSERT_EQUAL_UINT8(0, DiffServ.default_dscp());
    DiffServ.set_default(PROTOCORE_DSCP_EF);
    TEST_ASSERT_EQUAL_UINT8(46, DiffServ.default_dscp());
    DiffServ.set_default(0xFF);
    TEST_ASSERT_EQUAL_UINT8(63, DiffServ.default_dscp());
}

static void test_udp_dscp_roundtrip()
{
    TEST_ASSERT_EQUAL_UINT8(0, DiffServ.udp_dscp());
    DiffServ.set_udp(PROTOCORE_DSCP_AF31);
    TEST_ASSERT_EQUAL_UINT8(26, DiffServ.udp_dscp());
    DiffServ.set_udp(0);
    TEST_ASSERT_EQUAL_UINT8(0, DiffServ.udp_dscp());
}

static void test_conn_set_dscp_writes_pcb_tos()
{
    protocore_pcb pcb;
    pcb.tos = 0;
    conn_pool[0].pcb = &pcb;

    ConnPool.slot = 0;
    ConnPool.u8 = PROTOCORE_DSCP_EF;
    ConnPool.set_dscp(ConnPool.internal);
    TEST_ASSERT_TRUE(ConnPool.ok);
    TEST_ASSERT_EQUAL_UINT8(0xB8, pcb.tos);

    ConnPool.slot = 0;
    ConnPool.u8 = PROTOCORE_DSCP_CS0;
    ConnPool.set_dscp(ConnPool.internal);
    TEST_ASSERT_TRUE(ConnPool.ok);
    TEST_ASSERT_EQUAL_UINT8(0, pcb.tos);
}

static void test_conn_set_dscp_rejects_bad_slot()
{
    conn_pool[0].pcb = NULL;
    ConnPool.slot = 0;
    ConnPool.u8 = PROTOCORE_DSCP_EF;
    ConnPool.set_dscp(ConnPool.internal);
    TEST_ASSERT_FALSE(ConnPool.ok);
    ConnPool.slot = 255;
    ConnPool.u8 = PROTOCORE_DSCP_EF;
    ConnPool.set_dscp(ConnPool.internal);
    TEST_ASSERT_FALSE(ConnPool.ok);
}

static void test_listen_set_dscp_override_and_sentinel()
{
    TcpListener.idx = 0;
    TcpListener.bind.port = 8080;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(TcpListener.internal);
    TEST_ASSERT_EQUAL(1, TcpListener.i32);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_DSCP_UNSET, listener_pool[0].dscp);

    TcpListener.idx = 8080;
    TcpListener.bind.dscp = PROTOCORE_DSCP_EF;
    TcpListener.set_dscp(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TEST_ASSERT_EQUAL_UINT8(46, listener_pool[0].dscp);

    TcpListener.idx = 8080;
    TcpListener.bind.dscp = 0x7E;
    TcpListener.set_dscp(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TEST_ASSERT_EQUAL_UINT8(62, listener_pool[0].dscp);

    TcpListener.idx = 8080;
    TcpListener.bind.dscp = PROTOCORE_DSCP_UNSET;
    TcpListener.set_dscp(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_DSCP_UNSET, listener_pool[0].dscp);

    TcpListener.idx = 9999;
    TcpListener.bind.dscp = PROTOCORE_DSCP_EF;
    TcpListener.set_dscp(TcpListener.internal);
    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.idx = 0;
    TcpListener.stop(TcpListener.internal);
}

static void test_accept_cb_applies_per_listener_dscp_override()
{
    ConnPool.life.cfg = NULL;
    ConnPool.init(ConnPool.internal);
    TcpListener.idx = 0;
    TcpListener.bind.port = 8080;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(TcpListener.internal);
    TEST_ASSERT_EQUAL(1, TcpListener.i32);
    TcpListener.idx = 8080;
    TcpListener.bind.dscp = PROTOCORE_DSCP_EF;
    TcpListener.set_dscp(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);

    protocore_pcb pcb;
    pcb.tos = 0;
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_UINT8(0xB8, pcb.tos);
    TcpListener.idx = 0;
    TcpListener.stop(TcpListener.internal);
}

static void test_accept_cb_falls_back_to_server_default_dscp()
{
    ConnPool.life.cfg = NULL;
    ConnPool.init(ConnPool.internal);
    TcpListener.idx = 0;
    TcpListener.bind.port = 8080;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(TcpListener.internal);
    TEST_ASSERT_EQUAL(1, TcpListener.i32);
    DiffServ.set_default(PROTOCORE_DSCP_AF41);

    protocore_pcb pcb;
    pcb.tos = 0;
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_UINT8(0x88, pcb.tos);
    TcpListener.idx = 0;
    TcpListener.stop(TcpListener.internal);
}

static void test_accept_cb_skips_tos_write_at_best_effort()
{
    ConnPool.life.cfg = NULL;
    ConnPool.init(ConnPool.internal);
    TcpListener.idx = 0;
    TcpListener.bind.port = 8080;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(TcpListener.internal);
    TEST_ASSERT_EQUAL(1, TcpListener.i32);

    protocore_pcb pcb;
    pcb.tos = 0x77;
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_UINT8(0x77, pcb.tos);
    TcpListener.idx = 0;
    TcpListener.stop(TcpListener.internal);
}

static void test_dynamic_listener_inherits_default_dscp()
{
    TcpListener.bind.port = 1;
    TcpListener.bind.proto = 2222;
    TcpListener.bind.tls = PROTO_HTTP;
    TcpListener.add_dynamic(TcpListener.internal);
    TEST_ASSERT_EQUAL_INT32(1, TcpListener.i32);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_DSCP_UNSET, listener_pool[1].dscp);
    TcpListener.idx = 1;
    TcpListener.stop_dynamic(TcpListener.internal);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_dscp_to_tos_encode);
    RUN_TEST(test_default_dscp_roundtrip);
    RUN_TEST(test_udp_dscp_roundtrip);
    RUN_TEST(test_conn_set_dscp_writes_pcb_tos);
    RUN_TEST(test_conn_set_dscp_rejects_bad_slot);
    RUN_TEST(test_listen_set_dscp_override_and_sentinel);
    RUN_TEST(test_accept_cb_applies_per_listener_dscp_override);
    RUN_TEST(test_accept_cb_falls_back_to_server_default_dscp);
    RUN_TEST(test_accept_cb_skips_tos_write_at_best_effort);
    RUN_TEST(test_dynamic_listener_inherits_default_dscp);
    return UNITY_END();
}
