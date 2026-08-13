// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for src/network_drivers/transport/tcp/tcp.h - the one table joining the three halves of
// the transport.
//
// tcp.c holds no logic, so what there is to check is the wiring: that each half points at the
// module that owns it, and that every member of each half reaches the function its name promises.
//
// The second half of that is not pedantry. A table of same-typed function pointers initialized by
// position binds silently to whatever order the struct happens to declare, so a member inserted
// into the header shifts every binding after it and still compiles. These cases call each member
// and assert on behavior only that member produces, so a shifted binding fails here rather than on
// a device.

#include "network_drivers/transport/tcp/tcp.h"
#include "network_drivers/transport/tcp/protocol/protocol.h"
#include "network_drivers/transport/tcp/server/server.h"
#include <string.h>

#include <unity.h>

static protocore_pcb g_pcb;

void setUp(void)
{
    set_millis(0);
    queue_stage_reset();
    mock_abort_call_reset();
    memset(&g_pcb, 0, sizeof(g_pcb));
    Tcp.conn->init(NULL);
    Tcp.listener->stop_all();
    Tcp.listener->add(0, 80, PROTO_HTTP, PROTO_FALSE);
}

void tearDown(void)
{
    Tcp.listener->stop_all();
}

static void arm_slot(uint8_t slot)
{
    conn_pool[slot].id = slot;
    conn_pool[slot].pcb = &g_pcb;
    conn_pool[slot].listener_id = 0;
    conn_pool[slot].owner = 0;
    Tcp.conn->set_state(slot, CONN_ACTIVE);
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
    TEST_ASSERT_NOT_NULL(Tcp.conn->detach);
    TEST_ASSERT_NOT_NULL(Tcp.conn->abort);
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
    TEST_ASSERT_NOT_NULL(Tcp.listener->accept_allowed);
    TEST_ASSERT_NOT_NULL(Tcp.listener->accept_throttle_reset);
    TEST_ASSERT_NOT_NULL(Tcp.listener->accept_allowed_ip);
    TEST_ASSERT_NOT_NULL(Tcp.listener->per_ip_throttle_reset);
    TEST_ASSERT_NOT_NULL(Tcp.listener->ip_allow_add);
    TEST_ASSERT_NOT_NULL(Tcp.listener->ip_allow_add_cidr);
    TEST_ASSERT_NOT_NULL(Tcp.listener->ip_allowed);
    TEST_ASSERT_NOT_NULL(Tcp.listener->ip_allowlist_reset);
}

// ---------------------------------------------------------------------------
// Each pool member reaches the function its name promises
// ---------------------------------------------------------------------------

void test_alloc_free_reports_a_slot_index(void)
{
    TEST_ASSERT_EQUAL_INT32(0, Tcp.conn->alloc_free());
    Tcp.conn->set_state(0, CONN_ACTIVE);
    TEST_ASSERT_EQUAL_INT32(1, Tcp.conn->alloc_free()); // moves on, so it is the allocator
}

void test_set_state_writes_the_state_it_is_given(void)
{
    Tcp.conn->set_state(3, CONN_CLOSING);
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[3].state);
    Tcp.conn->set_state(3, CONN_FREE);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[3].state);
}

void test_timeout_ms_reports_what_init_was_configured_with(void)
{
    WebServerConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.conn_timeout_ms = 4242;
    Tcp.conn->init(&cfg);
    TEST_ASSERT_EQUAL_UINT32(4242, Tcp.conn->timeout_ms()); // init and timeout_ms are not swapped
}

void test_active_count_counts_and_stop_clears(void)
{
    arm_slot(0);
    arm_slot(1);
    TEST_ASSERT_EQUAL_UINT8(2, Tcp.conn->active_count());
    Tcp.conn->stop();
    TEST_ASSERT_EQUAL_UINT8(0, Tcp.conn->active_count());
}

void test_sndbuf_reports_the_room_the_stack_offers(void)
{
    arm_slot(0);
    mock_sndbuf_set(1234);
    TEST_ASSERT_EQUAL_UINT16(1234, Tcp.conn->sndbuf(0));
    mock_sndbuf_set(MOCK_SNDBUF_DEFAULT);
}

// send and send_flush both write; only send_flush pushes. Both are checked by what reaches the
// wire, which is the only thing that tells them from the other members of their type.
void test_send_and_send_flush_both_reach_the_wire(void)
{
    arm_slot(0);
    size_t before = protocore_net_host_tx_len;
    TEST_ASSERT_TRUE(Tcp.conn->send(0, "AB", 2));
    TEST_ASSERT_EQUAL_UINT(before + 2, protocore_net_host_tx_len);
    TEST_ASSERT_TRUE(Tcp.conn->send_flush(0, "CD", 2));
    TEST_ASSERT_EQUAL_UINT(before + 4, protocore_net_host_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("ABCD", protocore_net_host_tx + before, 4);
}

void test_raw_send_writes_to_a_control_block_with_no_slot(void)
{
    arm_slot(0); // raw_send needs the block bound to some slot to be considered live
    size_t before = protocore_net_host_tx_len;
    TEST_ASSERT_TRUE(Tcp.conn->raw_send(&g_pcb, "RAW", 3));
    TEST_ASSERT_EQUAL_UINT(before + 3, protocore_net_host_tx_len);
}

void test_flush_is_accepted_on_a_live_slot(void)
{
    arm_slot(0);
    Tcp.conn->flush(0); // no wire effect to observe; it must not disturb the slot
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

void test_touch_active_moves_the_idle_stamp(void)
{
    arm_slot(0);
    conn_pool[0].last_activity_ms = 0;
    set_millis(777);
    Tcp.conn->touch_active(0);
    TEST_ASSERT_EQUAL_UINT32(777, conn_pool[0].last_activity_ms);
}

void test_close_frees_the_slot_and_abort_slot_resets_it(void)
{
    arm_slot(0);
    Tcp.conn->close(0);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);

    arm_slot(1);
    int aborts_before = mock_abort_call_count();
    Tcp.conn->abort_slot(1);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state);
    TEST_ASSERT_EQUAL_INT(aborts_before + 1, mock_abort_call_count()); // the reset is what tells them apart
}

void test_begin_close_takes_the_slot_into_the_drain_dwell(void)
{
    arm_slot(0);
    g_pcb.snd_queuelen = 2;
    Tcp.conn->begin_close(0);
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state); // not close(), which frees it
}

void test_check_timeouts_reaps_a_stale_slot(void)
{
    arm_slot(0);
    conn_pool[0].last_activity_ms = 0;
    set_millis(Tcp.conn->timeout_ms() + 1);
    Tcp.conn->check_timeouts(0);
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
    protocore_conn_read(0, got, sizeof(got));
    mock_recved_reset();
    Tcp.conn->ack_consumed(0);
    TEST_ASSERT_EQUAL_UINT16(16, mock_recved_last());
}

void test_detach_and_abort_act_on_a_bare_control_block(void)
{
    protocore_pcb bare;
    memset(&bare, 0, sizeof(bare));
    bare.arg = (void *)0x1234;

    Tcp.conn->detach(&bare);
    TEST_ASSERT_NULL(bare.arg); // detach drops the back-reference

    int aborts_before = mock_abort_call_count();
    Tcp.conn->abort(&bare);
    TEST_ASSERT_EQUAL_INT(aborts_before + 1, mock_abort_call_count());
}

void test_remote_ip_and_remote_addr_report_the_peer(void)
{
    arm_slot(0);
    protocore_net_ip4_set(&g_pcb.remote_ip, 198, 51, 100, 3);

    TEST_ASSERT_NOT_EQUAL(0, Tcp.conn->remote_ip(0));
    protocore_ip out;
    TEST_ASSERT_TRUE(Tcp.conn->remote_addr(0, &out));
    TEST_ASSERT_EQUAL(PROTOCORE_IP_V4, out.family);
    TEST_ASSERT_EQUAL_UINT8(198, out.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(3, out.bytes[3]);
}

// ---------------------------------------------------------------------------
// Each listener member reaches the function its name promises
// ---------------------------------------------------------------------------

void test_add_binds_and_stop_releases(void)
{
    TEST_ASSERT_EQUAL_INT32(1, Tcp.listener->add(1, 8080, PROTO_HTTP, PROTO_FALSE));
    TEST_ASSERT_TRUE(listener_pool[1].active);
    Tcp.listener->stop(1);
    TEST_ASSERT_FALSE(listener_pool[1].active);
}

void test_add_dynamic_binds_a_plaintext_bridge(void)
{
    TEST_ASSERT_EQUAL_INT32(1, Tcp.listener->add_dynamic(1, 2222, PROTO_HTTP));
    TEST_ASSERT_TRUE(listener_pool[1].active);
    TEST_ASSERT_FALSE(listener_pool[1].tls);
    Tcp.listener->stop_dynamic(1);
    TEST_ASSERT_FALSE(listener_pool[1].active);
}

void test_stop_all_clears_every_row(void)
{
    Tcp.listener->add(1, 8080, PROTO_HTTP, PROTO_FALSE);
    Tcp.listener->stop_all();
    TEST_ASSERT_FALSE(listener_pool[0].active);
    TEST_ASSERT_FALSE(listener_pool[1].active);
}

void test_enqueue_posts_to_the_listener_queue(void)
{
    TcpEvt evt = {EVT_DATA, 0, 9};
    TEST_ASSERT_TRUE(Tcp.listener->enqueue(0, &evt));

    TcpEvt got;
    TEST_ASSERT_EQUAL(PROTOCORE_PLATFORM_OK, protocore_platform_queue_recv(listener_pool[0].queue, &got, 0));
    TEST_ASSERT_EQUAL(EVT_DATA, got.type);
    TEST_ASSERT_EQUAL_UINT(9, got.data_len);
}

// The four gate members are distinguished by which state each one clears or consults.
void test_the_accept_throttle_members_pair_up(void)
{
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed(0));
    Tcp.listener->accept_throttle_reset();
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed(0)); // the reset gave the budget back
}

void test_the_per_address_throttle_members_pair_up(void)
{
    protocore_ip ip;
    memset(&ip, 0, sizeof(ip));
    ip.family = PROTOCORE_IP_V4;
    ip.bytes[0] = 10;
    ip.bytes[3] = 1;

    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed_ip(&ip, 0));
    Tcp.listener->per_ip_throttle_reset();
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed_ip(&ip, 0));
}

void test_the_allowlist_members_pair_up(void)
{
    protocore_ip net;
    memset(&net, 0, sizeof(net));
    net.family = PROTOCORE_IP_V4;
    net.bytes[0] = 10;

    protocore_ip peer = net;
    peer.bytes[3] = 5;

    TEST_ASSERT_TRUE(Tcp.listener->ip_allowed(&peer)); // empty list admits everything
    TEST_ASSERT_TRUE(Tcp.listener->ip_allow_add(&net, 8));

    protocore_ip outside;
    memset(&outside, 0, sizeof(outside));
    outside.family = PROTOCORE_IP_V4;
    outside.bytes[0] = 11;
    TEST_ASSERT_FALSE(Tcp.listener->ip_allowed(&outside)); // the rule took effect

    Tcp.listener->ip_allowlist_reset();
    TEST_ASSERT_TRUE(Tcp.listener->ip_allowed(&outside)); // and the reset cleared it
}

void test_the_cidr_form_adds_the_same_rule(void)
{
    TEST_ASSERT_TRUE(Tcp.listener->ip_allow_add_cidr("10.0.0.0/8"));
    protocore_ip outside;
    memset(&outside, 0, sizeof(outside));
    outside.family = PROTOCORE_IP_V4;
    outside.bytes[0] = 11;
    TEST_ASSERT_FALSE(Tcp.listener->ip_allowed(&outside));
}


// The runner is generated: Unity's auto/generate_test_runner.rb scans this file for
// void test_*(void) and emits main() with every case registered, stamped with the line each test
// is defined on. See test/gen_test_runners.py.
