// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/presentation/presentation.h"
#include "network_drivers/session/session.h"
#include "network_drivers/transport/tcp/common.h"
#include "network_drivers/transport/tcp/protocol/protocol.h"
#include "network_drivers/transport/tcp/server/server.h"
#include "network_drivers/transport/tcp/tcp.h"
#include "server/clock/clock.h"
#include "server/core/proto_handler.h"
#include <unity.h>

static void push_to_slot(uint8_t slot, const char *data)
{
    TcpConn *s = &conn_pool[slot];
    for (size_t i = 0; data[i]; i++)
    {
        size_t next = (s->rx_head + 1) % RX_BUF_SIZE;
        s->rx_buffer[s->rx_head] = (uint8_t)data[i];
        s->rx_head = next;
    }
}

void setUp()
{
    set_millis(0);
    queue_stage_reset();
    ConnPool.life.cfg = NULL;
    ConnPool.init(ConnPool.internal);
    TcpListener.idx = 0;
    TcpListener.bind.port = 80;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(TcpListener.internal);
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].proto = PROTO_HTTP;
        HttpConn.slot = (uint8_t)i;
        HttpConn.reset(HttpConn.internal);
    }
}

void tearDown()
{
}

void test_empty_queue_does_not_crash()
{
    Session.worker_id = 0;
    Session.tick(Session.internal);
    TEST_PASS();
}

void test_pool_initializes_to_parse_method()
{
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[i].parse_state);
    }
}

void test_reset_clears_mid_parse_state()
{
    http_pool[0].parse_state = PARSE_HEADER_KEY;
    http_pool[0].header_count = 3;
    HttpConn.slot = (uint8_t)0;
    HttpConn.reset(HttpConn.internal);
    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(0, http_pool[0].header_count);
}

void test_tick_fires_check_timeouts_stale_slot_freed()
{
    conn_pool[0].last_activity_ms = 0;
    set_millis(CONN_TIMEOUT_MS);
    Session.worker_id = 0;
    Session.tick(Session.internal);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

void test_tick_does_not_free_fresh_connection()
{
    conn_pool[0].last_activity_ms = 0;
    set_millis(CONN_TIMEOUT_MS - 1);
    Session.worker_id = 0;
    Session.tick(Session.internal);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

void test_fn_tick_timeout_before_event_drain_ordering()
{
    conn_pool[1].last_activity_ms = 0;
    set_millis(CONN_TIMEOUT_MS);
    Session.worker_id = 0;
    Session.tick(Session.internal);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state);

    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state);
}

void test_fn_tick_only_active_slots_expire()
{
    conn_pool[0].state = CONN_FREE;
    conn_pool[1].state = CONN_ACTIVE;
    conn_pool[1].last_activity_ms = 0;
    conn_pool[2].state = CONN_FREE;
    conn_pool[3].state = CONN_ACTIVE;
    conn_pool[3].last_activity_ms = CONN_TIMEOUT_MS;

    set_millis(CONN_TIMEOUT_MS);
    Session.worker_id = 0;
    Session.tick(Session.internal);

    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[2].state);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[3].state);
}

void stress_1000_idle_ticks_stable()
{
    set_millis(0);
    for (int i = 0; i < 1000; i++)
    {
        Session.worker_id = 0;
        Session.tick(Session.internal);
    }
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[i].state);
    }
}

void stress_timeout_all_slots_10_cycles()
{
    for (int cycle = 0; cycle < 10; cycle++)
    {
        for (int i = 0; i < MAX_CONNS; i++)
        {
            conn_pool[i].state = CONN_ACTIVE;
            conn_pool[i].pcb = NULL;
            conn_pool[i].last_activity_ms = 0;
        }
        set_millis((uint32_t)(CONN_TIMEOUT_MS * (cycle + 1)));
        Session.worker_id = 0;
        Session.tick(Session.internal);
        for (int i = 0; i < MAX_CONNS; i++)
        {
            TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[i].state);
        }
    }
}

void stress_mixed_fresh_stale_slots_many_ticks()
{
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].last_activity_ms = 0;
    conn_pool[1].state = CONN_ACTIVE;
    conn_pool[1].last_activity_ms = 0;
    conn_pool[2].state = CONN_ACTIVE;
    conn_pool[2].last_activity_ms = CONN_TIMEOUT_MS;
    conn_pool[3].state = CONN_ACTIVE;
    conn_pool[3].last_activity_ms = CONN_TIMEOUT_MS;

    set_millis(CONN_TIMEOUT_MS);
    for (int tick = 0; tick < 200; tick++)
    {
        Session.worker_id = 0;
        Session.tick(Session.internal);
    }

    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[2].state);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[3].state);
}

void test_evt_connect_calls_http_reset()
{
    http_pool[1].parse_state = PARSE_HEADER_KEY;
    http_pool[1].header_count = 3;

    TcpEvt evt = {EVT_CONNECT, 1, 0};
    TcpListener.idx = 0;
    TcpListener.q.evt = &evt;
    TcpListener.enqueue(TcpListener.internal);
    (void)TcpListener.ok;
    Session.worker_id = 0;
    Session.tick(Session.internal);

    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[1].parse_state);
    TEST_ASSERT_EQUAL(0, http_pool[1].header_count);
}

void test_evt_disconnect_calls_http_reset()
{
    http_pool[0].parse_state = PARSE_COMPLETE;
    http_pool[0].header_count = 2;

    TcpEvt evt = {EVT_DISCONNECT, 0, 0};
    TcpListener.idx = 0;
    TcpListener.q.evt = &evt;
    TcpListener.enqueue(TcpListener.internal);
    (void)TcpListener.ok;
    Session.worker_id = 0;
    Session.tick(Session.internal);

    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(0, http_pool[0].header_count);
}

void test_evt_error_calls_http_reset()
{
    http_pool[2].parse_state = PARSE_ERROR;

    TcpEvt evt = {EVT_ERROR, 2, 0};
    TcpListener.idx = 0;
    TcpListener.q.evt = &evt;
    TcpListener.enqueue(TcpListener.internal);
    (void)TcpListener.ok;
    Session.worker_id = 0;
    Session.tick(Session.internal);

    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[2].parse_state);
}

void test_evt_data_calls_http_parse()
{
    push_to_slot(0, "GET /evt HTTP/1.1\r\n\r\n");

    TcpEvt evt = {EVT_DATA, 0, 0};
    TcpListener.idx = 0;
    TcpListener.q.evt = &evt;
    TcpListener.enqueue(TcpListener.internal);
    (void)TcpListener.ok;
    Session.worker_id = 0;
    Session.tick(Session.internal);

    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("GET", http_pool[0].method);
    TEST_ASSERT_EQUAL_STRING("/evt", http_pool[0].path);
}

void test_multiple_events_drained_in_one_tick()
{

    http_pool[0].parse_state = PARSE_COMPLETE;
    TcpEvt e0 = {EVT_CONNECT, 0, 0};
    TcpListener.idx = 0;
    TcpListener.q.evt = &e0;
    TcpListener.enqueue(TcpListener.internal);
    (void)TcpListener.ok;

    push_to_slot(1, "GET / HTTP/1.1\r\n\r\n");
    TcpEvt e1 = {EVT_DATA, 1, 0};
    TcpListener.idx = 0;
    TcpListener.q.evt = &e1;
    TcpListener.enqueue(TcpListener.internal);
    (void)TcpListener.ok;

    http_pool[2].parse_state = PARSE_HEADER_VAL;
    TcpEvt e2 = {EVT_DISCONNECT, 2, 0};
    TcpListener.idx = 0;
    TcpListener.q.evt = &e2;
    TcpListener.enqueue(TcpListener.internal);
    (void)TcpListener.ok;

    Session.worker_id = 0;

    Session.tick(Session.internal);

    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[1].parse_state);
    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[2].parse_state);
}

void test_protocore_register_out_of_range_is_nop()
{
    Protocols.proto = (ProtoConn)250;
    Protocols.h = NULL;
    Protocols.add(Protocols.internal);
    TEST_PASS();
}

void test_protocore_get_out_of_range_returns_null()
{
    Protocols.proto = (ProtoConn)250;
    Protocols.get(Protocols.internal);
    TEST_ASSERT_NULL(Protocols.handler);
}

void test_dispatch_drops_unregistered_protocol_event()
{
    conn_pool[0].proto = PROTO_NONE;
    http_pool[0].parse_state = PARSE_COMPLETE;

    TcpEvt evt = {EVT_CONNECT, 0, 0};
    TcpListener.idx = 0;
    TcpListener.q.evt = &evt;
    TcpListener.enqueue(TcpListener.internal);
    (void)TcpListener.ok;
    Session.worker_id = 0;
    Session.tick(Session.internal);

    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
}

void test_dispatch_skips_null_callback_fields()
{
    static const ProtoHandler fake_handler = {NULL, NULL, NULL, NULL};
    Protocols.proto = PROTO_TELNET;
    Protocols.h = &fake_handler;
    Protocols.add(Protocols.internal);

    conn_pool[0].proto = PROTO_TELNET;
    http_pool[0].parse_state = PARSE_COMPLETE;

    TcpEvt e0 = {EVT_CONNECT, 0, 0};
    TcpListener.idx = 0;
    TcpListener.q.evt = &e0;
    TcpListener.enqueue(TcpListener.internal);
    (void)TcpListener.ok;
    Session.worker_id = 0;
    Session.tick(Session.internal);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);

    TcpEvt e1 = {EVT_DATA, 0, 0};
    TcpListener.idx = 0;
    TcpListener.q.evt = &e1;
    TcpListener.enqueue(TcpListener.internal);
    (void)TcpListener.ok;
    Session.worker_id = 0;
    Session.tick(Session.internal);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);

    TcpEvt e2 = {EVT_DISCONNECT, 0, 0};
    TcpListener.idx = 0;
    TcpListener.q.evt = &e2;
    TcpListener.enqueue(TcpListener.internal);
    (void)TcpListener.ok;
    Session.worker_id = 0;
    Session.tick(Session.internal);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);

    conn_pool[0].proto = PROTO_HTTP;
}

void test_dispatch_ignores_unknown_evt_type()
{
    http_pool[0].parse_state = PARSE_COMPLETE;

    TcpEvt evt = {(EvtType)99, 0, 0};
    TcpListener.idx = 0;
    TcpListener.q.evt = &evt;
    TcpListener.enqueue(TcpListener.internal);
    (void)TcpListener.ok;
    Session.worker_id = 0;
    Session.tick(Session.internal);

    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
}

void test_tick_skips_active_listener_with_null_queue()
{
    listener_pool[1].active = PROTO_TRUE;
    listener_pool[1].queue = NULL;
    Session.worker_id = 0;
    Session.tick(Session.internal);
    listener_pool[1].active = PROTO_FALSE;
    TEST_PASS();
}

void race_external_free_between_ticks()
{
    conn_pool[0].last_activity_ms = 0;

    set_millis(CONN_TIMEOUT_MS);
    Session.worker_id = 0;
    Session.tick(Session.internal);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);

    Session.worker_id = 0;

    Session.tick(Session.internal);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

void race_activity_update_saves_slot_from_timeout()
{
    conn_pool[0].last_activity_ms = 0;
    set_millis(CONN_TIMEOUT_MS - 1);

    Session.worker_id = 0;

    Session.tick(Session.internal);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);

    conn_pool[0].last_activity_ms = CONN_TIMEOUT_MS - 1;

    set_millis(CONN_TIMEOUT_MS);
    Session.worker_id = 0;
    Session.tick(Session.internal);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

void race_all_expire_then_idle_tick()
{
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].last_activity_ms = 0;
    }
    set_millis(CONN_TIMEOUT_MS);
    Session.worker_id = 0;
    Session.tick(Session.internal);
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[i].state);
    }

    Session.worker_id = 0;

    Session.tick(Session.internal);
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[i].state);
    }
}

void race_millis_wraparound_no_spurious_timeout()
{

    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].last_activity_ms = 0xFFFFFFFF - 100u;

    set_millis((uint32_t)(CONN_TIMEOUT_MS - 200));
    Session.worker_id = 0;
    Session.tick(Session.internal);

    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

static void stage_data_evt(uint8_t slot)
{
    TcpEvt evt = {EVT_DATA, slot, 0};
    TcpListener.idx = 0;
    TcpListener.q.evt = &evt;
    TcpListener.enqueue(TcpListener.internal);
}

void test_first_data_event_arms_the_request_deadline()
{
    http_req_start_ms[0] = 0;
    set_millis(4242);
    stage_data_evt(0);
    Session.worker_id = 0;
    Session.tick(Session.internal);
    TEST_ASSERT_EQUAL_UINT32(4242, http_req_start_ms[0]);
}

void test_a_request_already_under_way_keeps_its_arm()
{
    http_req_start_ms[0] = 0;
    set_millis(4242);
    stage_data_evt(0);
    Session.worker_id = 0;
    Session.tick(Session.internal);

    set_millis(9999);
    stage_data_evt(0);
    Session.worker_id = 0;
    Session.tick(Session.internal);
    TEST_ASSERT_EQUAL_UINT32(4242, http_req_start_ms[0]);
}

void test_a_zero_stamp_still_reads_as_armed()
{
    http_req_start_ms[0] = 0;
    set_millis(0);
    stage_data_evt(0);
    Session.worker_id = 0;
    Session.tick(Session.internal);
    TEST_ASSERT_EQUAL_UINT32(1, http_req_start_ms[0]);
}
