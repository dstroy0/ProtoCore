// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for src/network_drivers/transport/tcp/tcp.h - the one table joining the halves of the
// transport.
//
// tcp.c holds no logic, so what there is to check is the wiring: that each half points at the
// module that owns it, and that every member of each half reaches the function its name promises.
//
// The second half of that is not pedantry. A table of same-typed function pointers initialized by
// position binds silently to whatever order the struct happens to declare, so a member inserted
// into the header shifts every binding after it and still compiles. These cases call each member
// and assert on behavior only that member produces, so a shifted binding fails here rather than on
// a device.
//
// Every entry takes the borrow its module's state lives in and reports on the namespace, so a case
// sets the operands, calls with the span, and reads the outcome back off the same handle.

#include "network_drivers/transport/tcp/lower/lower.h"
#include "network_drivers/transport/tcp/protocol/protocol.h"
#include "network_drivers/transport/tcp/server/server.h"
#include "network_drivers/transport/tcp/tcp.h"
#include "server/clock/clock.h"
#include <string.h>

#include <unity.h>

static protocore_pcb g_pcb;
static uint8_t *g_conn;     // the pool's borrow
static uint8_t *g_listener; // the listening side's
static uint8_t *g_lower;    // the seam's

// Move the virtual clock and take the stamp a dispatch pass would. Clock.ms is where the last
// reading landed, so a case that drives a module directly stamps for itself.
static void advance_to(uint32_t ms)
{
    set_millis(ms);
    Clock.millis(Clock.internal);
}

void setUp(void)
{
    g_conn = protocore_conn_pool_span();
    g_listener = protocore_tcp_listener_span();
    g_lower = protocore_tcp_lower_span();
    TEST_ASSERT_NOT_NULL(g_conn);
    TEST_ASSERT_NOT_NULL(g_listener);
    TEST_ASSERT_NOT_NULL(g_lower);

    advance_to(0);
    queue_stage_reset();
    mock_abort_call_reset();
    memset(&g_pcb, 0, sizeof(g_pcb));

    ConnPool.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    Tcp.conn->init(g_conn);

    Tcp.listener->stop_all(g_listener);
    TcpListener.idx = 0;
    TcpListener.bind.port = 80;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    Tcp.listener->add(g_listener);
}

void tearDown(void)
{
    Tcp.listener->stop_all(g_listener);
}

static void arm_slot(uint8_t slot)
{
    conn_pool[slot].id = slot;
    conn_pool[slot].pcb = &g_pcb;
    conn_pool[slot].listener_id = 0;
    conn_pool[slot].owner = 0;
    ConnPool.slot = slot;
    ConnPool.st = CONN_ACTIVE;
    Tcp.conn->set_state(g_conn);
}

// ---------------------------------------------------------------------------
// The halves
// ---------------------------------------------------------------------------

// Each half of the table is the module that owns it, reached through one symbol so no caller has
// to name the halves individually.
void test_each_half_points_at_the_module_that_owns_it(void)
{
    TEST_ASSERT_EQUAL_PTR(&ConnPool, Tcp.conn);
    TEST_ASSERT_EQUAL_PTR(&TcpListener, Tcp.listener);
#if PROTOCORE_NEED_CLIENT
    TEST_ASSERT_EQUAL_PTR(&TcpClient, Tcp.client);
#endif
}

void test_no_member_of_the_pool_half_is_unbound(void)
{
    TEST_ASSERT_NOT_NULL(Tcp.conn->alloc_free);
    TEST_ASSERT_NOT_NULL(Tcp.conn->sndbuf);
    TEST_ASSERT_NOT_NULL(Tcp.conn->init);
    TEST_ASSERT_NOT_NULL(Tcp.conn->stop);
    TEST_ASSERT_NOT_NULL(Tcp.conn->check_timeouts);
    TEST_ASSERT_NOT_NULL(Tcp.conn->timeout_ms);
    TEST_ASSERT_NOT_NULL(Tcp.conn->set_state);
    TEST_ASSERT_NOT_NULL(Tcp.conn->send);
    TEST_ASSERT_NOT_NULL(Tcp.conn->send_flush);
    TEST_ASSERT_NOT_NULL(Tcp.conn->flush);
    TEST_ASSERT_NOT_NULL(Tcp.conn->touch_active);
    TEST_ASSERT_NOT_NULL(Tcp.conn->ack_consumed);
    TEST_ASSERT_NOT_NULL(Tcp.conn->active_count);
    TEST_ASSERT_NOT_NULL(Tcp.conn->raw_send);
    TEST_ASSERT_NOT_NULL(Tcp.conn->close);
    TEST_ASSERT_NOT_NULL(Tcp.conn->begin_close);
    TEST_ASSERT_NOT_NULL(Tcp.conn->abort_slot);
    TEST_ASSERT_NOT_NULL(Tcp.conn->remote_ip);
    TEST_ASSERT_NOT_NULL(Tcp.conn->remote_addr);
}

void test_no_member_of_the_listener_half_is_unbound(void)
{
    TEST_ASSERT_NOT_NULL(Tcp.listener->stop);
    TEST_ASSERT_NOT_NULL(Tcp.listener->stop_all);
    TEST_ASSERT_NOT_NULL(Tcp.listener->stop_dynamic);
    TEST_ASSERT_NOT_NULL(Tcp.listener->add);
    TEST_ASSERT_NOT_NULL(Tcp.listener->add_dynamic);
    TEST_ASSERT_NOT_NULL(Tcp.listener->enqueue);
    TEST_ASSERT_NOT_NULL(Tcp.listener->set_dscp);
    TEST_ASSERT_NOT_NULL(Tcp.listener->accept_allowed);
    TEST_ASSERT_NOT_NULL(Tcp.listener->accept_throttle_reset);
    TEST_ASSERT_NOT_NULL(Tcp.listener->accept_allowed_ip);
    TEST_ASSERT_NOT_NULL(Tcp.listener->per_ip_throttle_reset);
    TEST_ASSERT_NOT_NULL(Tcp.listener->ip_allow_add);
    TEST_ASSERT_NOT_NULL(Tcp.listener->ip_allow_add_cidr);
    TEST_ASSERT_NOT_NULL(Tcp.listener->ip_allowed);
    TEST_ASSERT_NOT_NULL(Tcp.listener->ip_allowlist_reset);
}

// The seam below the pool is its own module, and detaching or resetting a control block is its
// call, not the pool's: RFC 9293 sec 3.9.2 puts every call into the lower-level protocol there.
void test_the_seam_below_the_pool_owns_the_raw_control_block_calls(void)
{
    TEST_ASSERT_NOT_NULL(TcpLower.detach);
    TEST_ASSERT_NOT_NULL(TcpLower.abort);
    TEST_ASSERT_NOT_NULL(TcpLower.marshal);
    TEST_ASSERT_NOT_NULL(TcpLower.set_ttl);
    TEST_ASSERT_NOT_NULL(TcpLower.apply_ttl);
}

// ---------------------------------------------------------------------------
// Each pool member reaches the function its name promises
// ---------------------------------------------------------------------------

void test_alloc_free_reports_a_slot_index(void)
{
    Tcp.conn->alloc_free(g_conn);
    TEST_ASSERT_EQUAL_INT32(0, ConnPool.i32);

    ConnPool.slot = 0;
    ConnPool.st = CONN_ACTIVE;
    Tcp.conn->set_state(g_conn);

    Tcp.conn->alloc_free(g_conn);
    TEST_ASSERT_EQUAL_INT32(1, ConnPool.i32); // moves on, so it is the allocator
}

void test_set_state_writes_the_state_it_is_given(void)
{
    ConnPool.slot = 3;
    ConnPool.st = CONN_CLOSING;
    Tcp.conn->set_state(g_conn);
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[3].state);

    ConnPool.slot = 3;
    ConnPool.st = CONN_FREE;
    Tcp.conn->set_state(g_conn);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[3].state);
}

void test_timeout_ms_reports_what_init_was_configured_with(void)
{
    ConnPool.life.conn_timeout_ms = 4242;
    Tcp.conn->init(g_conn);
    Tcp.conn->timeout_ms(g_conn);
    TEST_ASSERT_EQUAL_UINT32(4242, ConnPool.u32); // init and timeout_ms are not swapped
}

void test_active_count_counts_and_stop_clears(void)
{
    arm_slot(0);
    arm_slot(1);
    Tcp.conn->active_count(g_conn);
    TEST_ASSERT_EQUAL_UINT8(2, ConnPool.u8);

    Tcp.conn->stop(g_conn);
    Tcp.conn->active_count(g_conn);
    TEST_ASSERT_EQUAL_UINT8(0, ConnPool.u8);
}

void test_sndbuf_reports_the_room_the_stack_offers(void)
{
    arm_slot(0);
    mock_sndbuf_set(1234);
    ConnPool.slot = 0;
    Tcp.conn->sndbuf(g_conn);
    TEST_ASSERT_EQUAL_UINT16(1234, ConnPool.u16);
    mock_sndbuf_set(MOCK_SNDBUF_DEFAULT);
}

// send and send_flush both write; only send_flush pushes. Both are checked by what reaches the
// wire, which is the only thing that tells them from the other members of their type.
void test_send_and_send_flush_both_reach_the_wire(void)
{
    arm_slot(0);
    size_t before = protocore_net_host_tx_len;

    ConnPool.slot = 0;
    ConnPool.io.data = "AB";
    ConnPool.io.len = 2;
    Tcp.conn->send(g_conn);
    TEST_ASSERT_TRUE(ConnPool.ok);
    TEST_ASSERT_EQUAL_UINT(before + 2, protocore_net_host_tx_len);

    ConnPool.slot = 0;
    ConnPool.io.data = "CD";
    ConnPool.io.len = 2;
    Tcp.conn->send_flush(g_conn);
    TEST_ASSERT_TRUE(ConnPool.ok);
    TEST_ASSERT_EQUAL_UINT(before + 4, protocore_net_host_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("ABCD", protocore_net_host_tx + before, 4);
}

void test_raw_send_writes_to_a_control_block_with_no_slot(void)
{
    arm_slot(0); // raw_send needs the block bound to some slot to be considered live
    size_t before = protocore_net_host_tx_len;

    ConnPool.pcb = &g_pcb;
    ConnPool.io.data = "RAW";
    ConnPool.io.len = 3;
    Tcp.conn->raw_send(g_conn);
    TEST_ASSERT_TRUE(ConnPool.ok);
    TEST_ASSERT_EQUAL_UINT(before + 3, protocore_net_host_tx_len);
}

void test_flush_is_accepted_on_a_live_slot(void)
{
    arm_slot(0);
    ConnPool.slot = 0;
    Tcp.conn->flush(g_conn); // no wire effect to observe; it must not disturb the slot
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

void test_touch_active_moves_the_idle_stamp(void)
{
    arm_slot(0);
    conn_pool[0].last_activity_ms = 0;
    advance_to(777);
    ConnPool.slot = 0;
    Tcp.conn->touch_active(g_conn);
    TEST_ASSERT_EQUAL_UINT32(777, conn_pool[0].last_activity_ms);
}

void test_close_frees_the_slot_and_abort_slot_resets_it(void)
{
    arm_slot(0);
    ConnPool.slot = 0;
    Tcp.conn->close(g_conn);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);

    arm_slot(1);
    int aborts_before = mock_abort_call_count();
    ConnPool.slot = 1;
    Tcp.conn->abort_slot(g_conn);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state);
    TEST_ASSERT_EQUAL_INT(aborts_before + 1, mock_abort_call_count()); // the reset is what tells them apart
}

void test_begin_close_takes_the_slot_into_the_drain_dwell(void)
{
    arm_slot(0);
    g_pcb.snd_queuelen = 2;
    ConnPool.slot = 0;
    Tcp.conn->begin_close(g_conn);
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state); // not close(), which frees it
}

void test_check_timeouts_reaps_a_stale_slot(void)
{
    arm_slot(0);
    conn_pool[0].last_activity_ms = 0;
    Tcp.conn->timeout_ms(g_conn);
    advance_to(ConnPool.u32 + 1);

    ConnPool.life.worker_id = 0;
    Tcp.conn->check_timeouts(g_conn);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

void test_ack_consumed_reopens_the_window(void)
{
    arm_slot(0);
    protocore_pbuf p;
    uint8_t payload[16];
    memset(payload, 0xAB, sizeof(payload));
    memset(&p, 0, sizeof(p));
    p.payload = payload;
    p.len = 16;
    p.tot_len = 16;
    lowlevel_recv_cb(&conn_pool[0], &g_pcb, &p, PROTOCORE_NET_OK);

    uint8_t got[16];
    ConnPool.slot = 0;
    ConnPool.io.buf = got;
    ConnPool.io.cap = sizeof(got);
    Tcp.conn->read(g_conn);

    mock_recved_reset();
    ConnPool.slot = 0;
    Tcp.conn->ack_consumed(g_conn);
    TEST_ASSERT_EQUAL_UINT16(16, mock_recved_last());
}

// detach and abort belong to the seam below the pool, and act on a bare control block that no slot
// holds - which is what a late callback after a teardown reaches.
void test_detach_and_abort_act_on_a_bare_control_block(void)
{
    protocore_pcb bare;
    memset(&bare, 0, sizeof(bare));
    bare.arg = (void *)0x1234;

    TcpLower.pcb = &bare;
    TcpLower.slot = 0;
    TcpLower.detach(g_lower);
    TEST_ASSERT_NULL(bare.arg); // detach drops the back-reference

    int aborts_before = mock_abort_call_count();
    TcpLower.pcb = &bare;
    TcpLower.slot = 0;
    TcpLower.abort(g_lower);
    TEST_ASSERT_EQUAL_INT(aborts_before + 1, mock_abort_call_count());
}

// The TTL the seam stamps is configurable (RFC 9293 sec 3.9.2 MUST-49), and zero is refused: RFC
// 1122 sec 3.2.1.7 requires a datagram to leave with a non-zero TTL.
void test_set_ttl_takes_a_code_point_and_refuses_zero(void)
{
    TcpLower.len = 0;
    TcpLower.set_ttl(g_lower);
    TEST_ASSERT_FALSE(TcpLower.ok);

    TcpLower.len = 32;
    TcpLower.set_ttl(g_lower);
    TEST_ASSERT_TRUE(TcpLower.ok);

    TcpLower.pcb = &g_pcb;
    TcpLower.slot = 0;
    TcpLower.apply_ttl(g_lower);
    TEST_ASSERT_EQUAL_UINT8(32, g_pcb.ttl);

    TcpLower.len = PROTOCORE_TCP_TTL; // put it back for whatever runs next
    TcpLower.set_ttl(g_lower);
}

void test_remote_ip_and_remote_addr_report_the_peer(void)
{
    arm_slot(0);
    protocore_net_ip4_set(&g_pcb.remote_ip, 198, 51, 100, 3);

    ConnPool.slot = 0;
    Tcp.conn->remote_ip(g_conn);
    TEST_ASSERT_NOT_EQUAL(0, ConnPool.u32);

    protocore_ip out;
    ConnPool.slot = 0;
    ConnPool.out = &out;
    Tcp.conn->remote_addr(g_conn);
    TEST_ASSERT_TRUE(ConnPool.ok);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_IP_V4, out.family);
    TEST_ASSERT_EQUAL_UINT8(198, out.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(51, out.bytes[1]);
    TEST_ASSERT_EQUAL_UINT8(100, out.bytes[2]);
    TEST_ASSERT_EQUAL_UINT8(3, out.bytes[3]);
}

// ---------------------------------------------------------------------------
// Each listener member reaches the function its name promises
// ---------------------------------------------------------------------------

void test_add_binds_a_port_and_stop_takes_it_down(void)
{
    TcpListener.idx = 1;
    TcpListener.bind.port = 8081;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    Tcp.listener->add(g_listener);
    TEST_ASSERT_EQUAL_INT32(1, TcpListener.i32);
    TEST_ASSERT_EQUAL_UINT16(8081, listener_pool[1].port);

    TcpListener.idx = 1;
    Tcp.listener->stop(g_listener);
    TEST_ASSERT_FALSE(listener_pool[1].active);
}

// A dynamically started listener is a plaintext bridge, and stop_dynamic takes down only those.
void test_add_dynamic_and_stop_dynamic_are_their_own_pair(void)
{
    TcpListener.idx = 2;
    TcpListener.bind.port = 2222;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_TRUE; // a forwarded port is plaintext whatever this says
    Tcp.listener->add_dynamic(g_listener);
    TEST_ASSERT_EQUAL_INT32(1, TcpListener.i32);
    TEST_ASSERT_FALSE(listener_pool[2].tls);

    TcpListener.idx = 2;
    Tcp.listener->stop_dynamic(g_listener);
    TEST_ASSERT_FALSE(listener_pool[2].active);
    TEST_ASSERT_TRUE(listener_pool[0].active); // the one setUp bound is untouched
}

// set_dscp names the port, not the row, and preserves the sentinel rather than masking it.
void test_set_dscp_installs_a_code_point_on_the_port_it_names(void)
{
    TcpListener.bind.port = 80;
    TcpListener.bind.dscp = 46;
    Tcp.listener->set_dscp(g_listener);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TEST_ASSERT_EQUAL_UINT8(46, listener_pool[0].dscp);

    TcpListener.bind.port = 65535;
    TcpListener.bind.dscp = 46;
    Tcp.listener->set_dscp(g_listener);
    TEST_ASSERT_FALSE(TcpListener.ok); // no port bound there
}

void test_stop_all_takes_every_listener_down(void)
{
    TcpListener.idx = 1;
    TcpListener.bind.port = 8081;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    Tcp.listener->add(g_listener);

    Tcp.listener->stop_all(g_listener);
    TEST_ASSERT_FALSE(listener_pool[0].active);
    TEST_ASSERT_FALSE(listener_pool[1].active);
}
