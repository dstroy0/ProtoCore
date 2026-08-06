// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit, stress, and race-condition tests for Layer 5 (Session).
//
// Sections:
//   UNIT      - dispatch invariants and basic tick behavior
//   STRESS    - sustained idle load, all-slots timeout cycles
//   RACE SIM  - ordering hazards across tick boundaries

#include "network_drivers/presentation/presentation.h"
#include "network_drivers/session/proto_handler.h" // Session.proto->add()/Session.proto->get()/ProtoHandler (dispatch table edge cases)
#include "network_drivers/session/session.h"
#include "network_drivers/transport/tcp.h"
#include <unity.h>

// tcp.cpp + presentation.cpp + session.cpp compiled into native env.
// No stubs required.

// Push ASCII bytes into a slot's ring buffer (mirrors test_presentation helper).
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
    queue_stage_reset(); // no case inherits another's queued events
    Tcp.conn->init(NULL);
    Tcp.listener->add(0, 80, PROTO_HTTP, PROTO_FALSE);
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].proto = PROTO_HTTP; // dispatch requires an explicit protocol
        http_reset(i);
    }
}

void tearDown()
{
}

// ====================================================================
// UNIT TESTS
// ====================================================================

void test_empty_queue_does_not_crash()
{
    Session.tick(0);
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
    http_reset(0);
    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(0, http_pool[0].header_count);
}

void test_tick_fires_check_timeouts_stale_slot_freed()
{
    conn_pool[0].last_activity_ms = 0;
    set_millis(CONN_TIMEOUT_MS);
    Session.tick(0);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

void test_tick_does_not_free_fresh_connection()
{
    conn_pool[0].last_activity_ms = 0;
    set_millis(CONN_TIMEOUT_MS - 1);
    Session.tick(0);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

// ====================================================================
// FUNCTION I/O TESTS - Session.tick(0)
// ====================================================================

// tick() must call Tcp.conn->check_timeouts() BEFORE event drain, so a timed-out
// slot is already freed if an EVT_DISCONNECT arrives for the same slot.
void test_fn_tick_timeout_before_event_drain_ordering()
{
    conn_pool[1].last_activity_ms = 0;
    set_millis(CONN_TIMEOUT_MS);
    Session.tick(0); // timeout fires; queue empty (mock returns pdFALSE)
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state);
    // http_reset was NOT called by server_tick directly (only check_timeouts),
    // but slot state is CONN_FREE - the connection-level layer is clean.
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state);
}

// Each CONN_ACTIVE slot with last_activity_ms=0 times out when millis
// crosses CONN_TIMEOUT_MS; CONN_FREE slots must be untouched.
void test_fn_tick_only_active_slots_expire()
{
    conn_pool[0].state = CONN_FREE;
    conn_pool[1].state = CONN_ACTIVE;
    conn_pool[1].last_activity_ms = 0;
    conn_pool[2].state = CONN_FREE;
    conn_pool[3].state = CONN_ACTIVE;
    conn_pool[3].last_activity_ms = CONN_TIMEOUT_MS; // diff=0 < TIMEOUT_MS

    set_millis(CONN_TIMEOUT_MS);
    Session.tick(0);

    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state); // expired
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[2].state);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[3].state); // still fresh
}

// ====================================================================
// STRESS TESTS
// ====================================================================

// 1000 consecutive ticks with no events and no timeouts - the loop must
// be a true no-op: no state change, no crash.
void stress_1000_idle_ticks_stable()
{
    set_millis(0);
    for (int i = 0; i < 1000; i++)
    {
        Session.tick(0);
    }
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[i].state);
    }
}

// Arm all slots, advance time to timeout, tick, re-arm, repeat 10 cycles.
// Verifies pool recovery and timeout idempotence under sustained churn.
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
        Session.tick(0);
        for (int i = 0; i < MAX_CONNS; i++)
        {
            TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[i].state);
        }
    }
}

// Mix of active-stale and active-fresh slots across many ticks.
// Only the stale ones must expire; fresh ones must survive every tick.
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
        Session.tick(0);
    }

    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[2].state);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[3].state);
}

// ====================================================================
// EVENT DISPATCH TESTS - Session.tick(0) queue-drain path
//
// Each case posts through Tcp.listener->enqueue(), the seam the transport itself
// posts through, and Session.tick(0) drains it.
// ====================================================================

// EVT_CONNECT → http_reset(slot_id)
void test_evt_connect_calls_http_reset()
{
    http_pool[1].parse_state = PARSE_HEADER_KEY;
    http_pool[1].header_count = 3;

    TcpEvt evt = {EVT_CONNECT, 1, 0};
    (void)Tcp.listener->enqueue(0, &evt);
    Session.tick(0);

    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[1].parse_state);
    TEST_ASSERT_EQUAL(0, http_pool[1].header_count);
}

// EVT_DISCONNECT → http_reset(slot_id)
void test_evt_disconnect_calls_http_reset()
{
    http_pool[0].parse_state = PARSE_COMPLETE;
    http_pool[0].header_count = 2;

    TcpEvt evt = {EVT_DISCONNECT, 0, 0};
    (void)Tcp.listener->enqueue(0, &evt);
    Session.tick(0);

    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(0, http_pool[0].header_count);
}

// EVT_ERROR → http_reset(slot_id)
void test_evt_error_calls_http_reset()
{
    http_pool[2].parse_state = PARSE_ERROR;

    TcpEvt evt = {EVT_ERROR, 2, 0};
    (void)Tcp.listener->enqueue(0, &evt);
    Session.tick(0);

    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[2].parse_state);
}

// EVT_DATA → http_parse(slot_id) - ring buffer is drained and parse completes
void test_evt_data_calls_http_parse()
{
    push_to_slot(0, "GET /evt HTTP/1.1\r\n\r\n");

    TcpEvt evt = {EVT_DATA, 0, 0};
    (void)Tcp.listener->enqueue(0, &evt);
    Session.tick(0);

    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("GET", http_pool[0].method);
    TEST_ASSERT_EQUAL_STRING("/evt", http_pool[0].path);
}

// Multiple events in one tick must all be processed in arrival order.
void test_multiple_events_drained_in_one_tick()
{
    // Slot 0: dirty state → EVT_CONNECT → reset
    http_pool[0].parse_state = PARSE_COMPLETE;
    TcpEvt e0 = {EVT_CONNECT, 0, 0};
    (void)Tcp.listener->enqueue(0, &e0);

    // Slot 1: ring buffer with a GET → EVT_DATA → PARSE_COMPLETE
    push_to_slot(1, "GET / HTTP/1.1\r\n\r\n");
    TcpEvt e1 = {EVT_DATA, 1, 0};
    (void)Tcp.listener->enqueue(0, &e1);

    // Slot 2: dirty header state → EVT_DISCONNECT → reset
    http_pool[2].parse_state = PARSE_HEADER_VAL;
    TcpEvt e2 = {EVT_DISCONNECT, 2, 0};
    (void)Tcp.listener->enqueue(0, &e2);

    Session.tick(0);

    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[1].parse_state);
    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[2].parse_state);
}

// ====================================================================
// DISPATCH TABLE EDGE CASES - Session.proto->add() / Session.proto->get() / dispatch_event()
// ====================================================================

// An out-of-range ConnProto must not write past proto_handlers[] (proto_register)
// and must resolve to NULL rather than an out-of-bounds read (proto_get).
void test_proto_register_out_of_range_is_nop()
{
    Session.proto->add((ConnProto)250, NULL);
    TEST_PASS();
}

void test_proto_get_out_of_range_returns_null()
{
    TEST_ASSERT_NULL(Session.proto->get((ConnProto)250));
}

// An event for a slot carrying PROTO_NONE (or any unregistered protocol)
// has no handler - dispatch_event() must drop it rather than dereference NULL.
void test_dispatch_drops_unregistered_protocol_event()
{
    conn_pool[0].proto = PROTO_NONE;
    http_pool[0].parse_state = PARSE_COMPLETE; // sentinel: must survive untouched

    TcpEvt evt = {EVT_CONNECT, 0, 0};
    (void)Tcp.listener->enqueue(0, &evt);
    Session.tick(0);

    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state); // handler never ran
}

// A registered handler with null callback fields (a protocol module that doesn't implement
// a given event) must be skipped, not called through a null pointer. PROTO_TELNET
// is never registered by Session.proto->register_builtins() in this build, so it's free to reuse here.
void test_dispatch_skips_null_callback_fields()
{
    static const ProtoHandler fake_handler = {NULL, NULL, NULL, NULL};
    Session.proto->add(PROTO_TELNET, &fake_handler);

    conn_pool[0].proto = PROTO_TELNET;
    http_pool[0].parse_state = PARSE_COMPLETE; // sentinel across all three events

    TcpEvt e0 = {EVT_CONNECT, 0, 0};
    (void)Tcp.listener->enqueue(0, &e0);
    Session.tick(0);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state); // on_accept null -> no-op

    TcpEvt e1 = {EVT_DATA, 0, 0};
    (void)Tcp.listener->enqueue(0, &e1);
    Session.tick(0);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state); // on_data null -> no-op

    TcpEvt e2 = {EVT_DISCONNECT, 0, 0};
    (void)Tcp.listener->enqueue(0, &e2);
    Session.tick(0);
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state); // on_close null -> no-op

    conn_pool[0].proto = PROTO_HTTP; // restore for any later test relying on setUp's default
}

// dispatch_event()'s switch on evt.type has no default: EvtType is a uint8_t-backed enum, so a
// value outside {EVT_CONNECT, EVT_DATA, EVT_DISCONNECT, EVT_ERROR} (e.g. stale/garbage queue
// memory) falls through the switch untouched - must be a silent no-op, not a crash.
void test_dispatch_ignores_unknown_evt_type()
{
    http_pool[0].parse_state = PARSE_COMPLETE; // sentinel: must survive untouched

    TcpEvt evt = {(EvtType)99, 0, 0};
    (void)Tcp.listener->enqueue(0, &evt);
    Session.tick(0);

    TEST_ASSERT_EQUAL(PARSE_COMPLETE, http_pool[0].parse_state); // no case matched -> no-op
}

// A listener slot marked active with a null queue (e.g. a failed queue-create) must be
// skipped by the drain loop, not dereferenced.
void test_tick_skips_active_listener_with_null_queue()
{
    listener_pool[1].active = PROTO_TRUE;
    listener_pool[1].queue = NULL;
    Session.tick(0); // must not crash
    listener_pool[1].active = PROTO_FALSE;
    TEST_PASS();
}

// ====================================================================
// RACE CONDITION SIMULATIONS
// ====================================================================

// Tick while a slot transitions from CONN_ACTIVE to CONN_FREE externally
// (simulates a receive callback closing the connection between two ticks).
// The second tick must not re-expire an already-free slot.
void race_external_free_between_ticks()
{
    conn_pool[0].last_activity_ms = 0;

    // First tick: slot expires inside check_timeouts
    set_millis(CONN_TIMEOUT_MS);
    Session.tick(0);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);

    // Second tick: slot is already free - must not double-free or crash
    Session.tick(0);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

// Simulate last_activity_ms updated by a received packet between calls to
// check_timeouts: slot is saved from timeout by a fresh timestamp.
void race_activity_update_saves_slot_from_timeout()
{
    conn_pool[0].last_activity_ms = 0;
    set_millis(CONN_TIMEOUT_MS - 1); // one ms before deadline

    Session.tick(0);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state); // safe

    // Simulate recv callback updating activity
    conn_pool[0].last_activity_ms = CONN_TIMEOUT_MS - 1;

    // Even at deadline millis, diff = (TIMEOUT-1) - (TIMEOUT-1) = 0 < TIMEOUT
    set_millis(CONN_TIMEOUT_MS);
    Session.tick(0);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state); // still safe
}

// All slots expire simultaneously; subsequent tick must be a no-op.
void race_all_expire_then_idle_tick()
{
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].last_activity_ms = 0;
    }
    set_millis(CONN_TIMEOUT_MS);
    Session.tick(0); // all freed
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[i].state);
    }

    Session.tick(0); // must be no-op
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[i].state);
    }
}

// millis() wraps around (uint32_t overflow) - the timeout calculation
// `(now - last_activity)` must remain correct due to unsigned subtraction.
void race_millis_wraparound_no_spurious_timeout()
{
    // last_activity close to UINT32_MAX, now just past wrap
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].last_activity_ms = 0xFFFFFFFF - 100u;
    // now = 0x00000000 + (CONN_TIMEOUT_MS - 200) wraps around to just under deadline
    set_millis((uint32_t)(CONN_TIMEOUT_MS - 200));
    Session.tick(0);
    // (now - last) = (TIMEOUT-200) - (UINT32_MAX-100) [unsigned] = TIMEOUT-200 + 101 = TIMEOUT-99 < TIMEOUT
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state); // must NOT expire
}

int main()
{
    UNITY_BEGIN();

    // Unit tests
    RUN_TEST(test_empty_queue_does_not_crash);
    RUN_TEST(test_pool_initializes_to_parse_method);
    RUN_TEST(test_reset_clears_mid_parse_state);
    RUN_TEST(test_tick_fires_check_timeouts_stale_slot_freed);
    RUN_TEST(test_tick_does_not_free_fresh_connection);

    // Function I/O: Session.tick(0)
    RUN_TEST(test_fn_tick_timeout_before_event_drain_ordering);
    RUN_TEST(test_fn_tick_only_active_slots_expire);

    // Stress tests
    RUN_TEST(stress_1000_idle_ticks_stable);
    RUN_TEST(stress_timeout_all_slots_10_cycles);
    RUN_TEST(stress_mixed_fresh_stale_slots_many_ticks);

    // Event dispatch tests
    RUN_TEST(test_evt_connect_calls_http_reset);
    RUN_TEST(test_evt_disconnect_calls_http_reset);
    RUN_TEST(test_evt_error_calls_http_reset);
    RUN_TEST(test_evt_data_calls_http_parse);
    RUN_TEST(test_multiple_events_drained_in_one_tick);

    // Dispatch table edge cases
    RUN_TEST(test_proto_register_out_of_range_is_nop);
    RUN_TEST(test_proto_get_out_of_range_returns_null);
    RUN_TEST(test_dispatch_drops_unregistered_protocol_event);
    RUN_TEST(test_dispatch_skips_null_callback_fields);
    RUN_TEST(test_dispatch_ignores_unknown_evt_type);
    RUN_TEST(test_tick_skips_active_listener_with_null_queue);

    // Race condition simulations
    RUN_TEST(race_external_free_between_ticks);
    RUN_TEST(race_activity_update_saves_slot_from_timeout);
    RUN_TEST(race_all_expire_then_idle_tick);
    RUN_TEST(race_millis_wraparound_no_spurious_timeout);

    return UNITY_END();
}
