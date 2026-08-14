// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for src/network_drivers/transport/tcp/client/client.h - the outbound client pool.
//
// This translation unit had no test anywhere in the tree before this file, so everything here is
// first coverage: the slot claim, the non-blocking resolve/connect pump, the bound on the whole
// open, the wire ring, and the window reopening the ring drives.
//
// The RFC anchor is the same one the server side answers to - RFC 9293 sec 3.8.6, the receiver's
// algorithm (3.8.6.2.2) sets RCV.WND to RCV.BUFF-RCV.USER, and SHLD-14 says a receiver "SHOULD NOT
// shrink the window". The client reopens on consume for exactly that reason, so the amount it
// hands back is the assertable part.
//
// The client's stack callbacks are static, unlike the server's. A test reaches them the way the
// stack does: through the control block they were wired onto.

#include "network_drivers/transport/tcp/tcp.h"
#include "network_drivers/transport/tcp/client/client.h"
#include <string.h>

#include <unity.h>

static uint8_t g_payload[512];

void setUp(void)
{
    set_millis(0);
    mock_recved_reset();
    mock_abort_call_reset();
    // Hand every control block back, so a slot claimed by a previous case cannot be found here.
    for (int i = 0; i < PROTOCORE_NET_HOST_PCBS; i++)
    {
        memset(&protocore_net_host_pcbs[i], 0, sizeof(protocore_pcb));
    }
    for (int i = 0; i < PROTOCORE_CLIENT_CONNS; i++)
    {
        Tcp.client->close(i);
    }
    for (size_t i = 0; i < sizeof(g_payload); i++)
    {
        g_payload[i] = (uint8_t)(i & 0xFF);
    }
}

void tearDown(void)
{
    for (int i = 0; i < PROTOCORE_CLIENT_CONNS; i++)
    {
        Tcp.client->close(i);
    }
}

// The control block the client wired its callbacks onto: the one the pool handed out that now
// carries a recv callback. This is the seam the stack itself would call through.
static protocore_pcb *wired_pcb(void)
{
    for (int i = 0; i < PROTOCORE_NET_HOST_PCBS; i++)
    {
        if (protocore_net_host_pcbs[i].in_use && protocore_net_host_pcbs[i].on_recv != NULL)
        {
            return &protocore_net_host_pcbs[i];
        }
    }
    return NULL;
}

// Deliver a segment the way the stack does, through the wired callback.
static protocore_net_err deliver(protocore_pcb *pcb, size_t off, uint16_t len)
{
    protocore_pbuf p;
    memset(&p, 0, sizeof(p));
    p.payload = &g_payload[off];
    p.len = len;
    p.tot_len = len;
    return pcb->on_recv(pcb->arg, pcb, &p, PROTOCORE_NET_OK);
}

// ---------------------------------------------------------------------------
// Opening
// ---------------------------------------------------------------------------

// A dotted quad answers itself: the resolver's literal fast path returns an address with no query,
// so the connect goes out inside open() and the handshake is settled on the first poll.
void test_open_to_a_literal_address_connects_without_a_query(void)
{
    int cid = Tcp.client->open("10.0.0.5", 8080, 5000);
    TEST_ASSERT_TRUE(cid >= 0);
    TEST_ASSERT_TRUE(Tcp.client->connected(cid));
    TEST_ASSERT_FALSE(Tcp.client->is_closed(cid));

    protocore_pcb *pcb = wired_pcb();
    TEST_ASSERT_NOT_NULL(pcb);
    TEST_ASSERT_EQUAL_UINT16(8080, pcb->remote_port);
}

void test_open_rejects_a_null_host(void)
{
    TEST_ASSERT_EQUAL_INT(-2, Tcp.client->open(NULL, 80, 1000));
}

// The pool is fixed and static. Once every slot is taken the next open reports it rather than
// growing anything.
void test_open_reports_a_full_pool(void)
{
    for (int i = 0; i < PROTOCORE_CLIENT_CONNS; i++)
    {
        TEST_ASSERT_TRUE(Tcp.client->open("10.0.0.5", 80, 1000) >= 0);
    }
    TEST_ASSERT_EQUAL_INT(-1, Tcp.client->open("10.0.0.5", 80, 1000));
}

// Each open takes a distinct slot, and closing one returns it to the pool.
void test_slots_are_distinct_and_returned_on_close(void)
{
    int a = Tcp.client->open("10.0.0.5", 80, 1000);
    int b = Tcp.client->open("10.0.0.6", 80, 1000);
    TEST_ASSERT_TRUE(a >= 0);
    TEST_ASSERT_TRUE(b >= 0);
    TEST_ASSERT_NOT_EQUAL(a, b);

    TEST_ASSERT_EQUAL_INT(-1, Tcp.client->open("10.0.0.7", 80, 1000)); // full at two
    Tcp.client->close(a);
    int c = Tcp.client->open("10.0.0.7", 80, 1000);
    TEST_ASSERT_EQUAL_INT(a, c); // the freed slot is the one handed back
}

// A stack that cannot hand out a control block fails the open; the slot is left reporting closed
// rather than sitting half-open forever.
void test_open_closes_the_slot_when_no_control_block_is_available(void)
{
    mock_new_pcb_fail_once();
    int cid = Tcp.client->open("10.0.0.5", 80, 1000);
    TEST_ASSERT_TRUE(cid >= 0); // a slot was claimed...
    TEST_ASSERT_TRUE(Tcp.client->is_closed(cid));
    TEST_ASSERT_FALSE(Tcp.client->connected(cid));
}

// A refused connect (the peer resets the SYN) settles the slot as closed, not connected.
void test_a_refused_connect_closes_the_slot(void)
{
    mock_connect_fail_once();
    int cid = Tcp.client->open("10.0.0.5", 80, 1000);
    TEST_ASSERT_TRUE(cid >= 0);
    TEST_ASSERT_TRUE(Tcp.client->is_closed(cid));
    TEST_ASSERT_FALSE(Tcp.client->connected(cid));
}

// A name that needs a query does not settle inside open(): the lookup is out, the slot is neither
// connected nor closed, and the caller steps it from its own loop. The whole open - the name
// lookup included - is bounded by the timeout the caller passed, so a query that never answers
// cannot hold a slot from the fixed pool forever.
//
// The client's own budget is set well under the resolver's PROTOCORE_DNS_TIMEOUT_MS (5000 ms), so
// what fires here is the bound this module owns rather than the resolver giving up first.
void test_the_open_timeout_bounds_the_name_lookup(void)
{
    int cid = Tcp.client->open("example.invalid", 80, 2000);
    TEST_ASSERT_TRUE(cid >= 0);
    TEST_ASSERT_FALSE(Tcp.client->connected(cid));
    TEST_ASSERT_FALSE(Tcp.client->is_closed(cid)); // still resolving

    set_millis(1999);
    TEST_ASSERT_FALSE(Tcp.client->is_closed(cid)); // inside the caller's budget
    set_millis(2000);
    TEST_ASSERT_TRUE(Tcp.client->is_closed(cid));  // out of time
    TEST_ASSERT_FALSE(Tcp.client->connected(cid));
}

// ---------------------------------------------------------------------------
// Bounds
// ---------------------------------------------------------------------------

// An unknown connection id is closed, carries nothing, and accepts nothing. is_closed answering
// true for an id that was never opened is the safe direction: a caller polling it gives up rather
// than waiting on a slot that does not exist.
void test_an_unknown_connection_id_is_inert(void)
{
    const int bad[] = {-1, -99, PROTOCORE_CLIENT_CONNS, PROTOCORE_CLIENT_CONNS + 5};
    uint8_t buf[8];
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        TEST_ASSERT_TRUE(Tcp.client->is_closed(bad[i]));
        TEST_ASSERT_FALSE(Tcp.client->connected(bad[i]));
        TEST_ASSERT_FALSE(Tcp.client->send(bad[i], "x", 1));
        TEST_ASSERT_EQUAL_UINT(0, Tcp.client->available(bad[i]));
        TEST_ASSERT_EQUAL_UINT(0, Tcp.client->read(bad[i], buf, sizeof(buf)));
        Tcp.client->close(bad[i]); // must not fault
    }
}

// A slot that was never opened reports nothing rather than reporting a stale connection.
void test_an_unopened_slot_reports_no_connection(void)
{
    TEST_ASSERT_FALSE(Tcp.client->connected(0));
    TEST_ASSERT_FALSE(Tcp.client->send(0, "x", 1));
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

void test_send_reaches_the_wire(void)
{
    int cid = Tcp.client->open("10.0.0.5", 80, 1000);
    size_t before = protocore_net_host_tx_len;
    TEST_ASSERT_TRUE(Tcp.client->send(cid, "PING", 4));
    TEST_ASSERT_EQUAL_UINT(before + 4, protocore_net_host_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("PING", protocore_net_host_tx + before, 4);
}

// A send on a slot whose control block is gone is refused rather than written through a dangling
// pointer.
void test_send_is_refused_once_the_connection_is_gone(void)
{
    int cid = Tcp.client->open("10.0.0.5", 80, 1000);
    protocore_pcb *pcb = wired_pcb();
    TEST_ASSERT_NOT_NULL(pcb);

    pcb->on_err(pcb->arg, PROTOCORE_NET_ERR_RST); // the stack freed it under us
    TEST_ASSERT_TRUE(Tcp.client->is_closed(cid));
    TEST_ASSERT_FALSE(Tcp.client->send(cid, "x", 1));
}

// A write the stack cannot queue is reported as a failure, not swallowed.
void test_send_reports_a_full_send_buffer(void)
{
    int cid = Tcp.client->open("10.0.0.5", 80, 1000);
    mock_send_fail_after(0);
    TEST_ASSERT_FALSE(Tcp.client->send(cid, "x", 1));
    mock_send_fail_after(-1);
}

// ---------------------------------------------------------------------------
// Receiving - RFC 9293 sec 3.8.6
// ---------------------------------------------------------------------------

void test_delivered_bytes_are_readable_in_order(void)
{
    int cid = Tcp.client->open("10.0.0.5", 80, 1000);
    protocore_pcb *pcb = wired_pcb();
    TEST_ASSERT_NOT_NULL(pcb);

    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, deliver(pcb, 0, 128));
    TEST_ASSERT_EQUAL_UINT(128, Tcp.client->available(cid));

    uint8_t got[128];
    TEST_ASSERT_EQUAL_UINT(128, Tcp.client->read(cid, got, sizeof(got)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_payload, got, 128);
    TEST_ASSERT_EQUAL_UINT(0, Tcp.client->available(cid));
}

// The window is not reopened as the segment is copied - the bytes are still occupying the ring.
void test_arrival_alone_reopens_no_window(void)
{
    int cid = Tcp.client->open("10.0.0.5", 80, 1000);
    protocore_pcb *pcb = wired_pcb();
    mock_recved_reset();

    deliver(pcb, 0, 64);
    TEST_ASSERT_EQUAL_INT(0, mock_recved_call_count());
    TEST_ASSERT_EQUAL_UINT(64, Tcp.client->available(cid));
}

// It reopens on the read, by exactly what the read took.
void test_the_window_reopens_by_exactly_what_was_read(void)
{
    int cid = Tcp.client->open("10.0.0.5", 80, 1000);
    protocore_pcb *pcb = wired_pcb();
    deliver(pcb, 0, 200);
    mock_recved_reset();

    uint8_t got[70];
    TEST_ASSERT_EQUAL_UINT(70, Tcp.client->read(cid, got, sizeof(got)));
    TEST_ASSERT_EQUAL_INT(1, mock_recved_call_count());
    TEST_ASSERT_EQUAL_UINT16(70, mock_recved_last());

    TEST_ASSERT_EQUAL_UINT(130, Tcp.client->available(cid)); // the rest still charged

    size_t drained = 70;
    size_t n;
    while ((n = Tcp.client->read(cid, got, sizeof(got))) > 0)
    {
        drained += n;
    }
    TEST_ASSERT_EQUAL_UINT(200, drained);
    TEST_ASSERT_EQUAL_UINT32(200, mock_recved_total()); // reopened == consumed, exactly
}

// A read that finds nothing opens no window.
void test_an_empty_read_reopens_no_window(void)
{
    int cid = Tcp.client->open("10.0.0.5", 80, 1000);
    mock_recved_reset();
    uint8_t got[16];
    TEST_ASSERT_EQUAL_UINT(0, Tcp.client->read(cid, got, sizeof(got)));
    TEST_ASSERT_EQUAL_INT(0, mock_recved_call_count());
}

// A segment that will not fit the ring is refused whole, so the stack keeps it and redelivers -
// the same lossless backpressure the server side uses. Nothing is truncated.
void test_a_segment_that_will_not_fit_is_refused_whole(void)
{
    int cid = Tcp.client->open("10.0.0.5", 80, 1000);
    protocore_pcb *pcb = wired_pcb();

    // Fill the ring to one byte short of its usable capacity.
    size_t filled = 0;
    while (filled + sizeof(g_payload) <= PROTOCORE_CLIENT_RX_BUF - 1)
    {
        TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, deliver(pcb, 0, (uint16_t)sizeof(g_payload)));
        filled += sizeof(g_payload);
    }
    size_t room = (PROTOCORE_CLIENT_RX_BUF - 1) - filled;
    TEST_ASSERT_EQUAL(PROTOCORE_NET_ERR_MEM, deliver(pcb, 0, (uint16_t)(room + 1)));
    TEST_ASSERT_EQUAL_UINT(filled, Tcp.client->available(cid)); // untouched
}

// A chained segment lands as one contiguous run.
void test_a_multi_buffer_segment_is_reassembled_in_order(void)
{
    int cid = Tcp.client->open("10.0.0.5", 80, 1000);
    protocore_pcb *pcb = wired_pcb();

    protocore_pbuf head, tail;
    memset(&head, 0, sizeof(head));
    memset(&tail, 0, sizeof(tail));
    head.payload = &g_payload[0];
    head.len = 30;
    head.tot_len = 90;
    head.next = &tail;
    tail.payload = &g_payload[30];
    tail.len = 60;
    tail.tot_len = 60;

    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, pcb->on_recv(pcb->arg, pcb, &head, PROTOCORE_NET_OK));

    uint8_t got[90];
    TEST_ASSERT_EQUAL_UINT(90, Tcp.client->read(cid, got, sizeof(got)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_payload, got, 90);
}

// A read is bounded by the caller's buffer, and the remainder stays queued.
void test_a_short_read_leaves_the_remainder_queued(void)
{
    int cid = Tcp.client->open("10.0.0.5", 80, 1000);
    protocore_pcb *pcb = wired_pcb();
    deliver(pcb, 0, 100);

    uint8_t got[40];
    TEST_ASSERT_EQUAL_UINT(40, Tcp.client->read(cid, got, sizeof(got)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_payload, got, 40);
    TEST_ASSERT_EQUAL_UINT(60, Tcp.client->available(cid));

    TEST_ASSERT_EQUAL_UINT(40, Tcp.client->read(cid, got, sizeof(got)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&g_payload[40], got, 40);
}

// ---------------------------------------------------------------------------
// Closing
// ---------------------------------------------------------------------------

// A peer FIN arrives as a null segment and marks the slot closed.
void test_a_peer_fin_closes_the_slot(void)
{
    int cid = Tcp.client->open("10.0.0.5", 80, 1000);
    protocore_pcb *pcb = wired_pcb();
    TEST_ASSERT_FALSE(Tcp.client->is_closed(cid));

    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, pcb->on_recv(pcb->arg, pcb, NULL, PROTOCORE_NET_OK));
    TEST_ASSERT_TRUE(Tcp.client->is_closed(cid));
    TEST_ASSERT_FALSE(Tcp.client->connected(cid)); // connected is false once closed
}

// The error callback fires after the stack has freed the control block, so the slot drops its
// reference and must not close or abort it again.
void test_the_error_callback_drops_the_control_block_without_reusing_it(void)
{
    int cid = Tcp.client->open("10.0.0.5", 80, 1000);
    protocore_pcb *pcb = wired_pcb();
    int aborts_before = mock_abort_call_count();

    pcb->on_err(pcb->arg, PROTOCORE_NET_ERR_RST);
    TEST_ASSERT_TRUE(Tcp.client->is_closed(cid));
    TEST_ASSERT_EQUAL_INT(aborts_before, mock_abort_call_count());

    // Closing afterwards is safe: there is no control block left to act on.
    Tcp.client->close(cid);
}

// Closing unwires the callbacks, so a late delivery for that control block cannot reach a slot
// that has been handed back to the pool.
void test_close_unwires_the_stack_callbacks(void)
{
    int cid = Tcp.client->open("10.0.0.5", 80, 1000);
    protocore_pcb *pcb = wired_pcb();
    TEST_ASSERT_NOT_NULL(pcb->on_recv);

    Tcp.client->close(cid);
    TEST_ASSERT_NULL(pcb->on_recv);
    TEST_ASSERT_NULL(pcb->on_err);
    TEST_ASSERT_NULL(pcb->arg);
}

// A close that the stack cannot queue falls back to a reset rather than leaking the block.
void test_close_falls_back_to_a_reset(void)
{
    int cid = Tcp.client->open("10.0.0.5", 80, 1000);
    int aborts_before = mock_abort_call_count();
    mock_close_fail_once();
    Tcp.client->close(cid);
    TEST_ASSERT_EQUAL_INT(aborts_before + 1, mock_abort_call_count());
}

// Closing twice is safe: the second call finds the slot already returned.
void test_close_is_idempotent(void)
{
    int cid = Tcp.client->open("10.0.0.5", 80, 1000);
    Tcp.client->close(cid);
    int aborts_before = mock_abort_call_count();
    Tcp.client->close(cid);
    TEST_ASSERT_EQUAL_INT(aborts_before, mock_abort_call_count());
}

// A reopened slot does not inherit the previous connection's buffered bytes or its closed flag.
void test_a_reopened_slot_starts_clean(void)
{
    int cid = Tcp.client->open("10.0.0.5", 80, 1000);
    protocore_pcb *pcb = wired_pcb();
    deliver(pcb, 0, 64);
    pcb->on_recv(pcb->arg, pcb, NULL, PROTOCORE_NET_OK); // peer FIN
    TEST_ASSERT_TRUE(Tcp.client->is_closed(cid));
    Tcp.client->close(cid);

    int again = Tcp.client->open("10.0.0.6", 81, 1000);
    TEST_ASSERT_EQUAL_INT(cid, again);
    TEST_ASSERT_EQUAL_UINT(0, Tcp.client->available(again)); // no carry-over bytes
    TEST_ASSERT_FALSE(Tcp.client->is_closed(again));         // no carry-over close
}


// The runner is generated: Unity's auto/generate_test_runner.rb scans this file for
// void test_*(void) and emits main() with every case registered, stamped with the line each test
// is defined on. See test/gen_test_runners.py.
