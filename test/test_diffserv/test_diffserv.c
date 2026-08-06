// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

// DiffServ QoS marking (PC_ENABLE_DIFFSERV) host tests: the DSCP->TOS encode, the server-wide + UDP
// DSCP defaults, the per-connection setter (writes pcb->tos), the per-listener override incl. the
// UNSET sentinel + 6-bit mask + unknown-port miss, and the accept-callback apply itself
// (listener_accept_cb is non-static - see listener.cpp - specifically so this env can drive it
// directly with a fabricated pcb rather than needing a real lwIP accept event).

#include "network_drivers/transport/diffserv.h"
#include "network_drivers/transport/tcp.h"
#include "network_drivers/transport/tcp.h"
#include <unity.h>

void setUp()
{
    DiffServ.set_default(0); // best-effort baseline so each test starts clean
    DiffServ.set_udp(0);
    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i].pcb = NULL;
    }
}

void tearDown()
{
}

// DSCP -> DS field: DSCP occupies the high 6 bits, ECN the low 2 (left 0). TOS == DSCP << 2.
static void test_dscp_to_tos_encode()
{
    TEST_ASSERT_EQUAL_UINT8(0, pc_dscp_to_tos(PC_DSCP_CS0));     // best-effort
    TEST_ASSERT_EQUAL_UINT8(0xB8, pc_dscp_to_tos(PC_DSCP_EF));   // 46 << 2 = 0xB8
    TEST_ASSERT_EQUAL_UINT8(0xC0, pc_dscp_to_tos(PC_DSCP_CS6));  // 48 << 2 = 0xC0
    TEST_ASSERT_EQUAL_UINT8(0x88, pc_dscp_to_tos(PC_DSCP_AF41)); // 34 << 2 = 0x88
    TEST_ASSERT_EQUAL_UINT8(0xFC, pc_dscp_to_tos(63));           // max DSCP, ECN still 0
    TEST_ASSERT_EQUAL_UINT8(0x04, pc_dscp_to_tos(0x41));         // 0x41 masked to 6 bits -> 1
}

static void test_default_dscp_roundtrip()
{
    TEST_ASSERT_EQUAL_UINT8(0, DiffServ.default_dscp());
    DiffServ.set_default(PC_DSCP_EF);
    TEST_ASSERT_EQUAL_UINT8(46, DiffServ.default_dscp());
    DiffServ.set_default(0xFF); // masked to the low 6 bits, not stored raw
    TEST_ASSERT_EQUAL_UINT8(63, DiffServ.default_dscp());
}

static void test_udp_dscp_roundtrip()
{
    TEST_ASSERT_EQUAL_UINT8(0, DiffServ.udp_dscp());
    DiffServ.set_udp(PC_DSCP_AF31);
    TEST_ASSERT_EQUAL_UINT8(26, DiffServ.udp_dscp());
    DiffServ.set_udp(0);
    TEST_ASSERT_EQUAL_UINT8(0, DiffServ.udp_dscp());
}

static void test_conn_set_dscp_writes_pcb_tos()
{
    pc_pcb pcb;
    pcb.tos = 0;
    conn_pool[0].pcb = &pcb;

    TEST_ASSERT_TRUE(pc_conn_set_dscp(0, PC_DSCP_EF));
    TEST_ASSERT_EQUAL_UINT8(0xB8, pcb.tos); // EF stamped into the DS field

    TEST_ASSERT_TRUE(pc_conn_set_dscp(0, PC_DSCP_CS0)); // live re-tag to best-effort
    TEST_ASSERT_EQUAL_UINT8(0, pcb.tos);
}

static void test_conn_set_dscp_rejects_bad_slot()
{
    conn_pool[0].pcb = NULL;
    TEST_ASSERT_FALSE(pc_conn_set_dscp(0, PC_DSCP_EF));   // no live pcb
    TEST_ASSERT_FALSE(pc_conn_set_dscp(255, PC_DSCP_EF)); // slot out of range
}

static void test_listen_set_dscp_override_and_sentinel()
{
    TEST_ASSERT_EQUAL(1, listener_add(0, 8080, PROTO_HTTP, PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT8(PC_DSCP_UNSET, listener_pool[0].dscp); // no override until set

    TEST_ASSERT_TRUE(pc_listen_set_dscp(8080, PC_DSCP_EF));
    TEST_ASSERT_EQUAL_UINT8(46, listener_pool[0].dscp);

    TEST_ASSERT_TRUE(pc_listen_set_dscp(8080, 0x7E)); // wide value masked to 6 bits (0x3E = 62)
    TEST_ASSERT_EQUAL_UINT8(62, listener_pool[0].dscp);

    TEST_ASSERT_TRUE(pc_listen_set_dscp(8080, PC_DSCP_UNSET)); // sentinel preserved, not masked to 63
    TEST_ASSERT_EQUAL_UINT8(PC_DSCP_UNSET, listener_pool[0].dscp);

    TEST_ASSERT_FALSE(pc_listen_set_dscp(9999, PC_DSCP_EF)); // no listener on that port
    Tcp.listener->stop(0);
}

// listener_accept_cb() applies the resolved per-connection DSCP straight to the new pcb's TOS
// field at accept time: a per-listener override wins over the server-wide default.
static void test_accept_cb_applies_per_listener_dscp_override()
{
    Tcp.conn->init(NULL);
    TEST_ASSERT_EQUAL(1, listener_add(0, 8080, PROTO_HTTP, PROTO_FALSE));
    TEST_ASSERT_TRUE(pc_listen_set_dscp(8080, PC_DSCP_EF));

    pc_pcb pcb;
    pcb.tos = 0;
    TEST_ASSERT_EQUAL_INT(PC_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb, PC_NET_OK));
    TEST_ASSERT_EQUAL_UINT8(0xB8, pcb.tos); // EF, straight from the listener override
    Tcp.listener->stop(0);
}

// With no per-listener override (PC_DSCP_UNSET, the default), the accept path falls back to
// the server-wide default DSCP.
static void test_accept_cb_falls_back_to_server_default_dscp()
{
    Tcp.conn->init(NULL);
    TEST_ASSERT_EQUAL(1, listener_add(0, 8080, PROTO_HTTP, PROTO_FALSE)); // no override -> UNSET
    DiffServ.set_default(PC_DSCP_AF41);

    pc_pcb pcb;
    pcb.tos = 0;
    TEST_ASSERT_EQUAL_INT(PC_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb, PC_NET_OK));
    TEST_ASSERT_EQUAL_UINT8(0x88, pcb.tos); // server-wide default applied
    Tcp.listener->stop(0);
}

// A resolved DSCP of 0 (best-effort, the setUp() baseline) leaves the pcb's TOS field alone
// rather than writing a redundant 0 over it.
static void test_accept_cb_skips_tos_write_at_best_effort()
{
    Tcp.conn->init(NULL);
    TEST_ASSERT_EQUAL(1, listener_add(0, 8080, PROTO_HTTP, PROTO_FALSE)); // UNSET override, default dscp == 0

    pc_pcb pcb;
    pcb.tos = 0x77; // sentinel: must survive untouched
    TEST_ASSERT_EQUAL_INT(PC_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb, PC_NET_OK));
    TEST_ASSERT_EQUAL_UINT8(0x77, pcb.tos);
    Tcp.listener->stop(0);
}

// listener_add_dynamic() (the SSH-remote-forward listener path) also resets the
// per-listener DSCP override to UNSET, same as the static listener_add() path.
static void test_dynamic_listener_inherits_default_dscp()
{
    TEST_ASSERT_EQUAL_INT32(1, listener_add_dynamic(1, 2222, PROTO_HTTP));
    TEST_ASSERT_EQUAL_UINT8(PC_DSCP_UNSET, listener_pool[1].dscp);
    Tcp.listener->stop_dynamic(1);
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
