// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for src/network_drivers/transport/tcp/protocol/protocol.h - the connection pool, its RX ring,
// and the stack callback seam, checked against the normative text of RFC 9293.
//
// The RFC anchors, quoted from docs/learn/rfc/text/rfc9293.txt:
//
//   sec 3.6    "A TCP implementation will reliably deliver all buffers SENT before the connection
//              was CLOSED"; case 1: "All segments preceding and including FIN will be retransmitted
//              until acknowledged."  MUST-12: on a close driven by the remote side "the local
//              application MUST be informed whether it closed normally or was aborted".
//   sec 3.6.1  MAY-1 permits a half-duplex close; SHLD-3: if new data is received after CLOSE is
//              called "its TCP implementation SHOULD send a RST to show that data was lost".
//   sec 3.8.6  SHLD-14: "A TCP receiver SHOULD NOT shrink the window, i.e., move the right window
//              edge to the left."  The receiver's algorithm (3.8.6.2.2) divides RCV.BUFF into
//              RCV.USER (received, not yet consumed) + RCV.WND (advertised) + reduction, and sets
//              RCV.WND to RCV.BUFF-RCV.USER.
//   sec 3.10.5 ABORT in ESTABLISHED sends a reset segment and flushes the queues.
//   sec 3.10.8 USER TIMEOUT: "flush all queues, signal the user 'error: connection aborted due to
//              user timeout' ... delete the TCB, enter the CLOSED state".
//
// This layer is not the RFC state machine - the stack under it owns LISTEN/ESTABLISHED/FIN-WAIT/etc
// (common.h says so, and it is right). What is checked here is the part of the RFC this layer
// really does own: how much window it reopens and when, whether a close drains before it releases,
// and which signal the layer above gets for a normal close versus an abort.

#include "network_drivers/transport/tcp/tcp.h"
#include "network_drivers/transport/tcp/protocol/protocol.h"
#include "network_drivers/transport/tcp/server/server.h"
#include <string.h>

#include <unity.h>

#define RING_USABLE (RX_BUF_SIZE - 1) // one slot reserved so full is distinct from empty

static protocore_pcb g_pcb;
static uint8_t g_payload[RX_BUF_SIZE * 2];

void setUp(void)
{
    set_millis(0);
    queue_stage_reset();
    mock_abort_call_reset();
    mock_recved_reset();
    memset(&g_pcb, 0, sizeof(g_pcb));
    Tcp.conn->init(NULL);
    Tcp.listener->add(0, 80, PROTO_HTTP, PROTO_FALSE);
    for (size_t i = 0; i < sizeof(g_payload); i++)
    {
        g_payload[i] = (uint8_t)(i & 0xFF);
    }
}

void tearDown(void)
{
    Tcp.listener->stop(0);
}

// Put slot 0 in the state an accept would leave it in, without needing a real accept.
static void arm_slot(uint8_t slot)
{
    conn_pool[slot].id = slot;
    conn_pool[slot].pcb = &g_pcb;
    conn_pool[slot].listener_id = 0;
    conn_pool[slot].owner = 0;
    conn_pool[slot].rx_head = 0;
    conn_pool[slot].rx_tail = 0;
    conn_pool[slot].rx_acked = 0;
    conn_pool[slot].last_activity_ms = 0;
    conn_pool[slot].req_start_ms = 0;
    Tcp.conn->set_state(slot, CONN_ACTIVE);
}

// One inbound segment of @p len bytes drawn from g_payload at @p off.
static protocore_net_err deliver(uint8_t slot, size_t off, uint16_t len)
{
    protocore_pbuf p;
    memset(&p, 0, sizeof(p));
    p.payload = &g_payload[off];
    p.len = len;
    p.tot_len = len;
    return lowlevel_recv_cb(&conn_pool[slot], &g_pcb, &p, PROTOCORE_NET_OK);
}

// Drain the listener queue into @p out; returns how many events were waiting.
static int drain_events(TcpEvt *out, int cap)
{
    int n = 0;
    while (n < cap && protocore_platform_queue_recv(listener_pool[0].queue, &out[n], 0) == PROTOCORE_PLATFORM_OK)
    {
        n++;
    }
    return n;
}

// ---------------------------------------------------------------------------
// RFC 9293 sec 3.8.6 - managing the window (ack-on-consume)
// ---------------------------------------------------------------------------

// The receive path must not reopen the window as it copies. RCV.WND is RCV.BUFF-RCV.USER
// (3.8.6.2.2), and bytes sitting unread in the ring ARE RCV.USER, so acking on copy would
// advertise space that is still occupied.
void test_recv_does_not_reopen_the_window_on_copy(void)
{
    arm_slot(0);
    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, deliver(0, 0, 64));
    TEST_ASSERT_EQUAL_UINT(64, protocore_conn_available(0));
    TEST_ASSERT_EQUAL_INT(0, mock_recved_call_count());
    TEST_ASSERT_EQUAL_UINT32(0, mock_recved_total());
}

// The window reopens by exactly what the reader took, and by nothing else.
void test_window_reopens_by_exactly_the_bytes_consumed(void)
{
    arm_slot(0);
    deliver(0, 0, 200);

    uint8_t buf[80];
    TEST_ASSERT_EQUAL_UINT(80, protocore_conn_read(0, buf, sizeof(buf)));
    Tcp.conn->ack_consumed(0);
    TEST_ASSERT_EQUAL_INT(1, mock_recved_call_count());
    TEST_ASSERT_EQUAL_UINT16(80, mock_recved_last());
    TEST_ASSERT_EQUAL_UINT32(80, mock_recved_total());

    // Bytes still unread stay charged against the window.
    TEST_ASSERT_EQUAL_UINT(120, protocore_conn_available(0));
}

// A second ack with nothing consumed in between issues no update: the right window edge does not
// move, which is what SHLD-14 asks of a receiver.
void test_ack_with_nothing_consumed_issues_no_window_update(void)
{
    arm_slot(0);
    deliver(0, 0, 32);

    uint8_t buf[32];
    protocore_conn_read(0, buf, sizeof(buf));
    Tcp.conn->ack_consumed(0);
    TEST_ASSERT_EQUAL_INT(1, mock_recved_call_count());

    Tcp.conn->ack_consumed(0);
    Tcp.conn->ack_consumed(0);
    TEST_ASSERT_EQUAL_INT(1, mock_recved_call_count());
    TEST_ASSERT_EQUAL_UINT32(32, mock_recved_total());
}

// The invariant behind ack-on-consume: across an arbitrary interleaving of arrivals and partial
// drains, the total window reopened equals the total bytes the reader took - never more. More
// would advertise buffer space that unread bytes still occupy.
void test_reopened_window_never_exceeds_bytes_consumed(void)
{
    arm_slot(0);
    const uint16_t arrive[] = {300, 100, 400, 250, 64, 500};
    const size_t take[] = {128, 64, 700, 32, 300, 200};
    uint32_t consumed_total = 0;
    uint8_t buf[1024];

    for (size_t r = 0; r < sizeof(arrive) / sizeof(arrive[0]); r++)
    {
        if (arrive[r] <= protocore_ring_free(&conn_pool[0].rx_head, &conn_pool[0].rx_tail, RX_BUF_SIZE))
        {
            deliver(0, r * 32, arrive[r]);
        }
        consumed_total += (uint32_t)protocore_conn_read(0, buf, take[r]);
        Tcp.conn->ack_consumed(0);

        TEST_ASSERT_EQUAL_UINT32(consumed_total, mock_recved_total());
        // Unread bytes plus the space handed back never exceeds the buffer.
        TEST_ASSERT_TRUE(protocore_conn_available(0) <= RING_USABLE);
    }
}

// The ack cursor is a modular difference over RX_BUF_SIZE. At the largest occupancy the ring can
// reach, consuming everything must report RING_USABLE, not zero - a full-buffer drain reporting
// zero would leave the window shut with an empty ring and stall the connection.
void test_ack_at_maximum_occupancy_reports_the_whole_ring(void)
{
    arm_slot(0);
    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, deliver(0, 0, (uint16_t)RING_USABLE));
    TEST_ASSERT_EQUAL_UINT(RING_USABLE, protocore_conn_available(0));

    uint8_t buf[RX_BUF_SIZE];
    TEST_ASSERT_EQUAL_UINT(RING_USABLE, protocore_conn_read(0, buf, sizeof(buf)));
    Tcp.conn->ack_consumed(0);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)RING_USABLE, mock_recved_last());
    TEST_ASSERT_EQUAL_UINT(0, protocore_conn_available(0));
}

// The same accounting across the wrap: head and tail both past the end of the buffer.
void test_ack_is_correct_across_the_ring_wrap(void)
{
    arm_slot(0);
    uint8_t buf[RX_BUF_SIZE];

    // Push the cursors close to the wrap point and drain, so the next segment straddles it.
    deliver(0, 0, (uint16_t)(RX_BUF_SIZE - 16));
    protocore_conn_read(0, buf, RX_BUF_SIZE - 16);
    Tcp.conn->ack_consumed(0);
    mock_recved_reset();

    deliver(0, 0, 64); // 16 bytes to the end of the buffer, 48 after the wrap
    TEST_ASSERT_EQUAL_UINT(64, protocore_conn_available(0));
    TEST_ASSERT_EQUAL_UINT(64, protocore_conn_read(0, buf, sizeof(buf)));
    Tcp.conn->ack_consumed(0);
    TEST_ASSERT_EQUAL_UINT16(64, mock_recved_last());

    // The bytes survived the wrap in order.
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_payload, buf, 64);
}

// Lossless backpressure: a segment that will not fit is refused whole. The stack keeps it and
// redelivers, so no received octet is dropped and no window is opened for space that does not exist.
void test_oversized_segment_is_refused_whole_and_opens_no_window(void)
{
    arm_slot(0);
    deliver(0, 0, (uint16_t)(RING_USABLE - 10)); // ring now has 10 bytes free
    size_t occupancy = protocore_conn_available(0);
    mock_recved_reset();

    TEST_ASSERT_EQUAL(PROTOCORE_NET_ERR_MEM, deliver(0, 0, 64));
    TEST_ASSERT_EQUAL_UINT(occupancy, protocore_conn_available(0)); // nothing was taken
    TEST_ASSERT_EQUAL_INT(0, mock_recved_call_count());             // and no window opened
}

// A refused segment is not progress. Refreshing the idle timer on refusal would keep a connection
// that never drains alive forever, so a peer could hold a pool slot indefinitely by filling the ring.
void test_refused_segment_does_not_refresh_the_idle_timer(void)
{
    arm_slot(0);
    set_millis(1000);
    deliver(0, 0, (uint16_t)(RING_USABLE - 10));
    TEST_ASSERT_EQUAL_UINT32(1000, conn_pool[0].last_activity_ms);

    set_millis(2000);
    TEST_ASSERT_EQUAL(PROTOCORE_NET_ERR_MEM, deliver(0, 0, 64));
    TEST_ASSERT_EQUAL_UINT32(1000, conn_pool[0].last_activity_ms); // unchanged
}

// Accepted data is progress, and does refresh it.
void test_accepted_segment_refreshes_the_idle_timer(void)
{
    arm_slot(0);
    set_millis(1000);
    deliver(0, 0, 16);
    TEST_ASSERT_EQUAL_UINT32(1000, conn_pool[0].last_activity_ms);
    set_millis(2500);
    deliver(0, 0, 16);
    TEST_ASSERT_EQUAL_UINT32(2500, conn_pool[0].last_activity_ms);
}

// The request-completion deadline arms on the first byte and is not pushed out by later bytes -
// that separation is what a trickle cannot defeat.
void test_request_deadline_arms_once_and_does_not_slide(void)
{
    arm_slot(0);
    set_millis(500);
    deliver(0, 0, 1);
    TEST_ASSERT_EQUAL_UINT32(500, conn_pool[0].req_start_ms);

    set_millis(3000);
    deliver(0, 0, 1);
    TEST_ASSERT_EQUAL_UINT32(500, conn_pool[0].req_start_ms); // still the first byte's stamp
}

// A first byte at millis()==0 must still arm the deadline; 0 is the "not armed" sentinel, so the
// timestamp is biased to 1 rather than left looking unarmed.
void test_request_deadline_arms_at_time_zero(void)
{
    arm_slot(0);
    set_millis(0);
    deliver(0, 0, 1);
    TEST_ASSERT_EQUAL_UINT32(1, conn_pool[0].req_start_ms);
}

// A slot that is not receiving acks nothing: ack_consumed is a no-op off the active state.
void test_ack_consumed_is_inert_off_the_active_state(void)
{
    arm_slot(0);
    deliver(0, 0, 64);
    uint8_t buf[64];
    protocore_conn_read(0, buf, sizeof(buf));

    Tcp.conn->set_state(0, CONN_CLOSING);
    Tcp.conn->ack_consumed(0);
    TEST_ASSERT_EQUAL_INT(0, mock_recved_call_count());

    Tcp.conn->set_state(0, CONN_ACTIVE);
    conn_pool[0].pcb = NULL;
    Tcp.conn->ack_consumed(0);
    TEST_ASSERT_EQUAL_INT(0, mock_recved_call_count());
}

void test_ack_consumed_rejects_an_out_of_range_slot(void)
{
    Tcp.conn->ack_consumed(MAX_CONNS);
    Tcp.conn->ack_consumed((uint8_t)(MAX_CONNS + 40));
    TEST_ASSERT_EQUAL_INT(0, mock_recved_call_count());
}

// ---------------------------------------------------------------------------
// RFC 9293 sec 3.6 - closing a connection
// ---------------------------------------------------------------------------

// sec 3.6: "A TCP implementation will reliably deliver all buffers SENT before the connection was
// CLOSED". The dwell is how that is honored with a fixed pool: the slot is held, not recycled,
// while the peer still owes an ACK for what was sent.
void test_close_dwells_while_the_peer_still_owes_an_ack(void)
{
    arm_slot(0);
    g_pcb.snd_queuelen = 3; // response still unacknowledged

    Tcp.conn->begin_close(0);
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NOT_NULL(conn_pool[0].pcb); // the slot is still bound to its connection

    // Peer acks the last of it; the sent callback finalizes.
    g_pcb.snd_queuelen = 0;
    lowlevel_sent_cb(&conn_pool[0], &g_pcb, 3);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
}

// Nothing outstanding at close time: the slot is released in the same call, no dwell.
void test_close_with_a_drained_queue_releases_immediately(void)
{
    arm_slot(0);
    g_pcb.snd_queuelen = 0;
    Tcp.conn->begin_close(0);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

// A sent callback while data is still outstanding must not release the slot early - that would
// recycle it under bytes the peer has not acknowledged.
void test_sent_callback_does_not_release_a_slot_that_is_still_draining(void)
{
    arm_slot(0);
    g_pcb.snd_queuelen = 5;
    Tcp.conn->begin_close(0);
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);

    lowlevel_sent_cb(&conn_pool[0], &g_pcb, 1); // partial ack, queue not empty
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);
}

// The dwell expiring is the case sec 3.9.1.4 names outright: "It may happen (if the user-level
// protocol is not well thought out) that the closing side is unable to get rid of all its data
// before timing out. In this event, CLOSE turns into ABORT, and the closing TCP peer gives up."
// sec 3.10.8 reaches the same place for a user timeout - "flush all queues, signal the user
// 'error: connection aborted due to user timeout' ... delete the TCB, enter the CLOSED state".
//
// So giving up on the peer's ACK is an abort, not a graceful close. sec 3.6's promise that
// segments up to and including the FIN are retransmitted until acknowledged describes a close that
// completes; it does not cover the end that stops waiting.
void test_dwell_expiry_aborts_the_connection(void)
{
    arm_slot(0);
    g_pcb.snd_queuelen = 2; // peer never acks
    set_millis(1000);
    Tcp.conn->begin_close(0);
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);

    int aborts_before = mock_abort_call_count();
    set_millis(1000 + PROTOCORE_CLOSING_TIMEOUT_MS);
    Tcp.conn->check_timeouts(0);

    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
    TEST_ASSERT_EQUAL_INT(aborts_before + 1, mock_abort_call_count()); // CLOSE turned into ABORT
}

// The dwell is a deadline, not a grace period that a quiet peer can extend: before it expires the
// slot is left alone.
void test_dwell_is_not_reaped_before_its_deadline(void)
{
    arm_slot(0);
    g_pcb.snd_queuelen = 2;
    set_millis(1000);
    Tcp.conn->begin_close(0);

    set_millis(1000 + PROTOCORE_CLOSING_TIMEOUT_MS - 1);
    Tcp.conn->check_timeouts(0);
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);
}

// begin_close only acts on a live connection; a slot already freed by an error during the write is
// left alone rather than dragged back into a dwell.
void test_begin_close_is_a_no_op_off_the_active_state(void)
{
    arm_slot(0);
    Tcp.conn->set_state(0, CONN_FREE);
    Tcp.conn->begin_close(0);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);

    Tcp.conn->begin_close(MAX_CONNS); // out of range
    Tcp.conn->begin_close((uint8_t)(MAX_CONNS + 7));
}

// ---------------------------------------------------------------------------
// RFC 9293 sec 3.6.1 - data arriving after the application has closed
// ---------------------------------------------------------------------------

// This transport takes the half-duplex close sec 3.6.1 MAY-1 permits: once a slot is CONN_CLOSING
// the application cannot go on reading, so data arriving after the close can never be delivered.
// SHLD-3 covers exactly that case - "if new data is received after CLOSE is called, its TCP
// implementation SHOULD send a RST to show that data was lost".
//
// Acknowledging the bytes instead would be a promise to deliver them. sec 3.10.7 makes the two the
// same act: "When the TCP endpoint takes responsibility for delivering the data to the user, it
// must also acknowledge the receipt of the data." Acknowledging and then discarding tells the peer
// its data arrived and never tells it otherwise, which is the outcome SHLD-3 exists to prevent.
void test_data_after_close_resets_the_connection(void)
{
    arm_slot(0);
    g_pcb.snd_queuelen = 4; // hold the slot in the dwell
    Tcp.conn->begin_close(0);
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);

    mock_recved_reset();
    int aborts_before = mock_abort_call_count();

    TEST_ASSERT_EQUAL(PROTOCORE_NET_ERR_ABRT, deliver(0, 0, 48));

    TEST_ASSERT_EQUAL_INT(aborts_before + 1, mock_abort_call_count()); // the RST SHLD-3 asks for
    TEST_ASSERT_EQUAL_INT(0, mock_recved_call_count());                // never acknowledged
    TEST_ASSERT_EQUAL_UINT(0, protocore_conn_available(0));            // never ringed
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
}

// A zero-length segment carries no data, so nothing was lost and there is nothing to reset over.
void test_a_zero_length_segment_during_the_dwell_does_not_reset(void)
{
    arm_slot(0);
    g_pcb.snd_queuelen = 4;
    Tcp.conn->begin_close(0);
    int aborts_before = mock_abort_call_count();

    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, deliver(0, 0, 0));
    TEST_ASSERT_EQUAL_INT(aborts_before, mock_abort_call_count());
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);
}

// A peer FIN during the dwell means both sides are done. It must not be mistaken for the ordinary
// remote-close path and must not re-post a close event.
void test_peer_fin_during_the_dwell_leaves_the_slot_closing(void)
{
    arm_slot(0);
    g_pcb.snd_queuelen = 4;
    Tcp.conn->begin_close(0);
    drain_events((TcpEvt[4]){0}, 4);

    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, lowlevel_recv_cb(&conn_pool[0], &g_pcb, NULL, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);

    TcpEvt evts[4];
    TEST_ASSERT_EQUAL_INT(0, drain_events(evts, 4));
}

// ---------------------------------------------------------------------------
// RFC 9293 sec 3.6 MUST-12 - normal close and abort are distinguishable
// ---------------------------------------------------------------------------
//
// "If the local TCP connection is closed by the remote side due to a FIN or RST received from the
// remote side, then the local application MUST be informed whether it closed normally or was
// aborted (MUST-12)." At this seam the signal is the event type on the listener queue.

void test_remote_fin_reports_a_normal_close(void)
{
    arm_slot(0);
    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, lowlevel_recv_cb(&conn_pool[0], &g_pcb, NULL, PROTOCORE_NET_OK));

    TcpEvt evts[4];
    TEST_ASSERT_EQUAL_INT(1, drain_events(evts, 4));
    TEST_ASSERT_EQUAL(EVT_DISCONNECT, evts[0].type);
    TEST_ASSERT_EQUAL_UINT8(0, evts[0].slot_id);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

void test_stack_error_reports_an_abort(void)
{
    arm_slot(0);
    lowlevel_err_cb(&conn_pool[0], PROTOCORE_NET_ERR_RST);

    TcpEvt evts[4];
    TEST_ASSERT_EQUAL_INT(1, drain_events(evts, 4));
    TEST_ASSERT_EQUAL(EVT_ERROR, evts[0].type);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
}

// The two signals are different values, so a session layer switching on the type can tell a normal
// close from an abort - which is the whole of MUST-12 at this boundary.
void test_normal_close_and_abort_are_distinct_signals(void)
{
    TEST_ASSERT_NOT_EQUAL(EVT_DISCONNECT, EVT_ERROR);
}

// The error callback fires after the stack has already freed the control block, so this layer must
// not close or abort it again - it only drops its own reference.
void test_error_callback_does_not_touch_the_freed_control_block(void)
{
    arm_slot(0);
    int aborts_before = mock_abort_call_count();
    lowlevel_err_cb(&conn_pool[0], PROTOCORE_NET_ERR_RST);
    TEST_ASSERT_EQUAL_INT(aborts_before, mock_abort_call_count());
    TEST_ASSERT_NULL(conn_pool[0].pcb);
}

// An error on a slot already in the dwell is not a second close: its response was sent and the
// session was already told. The slot is released without another event.
void test_error_during_the_dwell_posts_no_further_event(void)
{
    arm_slot(0);
    g_pcb.snd_queuelen = 2;
    Tcp.conn->begin_close(0);
    drain_events((TcpEvt[4]){0}, 4);

    lowlevel_err_cb(&conn_pool[0], PROTOCORE_NET_ERR_RST);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);

    TcpEvt evts[4];
    TEST_ASSERT_EQUAL_INT(0, drain_events(evts, 4));
}

void test_callbacks_tolerate_a_null_slot_argument(void)
{
    TEST_ASSERT_EQUAL(PROTOCORE_NET_ERR_VAL, lowlevel_recv_cb(NULL, &g_pcb, NULL, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, lowlevel_sent_cb(NULL, &g_pcb, 0));
    lowlevel_err_cb(NULL, PROTOCORE_NET_ERR_RST);
}

// A segment for a slot that is neither active nor closing is refused rather than ringed.
void test_recv_on_a_free_slot_is_refused(void)
{
    conn_pool[0].id = 0;
    Tcp.conn->set_state(0, CONN_FREE);
    TEST_ASSERT_EQUAL(PROTOCORE_NET_ERR_VAL, deliver(0, 0, 16));
    TEST_ASSERT_EQUAL_UINT(0, protocore_conn_available(0));
}

// ---------------------------------------------------------------------------
// RFC 9293 sec 3.10.8 - USER TIMEOUT
// ---------------------------------------------------------------------------
//
// "For any state if the user timeout expires, flush all queues, signal the user 'error: connection
// aborted due to user timeout' in general and for any outstanding calls, delete the TCB, enter the
// CLOSED state, and return." The idle sweep is that timeout: it aborts (sec 3.10.5, a reset, queues
// flushed) and signals an error - not a graceful disconnect.

void test_idle_timeout_aborts_the_connection_and_signals_an_error(void)
{
    arm_slot(0);
    set_millis(1000);
    conn_pool[0].last_activity_ms = 1000;
    drain_events((TcpEvt[4]){0}, 4);

    int aborts_before = mock_abort_call_count();
    set_millis(1000 + Tcp.conn->timeout_ms());
    Tcp.conn->check_timeouts(0);

    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
    TEST_ASSERT_EQUAL_INT(aborts_before + 1, mock_abort_call_count()); // a reset, per sec 3.10.5

    TcpEvt evts[4];
    TEST_ASSERT_EQUAL_INT(1, drain_events(evts, 4));
    TEST_ASSERT_EQUAL(EVT_ERROR, evts[0].type); // "aborted", not a normal close
}

void test_idle_timeout_does_not_fire_before_its_deadline(void)
{
    arm_slot(0);
    set_millis(1000);
    conn_pool[0].last_activity_ms = 1000;
    set_millis(1000 + Tcp.conn->timeout_ms() - 1);
    Tcp.conn->check_timeouts(0);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

// The elapsed comparison is an unsigned subtraction, so it stays correct when the millisecond
// counter wraps past 2^32 mid-connection.
void test_idle_timeout_survives_the_millisecond_counter_wrap(void)
{
    arm_slot(0);
    conn_pool[0].last_activity_ms = 0xFFFFF000u;
    set_millis(0xFFFFF000u + Tcp.conn->timeout_ms()); // wraps through zero
    Tcp.conn->check_timeouts(0);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

// Each worker sweeps only the slots it owns, so a slot belonging to another worker is untouched.
void test_sweep_skips_slots_owned_by_another_worker(void)
{
    arm_slot(0);
    conn_pool[0].owner = 1;
    conn_pool[0].last_activity_ms = 0;
    set_millis(Tcp.conn->timeout_ms() + 1000);
    Tcp.conn->check_timeouts(0);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

// A response still being paged out is not idle. touch_active restarts the clock so the sweep does
// not truncate a body larger than one window.
void test_touch_active_defers_the_sweep_for_a_streaming_response(void)
{
    arm_slot(0);
    conn_pool[0].last_activity_ms = 0;
    set_millis(Tcp.conn->timeout_ms() - 1);
    Tcp.conn->touch_active(0);
    set_millis(Tcp.conn->timeout_ms() + 10);
    Tcp.conn->check_timeouts(0);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

void test_touch_active_is_bounded_and_state_guarded(void)
{
    arm_slot(0);
    Tcp.conn->set_state(0, CONN_CLOSING);
    conn_pool[0].last_activity_ms = 7;
    set_millis(9999);
    Tcp.conn->touch_active(0);
    TEST_ASSERT_EQUAL_UINT32(7, conn_pool[0].last_activity_ms); // only CONN_ACTIVE is refreshed

    Tcp.conn->touch_active(MAX_CONNS); // out of range: no write, no crash
}

// ---------------------------------------------------------------------------
// Slot accounting
// ---------------------------------------------------------------------------

// The allocator takes the lowest slot that is both free and unheld. Holding is what keeps an index
// out of circulation while a transfer still owns its bytes - the pool's stand-in for the reason
// sec 3.6.1 keeps a closed connection's identifier out of use (MUST-13's TIME-WAIT linger).
void test_allocator_takes_the_lowest_free_slot(void)
{
    TEST_ASSERT_EQUAL_INT32(0, Tcp.conn->alloc_free());
    Tcp.conn->set_state(0, CONN_ACTIVE);
    TEST_ASSERT_EQUAL_INT32(1, Tcp.conn->alloc_free());
    Tcp.conn->set_state(0, CONN_FREE);
    TEST_ASSERT_EQUAL_INT32(0, Tcp.conn->alloc_free());
}

void test_a_held_slot_is_not_allocatable_though_it_reads_free(void)
{
    protocore_slot_mark(&protocore_conn_bits.held, 0);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state); // free...
    TEST_ASSERT_EQUAL_INT32(1, Tcp.conn->alloc_free());          // ...but not handed out
    protocore_slot_clear(&protocore_conn_bits.held, 0);
    TEST_ASSERT_EQUAL_INT32(0, Tcp.conn->alloc_free());
}

void test_allocator_reports_exhaustion_when_every_slot_is_taken(void)
{
    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        Tcp.conn->set_state(i, CONN_ACTIVE);
    }
    TEST_ASSERT_EQUAL_INT32(-1, Tcp.conn->alloc_free());
}

// The free bitmap is written only through set_state, so it can never disagree with the states.
void test_free_bitmap_tracks_every_state_write(void)
{
    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        ConnState st = (i % 3 == 0) ? CONN_ACTIVE : (i % 3 == 1) ? CONN_CLOSING : CONN_FREE;
        Tcp.conn->set_state(i, st);
    }
    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        proto_bool free_bit = (PROTO_ATOMIC_LOAD(&protocore_conn_bits.free) >> i) & 1u;
        proto_bool is_free = PROTO_ATOMIC_LOAD(&conn_pool[i].state) == CONN_FREE;
        TEST_ASSERT_EQUAL(is_free, free_bit);
    }
}

void test_set_state_ignores_a_slot_past_the_pool(void)
{
    Tcp.conn->set_state(CONN_POOL_SLOTS, CONN_ACTIVE);
    Tcp.conn->set_state((uint8_t)(CONN_POOL_SLOTS + 5), CONN_ACTIVE);
    TEST_ASSERT_EQUAL_INT32(0, Tcp.conn->alloc_free()); // pool untouched
}

void test_active_count_counts_only_live_slots(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, Tcp.conn->active_count());
    Tcp.conn->set_state(0, CONN_ACTIVE);
    Tcp.conn->set_state(1, CONN_ACTIVE);
    Tcp.conn->set_state(2, CONN_CLOSING); // draining is not active
    TEST_ASSERT_EQUAL_UINT8(2, Tcp.conn->active_count());
}

// protocore_conn_active() is the one predicate every layer above uses to ask whether a slot can be
// written to. It folds the state test and the control-block test, so neither can be checked alone.
void test_active_predicate_requires_both_the_state_and_a_control_block(void)
{
    arm_slot(0);
    TEST_ASSERT_TRUE(protocore_conn_active(0));

    conn_pool[0].pcb = NULL;
    TEST_ASSERT_FALSE(protocore_conn_active(0)); // state alone is not enough

    conn_pool[0].pcb = &g_pcb;
    Tcp.conn->set_state(0, CONN_CLOSING);
    TEST_ASSERT_FALSE(protocore_conn_active(0)); // a control block alone is not enough
}

// ---------------------------------------------------------------------------
// Pool lifecycle
// ---------------------------------------------------------------------------

void test_init_leaves_every_slot_free_and_indexed(void)
{
    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i].pcb = &g_pcb;
        Tcp.conn->set_state(i, CONN_ACTIVE);
    }
    Tcp.conn->init(NULL);

    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[i].state);
        TEST_ASSERT_NULL(conn_pool[i].pcb);
        TEST_ASSERT_EQUAL_UINT8(i, conn_pool[i].id);
        TEST_ASSERT_EQUAL_UINT(0, protocore_conn_available(i));
        TEST_ASSERT_EQUAL_UINT32(0, conn_pool[i].last_activity_ms);
    }
}

void test_init_takes_the_idle_bound_from_the_config(void)
{
    WebServerConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.conn_timeout_ms = 12345;
    Tcp.conn->init(&cfg);
    TEST_ASSERT_EQUAL_UINT32(12345, Tcp.conn->timeout_ms());

    Tcp.conn->init(NULL); // no config falls back to the built-in default
    TEST_ASSERT_EQUAL_UINT32(CONN_TIMEOUT_MS, Tcp.conn->timeout_ms());
}

// stop() resets every connection: a shutdown is an abort, not a graceful close.
void test_stop_resets_live_slots_and_leaves_the_pool_free(void)
{
    arm_slot(0);
    arm_slot(2);
    Tcp.conn->set_state(2, CONN_CLOSING);
    int aborts_before = mock_abort_call_count();

    Tcp.conn->stop();

    TEST_ASSERT_EQUAL_INT(aborts_before + 2, mock_abort_call_count());
    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[i].state);
        TEST_ASSERT_NULL(conn_pool[i].pcb);
    }
}

// ---------------------------------------------------------------------------
// Send path
// ---------------------------------------------------------------------------

// A slot whose connection was torn down between the worker capturing the control block and the op
// running must be skipped, not written through. The state is read from the pool inside the op.
void test_send_on_a_torn_down_slot_is_refused(void)
{
    arm_slot(0);
    conn_pool[0].pcb = NULL;
    TEST_ASSERT_FALSE(Tcp.conn->send(0, "x", 1));
    TEST_ASSERT_FALSE(Tcp.conn->send_flush(0, "x", 1));
}

void test_send_writes_through_to_the_wire(void)
{
    arm_slot(0);
    size_t before = protocore_net_host_tx_len;
    TEST_ASSERT_TRUE(Tcp.conn->send_flush(0, "HELLO", 5));
    TEST_ASSERT_EQUAL_UINT(before + 5, protocore_net_host_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("HELLO", protocore_net_host_tx + before, 5);
}

void test_raw_send_rejects_a_null_control_block(void)
{
    TEST_ASSERT_FALSE(Tcp.conn->raw_send(NULL, "x", 1));
}

// The stack's room for the next write is reported straight through when there is a connection, and
// as nothing when there is not.
void test_sndbuf_reports_zero_without_a_control_block(void)
{
    arm_slot(0);
    mock_sndbuf_set(4096);
    TEST_ASSERT_EQUAL_UINT16(4096, Tcp.conn->sndbuf(0));
    conn_pool[0].pcb = NULL;
    TEST_ASSERT_EQUAL_UINT16(0, Tcp.conn->sndbuf(0));
    mock_sndbuf_set(MOCK_SNDBUF_DEFAULT);
}

// close() detaches and frees the slot before the FIN goes out, so a late callback for that control
// block finds nothing to walk into.
void test_close_frees_the_slot_before_handing_the_pcb_to_the_stack(void)
{
    arm_slot(0);
    Tcp.conn->close(0);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
}

// When the stack cannot queue the FIN, the close falls back to a reset rather than leaking the
// control block.
void test_close_falls_back_to_a_reset_when_the_fin_cannot_be_queued(void)
{
    arm_slot(0);
    int aborts_before = mock_abort_call_count();
    mock_close_fail_once();
    Tcp.conn->close(0);
    TEST_ASSERT_EQUAL_INT(aborts_before + 1, mock_abort_call_count());
}

void test_close_and_abort_slot_ignore_out_of_range_and_empty_slots(void)
{
    int aborts_before = mock_abort_call_count();
    Tcp.conn->close(MAX_CONNS);
    Tcp.conn->abort_slot(MAX_CONNS);
    Tcp.conn->close(0); // free slot, no control block
    Tcp.conn->abort_slot(0);
    TEST_ASSERT_EQUAL_INT(aborts_before, mock_abort_call_count());
}

void test_abort_slot_resets_the_connection_and_frees_the_slot(void)
{
    arm_slot(0);
    int aborts_before = mock_abort_call_count();
    Tcp.conn->abort_slot(0);
    TEST_ASSERT_EQUAL_INT(aborts_before + 1, mock_abort_call_count());
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
}

// ---------------------------------------------------------------------------
// Peer address
// ---------------------------------------------------------------------------

void test_remote_address_is_reported_only_for_a_live_connection(void)
{
    arm_slot(0);
    protocore_net_ip4_set(&g_pcb.remote_ip, 192, 168, 1, 50);

    protocore_ip out;
    TEST_ASSERT_TRUE(Tcp.conn->remote_addr(0, &out));
    TEST_ASSERT_EQUAL(PROTOCORE_IP_V4, out.family);
    TEST_ASSERT_NOT_EQUAL(0, Tcp.conn->remote_ip(0));

    Tcp.conn->set_state(0, CONN_FREE);
    out.family = PROTOCORE_IP_V4;
    TEST_ASSERT_FALSE(Tcp.conn->remote_addr(0, &out));
    TEST_ASSERT_EQUAL(PROTOCORE_IP_NONE, out.family); // cleared even on the failure path
    TEST_ASSERT_EQUAL_UINT32(0, Tcp.conn->remote_ip(0));
}

void test_remote_address_rejects_a_null_output_and_a_bad_slot(void)
{
    arm_slot(0);
    protocore_ip out;
    TEST_ASSERT_FALSE(Tcp.conn->remote_addr(0, NULL));
    TEST_ASSERT_FALSE(Tcp.conn->remote_addr(MAX_CONNS, &out));
    TEST_ASSERT_EQUAL(PROTOCORE_IP_NONE, out.family);
    TEST_ASSERT_EQUAL_UINT32(0, Tcp.conn->remote_ip(MAX_CONNS));
}

// ---------------------------------------------------------------------------
// Ring accessors
// ---------------------------------------------------------------------------

void test_peek_reads_without_consuming(void)
{
    arm_slot(0);
    deliver(0, 0, 32);

    uint8_t look[8];
    protocore_conn_peek(0, 4, look, sizeof(look));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&g_payload[4], look, 8);
    TEST_ASSERT_EQUAL_UINT(32, protocore_conn_available(0)); // nothing taken

    protocore_conn_consume(0, 4);
    TEST_ASSERT_EQUAL_UINT(28, protocore_conn_available(0));

    uint8_t b = 0;
    TEST_ASSERT_TRUE(protocore_conn_read_byte(0, &b));
    TEST_ASSERT_EQUAL_UINT8(g_payload[4], b);
}

void test_read_byte_reports_an_empty_ring(void)
{
    arm_slot(0);
    uint8_t b = 0xAA;
    TEST_ASSERT_FALSE(protocore_conn_read_byte(0, &b));
    TEST_ASSERT_EQUAL_UINT8(0xAA, b); // left untouched
}

// A segment arriving as a chain of buffers lands in the ring as one contiguous run.
void test_a_multi_buffer_segment_is_reassembled_in_order(void)
{
    arm_slot(0);
    protocore_pbuf head, tail;
    memset(&head, 0, sizeof(head));
    memset(&tail, 0, sizeof(tail));
    head.payload = &g_payload[0];
    head.len = 40;
    head.tot_len = 100;
    head.next = &tail;
    tail.payload = &g_payload[40];
    tail.len = 60;
    tail.tot_len = 60;

    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, lowlevel_recv_cb(&conn_pool[0], &g_pcb, &head, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_UINT(100, protocore_conn_available(0));

    uint8_t got[100];
    TEST_ASSERT_EQUAL_UINT(100, protocore_conn_read(0, got, sizeof(got)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_payload, got, 100);
}

// A zero-length segment is accepted and posts nothing: there is no data to report.
void test_zero_length_segment_posts_no_event(void)
{
    arm_slot(0);
    drain_events((TcpEvt[8]){0}, 8);
    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, deliver(0, 0, 0));

    TcpEvt evts[8];
    TEST_ASSERT_EQUAL_INT(0, drain_events(evts, 8));
}

// Accepted data posts EVT_DATA carrying the byte count, so the session layer knows there is
// something to drain.
void test_accepted_data_posts_its_length(void)
{
    arm_slot(0);
    drain_events((TcpEvt[8]){0}, 8);
    deliver(0, 0, 77);

    TcpEvt evts[8];
    TEST_ASSERT_EQUAL_INT(1, drain_events(evts, 8));
    TEST_ASSERT_EQUAL(EVT_DATA, evts[0].type);
    TEST_ASSERT_EQUAL_UINT8(0, evts[0].slot_id);
    TEST_ASSERT_EQUAL_UINT(77, evts[0].data_len);
}

// A refused segment still wakes the loop, so the reader drains and the window can reopen. Without
// it a full ring and an idle worker deadlock each other.
void test_a_refused_segment_still_wakes_the_reader(void)
{
    arm_slot(0);
    deliver(0, 0, (uint16_t)(RING_USABLE - 10));
    drain_events((TcpEvt[8]){0}, 8);

    TEST_ASSERT_EQUAL(PROTOCORE_NET_ERR_MEM, deliver(0, 0, 64));

    TcpEvt evts[8];
    TEST_ASSERT_EQUAL_INT(1, drain_events(evts, 8));
    TEST_ASSERT_EQUAL(EVT_DATA, evts[0].type);
    TEST_ASSERT_EQUAL_UINT(0, evts[0].data_len); // a nudge, not a delivery
}


// The runner is generated: Unity's auto/generate_test_runner.rb scans this file for
// void test_*(void) and emits main() with every case registered, stamped with the line each test
// is defined on. See test/gen_test_runners.py.
