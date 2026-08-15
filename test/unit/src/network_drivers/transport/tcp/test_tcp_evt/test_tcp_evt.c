// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for src/network_drivers/transport/tcp/evt.h - the event type and record that every
// layer above the transport reads.
//
// This header is the whole of what the session and presentation layers see of a connection, and a
// listener sizes its queue storage on ::TcpEvt. So what is checked is the shape: the enum stays one
// byte, the four kinds stay distinct, a record survives a copy through a real platform queue with
// every field intact, and the queue storage a listener reserves is actually big enough for the
// depth it claims.
//
// The header's own note that a record is "small enough (<=12 bytes on 32-bit)" is not asserted
// here. It is a statement about a 32-bit target; on this host size_t is eight bytes, so the record
// is larger and the claim says nothing checkable. The invariant that does hold on every target is
// the one below: the reserved storage has to hold EVT_QUEUE_DEPTH records of whatever size the
// record is on that target.

#include "network_drivers/transport/tcp/evt.h"
#include "network_drivers/transport/tcp/server/server.h"
#include "network_drivers/transport/tcp/tcp.h"
#include <string.h>

#include <unity.h>

static uint8_t g_storage[EVT_QUEUE_DEPTH * sizeof(TcpEvt)];
static protocore_platform_queue_ctrl g_ctrl;
static protocore_platform_queue g_queue;

void setUp(void)
{
    queue_stage_reset();
    memset(g_storage, 0, sizeof(g_storage));
    g_queue = protocore_platform_queue_create(EVT_QUEUE_DEPTH, sizeof(TcpEvt), g_storage, &g_ctrl);
}

void tearDown(void)
{
    protocore_platform_queue_delete(g_queue);
}

// ---------------------------------------------------------------------------
// The event type
// ---------------------------------------------------------------------------

// Every listener's queue storage is sized on a record containing this enum, so it staying one byte
// is a storage-layout fact, not a style preference. The header asserts it at compile time; this
// says the same thing where a failure names itself.
void test_the_event_type_is_one_byte(void)
{
    TEST_ASSERT_EQUAL_UINT(1, sizeof(EvtType));
}

// The four kinds are what the session layer switches on, so no two may collide.
void test_the_four_event_kinds_are_distinct(void)
{
    const EvtType all[] = {EVT_CONNECT, EVT_DATA, EVT_DISCONNECT, EVT_ERROR};
    const int n = (int)(sizeof(all) / sizeof(all[0]));
    for (int i = 0; i < n; i++)
    {
        for (int j = i + 1; j < n; j++)
        {
            TEST_ASSERT_NOT_EQUAL(all[i], all[j]);
        }
    }
}

// Every kind fits the byte the enum is stored in.
void test_every_event_kind_fits_one_byte(void)
{
    TEST_ASSERT_TRUE((unsigned)EVT_CONNECT <= 0xFFu);
    TEST_ASSERT_TRUE((unsigned)EVT_DATA <= 0xFFu);
    TEST_ASSERT_TRUE((unsigned)EVT_DISCONNECT <= 0xFFu);
    TEST_ASSERT_TRUE((unsigned)EVT_ERROR <= 0xFFu);
}

// A normal close and an abort are different kinds. RFC 9293 sec 3.6 MUST-12 requires the layer
// above to be told which of the two happened, and this pair is how it is told.
void test_a_normal_close_and_an_abort_are_different_kinds(void)
{
    TEST_ASSERT_NOT_EQUAL(EVT_DISCONNECT, EVT_ERROR);
}

// ---------------------------------------------------------------------------
// The record
// ---------------------------------------------------------------------------

// The slot id is a byte, so it has to be able to name every slot in the pool - the reserved
// internal slots included, since those also post through this record.
void test_the_slot_field_can_name_every_pool_slot(void)
{
    TEST_ASSERT_TRUE(CONN_POOL_SLOTS <= 0x100);

    TcpEvt e;
    memset(&e, 0, sizeof(e));
    e.slot_id = (uint8_t)(CONN_POOL_SLOTS - 1);
    TEST_ASSERT_EQUAL_UINT8(CONN_POOL_SLOTS - 1, e.slot_id);
}

// The length field carries a whole ring's worth of bytes, which is the most one delivery can
// report.
void test_the_length_field_holds_a_full_ring(void)
{
    TcpEvt e;
    memset(&e, 0, sizeof(e));
    e.data_len = RX_BUF_SIZE;
    TEST_ASSERT_EQUAL_UINT(RX_BUF_SIZE, e.data_len);
}

// The three fields are independent: writing one does not disturb the others.
void test_the_fields_are_independent(void)
{
    TcpEvt e;
    memset(&e, 0, sizeof(e));
    e.type = EVT_DATA;
    e.slot_id = 5;
    e.data_len = 1234;

    TEST_ASSERT_EQUAL(EVT_DATA, e.type);
    TEST_ASSERT_EQUAL_UINT8(5, e.slot_id);
    TEST_ASSERT_EQUAL_UINT(1234, e.data_len);
}

// ---------------------------------------------------------------------------
// Queue storage
// ---------------------------------------------------------------------------

// A listener embeds its queue storage, sized EVT_QUEUE_DEPTH records. If the record grew past what
// that reserves, a queue would write past its own backing store.
void test_a_listener_reserves_storage_for_the_depth_it_claims(void)
{
    TEST_ASSERT_EQUAL_UINT(EVT_QUEUE_DEPTH * sizeof(TcpEvt), sizeof(listener_pool[0]._queue_storage));
    TEST_ASSERT_TRUE(sizeof(listener_pool[0]._queue_storage) >= EVT_QUEUE_DEPTH * sizeof(TcpEvt));
}

// The depth absorbs a burst from every slot without blocking the stack's thread, which is what the
// configuration sizes it for.
void test_the_queue_depth_covers_a_burst_from_every_slot(void)
{
    TEST_ASSERT_TRUE(EVT_QUEUE_DEPTH >= MAX_CONNS * 4);
}

// The queue copies a record by value, so nothing in it can be a pointer into a slot whose lifetime
// ends before the record is drained. A round trip has to return every field unchanged.
void test_a_record_survives_a_round_trip_through_a_queue(void)
{
    TcpEvt sent = {EVT_DATA, 3, 4096};
    TEST_ASSERT_EQUAL(PROTOCORE_PLATFORM_OK, protocore_platform_queue_send(g_queue, &sent, 0));

    TcpEvt got;
    memset(&got, 0xFF, sizeof(got));
    TEST_ASSERT_EQUAL(PROTOCORE_PLATFORM_OK, protocore_platform_queue_recv(g_queue, &got, 0));

    TEST_ASSERT_EQUAL(EVT_DATA, got.type);
    TEST_ASSERT_EQUAL_UINT8(3, got.slot_id);
    TEST_ASSERT_EQUAL_UINT(4096, got.data_len);
}

// Records come back in the order they went in, so a connect is never drained after the data that
// followed it.
void test_records_are_drained_in_the_order_they_were_posted(void)
{
    const TcpEvt in[] = {{EVT_CONNECT, 1, 0}, {EVT_DATA, 1, 10}, {EVT_DATA, 1, 20}, {EVT_DISCONNECT, 1, 0}};
    const int n = (int)(sizeof(in) / sizeof(in[0]));
    for (int i = 0; i < n; i++)
    {
        TEST_ASSERT_EQUAL(PROTOCORE_PLATFORM_OK, protocore_platform_queue_send(g_queue, &in[i], 0));
    }

    for (int i = 0; i < n; i++)
    {
        TcpEvt got;
        memset(&got, 0, sizeof(got));
        TEST_ASSERT_EQUAL(PROTOCORE_PLATFORM_OK, protocore_platform_queue_recv(g_queue, &got, 0));
        TEST_ASSERT_EQUAL(in[i].type, got.type);
        TEST_ASSERT_EQUAL_UINT8(in[i].slot_id, got.slot_id);
        TEST_ASSERT_EQUAL_UINT(in[i].data_len, got.data_len);
    }
}

// Each of the four kinds survives the copy; none is folded into another by the queue's item size.
void test_every_event_kind_survives_the_queue(void)
{
    const EvtType kinds[] = {EVT_CONNECT, EVT_DATA, EVT_DISCONNECT, EVT_ERROR};
    for (int i = 0; i < 4; i++)
    {
        TcpEvt sent = {kinds[i], (uint8_t)i, (size_t)i * 100};
        TEST_ASSERT_EQUAL(PROTOCORE_PLATFORM_OK, protocore_platform_queue_send(g_queue, &sent, 0));

        TcpEvt got;
        memset(&got, 0, sizeof(got));
        TEST_ASSERT_EQUAL(PROTOCORE_PLATFORM_OK, protocore_platform_queue_recv(g_queue, &got, 0));
        TEST_ASSERT_EQUAL(kinds[i], got.type);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)i, got.slot_id);
        TEST_ASSERT_EQUAL_UINT((size_t)i * 100, got.data_len);
    }
}

// An empty queue reports nothing rather than handing back a stale record.
void test_an_empty_queue_yields_nothing(void)
{
    TcpEvt got;
    memset(&got, 0xAB, sizeof(got));
    TEST_ASSERT_NOT_EQUAL(PROTOCORE_PLATFORM_OK, protocore_platform_queue_recv(g_queue, &got, 0));
    TEST_ASSERT_EQUAL_UINT8(0xAB, got.slot_id); // untouched
}

// The runner is generated: Unity's auto/generate_test_runner.rb scans this file for
// void test_*(void) and emits main() with every case registered, stamped with the line each test
// is defined on. See test/gen_test_runners.py.
