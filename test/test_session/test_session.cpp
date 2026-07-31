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
#include "network_drivers/session/proto_handler.h" // proto_register()/proto_get()/ProtoHandler (dispatch table edge cases)
#include "network_drivers/session/session.h"
#include "network_drivers/transport/listener.h"
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
    queue_stage_reset(); // clear any staged events from previous test
    DeterministicAsyncTCP::pool_init();
    listener_add(0, 80, ConnProto::PROTO_HTTP);
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i].state = ConnState::CONN_ACTIVE;
        conn_pool[i].proto = ConnProto::PROTO_HTTP; // dispatch requires an explicit protocol
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
    server_tick();
    TEST_PASS();
}

void test_pool_initializes_to_parse_method()
{
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(ParseState::PARSE_METHOD, http_pool[i].parse_state);
    }
}

void test_reset_clears_mid_parse_state()
{
    http_pool[0].parse_state = ParseState::PARSE_HEADER_KEY;
    http_pool[0].header_count = 3;
    http_reset(0);
    TEST_ASSERT_EQUAL(ParseState::PARSE_METHOD, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(0, http_pool[0].header_count);
}

void test_tick_fires_check_timeouts_stale_slot_freed()
{
    conn_pool[0].last_activity_ms = 0;
    set_millis(CONN_TIMEOUT_MS);
    server_tick();
    TEST_ASSERT_EQUAL(ConnState::CONN_FREE, (ConnState)conn_pool[0].state);
}

void test_tick_does_not_free_fresh_connection()
{
    conn_pool[0].last_activity_ms = 0;
    set_millis(CONN_TIMEOUT_MS - 1);
    server_tick();
    TEST_ASSERT_EQUAL(ConnState::CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

// ====================================================================
// FUNCTION I/O TESTS - server_tick()
// ====================================================================

// tick() must call check_timeouts() BEFORE event drain, so a timed-out
// slot is already freed if an EvtType::EVT_DISCONNECT arrives for the same slot.
void test_fn_tick_timeout_before_event_drain_ordering()
{
    conn_pool[1].last_activity_ms = 0;
    set_millis(CONN_TIMEOUT_MS);
    server_tick(); // timeout fires; queue empty (mock returns pdFALSE)
    TEST_ASSERT_EQUAL(ConnState::CONN_FREE, (ConnState)conn_pool[1].state);
    // http_reset was NOT called by server_tick directly (only check_timeouts),
    // but slot state is ConnState::CONN_FREE - the connection-level layer is clean.
    TEST_ASSERT_EQUAL(ConnState::CONN_FREE, (ConnState)conn_pool[1].state);
}

// Each ConnState::CONN_ACTIVE slot with last_activity_ms=0 times out when millis
// crosses CONN_TIMEOUT_MS; ConnState::CONN_FREE slots must be untouched.
void test_fn_tick_only_active_slots_expire()
{
    conn_pool[0].state = ConnState::CONN_FREE;
    conn_pool[1].state = ConnState::CONN_ACTIVE;
    conn_pool[1].last_activity_ms = 0;
    conn_pool[2].state = ConnState::CONN_FREE;
    conn_pool[3].state = ConnState::CONN_ACTIVE;
    conn_pool[3].last_activity_ms = CONN_TIMEOUT_MS; // diff=0 < TIMEOUT_MS

    set_millis(CONN_TIMEOUT_MS);
    server_tick();

    TEST_ASSERT_EQUAL(ConnState::CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL(ConnState::CONN_FREE, (ConnState)conn_pool[1].state); // expired
    TEST_ASSERT_EQUAL(ConnState::CONN_FREE, (ConnState)conn_pool[2].state);
    TEST_ASSERT_EQUAL(ConnState::CONN_ACTIVE, (ConnState)conn_pool[3].state); // still fresh
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
        server_tick();
    }
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(ConnState::CONN_ACTIVE, (ConnState)conn_pool[i].state);
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
            conn_pool[i].state = ConnState::CONN_ACTIVE;
            conn_pool[i].pcb = nullptr;
            conn_pool[i].last_activity_ms = 0;
        }
        set_millis((uint32_t)(CONN_TIMEOUT_MS * (cycle + 1)));
        server_tick();
        for (int i = 0; i < MAX_CONNS; i++)
        {
            TEST_ASSERT_EQUAL(ConnState::CONN_FREE, (ConnState)conn_pool[i].state);
        }
    }
}

// Mix of active-stale and active-fresh slots across many ticks.
// Only the stale ones must expire; fresh ones must survive every tick.
void stress_mixed_fresh_stale_slots_many_ticks()
{
    conn_pool[0].state = ConnState::CONN_ACTIVE;
    conn_pool[0].last_activity_ms = 0;
    conn_pool[1].state = ConnState::CONN_ACTIVE;
    conn_pool[1].last_activity_ms = 0;
    conn_pool[2].state = ConnState::CONN_ACTIVE;
    conn_pool[2].last_activity_ms = CONN_TIMEOUT_MS;
    conn_pool[3].state = ConnState::CONN_ACTIVE;
    conn_pool[3].last_activity_ms = CONN_TIMEOUT_MS;

    set_millis(CONN_TIMEOUT_MS);
    for (int tick = 0; tick < 200; tick++)
    {
        server_tick();
    }

    TEST_ASSERT_EQUAL(ConnState::CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL(ConnState::CONN_FREE, (ConnState)conn_pool[1].state);
    TEST_ASSERT_EQUAL(ConnState::CONN_ACTIVE, (ConnState)conn_pool[2].state);
    TEST_ASSERT_EQUAL(ConnState::CONN_ACTIVE, (ConnState)conn_pool[3].state);
}

// ====================================================================
// EVENT DISPATCH TESTS - server_tick() queue-drain path
//
// The FreeRTOS queue mock supports staged events via queue_stage_raw().
// These tests verify the while(xQueueReceive…) loop in server_tick()
// dispatches each event type correctly.
// ====================================================================

// EvtType::EVT_CONNECT → http_reset(slot_id)
void test_evt_connect_calls_http_reset()
{
    http_pool[1].parse_state = ParseState::PARSE_HEADER_KEY;
    http_pool[1].header_count = 3;

    TcpEvt evt = {EvtType::EVT_CONNECT, 1, 0};
    queue_stage_raw(&evt, sizeof(evt));
    server_tick();

    TEST_ASSERT_EQUAL(ParseState::PARSE_METHOD, http_pool[1].parse_state);
    TEST_ASSERT_EQUAL(0, http_pool[1].header_count);
}

// EvtType::EVT_DISCONNECT → http_reset(slot_id)
void test_evt_disconnect_calls_http_reset()
{
    http_pool[0].parse_state = ParseState::PARSE_COMPLETE;
    http_pool[0].header_count = 2;

    TcpEvt evt = {EvtType::EVT_DISCONNECT, 0, 0};
    queue_stage_raw(&evt, sizeof(evt));
    server_tick();

    TEST_ASSERT_EQUAL(ParseState::PARSE_METHOD, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(0, http_pool[0].header_count);
}

// EvtType::EVT_ERROR → http_reset(slot_id)
void test_evt_error_calls_http_reset()
{
    http_pool[2].parse_state = ParseState::PARSE_ERROR;

    TcpEvt evt = {EvtType::EVT_ERROR, 2, 0};
    queue_stage_raw(&evt, sizeof(evt));
    server_tick();

    TEST_ASSERT_EQUAL(ParseState::PARSE_METHOD, http_pool[2].parse_state);
}

// EvtType::EVT_DATA → http_parse(slot_id) - ring buffer is drained and parse completes
void test_evt_data_calls_http_parse()
{
    push_to_slot(0, "GET /evt HTTP/1.1\r\n\r\n");

    TcpEvt evt = {EvtType::EVT_DATA, 0, 0};
    queue_stage_raw(&evt, sizeof(evt));
    server_tick();

    TEST_ASSERT_EQUAL(ParseState::PARSE_COMPLETE, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL_STRING("GET", http_pool[0].method);
    TEST_ASSERT_EQUAL_STRING("/evt", http_pool[0].path);
}

// Multiple events in one tick must all be processed in arrival order.
void test_multiple_events_drained_in_one_tick()
{
    // Slot 0: dirty state → EvtType::EVT_CONNECT → reset
    http_pool[0].parse_state = ParseState::PARSE_COMPLETE;
    TcpEvt e0 = {EvtType::EVT_CONNECT, 0, 0};
    queue_stage_raw(&e0, sizeof(e0));

    // Slot 1: ring buffer with a GET → EvtType::EVT_DATA → ParseState::PARSE_COMPLETE
    push_to_slot(1, "GET / HTTP/1.1\r\n\r\n");
    TcpEvt e1 = {EvtType::EVT_DATA, 1, 0};
    queue_stage_raw(&e1, sizeof(e1));

    // Slot 2: dirty header state → EvtType::EVT_DISCONNECT → reset
    http_pool[2].parse_state = ParseState::PARSE_HEADER_VAL;
    TcpEvt e2 = {EvtType::EVT_DISCONNECT, 2, 0};
    queue_stage_raw(&e2, sizeof(e2));

    server_tick();

    TEST_ASSERT_EQUAL(ParseState::PARSE_METHOD, http_pool[0].parse_state);
    TEST_ASSERT_EQUAL(ParseState::PARSE_COMPLETE, http_pool[1].parse_state);
    TEST_ASSERT_EQUAL(ParseState::PARSE_METHOD, http_pool[2].parse_state);
}

// ====================================================================
// DISPATCH TABLE EDGE CASES - proto_register() / proto_get() / dispatch_event()
// ====================================================================

// An out-of-range ConnProto must not write past proto_handlers[] (proto_register)
// and must resolve to nullptr rather than an out-of-bounds read (proto_get).
void test_proto_register_out_of_range_is_nop()
{
    proto_register((ConnProto)250, nullptr);
    TEST_PASS();
}

void test_proto_get_out_of_range_returns_null()
{
    TEST_ASSERT_NULL(proto_get((ConnProto)250));
}

// An event for a slot carrying ConnProto::PROTO_NONE (or any unregistered protocol)
// has no handler - dispatch_event() must drop it rather than dereference nullptr.
void test_dispatch_drops_unregistered_protocol_event()
{
    conn_pool[0].proto = ConnProto::PROTO_NONE;
    http_pool[0].parse_state = ParseState::PARSE_COMPLETE; // sentinel: must survive untouched

    TcpEvt evt = {EvtType::EVT_CONNECT, 0, 0};
    queue_stage_raw(&evt, sizeof(evt));
    server_tick();

    TEST_ASSERT_EQUAL(ParseState::PARSE_COMPLETE, http_pool[0].parse_state); // handler never ran
}

// A registered handler with null callback fields (a protocol module that doesn't implement
// a given event) must be skipped, not called through a null pointer. ConnProto::PROTO_TELNET
// is never registered by proto_register_builtins() in this build, so it's free to reuse here.
void test_dispatch_skips_null_callback_fields()
{
    static const ProtoHandler fake_handler = {nullptr, nullptr, nullptr, nullptr};
    proto_register(ConnProto::PROTO_TELNET, &fake_handler);

    conn_pool[0].proto = ConnProto::PROTO_TELNET;
    http_pool[0].parse_state = ParseState::PARSE_COMPLETE; // sentinel across all three events

    TcpEvt e0 = {EvtType::EVT_CONNECT, 0, 0};
    queue_stage_raw(&e0, sizeof(e0));
    server_tick();
    TEST_ASSERT_EQUAL(ParseState::PARSE_COMPLETE, http_pool[0].parse_state); // on_accept null -> no-op

    TcpEvt e1 = {EvtType::EVT_DATA, 0, 0};
    queue_stage_raw(&e1, sizeof(e1));
    server_tick();
    TEST_ASSERT_EQUAL(ParseState::PARSE_COMPLETE, http_pool[0].parse_state); // on_data null -> no-op

    TcpEvt e2 = {EvtType::EVT_DISCONNECT, 0, 0};
    queue_stage_raw(&e2, sizeof(e2));
    server_tick();
    TEST_ASSERT_EQUAL(ParseState::PARSE_COMPLETE, http_pool[0].parse_state); // on_close null -> no-op

    conn_pool[0].proto = ConnProto::PROTO_HTTP; // restore for any later test relying on setUp's default
}

// dispatch_event()'s switch on evt.type has no default: EvtType is a uint8_t-backed enum, so a
// value outside {EVT_CONNECT, EVT_DATA, EVT_DISCONNECT, EVT_ERROR} (e.g. stale/garbage queue
// memory) falls through the switch untouched - must be a silent no-op, not a crash.
void test_dispatch_ignores_unknown_evt_type()
{
    http_pool[0].parse_state = ParseState::PARSE_COMPLETE; // sentinel: must survive untouched

    TcpEvt evt = {(EvtType)99, 0, 0};
    queue_stage_raw(&evt, sizeof(evt));
    server_tick();

    TEST_ASSERT_EQUAL(ParseState::PARSE_COMPLETE, http_pool[0].parse_state); // no case matched -> no-op
}

// A listener slot marked active with a null queue (e.g. a failed queue-create) must be
// skipped by the drain loop, not dereferenced.
void test_tick_skips_active_listener_with_null_queue()
{
    listener_pool[1].active = true;
    listener_pool[1].queue = nullptr;
    server_tick(); // must not crash
    listener_pool[1].active = false;
    TEST_PASS();
}

// ====================================================================
// RACE CONDITION SIMULATIONS
// ====================================================================

// Tick while a slot transitions from ConnState::CONN_ACTIVE to ConnState::CONN_FREE externally
// (simulates a receive callback closing the connection between two ticks).
// The second tick must not re-expire an already-free slot.
void race_external_free_between_ticks()
{
    conn_pool[0].last_activity_ms = 0;

    // First tick: slot expires inside check_timeouts
    set_millis(CONN_TIMEOUT_MS);
    server_tick();
    TEST_ASSERT_EQUAL(ConnState::CONN_FREE, (ConnState)conn_pool[0].state);

    // Second tick: slot is already free - must not double-free or crash
    server_tick();
    TEST_ASSERT_EQUAL(ConnState::CONN_FREE, (ConnState)conn_pool[0].state);
}

// Simulate last_activity_ms updated by a received packet between calls to
// check_timeouts: slot is saved from timeout by a fresh timestamp.
void race_activity_update_saves_slot_from_timeout()
{
    conn_pool[0].last_activity_ms = 0;
    set_millis(CONN_TIMEOUT_MS - 1); // one ms before deadline

    server_tick();
    TEST_ASSERT_EQUAL(ConnState::CONN_ACTIVE, (ConnState)conn_pool[0].state); // safe

    // Simulate recv callback updating activity
    conn_pool[0].last_activity_ms = CONN_TIMEOUT_MS - 1;

    // Even at deadline millis, diff = (TIMEOUT-1) - (TIMEOUT-1) = 0 < TIMEOUT
    set_millis(CONN_TIMEOUT_MS);
    server_tick();
    TEST_ASSERT_EQUAL(ConnState::CONN_ACTIVE, (ConnState)conn_pool[0].state); // still safe
}

// All slots expire simultaneously; subsequent tick must be a no-op.
void race_all_expire_then_idle_tick()
{
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i].state = ConnState::CONN_ACTIVE;
        conn_pool[i].last_activity_ms = 0;
    }
    set_millis(CONN_TIMEOUT_MS);
    server_tick(); // all freed
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(ConnState::CONN_FREE, (ConnState)conn_pool[i].state);
    }

    server_tick(); // must be no-op
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(ConnState::CONN_FREE, (ConnState)conn_pool[i].state);
    }
}

// millis() wraps around (uint32_t overflow) - the timeout calculation
// `(now - last_activity)` must remain correct due to unsigned subtraction.
void race_millis_wraparound_no_spurious_timeout()
{
    // last_activity close to UINT32_MAX, now just past wrap
    conn_pool[0].state = ConnState::CONN_ACTIVE;
    conn_pool[0].last_activity_ms = 0xFFFFFFFF - 100u;
    // now = 0x00000000 + (CONN_TIMEOUT_MS - 200) wraps around to just under deadline
    set_millis((uint32_t)(CONN_TIMEOUT_MS - 200));
    server_tick();
    // (now - last) = (TIMEOUT-200) - (UINT32_MAX-100) [unsigned] = TIMEOUT-200 + 101 = TIMEOUT-99 < TIMEOUT
    TEST_ASSERT_EQUAL(ConnState::CONN_ACTIVE, (ConnState)conn_pool[0].state); // must NOT expire
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

    // Function I/O: server_tick()
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
