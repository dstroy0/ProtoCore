// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit and stress tests for Layer 4 (Transport) - constants, pool invariants,
// ring-buffer arithmetic, timeout logic, event-queue behavior, and
// sustained-load correctness.

#include "network_drivers/network/ip.h"
#include "network_drivers/transport/listener.h"
#include "network_drivers/transport/tcp.h"
#include <unity.h>

// tcp.cpp + listener.cpp are compiled into the native env - no stubs needed.

// Build a v4 pc_ip from a host-order word (0xC0A80005 -> 192.168.0.5). The accept-time
// gates key on the full family-tagged address, so the tests carry a pc_ip, not a uint32.
static pc_ip v4w(uint32_t host_order)
{
    return pc_ip_from_v4_octets((uint8_t)(host_order >> 24), (uint8_t)(host_order >> 16), (uint8_t)(host_order >> 8),
                                (uint8_t)host_order);
}

void setUp()
{
    set_millis(0);
    DeterministicAsyncTCP::pool_init();
    listener_add(0, 80, PROTO_HTTP);
}

void tearDown()
{
}

// ====================================================================
// UNIT TESTS
// ====================================================================

// ---- Compile-time constants ----------------------------------------

void test_pool_capacity_default_is_eight()
{
    // The default connection pool is 8 (keep-alive/concurrency headroom; see protocore_config.h).
    TEST_ASSERT_EQUAL(8, MAX_CONNS);
}
void test_rx_buffer_size_is_one_kb()
{
    TEST_ASSERT_EQUAL(1024, RX_BUF_SIZE);
}
void test_timeout_constant_is_5000ms()
{
    TEST_ASSERT_EQUAL(5000, CONN_TIMEOUT_MS);
}

// ---- Pool defaults after init() ------------------------------------

void test_all_slots_free_after_init()
{
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[i].state);
    }
}

void test_all_pcbs_null_after_init()
{
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_NULL(conn_pool[i].pcb);
    }
}

void test_all_ring_buffers_empty_after_init()
{
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(conn_pool[i].rx_head, conn_pool[i].rx_tail);
    }
}

void test_slot_ids_match_indices()
{
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(i, conn_pool[i].id);
    }
}

// ---- Ring buffer arithmetic ----------------------------------------

void test_ring_empty_when_head_equals_tail()
{
    TcpConn s = {};
    TEST_ASSERT_EQUAL(s.rx_head, s.rx_tail);
}

void test_ring_wrap_at_boundary()
{
    size_t next = (size_t)(RX_BUF_SIZE - 1 + 1) % RX_BUF_SIZE;
    TEST_ASSERT_EQUAL(0, (int)next);
}

void test_ring_full_sentinel_one_slot_reserved()
{
    size_t tail = 0;
    size_t head = RX_BUF_SIZE - 1;
    TEST_ASSERT_EQUAL(tail, (head + 1) % RX_BUF_SIZE);
}

void test_ring_can_store_size_minus_one_bytes()
{
    TcpConn s = {};
    s.rx_head = 0;
    s.rx_tail = 0;
    size_t count = 0;
    while (true)
    {
        size_t next = (s.rx_head + 1) % RX_BUF_SIZE;
        if (next == s.rx_tail)
        {
            break;
        }
        s.rx_buffer[s.rx_head] = (uint8_t)count;
        s.rx_head = next;
        count++;
    }
    TEST_ASSERT_EQUAL(RX_BUF_SIZE - 1, (int)count);
}

// ---- Event types ---------------------------------------------------

void test_event_types_are_distinct()
{
    TEST_ASSERT_NOT_EQUAL((int)EVT_CONNECT, (int)EVT_DATA);
    TEST_ASSERT_NOT_EQUAL((int)EVT_DATA, (int)EVT_DISCONNECT);
    TEST_ASSERT_NOT_EQUAL((int)EVT_DISCONNECT, (int)EVT_ERROR);
    TEST_ASSERT_NOT_EQUAL((int)EVT_CONNECT, (int)EVT_ERROR);
}

// ---- Timeout logic -------------------------------------------------

void test_timeout_does_not_fire_on_free_slot()
{
    conn_pool[0].state = CONN_FREE;
    set_millis(CONN_TIMEOUT_MS + 1);
    DeterministicAsyncTCP::check_timeouts();
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

void test_timeout_does_not_fire_before_deadline()
{
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = NULL;
    conn_pool[0].last_activity_ms = 0;
    set_millis(CONN_TIMEOUT_MS - 1);
    DeterministicAsyncTCP::check_timeouts();
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

void test_timeout_fires_at_deadline()
{
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = NULL;
    conn_pool[0].last_activity_ms = 0;
    set_millis(CONN_TIMEOUT_MS);
    DeterministicAsyncTCP::check_timeouts();
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
}

void test_timeout_fires_only_on_stale_slots()
{
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = NULL;
    conn_pool[0].last_activity_ms = 0;

    conn_pool[1].state = CONN_ACTIVE;
    conn_pool[1].pcb = NULL;
    conn_pool[1].last_activity_ms = CONN_TIMEOUT_MS; // fresh

    set_millis(CONN_TIMEOUT_MS);
    DeterministicAsyncTCP::check_timeouts();

    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[1].state);
}

// Regression (BUGS.md "large streamed response truncates mid-transfer"): a slot still paging out a
// body is active, not idle. The file/chunk send pumps call pc_conn_touch_active() each poll while a
// body is in flight, so an actively-sending slot must survive the idle sweep even past the deadline -
// otherwise a transient send stall truncates a large chunked/file response. Slot 0 is touched (like the
// pump would) and survives; an equally-stale UNtouched active slot 1 is still reaped (idle keep-alive).
void test_active_send_not_reaped()
{
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = NULL;
    conn_pool[0].last_activity_ms = 0; // stale - would be reaped at the deadline

    conn_pool[1].state = CONN_ACTIVE;
    conn_pool[1].pcb = NULL;
    conn_pool[1].last_activity_ms = 0; // equally stale, but NOT touched

    set_millis(CONN_TIMEOUT_MS + 10); // past the idle deadline
    pc_conn_touch_active(0);          // the pump's per-poll refresh for an in-flight body
    DeterministicAsyncTCP::check_timeouts();

    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state); // survives (streaming)
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state);   // reaped (idle)
}

// pool_init() with a real config uses its conn_timeout_ms instead of the compile-time default.
void test_pool_init_applies_custom_config()
{
    WebServerConfig cfg;
    cfg.conn_timeout_ms = 12345;
    DeterministicAsyncTCP::pool_init(&cfg);
    TEST_ASSERT_EQUAL_UINT32(12345, DeterministicAsyncTCP::conn_timeout_ms);
    DeterministicAsyncTCP::pool_init(); // restore the default for the rest of this test file
}

void test_init_succeeds_on_native()
{
    DeterministicAsyncTCP::pool_init();
    int32_t ok = listener_add(0, 80, PROTO_HTTP);
    TEST_ASSERT_EQUAL(1, ok);
}

// listener_add()'s bound check, and its full lwIP error surface on the host (non-ARDUINO)
// path: out of PCBs, the port already bound, and a listen-backlog allocation failure -
// each must fail closed (-1) without leaving a half-initialized listener behind.
void test_listener_add_bounds_and_lwip_failure_paths()
{
    TEST_ASSERT_EQUAL_INT32(-1, listener_add((uint8_t)MAX_LISTENERS, 80, PROTO_HTTP));

    mock_new_pcb_fail_once() = true;
    TEST_ASSERT_EQUAL_INT32(-1, listener_add(1, 81, PROTO_HTTP));

    mock_bind_fail_once() = true;
    TEST_ASSERT_EQUAL_INT32(-1, listener_add(1, 81, PROTO_HTTP));

    mock_listen_fail_once() = true;
    int before = mock_abort_call_count();
    TEST_ASSERT_EQUAL_INT32(-1, listener_add(1, 81, PROTO_HTTP));
    TEST_ASSERT_EQUAL_INT(before + 1, mock_abort_call_count()); // the allocated pcb is aborted, not leaked

    mock_queue_create_fail_once() = true;
    TEST_ASSERT_EQUAL_INT32(-1, listener_add(1, 81, PROTO_HTTP));

    // A normal call afterward still succeeds (the failure knobs auto-cleared).
    TEST_ASSERT_EQUAL_INT32(1, listener_add(1, 81, PROTO_HTTP));
    listener_stop(1);
}

// listener_stop()'s bound check.
void test_listener_stop_rejects_out_of_range_idx()
{
    listener_stop((uint8_t)MAX_LISTENERS); // no-op, must not crash
}

// listener_stop() / listener_stop_dynamic() only delete a queue that actually exists - an
// active listener somehow left without one (inconsistent, but guarded independently) is a
// clean no-op rather than a null-handle vQueueDelete().
void test_listener_stop_and_stop_dynamic_tolerate_a_missing_queue()
{
    listener_pool[0].active = true;
    listener_pool[0].queue = NULL;
    listener_stop(0); // must not crash; still deactivates
    TEST_ASSERT_FALSE(listener_pool[0].active);

    TEST_ASSERT_EQUAL_INT32(1, listener_add_dynamic(1, 5555, PROTO_HTTP));
    listener_pool[1].queue = NULL;
    listener_stop_dynamic(1);
    TEST_ASSERT_FALSE(listener_pool[1].active);
}

// Regression: init() must zero last_activity_ms (was left uninitialized
// before the conn_pool[i]={} fix, causing cross-test timeout spurious fires).
void test_all_last_activity_ms_zero_after_init()
{
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(0, (int)conn_pool[i].last_activity_ms);
    }
}

void test_queue_not_null_after_init()
{
    TEST_ASSERT_NOT_NULL(listener_pool[0].queue);
}

// ====================================================================
// STRESS TESTS
// ====================================================================

// Fill the ring buffer to max capacity with a known pattern,
// then drain and verify every byte - no corruption under full load.
void stress_ring_buffer_fill_drain_integrity()
{
    TcpConn *s = &conn_pool[0];
    s->rx_head = 0;
    s->rx_tail = 0;
    const int FILL = RX_BUF_SIZE - 1; // sentinel leaves one slot empty

    // Write known pattern
    for (int i = 0; i < FILL; i++)
    {
        size_t next = (s->rx_head + 1) % RX_BUF_SIZE;
        s->rx_buffer[s->rx_head] = (uint8_t)(i & 0xFF);
        s->rx_head = next;
    }

    // Buffer must report full (next_head == tail)
    TEST_ASSERT_EQUAL(RX_BUF_SIZE - 1, (int)((s->rx_head - s->rx_tail + RX_BUF_SIZE) % RX_BUF_SIZE));

    // Drain and verify every byte
    for (int i = 0; i < FILL; i++)
    {
        uint8_t expected = (uint8_t)(i & 0xFF);
        uint8_t actual = s->rx_buffer[s->rx_tail];
        s->rx_tail = (s->rx_tail + 1) % RX_BUF_SIZE;
        TEST_ASSERT_EQUAL_MESSAGE(expected, actual, "ring buffer byte mismatch");
    }

    // Must be empty
    TEST_ASSERT_EQUAL(s->rx_head, s->rx_tail);
}

// Half-fill, half-drain, repeat - forces pointer wrap-around multiple
// times and proves the circular invariant holds under sustained partial load.
void stress_ring_buffer_multi_cycle_no_corruption()
{
    TcpConn *s = &conn_pool[0];
    s->rx_head = 0;
    s->rx_tail = 0;

    uint8_t write_val = 0;
    uint8_t read_val = 0;

    for (int cycle = 0; cycle < 8; cycle++)
    {
        const int BATCH = RX_BUF_SIZE / 2; // 512 bytes per batch

        for (int i = 0; i < BATCH; i++)
        {
            size_t next = (s->rx_head + 1) % RX_BUF_SIZE;
            TEST_ASSERT_NOT_EQUAL_MESSAGE(next, s->rx_tail, "ring full during stress write");
            s->rx_buffer[s->rx_head] = write_val++;
            s->rx_head = next;
        }

        while (s->rx_tail != s->rx_head)
        {
            TEST_ASSERT_EQUAL_MESSAGE(read_val, s->rx_buffer[s->rx_tail], "ring corrupt on drain");
            read_val++;
            s->rx_tail = (s->rx_tail + 1) % RX_BUF_SIZE;
        }
    }

    TEST_ASSERT_EQUAL(s->rx_head, s->rx_tail); // empty after all cycles
}

// All four connection slots timeout simultaneously - every slot must be
// freed and none must corrupt a neighbour.
void stress_all_slots_timeout_simultaneously()
{
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].pcb = NULL;
        conn_pool[i].last_activity_ms = 0;
    }

    set_millis(CONN_TIMEOUT_MS);
    DeterministicAsyncTCP::check_timeouts();

    for (int i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[i].state);
        TEST_ASSERT_NULL(conn_pool[i].pcb);
        TEST_ASSERT_EQUAL(i, conn_pool[i].id); // id must not be smashed
    }
}

// Arms all slots, times them out, re-arms, times out again - 5 cycles.
// Verifies the pool recovers cleanly and check_timeouts is idempotent.
void stress_timeout_arm_recover_cycle()
{
    for (int cycle = 0; cycle < 5; cycle++)
    {
        for (int i = 0; i < MAX_CONNS; i++)
        {
            conn_pool[i].state = CONN_ACTIVE;
            conn_pool[i].pcb = NULL;
            conn_pool[i].last_activity_ms = 0;
        }

        set_millis((uint32_t)(CONN_TIMEOUT_MS * (cycle + 1)));
        DeterministicAsyncTCP::check_timeouts();

        for (int i = 0; i < MAX_CONNS; i++)
        {
            TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[i].state);
        }
    }
}

// Runs check_timeouts() 2000 times against a mix of free, active-fresh,
// and active-stale slots - verifies no crash and final state is correct.
void stress_check_timeouts_high_call_rate()
{
    conn_pool[0].state = CONN_FREE;
    conn_pool[1].state = CONN_ACTIVE;
    conn_pool[1].pcb = NULL;
    conn_pool[1].last_activity_ms = 0;
    conn_pool[2].state = CONN_ACTIVE;
    conn_pool[2].pcb = NULL;
    conn_pool[2].last_activity_ms = CONN_TIMEOUT_MS; // diff = (now - TIMEOUT_MS) = 0 < TIMEOUT_MS
    conn_pool[3].state = CONN_FREE;

    set_millis(CONN_TIMEOUT_MS); // slot 1 will expire, slot 2 won't

    for (int i = 0; i < 2000; i++)
    {
        DeterministicAsyncTCP::check_timeouts();
    }

    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state);   // expired
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[2].state); // still fresh
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[3].state);
}

// Fills ring buffer with ascending bytes one byte at a time, checking
// capacity after each write, then drains one byte at a time and verifies.
void stress_ring_buffer_byte_by_byte_fill_and_drain()
{
    TcpConn *s = &conn_pool[0];
    s->rx_head = 0;
    s->rx_tail = 0;

    int written = 0;
    while (true)
    {
        size_t next = (s->rx_head + 1) % RX_BUF_SIZE;
        if (next == s->rx_tail)
        {
            break; // full
        }
        s->rx_buffer[s->rx_head] = (uint8_t)(written & 0xFF);
        s->rx_head = next;
        written++;
    }
    TEST_ASSERT_EQUAL(RX_BUF_SIZE - 1, written);

    int read = 0;
    while (s->rx_tail != s->rx_head)
    {
        TEST_ASSERT_EQUAL((uint8_t)(read & 0xFF), s->rx_buffer[s->rx_tail]);
        s->rx_tail = (s->rx_tail + 1) % RX_BUF_SIZE;
        read++;
    }
    TEST_ASSERT_EQUAL(written, read);
}

// ====================================================================
// Accept-rate throttle (connection-flood defense)
// ====================================================================

// Within one window, the first MAX accepts pass and the next is rejected.
void test_accept_throttle_blocks_over_budget()
{
    listener_accept_throttle_reset();
    for (int i = 0; i < PC_ACCEPT_THROTTLE_MAX; i++)
    {
        TEST_ASSERT_TRUE(listener_accept_allowed(0));
    }
    TEST_ASSERT_FALSE(listener_accept_allowed(0)); // budget exhausted
}

// Crossing into the next window refills the budget.
void test_accept_throttle_window_refills()
{
    listener_accept_throttle_reset();
    for (int i = 0; i < PC_ACCEPT_THROTTLE_MAX; i++)
    {
        TEST_ASSERT_TRUE(listener_accept_allowed(10));
    }
    TEST_ASSERT_FALSE(listener_accept_allowed(10));
    // One full window later the counter resets.
    TEST_ASSERT_TRUE(listener_accept_allowed(10 + PC_ACCEPT_THROTTLE_WINDOW_MS));
}

// The unsigned window math survives a millis() rollover near 2^32.
void test_accept_throttle_handles_rollover()
{
    listener_accept_throttle_reset();
    uint32_t near_max = 0xFFFFFFFFu - 5;
    TEST_ASSERT_TRUE(listener_accept_allowed(near_max));
    // Wrap past zero: elapsed = (small - near_max) wraps to a large window jump.
    TEST_ASSERT_TRUE(listener_accept_allowed(near_max + PC_ACCEPT_THROTTLE_WINDOW_MS));
}

// ====================================================================
// Per-IP accept-rate throttle (per-source connection-flood defense)
// ====================================================================

// Within one window, a single source IP gets MAX accepts then is rejected.
void test_per_ip_throttle_blocks_over_budget()
{
    listener_per_ip_throttle_reset();
    pc_ip ip = v4w(0xC0A80005u); // 192.168.0.5
    for (int i = 0; i < PC_PER_IP_THROTTLE_MAX; i++)
    {
        TEST_ASSERT_TRUE(listener_accept_allowed_ip(&ip, 0));
    }
    TEST_ASSERT_FALSE(listener_accept_allowed_ip(&ip, 0)); // this address's budget exhausted
}

// One noisy address being throttled does not affect a different address.
void test_per_ip_throttle_isolates_addresses()
{
    listener_per_ip_throttle_reset();
    pc_ip noisy = v4w(0x0A000001u), quiet = v4w(0x0A000002u);
    for (int i = 0; i < PC_PER_IP_THROTTLE_MAX; i++)
    {
        TEST_ASSERT_TRUE(listener_accept_allowed_ip(&noisy, 0));
    }
    TEST_ASSERT_FALSE(listener_accept_allowed_ip(&noisy, 0)); // noisy is blocked
    TEST_ASSERT_TRUE(listener_accept_allowed_ip(&quiet, 0));  // a different IP is unaffected
}

// Crossing into the next window refills that address's budget.
void test_per_ip_throttle_window_refills()
{
    listener_per_ip_throttle_reset();
    pc_ip ip = v4w(0x0A000003u);
    for (int i = 0; i < PC_PER_IP_THROTTLE_MAX; i++)
    {
        TEST_ASSERT_TRUE(listener_accept_allowed_ip(&ip, 50));
    }
    TEST_ASSERT_FALSE(listener_accept_allowed_ip(&ip, 50));
    TEST_ASSERT_TRUE(listener_accept_allowed_ip(&ip, 50 + PC_PER_IP_THROTTLE_WINDOW_MS));
}

// When the bucket table is full of distinct addresses, a brand-new address still
// gets through (the least-recently-started bucket is evicted - bounded memory).
void test_per_ip_throttle_evicts_when_full()
{
    listener_per_ip_throttle_reset();
    for (int i = 0; i < PC_PER_IP_THROTTLE_SLOTS; i++)
    {
        pc_ip ip = v4w(0xAC100001u + (uint32_t)i);
        TEST_ASSERT_TRUE(listener_accept_allowed_ip(&ip, 100));
    }
    pc_ip fresh = v4w(0xDEADBEEFu);
    TEST_ASSERT_TRUE(listener_accept_allowed_ip(&fresh, 100)); // evicts an old bucket
}

// An unspecified source address is untrackable and is always allowed - it defers to
// the global throttle rather than being mis-tracked.
void test_per_ip_throttle_zero_ip_always_allowed()
{
    listener_per_ip_throttle_reset();
    pc_ip none;
    none.family = PC_IP_NONE;
    for (int i = 0; i < PC_PER_IP_THROTTLE_MAX + 5; i++)
    {
        TEST_ASSERT_TRUE(listener_accept_allowed_ip(&none, 0));
    }
}

// Distinct IPv6 peers keep independent budgets (the key is the full 128-bit address,
// so a v6 attacker cannot collapse many addresses onto one bucket).
void test_per_ip_throttle_v6_distinct()
{
    listener_per_ip_throttle_reset();
    pc_ip a;
    a.family = PC_IP_NONE;
    pc_ip b;
    b.family = PC_IP_NONE;
    TEST_ASSERT_TRUE(pc_ip_parse("2001:db8::1", &a));
    TEST_ASSERT_TRUE(pc_ip_parse("2001:db8::2", &b));
    for (int i = 0; i < PC_PER_IP_THROTTLE_MAX; i++)
    {
        TEST_ASSERT_TRUE(listener_accept_allowed_ip(&a, 0));
    }
    TEST_ASSERT_FALSE(listener_accept_allowed_ip(&a, 0)); // a exhausted
    TEST_ASSERT_TRUE(listener_accept_allowed_ip(&b, 0));  // b has its own budget
}

// The per-IP window math survives a millis() rollover near 2^32.
void test_per_ip_throttle_handles_rollover()
{
    listener_per_ip_throttle_reset();
    pc_ip ip = v4w(0x0A000009u);
    uint32_t near_max = 0xFFFFFFFFu - 5;
    TEST_ASSERT_TRUE(listener_accept_allowed_ip(&ip, near_max));
    TEST_ASSERT_TRUE(listener_accept_allowed_ip(&ip, near_max + PC_PER_IP_THROTTLE_WINDOW_MS));
}

// ====================================================================
// Source-IP allowlist (accept-time firewall)
// ====================================================================

// An empty allowlist allows every address (enabling the feature without rules is
// a no-op and cannot lock the device out).
void test_ip_allowlist_empty_allows_all()
{
    listener_ip_allowlist_reset();
    pc_ip a = v4w(0xC0A8010Au), b = v4w(0x08080808u); // 192.168.1.10, 8.8.8.8
    pc_ip none;
    none.family = PC_IP_NONE;
    TEST_ASSERT_TRUE(listener_ip_allowed(&a));
    TEST_ASSERT_TRUE(listener_ip_allowed(&b));
    TEST_ASSERT_TRUE(listener_ip_allowed(&none));
}

// A /32 rule admits exactly one host and rejects all others.
void test_ip_allowlist_host_match()
{
    listener_ip_allowlist_reset();
    pc_ip net = v4w(0xC0A8010Au); // 192.168.1.10
    TEST_ASSERT_TRUE(listener_ip_allow_add(&net, 32));
    pc_ip host = v4w(0xC0A8010Au), near = v4w(0xC0A8010Bu), far = v4w(0x0A000001u);
    TEST_ASSERT_TRUE(listener_ip_allowed(&host));
    TEST_ASSERT_FALSE(listener_ip_allowed(&near));
    TEST_ASSERT_FALSE(listener_ip_allowed(&far));
}

// A /24 rule admits the whole subnet and rejects addresses outside it.
void test_ip_allowlist_cidr_match()
{
    listener_ip_allowlist_reset();
    pc_ip net = v4w(0xC0A80100u); // 192.168.1.0
    TEST_ASSERT_TRUE(listener_ip_allow_add(&net, 24));
    pc_ip lo = v4w(0xC0A80101u), hi = v4w(0xC0A801FEu), out = v4w(0xC0A80201u);
    TEST_ASSERT_TRUE(listener_ip_allowed(&lo));
    TEST_ASSERT_TRUE(listener_ip_allowed(&hi));
    TEST_ASSERT_FALSE(listener_ip_allowed(&out));
}

// Host bits below the prefix are masked at compare time, so a network argument with
// stray host bits still matches the whole subnet.
void test_ip_allowlist_masks_host_bits()
{
    listener_ip_allowlist_reset();
    pc_ip net = v4w(0xC0A80137u); // 192.168.1.55 as a /24
    TEST_ASSERT_TRUE(listener_ip_allow_add(&net, 24));
    pc_ip lo = v4w(0xC0A80101u), hi = v4w(0xC0A801C8u);
    TEST_ASSERT_TRUE(listener_ip_allowed(&lo));
    TEST_ASSERT_TRUE(listener_ip_allowed(&hi));
}

// Multiple rules are OR-ed: an address matching any rule is allowed.
void test_ip_allowlist_multiple_rules()
{
    listener_ip_allowlist_reset();
    pc_ip r1 = v4w(0x0A000000u), r2 = v4w(0xC0A80000u); // 10.0.0.0/8, 192.168.0.0/16
    TEST_ASSERT_TRUE(listener_ip_allow_add(&r1, 8));
    TEST_ASSERT_TRUE(listener_ip_allow_add(&r2, 16));
    pc_ip a = v4w(0x0A010203u), b = v4w(0xC0A80505u), out = v4w(0xAC100001u);
    TEST_ASSERT_TRUE(listener_ip_allowed(&a));
    TEST_ASSERT_TRUE(listener_ip_allowed(&b));
    TEST_ASSERT_FALSE(listener_ip_allowed(&out));
}

// A /0 rule matches every address of its family.
void test_ip_allowlist_zero_prefix_matches_all()
{
    listener_ip_allowlist_reset();
    pc_ip z = v4w(0u);
    TEST_ASSERT_TRUE(listener_ip_allow_add(&z, 0));
    pc_ip a = v4w(0x01020304u), b = v4w(0xFFFFFFFFu);
    TEST_ASSERT_TRUE(listener_ip_allowed(&a));
    TEST_ASSERT_TRUE(listener_ip_allowed(&b));
}

// An IPv6 CIDR rule admits its v6 subnet and never a v4 peer (families are isolated).
void test_ip_allowlist_v6_cidr()
{
    listener_ip_allowlist_reset();
    TEST_ASSERT_TRUE(listener_ip_allow_add_cidr("2001:db8::/32"));
    pc_ip in;
    in.family = PC_IP_NONE;
    pc_ip out;
    out.family = PC_IP_NONE;
    TEST_ASSERT_TRUE(pc_ip_parse("2001:db8:0:0:1234::abcd", &in));
    TEST_ASSERT_TRUE(pc_ip_parse("2001:db9::1", &out));
    TEST_ASSERT_TRUE(listener_ip_allowed(&in));
    TEST_ASSERT_FALSE(listener_ip_allowed(&out));
    pc_ip v4peer = v4w(0xC0A80101u);
    TEST_ASSERT_FALSE(listener_ip_allowed(&v4peer)); // a v4 peer never matches a v6 rule
}

// A prefix length above the family width is rejected.
void test_ip_allowlist_rejects_bad_prefix()
{
    listener_ip_allowlist_reset();
    pc_ip net = v4w(0xC0A80100u);
    TEST_ASSERT_FALSE(listener_ip_allow_add(&net, 33));
}

// The rule table is bounded: it fills to capacity then refuses more rules.
void test_ip_allowlist_table_full()
{
    listener_ip_allowlist_reset();
    for (int i = 0; i < PC_IP_ALLOWLIST_SLOTS; i++)
    {
        pc_ip r = v4w(0x0A000000u + (uint32_t)i);
        TEST_ASSERT_TRUE(listener_ip_allow_add(&r, 32));
    }
    pc_ip overflow = v4w(0x0A010000u);
    TEST_ASSERT_FALSE(listener_ip_allow_add(&overflow, 32));
}

// With the bucket table full, scanning for a slot must walk every bucket, tracking both
// "the first expired one seen" and "the single most-idle one" (the LRU eviction fallback)
// independently. Reversed stagger (index 0 = latest start / least elapsed, each later
// index earlier / more elapsed) forces the LRU comparison to reassign repeatedly while
// scanning forward, and by query time every bucket's own window has also elapsed.
void test_per_ip_throttle_scans_expired_and_lru_across_a_full_table()
{
    listener_per_ip_throttle_reset();
    for (int i = 0; i < PC_PER_IP_THROTTLE_SLOTS; i++)
    {
        pc_ip ip = v4w(0x0A000000u + (uint32_t)(i + 1));
        uint32_t start = (uint32_t)(PC_PER_IP_THROTTLE_SLOTS - 1 - i) * 100;
        TEST_ASSERT_TRUE(listener_accept_allowed_ip(&ip, start));
    }
    uint32_t now = PC_PER_IP_THROTTLE_WINDOW_MS + (uint32_t)PC_PER_IP_THROTTLE_SLOTS * 100;
    pc_ip fresh = v4w(0xAC100001u);
    TEST_ASSERT_TRUE(listener_accept_allowed_ip(&fresh, now)); // table full, every bucket expired -> reuses one
}

// listener_ip_allow_add() / listener_ip_allow_add_cidr() reject a null argument instead of
// dereferencing it, and a non-null network of an unrecognized family (bits resolves to -1).
void test_ip_allowlist_rejects_null_args()
{
    listener_ip_allowlist_reset();
    TEST_ASSERT_FALSE(listener_ip_allow_add(NULL, 24));
    TEST_ASSERT_FALSE(listener_ip_allow_add_cidr(NULL));

    pc_ip none;
    none.family = PC_IP_NONE;
    TEST_ASSERT_FALSE(listener_ip_allow_add(&none, 24));
}

// A CIDR string whose address portion (before '/') is too long to fit the parser's bounded
// scratch buffer is rejected rather than silently truncated.
void test_ip_allowlist_rejects_overlong_address_text()
{
    listener_ip_allowlist_reset();
    char too_long[64];
    for (int i = 0; i < 60; i++)
    {
        too_long[i] = '1'; // no '/' at all -> parser keeps scanning past PC_IP_STR_MAX
    }
    too_long[60] = '\0';
    TEST_ASSERT_FALSE(listener_ip_allow_add_cidr(too_long));
}

// A non-digit character in the prefix-length text is rejected (hand-rolled decimal parse,
// no stdlib) - both above '9' and below '0'.
void test_ip_allowlist_rejects_non_digit_prefix()
{
    listener_ip_allowlist_reset();
    TEST_ASSERT_FALSE(listener_ip_allow_add_cidr("10.0.0.0/2x"));
    TEST_ASSERT_FALSE(listener_ip_allow_add_cidr("10.0.0.0/-1"));
}

// listener_enqueue() rejects an out-of-range listener id before touching listener_pool[],
// and reports a full queue (the application not draining server_tick(0) fast enough)
// instead of silently dropping the event without telling the caller.
void test_enqueue_rejects_out_of_range_listener_id()
{
    TcpEvt evt = {EVT_DATA, 0, 0};
    TEST_ASSERT_FALSE(listener_enqueue((uint8_t)MAX_LISTENERS, &evt));

    mock_queue_send_fail_once() = true;
    TEST_ASSERT_FALSE(listener_enqueue(0, &evt)); // listener 0 is active (setUp's listener_add)

    listener_pool[0].active = true; // active but no queue - an inconsistent state a real
    listener_pool[0].queue = NULL;  // listener_add() never leaves, but guarded independently
    TEST_ASSERT_FALSE(listener_enqueue(0, &evt));
}

// listener_add_dynamic() / listener_stop_dynamic(): the SSH-remote-forward-owned dynamic
// listener lifecycle - bounds-checked, idempotent stop, and (on the native host) no real
// lwIP pcb to create or close.
void test_dynamic_listener_lifecycle()
{
    TEST_ASSERT_EQUAL_INT32(-1, listener_add_dynamic((uint8_t)MAX_LISTENERS, 2222, PROTO_HTTP));

    mock_queue_create_fail_once() = true;
    TEST_ASSERT_EQUAL_INT32(-1, listener_add_dynamic(1, 2222, PROTO_HTTP));

    TEST_ASSERT_EQUAL_INT32(1, listener_add_dynamic(1, 2222, PROTO_HTTP));
    TEST_ASSERT_TRUE(listener_pool[1].active);
    TEST_ASSERT_FALSE(listener_pool[1].tls); // forwarded ports are always plaintext
    TEST_ASSERT_NOT_NULL(listener_pool[1].queue);
    TEST_ASSERT_NULL(listener_pool[1].listen_pcb); // host build: no real lwIP pcb

    // Re-adding on the same slot cleans up the prior instance first (idempotent create).
    TEST_ASSERT_EQUAL_INT32(1, listener_add_dynamic(1, 3333, PROTO_HTTP));
    TEST_ASSERT_EQUAL_UINT16(3333, listener_pool[1].port);

    listener_stop_dynamic((uint8_t)MAX_LISTENERS); // out of range: no-op, no crash
    listener_stop_dynamic(1);
    TEST_ASSERT_FALSE(listener_pool[1].active);
    TEST_ASSERT_NULL(listener_pool[1].queue);

    listener_stop_dynamic(1); // already stopped: idempotent no-op
    TEST_ASSERT_FALSE(listener_pool[1].active);
}

// The live-slot bitmask + ctz allocator (pc_conn_set_state / pc_conn_alloc_free): claim, free, pool-full,
// lowest-first, and that CONN_CLOSING keeps a slot reserved. setUp() ran pool_init() -> all slots free.
void test_freeslot_bitmask_alloc()
{
    TEST_ASSERT_EQUAL_INT32(0, pc_conn_alloc_free()); // first free is slot 0

    pc_conn_set_state(0, CONN_ACTIVE); // claim 0
    TEST_ASSERT_EQUAL_INT32(1, pc_conn_alloc_free());

    for (uint8_t i = 1; i < MAX_CONNS; i++) // claim the rest -> full
    {
        pc_conn_set_state(i, CONN_ACTIVE);
    }
    TEST_ASSERT_EQUAL_INT32(-1, pc_conn_alloc_free());

    pc_conn_set_state(3, CONN_FREE); // free 3 -> allocator hands out 3
    TEST_ASSERT_EQUAL_INT32(3, pc_conn_alloc_free());

    pc_conn_set_state(1, CONN_FREE); // free 1 too -> lowest free is now 1
    TEST_ASSERT_EQUAL_INT32(1, pc_conn_alloc_free());

    pc_conn_set_state(1, CONN_ACTIVE);                // 1 active again
    pc_conn_set_state(1, CONN_CLOSING);               // CLOSING is not free
    TEST_ASSERT_EQUAL_INT32(3, pc_conn_alloc_free()); // 3 free, 1 reserved (CLOSING)
}

// ====================================================================
// Direct-call coverage for tcp.cpp low-level paths not reached through
// the tests above (bounds guards, host-build branches, and the raw lwIP
// callbacks driven directly with a fabricated struct tcp_pcb / pbuf).
// ====================================================================

// Out-of-range slot guards are pure defensive bounds checks - direct-call them to
// prove they return without touching pool state.
void test_bounds_guards_reject_out_of_range_slots()
{
    int32_t before = pc_conn_alloc_free();
    pc_conn_set_state((uint8_t)CONN_POOL_SLOTS, CONN_ACTIVE); // no-op: out of range
    TEST_ASSERT_EQUAL_INT32(before, pc_conn_alloc_free());

    pc_conn_ack_consumed((uint8_t)(MAX_CONNS + 50)); // no-op, must not crash
    pc_conn_close((uint8_t)(MAX_CONNS + 50));
    pc_conn_abort_slot((uint8_t)(MAX_CONNS + 50));
    pc_conn_touch_active((uint8_t)(MAX_CONNS + 50));

    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].last_activity_ms = 0;
    pc_conn_begin_close((uint8_t)(MAX_CONNS + 50)); // no-op: state must stay ACTIVE
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

// pc_conn_sndbuf / pc_conn_close / pc_conn_abort_slot all no-op safely when the
// slot's pcb is already null (never attached, or already torn down); pc_conn_sndbuf
// with a live pcb reports the mock's advertised send window instead.
void test_null_pcb_slots_are_safe_no_ops()
{
    conn_pool[0].pcb = NULL;
    TEST_ASSERT_EQUAL_UINT16(0, pc_conn_sndbuf(0));
    pc_conn_close(0);      // no pcb -> returns before touching state
    pc_conn_abort_slot(0); // same

    struct tcp_pcb fake = {};
    conn_pool[1].pcb = &fake;
    TEST_ASSERT_EQUAL_UINT16(MOCK_SNDBUF_DEFAULT, pc_conn_sndbuf(1));
}

// pc_conn_ack_consumed(): out-of-range slot and an inactive slot are no-ops; an ACTIVE
// slot with unacked bytes advances rx_acked and issues a real tcp_recved() (marshaled
// via PC_OP_RECVED off-host, called directly here) for exactly the consumed count.
void test_ack_consumed_bounds_inactive_and_real_advance()
{
    pc_conn_ack_consumed((uint8_t)(MAX_CONNS + 1)); // out of range: no-op

    conn_pool[0].state = CONN_FREE; // not ACTIVE: no-op
    conn_pool[0].rx_tail = 5;
    conn_pool[0].rx_acked = 0;
    pc_conn_ack_consumed(0);
    TEST_ASSERT_EQUAL(0u, (size_t)conn_pool[0].rx_acked); // untouched

    struct tcp_pcb fake = {};
    conn_pool[0].pcb = &fake;
    pc_conn_set_state(0, CONN_ACTIVE);
    conn_pool[0].rx_tail = 5;
    conn_pool[0].rx_acked = 2; // 3 bytes consumed since the last ack
    pc_conn_ack_consumed(0);
    TEST_ASSERT_EQUAL(5u, (size_t)conn_pool[0].rx_acked);

    pc_conn_ack_consumed(0); // nothing new consumed since the last call: no-op
    TEST_ASSERT_EQUAL(5u, (size_t)conn_pool[0].rx_acked);
}

// pc_conn_send_flush's host path: a normal write flushes and reports success; a
// failed write reports failure instead of silently "succeeding" and flushing anyway.
void test_send_flush_success_and_write_failure()
{
    struct tcp_pcb fake = {};
    conn_pool[0].pcb = &fake;
    TEST_ASSERT_TRUE(pc_conn_send_flush(0, "x", 1));

    mock_send_fail_after() = 0; // next tcp_write call fails
    TEST_ASSERT_FALSE(pc_conn_send_flush(0, "x", 1));
    mock_send_fail_after() = -1; // restore: never fail
}

// pc_conn_raw_send: null pcb rejected, a normal write succeeds, and a failed write
// is reported (no tcp_output on failure).
void test_raw_send_null_success_and_failure()
{
    TEST_ASSERT_FALSE(pc_conn_raw_send(NULL, "x", 1));

    struct tcp_pcb fake = {};
    TEST_ASSERT_TRUE(pc_conn_raw_send(&fake, "hello", 5));

    mock_send_fail_after() = 0;
    TEST_ASSERT_FALSE(pc_conn_raw_send(&fake, "x", 1));
    mock_send_fail_after() = -1;
}

// pc_conn_close's host tcp_close-fails fallback: tcp_abort is called (proven via the
// mock's call counter, since tcp_abort itself has no other observable effect).
void test_close_falls_back_to_abort_on_tcp_close_failure()
{
    struct tcp_pcb fake = {};
    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    pc_conn_set_state(0, CONN_ACTIVE);

    int before = mock_abort_call_count();
    mock_close_fail_once() = true;
    pc_conn_close(0);
    TEST_ASSERT_EQUAL_INT(before + 1, mock_abort_call_count());
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);

    // The ordinary (tcp_close succeeds) path does NOT call tcp_abort.
    conn_pool[0].pcb = &fake;
    pc_conn_set_state(0, CONN_ACTIVE);
    before = mock_abort_call_count();
    pc_conn_close(0);
    TEST_ASSERT_EQUAL_INT(before, mock_abort_call_count());
}

// pc_conn_begin_close's host path (closing_check -> closing_finalize, reached only
// through this public entry point): a slot with no pcb finalizes immediately without
// touching lwIP, and the tcp_close-fails fallback aborts just like pc_conn_close's.
void test_begin_close_finalizes_immediately_with_and_without_a_pcb()
{
    // No pcb: closing_finalize's `if (pcb)` false branch - no tcp_arg/tcp_close/tcp_abort at all.
    conn_pool[1].id = 1;
    conn_pool[1].pcb = NULL;
    pc_conn_set_state(1, CONN_ACTIVE);
    pc_conn_begin_close(1);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state);

    // With a pcb whose send queue already drained (snd_queuelen==0, the default): finalizes
    // immediately via the ordinary tcp_close path.
    struct tcp_pcb fake = {};
    conn_pool[2].id = 2;
    conn_pool[2].pcb = &fake;
    pc_conn_set_state(2, CONN_ACTIVE);
    int before = mock_abort_call_count();
    pc_conn_begin_close(2);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[2].state);
    TEST_ASSERT_EQUAL_INT(before, mock_abort_call_count()); // ordinary close, no abort

    // Same, but tcp_close is forced to fail: closing_finalize falls back to tcp_abort.
    struct tcp_pcb fake2 = {};
    conn_pool[3].id = 3;
    conn_pool[3].pcb = &fake2;
    pc_conn_set_state(3, CONN_ACTIVE);
    mock_close_fail_once() = true;
    before = mock_abort_call_count();
    pc_conn_begin_close(3);
    TEST_ASSERT_EQUAL_INT(before + 1, mock_abort_call_count());
}

// pc_conn_remote_ip / pc_conn_remote_addr: ESP32-only accessors report the host-build
// "no address" default without touching any lwIP state.
void test_remote_addr_accessors_host_stub()
{
    TEST_ASSERT_EQUAL_UINT32(0, pc_conn_remote_ip(0));

    pc_ip out;
    TEST_ASSERT_FALSE(pc_conn_remote_addr(0, &out));
    TEST_ASSERT_EQUAL_INT((int)PC_IP_NONE, (int)out.family);
    TEST_ASSERT_FALSE(pc_conn_remote_addr(0, NULL)); // null out is tolerated, not dereferenced
}

// DeterministicAsyncTCP::stop(): aborts every ACTIVE/CLOSING slot that still owns a pcb,
// leaves a pcb-less slot alone (nothing to abort), and frees an already-free slot as a no-op.
void test_stop_aborts_live_slots_and_skips_the_rest()
{
    struct tcp_pcb fake_active = {};
    struct tcp_pcb fake_closing = {};

    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake_active;
    pc_conn_set_state(0, CONN_ACTIVE); // aborted

    conn_pool[1].id = 1;
    conn_pool[1].pcb = &fake_closing;
    pc_conn_set_state(1, CONN_CLOSING); // aborted

    conn_pool[2].id = 2;
    conn_pool[2].pcb = NULL;
    pc_conn_set_state(2, CONN_ACTIVE); // ACTIVE but no pcb - skipped, only freed

    conn_pool[3].id = 3;
    conn_pool[3].pcb = NULL;
    pc_conn_set_state(3, CONN_FREE); // already free

    int before = mock_abort_call_count();
    DeterministicAsyncTCP::stop();
    TEST_ASSERT_EQUAL_INT(before + 2, mock_abort_call_count()); // exactly the two pcb-owning slots

    for (int i = 0; i < 4; i++)
    {
        TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[i].state);
        TEST_ASSERT_NULL(conn_pool[i].pcb);
    }
}

// check_timeouts()'s CLOSING branch: a slot dwelling in CLOSING survives until
// PC_CLOSING_TIMEOUT_MS, then is force-freed (with, and without, a live pcb).
void test_check_timeouts_reaps_stale_closing_slots()
{
    struct tcp_pcb fake = {};

    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    pc_conn_set_state(0, CONN_CLOSING);
    conn_pool[0].last_activity_ms = 0;

    conn_pool[1].id = 1;
    conn_pool[1].pcb = NULL;
    pc_conn_set_state(1, CONN_CLOSING);
    conn_pool[1].last_activity_ms = 0;

    set_millis(PC_CLOSING_TIMEOUT_MS - 1); // not yet stale: both must survive
    DeterministicAsyncTCP::check_timeouts();
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[1].state);

    int before = mock_abort_call_count();
    set_millis(PC_CLOSING_TIMEOUT_MS); // now stale: force-freed
    DeterministicAsyncTCP::check_timeouts();
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state);
    TEST_ASSERT_EQUAL_INT(before + 1, mock_abort_call_count()); // only slot 0 had a pcb to abort
}

// check_timeouts()'s ACTIVE-slot reap path detaches and aborts a REAL pcb (not just the
// pcb==NULL case the constants/stress tests above already cover).
void test_check_timeouts_detaches_and_aborts_a_real_pcb()
{
    struct tcp_pcb fake = {};
    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    pc_conn_set_state(0, CONN_ACTIVE);
    conn_pool[0].last_activity_ms = 0;

    int before = mock_abort_call_count();
    set_millis(CONN_TIMEOUT_MS);
    DeterministicAsyncTCP::check_timeouts();
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
    TEST_ASSERT_EQUAL_INT(before + 1, mock_abort_call_count());
}

// pc_conn_touch_active(): the out-of-range guard and the "not ACTIVE" no-op (only an
// ACTIVE slot's timestamp is refreshed).
void test_touch_active_bounds_and_state_guard()
{
    pc_conn_touch_active((uint8_t)(MAX_CONNS + 1)); // no-op, must not crash

    conn_pool[0].state = CONN_FREE;
    conn_pool[0].last_activity_ms = 111;
    set_millis(999);
    pc_conn_touch_active(0);
    TEST_ASSERT_EQUAL_UINT32(111, conn_pool[0].last_activity_ms); // untouched: not ACTIVE
}

// ---- lwIP callback direct-call coverage --------------------------------

// lowlevel_recv_cb: null arg is rejected, and a CLOSING slot drains (and ACKs) both a
// real segment and a null (FIN) pbuf without processing it.
void test_recv_cb_null_arg_and_closing_drain()
{
    struct tcp_pcb fake = {};
    TEST_ASSERT_EQUAL_INT(ERR_VAL, lowlevel_recv_cb(NULL, &fake, NULL, ERR_OK));

    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    pc_conn_set_state(0, CONN_CLOSING);

    struct pbuf seg = {};
    uint8_t payload[4] = {1, 2, 3, 4};
    seg.payload = payload;
    seg.len = 4;
    seg.tot_len = 4;
    seg.next = NULL;
    TEST_ASSERT_EQUAL_INT(ERR_OK, lowlevel_recv_cb(&conn_pool[0], &fake, &seg, ERR_OK));
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state); // still dwelling

    TEST_ASSERT_EQUAL_INT(ERR_OK, lowlevel_recv_cb(&conn_pool[0], &fake, NULL, ERR_OK)); // FIN while closing
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);
}

// A null pbuf on an ACTIVE slot is a graceful remote FIN: the slot is freed and an
// EVT_DISCONNECT posted. If tcp_close() itself fails, the same tcp_abort()
// fallback as pc_conn_close()/closing_finalize() applies here too.
void test_recv_cb_fin_close_falls_back_to_abort_on_tcp_close_failure()
{
    struct tcp_pcb fake = {};
    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    conn_pool[0].listener_id = 0;
    pc_conn_set_state(0, CONN_ACTIVE);

    mock_close_fail_once() = true;
    int before = mock_abort_call_count();
    TEST_ASSERT_EQUAL_INT(ERR_OK, lowlevel_recv_cb(&conn_pool[0], &fake, NULL, ERR_OK));
    TEST_ASSERT_EQUAL_INT(before + 1, mock_abort_call_count());
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
}

// The ordinary (tcp_close succeeds) side of the same FIN-close path: no abort.
void test_recv_cb_fin_close_ordinary_path_does_not_abort()
{
    struct tcp_pcb fake = {};
    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    conn_pool[0].listener_id = 0;
    pc_conn_set_state(0, CONN_ACTIVE);

    int before = mock_abort_call_count();
    TEST_ASSERT_EQUAL_INT(ERR_OK, lowlevel_recv_cb(&conn_pool[0], &fake, NULL, ERR_OK));
    TEST_ASSERT_EQUAL_INT(before, mock_abort_call_count());
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

// A non-ACTIVE, non-CLOSING slot (e.g. already FREE, a stale callback for a torn-down
// connection) rejects incoming data instead of touching a slot no one owns.
void test_recv_cb_rejects_non_active_slot()
{
    struct tcp_pcb fake = {};
    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    pc_conn_set_state(0, CONN_FREE);
    TEST_ASSERT_EQUAL_INT(ERR_VAL, lowlevel_recv_cb(&conn_pool[0], &fake, NULL, ERR_OK));
}

// A segment larger than the ring's free space is refused (ERR_MEM, lwIP keeps and
// redelivers it) rather than partially copied - lossless backpressure.
void test_recv_cb_refuses_a_segment_that_does_not_fit()
{
    struct tcp_pcb fake = {};
    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    pc_conn_set_state(0, CONN_ACTIVE);
    conn_pool[0].rx_head = RX_BUF_SIZE - 2; // free space == 1 byte
    conn_pool[0].rx_tail = 0;
    conn_pool[0].last_activity_ms = 5;

    struct pbuf seg = {};
    uint8_t payload[10] = {0};
    seg.payload = payload;
    seg.len = 10;
    seg.tot_len = 10; // > the 1 free byte
    seg.next = NULL;

    TEST_ASSERT_EQUAL_INT(ERR_MEM, lowlevel_recv_cb(&conn_pool[0], &fake, &seg, ERR_OK));
    TEST_ASSERT_EQUAL_UINT32(5, conn_pool[0].last_activity_ms); // NOT refreshed on refusal (see tcp.cpp comment)
}

// The full accept path: a fitting segment (across two chained pbufs, to exercise the
// chain walk) is copied into the ring, the idle timer and slow-loris deadline are
// armed on the first byte only, and an EVT_DATA event is posted.
void test_recv_cb_accepts_and_copies_a_two_pbuf_segment()
{
    struct tcp_pcb fake = {};
    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    conn_pool[0].listener_id = 0; // listener 0 is armed by setUp()'s listener_add(0, 80, ...)
    pc_conn_set_state(0, CONN_ACTIVE);
    conn_pool[0].rx_head = 0;
    conn_pool[0].rx_tail = 0;
    conn_pool[0].req_start_ms = 0;
    conn_pool[0].last_activity_ms = 0;
    set_millis(4242);

    uint8_t part1[3] = {'a', 'b', 'c'};
    uint8_t part2[2] = {'d', 'e'};
    struct pbuf seg2 = {};
    seg2.payload = part2;
    seg2.len = 2;
    seg2.tot_len = 2;
    seg2.next = NULL;
    struct pbuf seg1 = {};
    seg1.payload = part1;
    seg1.len = 3;
    seg1.tot_len = 5; // whole-chain total, as lwIP sets it on the head pbuf
    seg1.next = &seg2;

    TEST_ASSERT_EQUAL_INT(ERR_OK, lowlevel_recv_cb(&conn_pool[0], &fake, &seg1, ERR_OK));
    TEST_ASSERT_EQUAL_UINT32(4242, conn_pool[0].last_activity_ms);
    TEST_ASSERT_EQUAL_UINT32(4242, conn_pool[0].req_start_ms); // first byte of a new request arms the deadline
    TEST_ASSERT_EQUAL(5u, (size_t)conn_pool[0].rx_head);       // 5 bytes copied

    uint8_t got[5];
    for (int i = 0; i < 5; i++)
    {
        got[i] = conn_pool[0].rx_buffer[i];
    }
    TEST_ASSERT_EQUAL_INT(0, memcmp("abcde", got, 5));

    // A second segment must NOT re-arm req_start_ms (only the first byte of a request does).
    uint8_t part3[1] = {'f'};
    struct pbuf seg3 = {};
    seg3.payload = part3;
    seg3.len = 1;
    seg3.tot_len = 1;
    seg3.next = NULL;
    set_millis(5000);
    TEST_ASSERT_EQUAL_INT(ERR_OK, lowlevel_recv_cb(&conn_pool[0], &fake, &seg3, ERR_OK));
    TEST_ASSERT_EQUAL_UINT32(4242, conn_pool[0].req_start_ms); // unchanged
}

// Edge cases in the accept-and-copy path: pc_millis()==0 at the very first byte (the
// req_start_ms ternary's "rx_now falsy" side - armed to 1, not left at the 0 "unarmed"
// sentinel), and a zero-length segment (fits trivially, but posts no EVT_DATA -
// there is nothing to drain).
void test_recv_cb_zero_clock_and_zero_length_segment_edge_cases()
{
    struct tcp_pcb fake = {};
    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    conn_pool[0].listener_id = 0;
    pc_conn_set_state(0, CONN_ACTIVE);
    conn_pool[0].rx_head = 0;
    conn_pool[0].rx_tail = 0;
    conn_pool[0].req_start_ms = 0;
    set_millis(0); // pc_millis() reports 0 at the very first byte

    uint8_t byte = 'z';
    struct pbuf seg = {};
    seg.payload = &byte;
    seg.len = 1;
    seg.tot_len = 1;
    seg.next = NULL;
    TEST_ASSERT_EQUAL_INT(ERR_OK, lowlevel_recv_cb(&conn_pool[0], &fake, &seg, ERR_OK));
    TEST_ASSERT_EQUAL_UINT32(1, conn_pool[0].req_start_ms); // rx_now==0 -> armed to 1, not left "unarmed"

    struct pbuf empty_seg = {};
    empty_seg.payload = NULL;
    empty_seg.len = 0;
    empty_seg.tot_len = 0;
    empty_seg.next = NULL;
    TEST_ASSERT_EQUAL_INT(ERR_OK, lowlevel_recv_cb(&conn_pool[0], &fake, &empty_seg, ERR_OK));
    TEST_ASSERT_EQUAL(1u, (size_t)conn_pool[0].rx_head); // unchanged: nothing to copy
}

// lowlevel_sent_cb: a null arg is a no-op, an ACTIVE slot just refreshes its timestamp,
// and a CLOSING slot finalizes through closing_check once its send queue has drained.
void test_sent_cb_null_active_and_closing()
{
    TEST_ASSERT_EQUAL_INT(ERR_OK, lowlevel_sent_cb(NULL, NULL, 0)); // no-op, no crash

    struct tcp_pcb fake = {};
    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    pc_conn_set_state(0, CONN_ACTIVE);
    conn_pool[0].last_activity_ms = 0;
    set_millis(777);
    TEST_ASSERT_EQUAL_INT(ERR_OK, lowlevel_sent_cb(&conn_pool[0], &fake, 10));
    TEST_ASSERT_EQUAL_UINT32(777, conn_pool[0].last_activity_ms);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state); // untouched

    conn_pool[1].id = 1;
    conn_pool[1].pcb = &fake;
    pc_conn_set_state(1, CONN_CLOSING);
    TEST_ASSERT_EQUAL_INT(ERR_OK, lowlevel_sent_cb(&conn_pool[1], &fake, 0));
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state); // finalized (drained: snd_queuelen==0)
}

// lowlevel_err_cb: a null arg is a no-op, an ACTIVE slot frees + posts EVT_ERROR, and a slot
// that was already dwelling in CLOSING just releases the slot (no re-posted close event).
void test_err_cb_null_active_and_closing()
{
    lowlevel_err_cb(NULL, ERR_ABRT); // no-op, no crash

    struct tcp_pcb fake = {};
    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    pc_conn_set_state(0, CONN_ACTIVE);
    lowlevel_err_cb(&conn_pool[0], ERR_ABRT);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);

    conn_pool[1].id = 1;
    conn_pool[1].pcb = &fake;
    pc_conn_set_state(1, CONN_CLOSING);
    lowlevel_err_cb(&conn_pool[1], ERR_ABRT);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state);
    TEST_ASSERT_NULL(conn_pool[1].pcb);
}

// ====================================================================
// listener_accept_cb direct-call coverage (listener.cpp).
//
// On native there is no real lwIP accept event: tcp_accept()'s mock
// (test/mocks/lwip/tcp.h) does not store or invoke the registered callback at
// all, so listener_accept_cb is called directly here with a fabricated newpcb,
// exactly like tcp.cpp's lowlevel_*_cb tests above.
// ====================================================================

// A non-ERR_OK accept error, or a null newpcb, is rejected without touching the pool.
void test_accept_cb_rejects_error_and_null_pcb()
{
    struct tcp_pcb fake = {};
    int32_t before = pc_conn_alloc_free();

    TEST_ASSERT_EQUAL_INT(ERR_VAL, listener_accept_cb((void *)(uintptr_t)0, &fake, ERR_ABRT));
    TEST_ASSERT_EQUAL_INT32(before, pc_conn_alloc_free()); // no slot claimed

    TEST_ASSERT_EQUAL_INT(ERR_VAL, listener_accept_cb((void *)(uintptr_t)0, NULL, ERR_OK));
    TEST_ASSERT_EQUAL_INT32(before, pc_conn_alloc_free());
}

// An out-of-range listener index (the PCB user-data arg) is rejected before any pool work.
void test_accept_cb_rejects_out_of_range_listener_idx()
{
    struct tcp_pcb fake = {};
    int before_aborts = mock_abort_call_count();
    TEST_ASSERT_EQUAL_INT(ERR_VAL, listener_accept_cb((void *)(uintptr_t)MAX_LISTENERS, &fake, ERR_OK));
    TEST_ASSERT_EQUAL_INT(before_aborts, mock_abort_call_count()); // rejected before the pool-full abort path
}

// A full connection pool aborts the new PCB and reports ERR_ABRT (lwIP: "already gone").
void test_accept_cb_rejects_when_pool_full()
{
    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        pc_conn_set_state(i, CONN_ACTIVE);
    }
    TEST_ASSERT_EQUAL_INT32(-1, pc_conn_alloc_free());

    struct tcp_pcb fake = {};
    int before_aborts = mock_abort_call_count();
    TEST_ASSERT_EQUAL_INT(ERR_ABRT, listener_accept_cb((void *)(uintptr_t)0, &fake, ERR_OK));
    TEST_ASSERT_EQUAL_INT(before_aborts + 1, mock_abort_call_count());
}

// The full success path: claims the lowest free slot, wires it to the listener's own
// protocol, and leaves it in the state a fresh accept should - host-build iface/tls
// defaults, empty ring, no request-deadline armed yet.
void test_accept_cb_claims_slot_and_wires_connection()
{
    struct tcp_pcb fake = {};
    set_millis(9001);

    TEST_ASSERT_EQUAL_INT(ERR_OK, listener_accept_cb((void *)(uintptr_t)0, &fake, ERR_OK));

    TcpConn *c = &conn_pool[0]; // pool_init() in setUp() guarantees slot 0 is the lowest free
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)c->state);
    TEST_ASSERT_EQUAL_PTR(&fake, c->pcb);
    TEST_ASSERT_EQUAL_UINT32(9001, c->last_activity_ms);
    TEST_ASSERT_EQUAL_UINT32(0, c->req_start_ms); // armed on the first RX byte, not at accept
    TEST_ASSERT_EQUAL(0u, (size_t)c->rx_head);
    TEST_ASSERT_EQUAL(0u, (size_t)c->rx_tail);
    TEST_ASSERT_EQUAL_UINT8(0, c->listener_id);
    TEST_ASSERT_EQUAL_INT((int)PROTO_HTTP, (int)c->proto);   // from listener_pool[0] (setUp's listener_add)
    TEST_ASSERT_EQUAL_INT((int)PC_IFACE_ANY, (int)c->iface); // host build: no real pcb IP to classify
    TEST_ASSERT_EQUAL_UINT8(0, c->tls);                      // PC_ENABLE_TLS is off on native
}

// Two back-to-back accepts on the same listener claim two DIFFERENT slots (not a stale
// reuse of slot 0) and each is independently addressable afterward.
void test_accept_cb_second_accept_claims_a_different_slot()
{
    struct tcp_pcb fake1 = {}, fake2 = {};
    TEST_ASSERT_EQUAL_INT(ERR_OK, listener_accept_cb((void *)(uintptr_t)0, &fake1, ERR_OK));
    TEST_ASSERT_EQUAL_INT(ERR_OK, listener_accept_cb((void *)(uintptr_t)0, &fake2, ERR_OK));
    TEST_ASSERT_EQUAL_PTR(&fake1, conn_pool[0].pcb);
    TEST_ASSERT_EQUAL_PTR(&fake2, conn_pool[1].pcb);
}

// listener_enqueue() failing (here: the target listener slot marked inactive) must not
// fail the accept itself - the connection is still claimed and wired; only the
// EVT_CONNECT post is dropped (observed as a defer-drop notice, not an abort).
void test_accept_cb_survives_a_failed_enqueue()
{
    listener_pool[0].active = false; // makes listener_enqueue() report failure
    struct tcp_pcb fake = {};
    TEST_ASSERT_EQUAL_INT(ERR_OK, listener_accept_cb((void *)(uintptr_t)0, &fake, ERR_OK));
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state); // still claimed
    TEST_ASSERT_EQUAL_PTR(&fake, conn_pool[0].pcb);
}

int main()
{
    UNITY_BEGIN();

    // Unit tests
    RUN_TEST(test_pool_capacity_default_is_eight);
    RUN_TEST(test_rx_buffer_size_is_one_kb);
    RUN_TEST(test_timeout_constant_is_5000ms);
    RUN_TEST(test_all_slots_free_after_init);
    RUN_TEST(test_all_pcbs_null_after_init);
    RUN_TEST(test_all_ring_buffers_empty_after_init);
    RUN_TEST(test_slot_ids_match_indices);
    RUN_TEST(test_freeslot_bitmask_alloc);
    RUN_TEST(test_ring_empty_when_head_equals_tail);
    RUN_TEST(test_ring_wrap_at_boundary);
    RUN_TEST(test_ring_full_sentinel_one_slot_reserved);
    RUN_TEST(test_ring_can_store_size_minus_one_bytes);
    RUN_TEST(test_event_types_are_distinct);
    RUN_TEST(test_timeout_does_not_fire_on_free_slot);
    RUN_TEST(test_timeout_does_not_fire_before_deadline);
    RUN_TEST(test_timeout_fires_at_deadline);
    RUN_TEST(test_timeout_fires_only_on_stale_slots);
    RUN_TEST(test_active_send_not_reaped);
    RUN_TEST(test_pool_init_applies_custom_config);
    RUN_TEST(test_init_succeeds_on_native);
    RUN_TEST(test_listener_add_bounds_and_lwip_failure_paths);
    RUN_TEST(test_listener_stop_rejects_out_of_range_idx);
    RUN_TEST(test_listener_stop_and_stop_dynamic_tolerate_a_missing_queue);
    RUN_TEST(test_all_last_activity_ms_zero_after_init);
    RUN_TEST(test_queue_not_null_after_init);

    // Stress tests
    RUN_TEST(stress_ring_buffer_fill_drain_integrity);
    RUN_TEST(stress_ring_buffer_multi_cycle_no_corruption);
    RUN_TEST(stress_all_slots_timeout_simultaneously);
    RUN_TEST(stress_timeout_arm_recover_cycle);
    RUN_TEST(stress_check_timeouts_high_call_rate);
    RUN_TEST(stress_ring_buffer_byte_by_byte_fill_and_drain);

    // Accept-rate throttle
    RUN_TEST(test_accept_throttle_blocks_over_budget);
    RUN_TEST(test_accept_throttle_window_refills);
    RUN_TEST(test_accept_throttle_handles_rollover);

    // Per-IP accept-rate throttle
    RUN_TEST(test_per_ip_throttle_blocks_over_budget);
    RUN_TEST(test_per_ip_throttle_isolates_addresses);
    RUN_TEST(test_per_ip_throttle_window_refills);
    RUN_TEST(test_per_ip_throttle_evicts_when_full);
    RUN_TEST(test_per_ip_throttle_zero_ip_always_allowed);
    RUN_TEST(test_per_ip_throttle_v6_distinct);
    RUN_TEST(test_per_ip_throttle_handles_rollover);
    RUN_TEST(test_per_ip_throttle_scans_expired_and_lru_across_a_full_table);

    // Source-IP allowlist
    RUN_TEST(test_ip_allowlist_empty_allows_all);
    RUN_TEST(test_ip_allowlist_host_match);
    RUN_TEST(test_ip_allowlist_cidr_match);
    RUN_TEST(test_ip_allowlist_masks_host_bits);
    RUN_TEST(test_ip_allowlist_multiple_rules);
    RUN_TEST(test_ip_allowlist_zero_prefix_matches_all);
    RUN_TEST(test_ip_allowlist_v6_cidr);
    RUN_TEST(test_ip_allowlist_rejects_bad_prefix);
    RUN_TEST(test_ip_allowlist_table_full);
    RUN_TEST(test_ip_allowlist_rejects_null_args);
    RUN_TEST(test_ip_allowlist_rejects_overlong_address_text);
    RUN_TEST(test_ip_allowlist_rejects_non_digit_prefix);
    RUN_TEST(test_enqueue_rejects_out_of_range_listener_id);
    RUN_TEST(test_dynamic_listener_lifecycle);

    // Direct-call tcp.cpp gap coverage
    RUN_TEST(test_bounds_guards_reject_out_of_range_slots);
    RUN_TEST(test_null_pcb_slots_are_safe_no_ops);
    RUN_TEST(test_ack_consumed_bounds_inactive_and_real_advance);
    RUN_TEST(test_send_flush_success_and_write_failure);
    RUN_TEST(test_raw_send_null_success_and_failure);
    RUN_TEST(test_close_falls_back_to_abort_on_tcp_close_failure);
    RUN_TEST(test_begin_close_finalizes_immediately_with_and_without_a_pcb);
    RUN_TEST(test_remote_addr_accessors_host_stub);
    RUN_TEST(test_stop_aborts_live_slots_and_skips_the_rest);
    RUN_TEST(test_check_timeouts_reaps_stale_closing_slots);
    RUN_TEST(test_check_timeouts_detaches_and_aborts_a_real_pcb);
    RUN_TEST(test_touch_active_bounds_and_state_guard);
    RUN_TEST(test_recv_cb_null_arg_and_closing_drain);
    RUN_TEST(test_recv_cb_fin_close_falls_back_to_abort_on_tcp_close_failure);
    RUN_TEST(test_recv_cb_fin_close_ordinary_path_does_not_abort);
    RUN_TEST(test_recv_cb_rejects_non_active_slot);
    RUN_TEST(test_recv_cb_refuses_a_segment_that_does_not_fit);
    RUN_TEST(test_recv_cb_accepts_and_copies_a_two_pbuf_segment);
    RUN_TEST(test_recv_cb_zero_clock_and_zero_length_segment_edge_cases);
    RUN_TEST(test_sent_cb_null_active_and_closing);
    RUN_TEST(test_err_cb_null_active_and_closing);

    // listener_accept_cb direct-call coverage
    RUN_TEST(test_accept_cb_rejects_error_and_null_pcb);
    RUN_TEST(test_accept_cb_rejects_out_of_range_listener_idx);
    RUN_TEST(test_accept_cb_rejects_when_pool_full);
    RUN_TEST(test_accept_cb_claims_slot_and_wires_connection);
    RUN_TEST(test_accept_cb_second_accept_claims_a_different_slot);
    RUN_TEST(test_accept_cb_survives_a_failed_enqueue);

    return UNITY_END();
}
