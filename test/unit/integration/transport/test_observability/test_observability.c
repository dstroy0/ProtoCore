// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/transport/tcp/common.h"
#include "network_drivers/transport/tcp/protocol/protocol.h"
#include "network_drivers/transport/tcp/server/server.h"
#include "network_drivers/transport/tcp/tcp.h"
#include "server/clock/clock.h" // Clock.millis: what a dispatch pass stamps
#include <string.h>

#include <unity.h>

static int g_calls;
static uint8_t g_slot;
static ConnState g_old, g_new;
static protocore_conn_reason g_reason;

static void on_event(uint8_t slot, ConnState olds, ConnState news, protocore_conn_reason reason)
{
    g_calls++;
    g_slot = slot;
    g_old = olds;
    g_new = news;
    g_reason = reason;
}

// One turn of the dispatch, as far as the clock is concerned. service_once() stamps Clock.ms
// before anything reads the time, and Session.tick calls check_timeouts after it; a suite that
// drives check_timeouts directly has to stamp for itself or the sweep measures zero elapsed.
static void advance_to(uint32_t ms)
{
    set_millis(ms);
    Clock.millis(Clock.internal);
}

void setUp()
{
    advance_to(0);
    ConnPoolV.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(protocore_conn_pool_span());
    TcpListenerV.idx = 0;
    TcpListenerV.bind.port = 80;
    TcpListenerV.bind.proto = PROTO_HTTP;
    TcpListenerV.bind.tls = PROTO_FALSE;
    TcpListener.add(protocore_tcp_listener_span());
    ConnPoolV.obs.event_cb_in = on_event;
    ConnPool.on_event(protocore_conn_pool_span());
    ConnPool.counters_reset(protocore_conn_pool_span());
    g_calls = 0;
}

void tearDown()
{
    ConnPoolV.obs.event_cb_in = NULL;
    ConnPool.on_event(protocore_conn_pool_span());
}

void test_transition_fires_hook_with_args()
{
    protocore_obs_transition(2, CONN_FREE, CONN_ACTIVE, PROTOCORE_CONN_R_ACCEPT);
    TEST_ASSERT_EQUAL(1, g_calls);
    TEST_ASSERT_EQUAL(2, g_slot);
    TEST_ASSERT_EQUAL(CONN_FREE, g_old);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, g_new);
    TEST_ASSERT_EQUAL(PROTOCORE_CONN_R_ACCEPT, g_reason);
}

void test_each_reason_bumps_its_counter()
{
    protocore_obs_transition(0, CONN_FREE, CONN_ACTIVE, PROTOCORE_CONN_R_ACCEPT);
    protocore_obs_transition(0, CONN_ACTIVE, CONN_FREE, PROTOCORE_CONN_R_CLOSE_REMOTE);
    protocore_obs_transition(0, CONN_ACTIVE, CONN_FREE, PROTOCORE_CONN_R_CLOSE_LOCAL);
    protocore_obs_transition(0, CONN_ACTIVE, CONN_FREE, PROTOCORE_CONN_R_ERROR);
    protocore_obs_transition(0, CONN_ACTIVE, CONN_FREE, PROTOCORE_CONN_R_TIMEOUT);
    protocore_obs_transition(0, CONN_ACTIVE, CONN_FREE, PROTOCORE_CONN_R_ABORT);
    protocore_obs_notice(0, CONN_ACTIVE, PROTOCORE_CONN_R_BACKPRESSURE);
    protocore_obs_notice(0, CONN_ACTIVE, PROTOCORE_CONN_R_DEFER_DROP);

    ConnPool.counters_get(protocore_conn_pool_span());
    protocore_conn_counters c = ConnPoolV.obs.counters;
    TEST_ASSERT_EQUAL_UINT32(1, c.accepts);
    TEST_ASSERT_EQUAL_UINT32(1, c.closes_remote);
    TEST_ASSERT_EQUAL_UINT32(1, c.closes_local);
    TEST_ASSERT_EQUAL_UINT32(1, c.closes_error);
    TEST_ASSERT_EQUAL_UINT32(1, c.closes_timeout);
    TEST_ASSERT_EQUAL_UINT32(1, c.closes_abort);
    TEST_ASSERT_EQUAL_UINT32(1, c.backpressure);
    TEST_ASSERT_EQUAL_UINT32(1, c.defer_drops);
}

void test_closing_gauge_is_derived_from_pool()
{
    ConnPool.counters_get(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT32(0, ConnPoolV.obs.counters.closing_gauge);

    conn_pool[1].state = CONN_CLOSING;
    ConnPool.counters_get(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT32(1, ConnPoolV.obs.counters.closing_gauge);
    conn_pool[2].state = CONN_CLOSING;
    ConnPool.counters_get(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT32(2, ConnPoolV.obs.counters.closing_gauge);

    conn_pool[1].state = CONN_FREE;
    conn_pool[2].state = CONN_FREE;
    ConnPool.counters_get(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT32(0, ConnPoolV.obs.counters.closing_gauge);

    protocore_obs_transition(1, CONN_CLOSING, CONN_FREE, PROTOCORE_CONN_R_DRAINED);
    ConnPool.counters_get(protocore_conn_pool_span());
    protocore_conn_counters c = ConnPoolV.obs.counters;
    TEST_ASSERT_EQUAL_UINT32(0, c.closes_local);
    TEST_ASSERT_EQUAL_UINT32(0, c.closes_remote);
}

void test_reset_clears_cumulative_not_derived_gauge()
{
    protocore_obs_transition(0, CONN_FREE, CONN_ACTIVE, PROTOCORE_CONN_R_ACCEPT);
    conn_pool[0].state = CONN_CLOSING;
    ConnPool.counters_reset(protocore_conn_pool_span());
    ConnPool.counters_get(protocore_conn_pool_span());
    protocore_conn_counters c = ConnPoolV.obs.counters;
    TEST_ASSERT_EQUAL_UINT32(0, c.accepts);
    TEST_ASSERT_EQUAL_UINT32(1, c.closing_gauge);
}

void test_no_hook_after_unregister()
{
    ConnPoolV.obs.event_cb_in = NULL;
    ConnPool.on_event(protocore_conn_pool_span());
    protocore_obs_transition(0, CONN_FREE, CONN_ACTIVE, PROTOCORE_CONN_R_ACCEPT);
    TEST_ASSERT_EQUAL(0, g_calls);
    ConnPool.counters_get(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT32(1, ConnPoolV.obs.counters.accepts);
}

void test_notice_without_hook_still_counts()
{
    ConnPoolV.obs.event_cb_in = NULL;
    ConnPool.on_event(protocore_conn_pool_span());
    protocore_obs_notice(0, CONN_ACTIVE, PROTOCORE_CONN_R_BACKPRESSURE);
    TEST_ASSERT_EQUAL(0, g_calls);
    ConnPool.counters_get(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT32(1, ConnPoolV.obs.counters.backpressure);
}

void test_recv_fin_counts_remote_close()
{
    protocore_pcb pcb;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = &pcb;
    lowlevel_recv_cb(&conn_pool[0], &pcb, NULL, PROTOCORE_NET_OK);
    ConnPool.counters_get(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT32(1, ConnPoolV.obs.counters.closes_remote);
    TEST_ASSERT_EQUAL(PROTOCORE_CONN_R_CLOSE_REMOTE, g_reason);
}

void test_err_cb_counts_error_close()
{
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = NULL;
    lowlevel_err_cb(&conn_pool[0], PROTOCORE_NET_ERR_ABRT);
    ConnPool.counters_get(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT32(1, ConnPoolV.obs.counters.closes_error);
    TEST_ASSERT_EQUAL(PROTOCORE_CONN_R_ERROR, g_reason);
}

void test_timeout_sweep_counts_timeout()
{
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = NULL;
    conn_pool[0].owner = 0;
    conn_pool[0].last_activity_ms = 0;
    advance_to(CONN_TIMEOUT_MS + 1);
    ConnPoolV.life.worker_id = 0;
    ConnPool.check_timeouts(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    ConnPool.counters_get(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT32(1, ConnPoolV.obs.counters.closes_timeout);
    TEST_ASSERT_EQUAL(PROTOCORE_CONN_R_TIMEOUT, g_reason);
}

void test_local_close_counts_local()
{

    protocore_pcb pcb;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = &pcb;
    ConnPoolV.slot = 0;
    ConnPool.close(protocore_conn_pool_span());
    ConnPool.counters_get(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT32(1, ConnPoolV.obs.counters.closes_local);
    TEST_ASSERT_EQUAL(PROTOCORE_CONN_R_CLOSE_LOCAL, g_reason);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
}

void test_abort_slot_counts_abort_and_frees()
{
    protocore_pcb pcb;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = &pcb;
    ConnPoolV.slot = 0;
    ConnPool.abort_slot(protocore_conn_pool_span());
    ConnPool.counters_get(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT32(1, ConnPoolV.obs.counters.closes_abort);
    TEST_ASSERT_EQUAL(PROTOCORE_CONN_R_ABORT, g_reason);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
}

void test_abort_slot_noop_on_free_slot()
{
    conn_pool[0].state = CONN_FREE;
    conn_pool[0].pcb = NULL;
    ConnPoolV.slot = 0;
    ConnPool.abort_slot(protocore_conn_pool_span());
    ConnPool.counters_get(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT32(0, ConnPoolV.obs.counters.closes_abort);
    TEST_ASSERT_EQUAL(0, g_calls);
}

void test_backpressure_counts_when_ring_full()
{
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = NULL;
    conn_pool[0].rx_head = 0;
    conn_pool[0].rx_tail = 0;
    protocore_pbuf p;
    memset(&p, 0, sizeof(p));
    p.tot_len = RX_BUF_SIZE * 2;
    protocore_net_err rc = lowlevel_recv_cb(&conn_pool[0], NULL, &p, PROTOCORE_NET_OK);
    TEST_ASSERT_EQUAL(PROTOCORE_NET_ERR_MEM, rc);
    ConnPool.counters_get(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT32(1, ConnPoolV.obs.counters.backpressure);
    TEST_ASSERT_EQUAL(PROTOCORE_CONN_R_BACKPRESSURE, g_reason);
}

void test_begin_close_dwells_then_drains_on_ack()
{
    protocore_pcb pcb;
    pcb.snd_queuelen = 1;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = &pcb;

    ConnPoolV.slot = 0;
    ConnPool.begin_close(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);
    ConnPool.counters_get(protocore_conn_pool_span());
    protocore_conn_counters c = ConnPoolV.obs.counters;
    TEST_ASSERT_EQUAL_UINT32(1, c.closes_local);
    TEST_ASSERT_EQUAL_UINT32(1, c.closing_gauge);
    TEST_ASSERT_EQUAL(PROTOCORE_CONN_R_CLOSE_LOCAL, g_reason);

    pcb.snd_queuelen = 0;
    lowlevel_sent_cb(&conn_pool[0], &pcb, 100);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    ConnPool.counters_get(protocore_conn_pool_span());
    c = ConnPoolV.obs.counters;
    TEST_ASSERT_EQUAL_UINT32(0, c.closing_gauge);
    TEST_ASSERT_EQUAL(PROTOCORE_CONN_R_DRAINED, g_reason);
}

void test_begin_close_finalizes_immediately_when_already_drained()
{
    protocore_pcb pcb;
    pcb.snd_queuelen = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = &pcb;

    ConnPoolV.slot = 0;
    ConnPool.begin_close(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    ConnPool.counters_get(protocore_conn_pool_span());
    protocore_conn_counters c = ConnPoolV.obs.counters;
    TEST_ASSERT_EQUAL_UINT32(1, c.closes_local);
    TEST_ASSERT_EQUAL_UINT32(0, c.closing_gauge);
}

void test_begin_close_noop_if_not_active()
{
    conn_pool[0].state = CONN_FREE;
    ConnPoolV.slot = 0;
    ConnPool.begin_close(protocore_conn_pool_span());
    ConnPool.counters_get(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT32(0, ConnPoolV.obs.counters.closes_local);
    ConnPool.counters_get(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT32(0, ConnPoolV.obs.counters.closing_gauge);
}

void test_closing_timeout_reaps_stuck_slot()
{
    protocore_pcb pcb;
    pcb.snd_queuelen = 1;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = &pcb;
    conn_pool[0].owner = 0;
    advance_to(1000);

    ConnPoolV.slot = 0;
    ConnPool.begin_close(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);

    advance_to(1000 + PROTOCORE_CLOSING_TIMEOUT_MS - 1);
    ConnPoolV.life.worker_id = 0;
    ConnPool.check_timeouts(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);

    advance_to(1000 + PROTOCORE_CLOSING_TIMEOUT_MS + 1);
    ConnPoolV.life.worker_id = 0;
    ConnPool.check_timeouts(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    ConnPool.counters_get(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT32(0, ConnPoolV.obs.counters.closing_gauge);
}

void test_stop_posts_abort_transition_for_each_live_slot()
{
    protocore_pcb pcb;
    conn_pool[0].id = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = &pcb;

    ConnPool.stop(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL(PROTOCORE_CONN_R_ABORT, g_reason);
    ConnPool.counters_get(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT32(1, ConnPoolV.obs.counters.closes_abort);
}

void test_err_cb_during_closing_counts_drained_not_error()
{
    protocore_pcb pcb;
    pcb.snd_queuelen = 1;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = &pcb;
    ConnPoolV.slot = 0;
    ConnPool.begin_close(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);
    ConnPool.counters_reset(protocore_conn_pool_span());

    lowlevel_err_cb(&conn_pool[0], PROTOCORE_NET_ERR_ABRT);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
    TEST_ASSERT_EQUAL(PROTOCORE_CONN_R_DRAINED, g_reason);
    ConnPool.counters_get(protocore_conn_pool_span());
    protocore_conn_counters c = ConnPoolV.obs.counters;
    TEST_ASSERT_EQUAL_UINT32(0, c.closes_error);
    TEST_ASSERT_EQUAL_UINT32(0, c.closing_gauge);
}

void test_enqueue_failure_from_recv_cb_counts_defer_drop()
{
    protocore_pcb pcb;
    conn_pool[0].id = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = &pcb;
    conn_pool[0].rx_head = 0;
    conn_pool[0].rx_tail = 0;
    conn_pool[0].listener_id = 1;
    listener_pool[1].active = PROTO_FALSE;

    uint8_t byte = 'x';
    protocore_pbuf p;
    memset(&p, 0, sizeof(p));
    p.payload = &byte;
    p.len = 1;
    p.tot_len = 1;
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, lowlevel_recv_cb(&conn_pool[0], &pcb, &p, PROTOCORE_NET_OK));
    ConnPool.counters_get(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT32(1, ConnPoolV.obs.counters.defer_drops);
    TEST_ASSERT_EQUAL(PROTOCORE_CONN_R_DEFER_DROP, g_reason);
}

void test_accept_cb_posts_accept_transition()
{
    protocore_pcb pcb = {0};
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL(PROTOCORE_CONN_R_ACCEPT, g_reason);
    ConnPool.counters_get(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT32(1, ConnPoolV.obs.counters.accepts);
}

void test_accept_cb_enqueue_failure_posts_defer_drop()
{
    listener_pool[0].active = PROTO_FALSE;
    protocore_pcb pcb = {0};
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL(PROTOCORE_CONN_R_DEFER_DROP, g_reason);
    ConnPool.counters_get(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT32(1, ConnPoolV.obs.counters.defer_drops);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

void test_recv_during_closing_is_reset_not_processed()
{
    protocore_pcb pcb;
    pcb.snd_queuelen = 1;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = &pcb;
    ConnPoolV.slot = 0;
    ConnPool.begin_close(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);

    protocore_pbuf p;
    memset(&p, 0, sizeof(p));
    p.tot_len = 8;
    protocore_net_err rc = lowlevel_recv_cb(&conn_pool[0], &pcb, &p, PROTOCORE_NET_OK);
    TEST_ASSERT_EQUAL(PROTOCORE_NET_ERR_ABRT, rc);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL(PROTOCORE_CONN_R_ABORT, g_reason);
}
