// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Transport observability (PC_ENABLE_OBSERVABILITY): the Tcp.conn->on_event
// hook, the by-reason counters, the live CONN_CLOSING gauge, and that the real
// lwIP callbacks (recv FIN / error / timeout / local close / backpressure) drive
// the right counter and fire the hook.

#include "network_drivers/transport/tcp.h"
#include "network_drivers/transport/tcp.h"
#include <string.h>

#include <unity.h>

// Last event the hook saw.
static int g_calls;
static uint8_t g_slot;
static ConnState g_old, g_new;
static pc_conn_reason g_reason;

static void on_event(uint8_t slot, ConnState olds, ConnState news, pc_conn_reason reason)
{
    g_calls++;
    g_slot = slot;
    g_old = olds;
    g_new = news;
    g_reason = reason;
}

void setUp()
{
    set_millis(0);
    Tcp.conn->init(NULL);
    listener_add(0, 80, PROTO_HTTP, PROTO_FALSE);
    Tcp.conn->on_event(on_event);
    Tcp.conn->counters_reset();
    g_calls = 0;
}

void tearDown()
{
    Tcp.conn->on_event(NULL);
}

// ---- the notify machinery (drives every reason directly) -------------------

void test_transition_fires_hook_with_args()
{
    pc_obs_transition(2, CONN_FREE, CONN_ACTIVE, PC_CONN_R_ACCEPT);
    TEST_ASSERT_EQUAL(1, g_calls);
    TEST_ASSERT_EQUAL(2, g_slot);
    TEST_ASSERT_EQUAL(CONN_FREE, g_old);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, g_new);
    TEST_ASSERT_EQUAL(PC_CONN_R_ACCEPT, g_reason);
}

void test_each_reason_bumps_its_counter()
{
    pc_obs_transition(0, CONN_FREE, CONN_ACTIVE, PC_CONN_R_ACCEPT);
    pc_obs_transition(0, CONN_ACTIVE, CONN_FREE, PC_CONN_R_CLOSE_REMOTE);
    pc_obs_transition(0, CONN_ACTIVE, CONN_FREE, PC_CONN_R_CLOSE_LOCAL);
    pc_obs_transition(0, CONN_ACTIVE, CONN_FREE, PC_CONN_R_ERROR);
    pc_obs_transition(0, CONN_ACTIVE, CONN_FREE, PC_CONN_R_TIMEOUT);
    pc_obs_transition(0, CONN_ACTIVE, CONN_FREE, PC_CONN_R_ABORT);
    pc_obs_notice(0, CONN_ACTIVE, PC_CONN_R_BACKPRESSURE);
    pc_obs_notice(0, CONN_ACTIVE, PC_CONN_R_DEFER_DROP);

    pc_conn_counters c = Tcp.conn->counters_get();
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
    TEST_ASSERT_EQUAL_UINT32(0, Tcp.conn->counters_get().closing_gauge);

    conn_pool[1].state = CONN_CLOSING; // a slot actually dwelling
    TEST_ASSERT_EQUAL_UINT32(1, Tcp.conn->counters_get().closing_gauge);
    conn_pool[2].state = CONN_CLOSING;
    TEST_ASSERT_EQUAL_UINT32(2, Tcp.conn->counters_get().closing_gauge);

    conn_pool[1].state = CONN_FREE;
    conn_pool[2].state = CONN_FREE;
    TEST_ASSERT_EQUAL_UINT32(0, Tcp.conn->counters_get().closing_gauge);

    // DRAINED is gauge-only: it must not inflate any cumulative close counter.
    pc_obs_transition(1, CONN_CLOSING, CONN_FREE, PC_CONN_R_DRAINED);
    pc_conn_counters c = Tcp.conn->counters_get();
    TEST_ASSERT_EQUAL_UINT32(0, c.closes_local);
    TEST_ASSERT_EQUAL_UINT32(0, c.closes_remote);
}

void test_reset_clears_cumulative_not_derived_gauge()
{
    pc_obs_transition(0, CONN_FREE, CONN_ACTIVE, PC_CONN_R_ACCEPT);
    conn_pool[0].state = CONN_CLOSING; // a slot is genuinely closing
    Tcp.conn->counters_reset();
    pc_conn_counters c = Tcp.conn->counters_get();
    TEST_ASSERT_EQUAL_UINT32(0, c.accepts);       // cumulative cleared
    TEST_ASSERT_EQUAL_UINT32(1, c.closing_gauge); // derived from the pool, not by reset
}

void test_no_hook_after_unregister()
{
    Tcp.conn->on_event(NULL);
    pc_obs_transition(0, CONN_FREE, CONN_ACTIVE, PC_CONN_R_ACCEPT);
    TEST_ASSERT_EQUAL(0, g_calls);                               // hook silent
    TEST_ASSERT_EQUAL_UINT32(1, Tcp.conn->counters_get().accepts); // counters still move
}

// A notice (pc_obs_notice, distinct from the transition path above) with no hook registered
// still counts but must not dispatch - drives the null-callback arm of the notice's own guard.
void test_notice_without_hook_still_counts()
{
    Tcp.conn->on_event(NULL);
    pc_obs_notice(0, CONN_ACTIVE, PC_CONN_R_BACKPRESSURE);
    TEST_ASSERT_EQUAL(0, g_calls); // hook silent
    TEST_ASSERT_EQUAL_UINT32(1, Tcp.conn->counters_get().backpressure);
}

// ---- integration: the real transport callbacks ----------------------------

void test_recv_fin_counts_remote_close()
{
    pc_pcb pcb;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = &pcb;
    lowlevel_recv_cb(&conn_pool[0], &pcb, NULL, PC_NET_OK); // null pbuf = FIN
    TEST_ASSERT_EQUAL_UINT32(1, Tcp.conn->counters_get().closes_remote);
    TEST_ASSERT_EQUAL(PC_CONN_R_CLOSE_REMOTE, g_reason);
}

void test_err_cb_counts_error_close()
{
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = NULL;
    lowlevel_err_cb(&conn_pool[0], PC_NET_ERR_ABRT);
    TEST_ASSERT_EQUAL_UINT32(1, Tcp.conn->counters_get().closes_error);
    TEST_ASSERT_EQUAL(PC_CONN_R_ERROR, g_reason);
}

void test_timeout_sweep_counts_timeout()
{
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = NULL;
    conn_pool[0].owner = 0;
    conn_pool[0].last_activity_ms = 0;
    set_millis(CONN_TIMEOUT_MS + 1);
    Tcp.conn->check_timeouts(0);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL_UINT32(1, Tcp.conn->counters_get().closes_timeout);
    TEST_ASSERT_EQUAL(PC_CONN_R_TIMEOUT, g_reason);
}

void test_local_close_counts_local()
{
    // Tcp.conn->close(slot) reads the slot's pcb, frees the slot, and counts a
    // local close. The transport owns the teardown: the slot ends FREE/null.
    pc_pcb pcb;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = &pcb;
    Tcp.conn->close(0);
    TEST_ASSERT_EQUAL_UINT32(1, Tcp.conn->counters_get().closes_local);
    TEST_ASSERT_EQUAL(PC_CONN_R_CLOSE_LOCAL, g_reason);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
}

// Tcp.conn->abort_slot(slot) owns the hard-RST teardown: it frees the slot and
// counts an abort. A no-op (no count, no hook) when the slot has no live pcb.
void test_abort_slot_counts_abort_and_frees()
{
    pc_pcb pcb;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = &pcb;
    Tcp.conn->abort_slot(0);
    TEST_ASSERT_EQUAL_UINT32(1, Tcp.conn->counters_get().closes_abort);
    TEST_ASSERT_EQUAL(PC_CONN_R_ABORT, g_reason);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
}

void test_abort_slot_noop_on_free_slot()
{
    conn_pool[0].state = CONN_FREE;
    conn_pool[0].pcb = NULL;
    Tcp.conn->abort_slot(0);
    TEST_ASSERT_EQUAL_UINT32(0, Tcp.conn->counters_get().closes_abort);
    TEST_ASSERT_EQUAL(0, g_calls);
}

void test_backpressure_counts_when_ring_full()
{
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = NULL;
    conn_pool[0].rx_head = 0;
    conn_pool[0].rx_tail = 0;
    pc_pbuf p;
    memset(&p, 0, sizeof(p));
    p.tot_len = RX_BUF_SIZE * 2; // larger than the whole ring -> refused
    pc_net_err rc = lowlevel_recv_cb(&conn_pool[0], NULL, &p, PC_NET_OK);
    TEST_ASSERT_EQUAL(PC_NET_ERR_MEM, rc);
    TEST_ASSERT_EQUAL_UINT32(1, Tcp.conn->counters_get().backpressure);
    TEST_ASSERT_EQUAL(PC_CONN_R_BACKPRESSURE, g_reason);
}

// ---- CONN_CLOSING real dwell (part 2) -------------------------------------

void test_begin_close_dwells_then_drains_on_ack()
{
    pc_pcb pcb;
    pcb.snd_queuelen = 1; // response still in flight -> must dwell
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = &pcb;

    Tcp.conn->begin_close(0);
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state); // dwelling
    pc_conn_counters c = Tcp.conn->counters_get();
    TEST_ASSERT_EQUAL_UINT32(1, c.closes_local);
    TEST_ASSERT_EQUAL_UINT32(1, c.closing_gauge);
    TEST_ASSERT_EQUAL(PC_CONN_R_CLOSE_LOCAL, g_reason);

    // Peer ACKs the whole response -> the sent callback finalizes the close.
    pcb.snd_queuelen = 0;
    lowlevel_sent_cb(&conn_pool[0], &pcb, 100);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    c = Tcp.conn->counters_get();
    TEST_ASSERT_EQUAL_UINT32(0, c.closing_gauge);
    TEST_ASSERT_EQUAL(PC_CONN_R_DRAINED, g_reason);
}

void test_begin_close_finalizes_immediately_when_already_drained()
{
    pc_pcb pcb;
    pcb.snd_queuelen = 0; // nothing pending -> close now, no dwell
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = &pcb;

    Tcp.conn->begin_close(0);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    pc_conn_counters c = Tcp.conn->counters_get();
    TEST_ASSERT_EQUAL_UINT32(1, c.closes_local);
    TEST_ASSERT_EQUAL_UINT32(0, c.closing_gauge);
}

void test_begin_close_noop_if_not_active()
{
    conn_pool[0].state = CONN_FREE;
    Tcp.conn->begin_close(0);
    TEST_ASSERT_EQUAL_UINT32(0, Tcp.conn->counters_get().closes_local);
    TEST_ASSERT_EQUAL_UINT32(0, Tcp.conn->counters_get().closing_gauge);
}

void test_closing_timeout_reaps_stuck_slot()
{
    pc_pcb pcb;
    pcb.snd_queuelen = 1; // peer never ACKs -> would dwell forever
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = &pcb;
    conn_pool[0].owner = 0;
    set_millis(1000);

    Tcp.conn->begin_close(0);
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);

    // Before the bound: not reaped.
    set_millis(1000 + PC_CLOSING_TIMEOUT_MS - 1);
    Tcp.conn->check_timeouts(0);
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);

    // Past the bound: the sweep force-frees it (no pool leak).
    set_millis(1000 + PC_CLOSING_TIMEOUT_MS + 1);
    Tcp.conn->check_timeouts(0);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL_UINT32(0, Tcp.conn->counters_get().closing_gauge);
}

// DeterministicAsyncTCP::stop() posts a PC_CONN_R_ABORT transition for every ACTIVE/CLOSING
// slot it aborts (this specific call site, inside stop()'s own loop - not the direct
// pc_obs_transition() call test_each_reason_bumps_its_counter already drives).
void test_stop_posts_abort_transition_for_each_live_slot()
{
    pc_pcb pcb;
    conn_pool[0].id = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = &pcb;

    Tcp.conn->stop();
    TEST_ASSERT_EQUAL(PC_CONN_R_ABORT, g_reason);
    TEST_ASSERT_EQUAL_UINT32(1, Tcp.conn->counters_get().closes_abort);
}

// A slot that errors while already dwelling in CONN_CLOSING just releases the slot
// (DRAINED, gauge-only) - it must not also count as an ERROR close (its response was already
// sent; the session already reset). This call site is lowlevel_err_cb's own CLOSING branch,
// not the direct pc_obs_transition() call test_closing_gauge_is_derived_from_pool drives.
void test_err_cb_during_closing_counts_drained_not_error()
{
    pc_pcb pcb;
    pcb.snd_queuelen = 1; // dwell, don't finalize immediately
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = &pcb;
    Tcp.conn->begin_close(0);
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);
    Tcp.conn->counters_reset();

    lowlevel_err_cb(&conn_pool[0], PC_NET_ERR_ABRT);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
    TEST_ASSERT_EQUAL(PC_CONN_R_DRAINED, g_reason);
    pc_conn_counters c = Tcp.conn->counters_get();
    TEST_ASSERT_EQUAL_UINT32(0, c.closes_error); // not counted as an error close
    TEST_ASSERT_EQUAL_UINT32(0, c.closing_gauge);
}

// Tcp.listener->enqueue() failing (the target listener slot inactive) inside tcp.cpp's own
// enqueue() helper - reached from the real recv callback, not a direct pc_obs_notice() call
// - is observed as a defer-drop notice rather than silently losing the event.
void test_enqueue_failure_from_recv_cb_counts_defer_drop()
{
    pc_pcb pcb;
    conn_pool[0].id = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = &pcb;
    conn_pool[0].rx_head = 0;
    conn_pool[0].rx_tail = 0;
    conn_pool[0].listener_id = 1;          // listener 1 was never listener_add()'ed by setUp()
    listener_pool[1].active = PROTO_FALSE; // -> Tcp.listener->enqueue() reports failure

    uint8_t byte = 'x';
    pc_pbuf p;
    memset(&p, 0, sizeof(p));
    p.payload = &byte;
    p.len = 1;
    p.tot_len = 1;
    TEST_ASSERT_EQUAL_INT(PC_NET_OK, lowlevel_recv_cb(&conn_pool[0], &pcb, &p, PC_NET_OK));
    TEST_ASSERT_EQUAL_UINT32(1, Tcp.conn->counters_get().defer_drops);
    TEST_ASSERT_EQUAL(PC_CONN_R_DEFER_DROP, g_reason);
}

// listener_accept_cb() posts its own PC_CONN_R_ACCEPT transition on a successful accept -
// a different call site than the ones test_each_reason_bumps_its_counter drives directly.
// (listener_accept_cb is non-static specifically so this can be called directly - see
// listener.cpp / listener.h.)
void test_accept_cb_posts_accept_transition()
{
    pc_pcb pcb = {0};
    TEST_ASSERT_EQUAL_INT(PC_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb, PC_NET_OK));
    TEST_ASSERT_EQUAL(PC_CONN_R_ACCEPT, g_reason);
    TEST_ASSERT_EQUAL_UINT32(1, Tcp.conn->counters_get().accepts);
}

// listener_accept_cb()'s own enqueue-failure fallback (the target listener marked inactive)
// posts a defer-drop notice - the accept itself still succeeds, only the EVT_CONNECT
// post is dropped.
void test_accept_cb_enqueue_failure_posts_defer_drop()
{
    listener_pool[0].active = PROTO_FALSE;
    pc_pcb pcb = {0};
    TEST_ASSERT_EQUAL_INT(PC_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb, PC_NET_OK));
    TEST_ASSERT_EQUAL(PC_CONN_R_DEFER_DROP, g_reason);
    TEST_ASSERT_EQUAL_UINT32(1, Tcp.conn->counters_get().defer_drops);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state); // still claimed
}

void test_recv_during_closing_is_drained_not_processed()
{
    pc_pcb pcb;
    pcb.snd_queuelen = 1;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = &pcb;
    Tcp.conn->begin_close(0);
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);

    // Late inbound data while closing: acked + dropped, slot stays CLOSING.
    pc_pbuf p;
    memset(&p, 0, sizeof(p));
    p.tot_len = 8;
    pc_net_err rc = lowlevel_recv_cb(&conn_pool[0], &pcb, &p, PC_NET_OK);
    TEST_ASSERT_EQUAL(PC_NET_OK, rc);
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_transition_fires_hook_with_args);
    RUN_TEST(test_each_reason_bumps_its_counter);
    RUN_TEST(test_closing_gauge_is_derived_from_pool);
    RUN_TEST(test_reset_clears_cumulative_not_derived_gauge);
    RUN_TEST(test_no_hook_after_unregister);
    RUN_TEST(test_notice_without_hook_still_counts);
    RUN_TEST(test_recv_fin_counts_remote_close);
    RUN_TEST(test_err_cb_counts_error_close);
    RUN_TEST(test_timeout_sweep_counts_timeout);
    RUN_TEST(test_local_close_counts_local);
    RUN_TEST(test_abort_slot_counts_abort_and_frees);
    RUN_TEST(test_abort_slot_noop_on_free_slot);
    RUN_TEST(test_backpressure_counts_when_ring_full);
    // CONN_CLOSING real dwell
    RUN_TEST(test_begin_close_dwells_then_drains_on_ack);
    RUN_TEST(test_begin_close_finalizes_immediately_when_already_drained);
    RUN_TEST(test_begin_close_noop_if_not_active);
    RUN_TEST(test_closing_timeout_reaps_stuck_slot);
    RUN_TEST(test_recv_during_closing_is_drained_not_processed);
    RUN_TEST(test_stop_posts_abort_transition_for_each_live_slot);
    RUN_TEST(test_err_cb_during_closing_counts_drained_not_error);
    RUN_TEST(test_enqueue_failure_from_recv_cb_counts_defer_drop);
    RUN_TEST(test_accept_cb_posts_accept_transition);
    RUN_TEST(test_accept_cb_enqueue_failure_posts_defer_drop);
    return UNITY_END();
}
