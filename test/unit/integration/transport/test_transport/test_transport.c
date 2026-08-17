// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/presentation/presentation.h"
#include "network_drivers/transport/tcp/common.h"
#include "network_drivers/transport/tcp/protocol/protocol.h"
#include "network_drivers/transport/tcp/server/server.h"
#include "network_drivers/transport/tcp/tcp.h"
#include "shared/ip/ip.h"
#include <string.h>
#include <unity.h>
#include "server/clock/clock.h"

// Move the virtual clock and take the pass stamp. service_once() reads the source once per pass,
// before anything reads the time, and every module measures against that stamp; a case that drives
// a module directly is standing in for the pass, so it takes the stamp the pass would have.
static void set_now_ms(uint32_t ms)
{
    set_millis(ms);
    Clock.millis(Clock.internal);
}

static protocore_ip v4w(uint32_t host_order)
{
    return protocore_ip_from_v4_octets((uint8_t)(host_order >> 24), (uint8_t)(host_order >> 16),
                                       (uint8_t)(host_order >> 8), (uint8_t)host_order);
}

void setUp()
{
    set_now_ms(0);
    ConnPool.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(ConnPool.internal);
    TcpListener.idx = 0;
    TcpListener.bind.port = 80;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(TcpListener.internal);

}

void tearDown()
{
}

void test_pool_capacity_default_is_eight()
{

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

void test_ring_empty_when_head_equals_tail()
{
    TcpConn s = {0};
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
    TcpConn s = {0};
    s.rx_head = 0;
    s.rx_tail = 0;
    size_t count = 0;
    while (PROTO_TRUE)
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

void test_event_types_are_distinct()
{
    TEST_ASSERT_NOT_EQUAL((int)EVT_CONNECT, (int)EVT_DATA);
    TEST_ASSERT_NOT_EQUAL((int)EVT_DATA, (int)EVT_DISCONNECT);
    TEST_ASSERT_NOT_EQUAL((int)EVT_DISCONNECT, (int)EVT_ERROR);
    TEST_ASSERT_NOT_EQUAL((int)EVT_CONNECT, (int)EVT_ERROR);
}

void test_timeout_does_not_fire_on_free_slot()
{
    conn_pool[0].state = CONN_FREE;
    set_now_ms(CONN_TIMEOUT_MS + 1);
    ConnPool.life.worker_id = 0;
    ConnPool.check_timeouts(ConnPool.internal);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

void test_timeout_does_not_fire_before_deadline()
{
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = NULL;
    conn_pool[0].last_activity_ms = 0;
    set_now_ms(CONN_TIMEOUT_MS - 1);
    ConnPool.life.worker_id = 0;
    ConnPool.check_timeouts(ConnPool.internal);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

void test_timeout_fires_at_deadline()
{
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = NULL;
    conn_pool[0].last_activity_ms = 0;
    set_now_ms(CONN_TIMEOUT_MS);
    ConnPool.life.worker_id = 0;
    ConnPool.check_timeouts(ConnPool.internal);
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
    conn_pool[1].last_activity_ms = CONN_TIMEOUT_MS;

    set_now_ms(CONN_TIMEOUT_MS);
    ConnPool.life.worker_id = 0;
    ConnPool.check_timeouts(ConnPool.internal);

    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[1].state);
}

void test_active_send_not_reaped()
{
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = NULL;
    conn_pool[0].last_activity_ms = 0;

    conn_pool[1].state = CONN_ACTIVE;
    conn_pool[1].pcb = NULL;
    conn_pool[1].last_activity_ms = 0;

    set_now_ms(CONN_TIMEOUT_MS + 10);
    ConnPool.slot = 0;
    ConnPool.touch_active(ConnPool.internal);
    ConnPool.life.worker_id = 0;
    ConnPool.check_timeouts(ConnPool.internal);

    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state);
}

void test_pool_init_applies_custom_config()
{
    ConnPool.life.conn_timeout_ms = 12345;
    ConnPool.init(ConnPool.internal);
    ConnPool.timeout_ms(ConnPool.internal);

    TEST_ASSERT_EQUAL_UINT32(12345, ConnPool.u32);
    ConnPool.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(ConnPool.internal);
}

void test_init_succeeds_on_native()
{
    ConnPool.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(ConnPool.internal);
    TcpListener.idx = 0;
    TcpListener.bind.port = 80;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(TcpListener.internal);

    int32_t ok = TcpListener.i32;
    TEST_ASSERT_EQUAL(1, ok);
}

void test_listener_add_bounds_and_lwip_failure_paths()
{
    TcpListener.idx = (uint8_t)MAX_LISTENERS;
    TcpListener.bind.port = 80;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(TcpListener.internal);

    TEST_ASSERT_EQUAL_INT32(-1, TcpListener.i32);

    mock_new_pcb_fail_once();
    TcpListener.idx = 1;
    TcpListener.bind.port = 81;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(TcpListener.internal);

    TEST_ASSERT_EQUAL_INT32(-1, TcpListener.i32);

    mock_bind_fail_once();
    TcpListener.idx = 1;
    TcpListener.bind.port = 81;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(TcpListener.internal);

    TEST_ASSERT_EQUAL_INT32(-1, TcpListener.i32);

    mock_listen_fail_once();
    int before = mock_abort_call_count();
    TcpListener.idx = 1;
    TcpListener.bind.port = 81;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(TcpListener.internal);

    TEST_ASSERT_EQUAL_INT32(-1, TcpListener.i32);
    TEST_ASSERT_EQUAL_INT(before + 1, mock_abort_call_count());

    mock_queue_create_fail_once();
    TcpListener.idx = 1;
    TcpListener.bind.port = 81;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(TcpListener.internal);

    TEST_ASSERT_EQUAL_INT32(-1, TcpListener.i32);
    TcpListener.idx = 1;
    TcpListener.bind.port = 81;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(TcpListener.internal);

    TEST_ASSERT_EQUAL_INT32(1, TcpListener.i32);
    TcpListener.idx = 1;
    TcpListener.stop(TcpListener.internal);
}

void test_listener_stop_rejects_out_of_range_idx()
{
    TcpListener.idx = (uint8_t)MAX_LISTENERS;
    TcpListener.stop(TcpListener.internal);
}

void test_listener_stop_and_stop_dynamic_tolerate_a_missing_queue()
{
    listener_pool[0].active = PROTO_TRUE;
    listener_pool[0].queue = NULL;
    TcpListener.idx = 0;
    TcpListener.stop(TcpListener.internal);
    TEST_ASSERT_FALSE(listener_pool[0].active);
    TcpListener.idx = 1;
    TcpListener.bind.port = 5555;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.add_dynamic(TcpListener.internal);

    TEST_ASSERT_EQUAL_INT32(1, TcpListener.i32);
    listener_pool[1].queue = NULL;
    TcpListener.idx = 1;
    TcpListener.stop_dynamic(TcpListener.internal);
    TEST_ASSERT_FALSE(listener_pool[1].active);
}

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

void stress_ring_buffer_fill_drain_integrity()
{
    TcpConn *s = &conn_pool[0];
    s->rx_head = 0;
    s->rx_tail = 0;
    const int FILL = RX_BUF_SIZE - 1;

    for (int i = 0; i < FILL; i++)
    {
        size_t next = (s->rx_head + 1) % RX_BUF_SIZE;
        s->rx_buffer[s->rx_head] = (uint8_t)(i & 0xFF);
        s->rx_head = next;
    }

    TEST_ASSERT_EQUAL(RX_BUF_SIZE - 1, (int)((s->rx_head - s->rx_tail + RX_BUF_SIZE) % RX_BUF_SIZE));

    for (int i = 0; i < FILL; i++)
    {
        uint8_t expected = (uint8_t)(i & 0xFF);
        uint8_t actual = s->rx_buffer[s->rx_tail];
        s->rx_tail = (s->rx_tail + 1) % RX_BUF_SIZE;
        TEST_ASSERT_EQUAL_MESSAGE(expected, actual, "ring buffer byte mismatch");
    }

    TEST_ASSERT_EQUAL(s->rx_head, s->rx_tail);
}

void stress_ring_buffer_multi_cycle_no_corruption()
{
    TcpConn *s = &conn_pool[0];
    s->rx_head = 0;
    s->rx_tail = 0;

    uint8_t write_val = 0;
    uint8_t read_val = 0;

    for (int cycle = 0; cycle < 8; cycle++)
    {
        const int BATCH = RX_BUF_SIZE / 2;

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

    TEST_ASSERT_EQUAL(s->rx_head, s->rx_tail);
}

void stress_all_slots_timeout_simultaneously()
{
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].pcb = NULL;
        conn_pool[i].last_activity_ms = 0;
    }

    set_now_ms(CONN_TIMEOUT_MS);
    ConnPool.life.worker_id = 0;
    ConnPool.check_timeouts(ConnPool.internal);

    for (int i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[i].state);
        TEST_ASSERT_NULL(conn_pool[i].pcb);
        TEST_ASSERT_EQUAL(i, conn_pool[i].id);
    }
}

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

        set_now_ms((uint32_t)(CONN_TIMEOUT_MS * (cycle + 1)));
        ConnPool.life.worker_id = 0;
        ConnPool.check_timeouts(ConnPool.internal);

        for (int i = 0; i < MAX_CONNS; i++)
        {
            TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[i].state);
        }
    }
}

void stress_check_timeouts_high_call_rate()
{
    conn_pool[0].state = CONN_FREE;
    conn_pool[1].state = CONN_ACTIVE;
    conn_pool[1].pcb = NULL;
    conn_pool[1].last_activity_ms = 0;
    conn_pool[2].state = CONN_ACTIVE;
    conn_pool[2].pcb = NULL;
    conn_pool[2].last_activity_ms = CONN_TIMEOUT_MS;
    conn_pool[3].state = CONN_FREE;

    set_now_ms(CONN_TIMEOUT_MS);

    for (int i = 0; i < 2000; i++)
    {
        ConnPool.life.worker_id = 0;
        ConnPool.check_timeouts(ConnPool.internal);
    }

    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[2].state);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[3].state);
}

void stress_ring_buffer_byte_by_byte_fill_and_drain()
{
    TcpConn *s = &conn_pool[0];
    s->rx_head = 0;
    s->rx_tail = 0;

    int written = 0;
    while (PROTO_TRUE)
    {
        size_t next = (s->rx_head + 1) % RX_BUF_SIZE;
        if (next == s->rx_tail)
        {
            break;
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

void test_accept_throttle_blocks_over_budget()
{
    TcpListener.accept_throttle_reset(TcpListener.internal);
    for (int i = 0; i < PROTOCORE_ACCEPT_THROTTLE_MAX; i++)
    {
        TcpListener.gate.now_ms = 0;
        TcpListener.accept_allowed(TcpListener.internal);

        TEST_ASSERT_TRUE(TcpListener.ok);
    }
    TcpListener.gate.now_ms = 0;
    TcpListener.accept_allowed(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_accept_throttle_window_refills()
{
    TcpListener.accept_throttle_reset(TcpListener.internal);
    for (int i = 0; i < PROTOCORE_ACCEPT_THROTTLE_MAX; i++)
    {
        TcpListener.gate.now_ms = 10;
        TcpListener.accept_allowed(TcpListener.internal);

        TEST_ASSERT_TRUE(TcpListener.ok);
    }
    TcpListener.gate.now_ms = 10;
    TcpListener.accept_allowed(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.now_ms = 10 + PROTOCORE_ACCEPT_THROTTLE_WINDOW_MS;
    TcpListener.accept_allowed(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
}

void test_accept_throttle_handles_rollover()
{
    TcpListener.accept_throttle_reset(TcpListener.internal);
    uint32_t near_max = 0xFFFFFFFFu - 5;
    TcpListener.gate.now_ms = near_max;
    TcpListener.accept_allowed(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.now_ms = near_max + PROTOCORE_ACCEPT_THROTTLE_WINDOW_MS;
    TcpListener.accept_allowed(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
}

void test_per_ip_throttle_blocks_over_budget()
{
    TcpListener.per_ip_throttle_reset(TcpListener.internal);
    protocore_ip ip = v4w(0xC0A80005u);
    for (int i = 0; i < PROTOCORE_PER_IP_THROTTLE_MAX; i++)
    {
        TcpListener.gate.addr = &ip;
        TcpListener.gate.now_ms = 0;
        TcpListener.accept_allowed_ip(TcpListener.internal);

        TEST_ASSERT_TRUE(TcpListener.ok);
    }
    TcpListener.gate.addr = &ip;
    TcpListener.gate.now_ms = 0;
    TcpListener.accept_allowed_ip(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_per_ip_throttle_isolates_addresses()
{
    TcpListener.per_ip_throttle_reset(TcpListener.internal);
    protocore_ip noisy = v4w(0x0A000001u), quiet = v4w(0x0A000002u);
    for (int i = 0; i < PROTOCORE_PER_IP_THROTTLE_MAX; i++)
    {
        TcpListener.gate.addr = &noisy;
        TcpListener.gate.now_ms = 0;
        TcpListener.accept_allowed_ip(TcpListener.internal);

        TEST_ASSERT_TRUE(TcpListener.ok);
    }
    TcpListener.gate.addr = &noisy;
    TcpListener.gate.now_ms = 0;
    TcpListener.accept_allowed_ip(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.addr = &quiet;
    TcpListener.gate.now_ms = 0;
    TcpListener.accept_allowed_ip(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
}

void test_per_ip_throttle_window_refills()
{
    TcpListener.per_ip_throttle_reset(TcpListener.internal);
    protocore_ip ip = v4w(0x0A000003u);
    for (int i = 0; i < PROTOCORE_PER_IP_THROTTLE_MAX; i++)
    {
        TcpListener.gate.addr = &ip;
        TcpListener.gate.now_ms = 50;
        TcpListener.accept_allowed_ip(TcpListener.internal);

        TEST_ASSERT_TRUE(TcpListener.ok);
    }
    TcpListener.gate.addr = &ip;
    TcpListener.gate.now_ms = 50;
    TcpListener.accept_allowed_ip(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.addr = &ip;
    TcpListener.gate.now_ms = 50 + PROTOCORE_PER_IP_THROTTLE_WINDOW_MS;
    TcpListener.accept_allowed_ip(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
}

void test_per_ip_throttle_evicts_when_full()
{
    TcpListener.per_ip_throttle_reset(TcpListener.internal);
    for (int i = 0; i < PROTOCORE_PER_IP_THROTTLE_SLOTS; i++)
    {
        protocore_ip ip = v4w(0xAC100001u + (uint32_t)i);
        TcpListener.gate.addr = &ip;
        TcpListener.gate.now_ms = 100;
        TcpListener.accept_allowed_ip(TcpListener.internal);

        TEST_ASSERT_TRUE(TcpListener.ok);
    }
    protocore_ip fresh = v4w(0xDEADBEEFu);
    TcpListener.gate.addr = &fresh;
    TcpListener.gate.now_ms = 100;
    TcpListener.accept_allowed_ip(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
}

void test_per_ip_throttle_zero_ip_always_allowed()
{
    TcpListener.per_ip_throttle_reset(TcpListener.internal);
    protocore_ip none;
    none.family = PROTOCORE_IP_NONE;
    for (int i = 0; i < PROTOCORE_PER_IP_THROTTLE_MAX + 5; i++)
    {
        TcpListener.gate.addr = &none;
        TcpListener.gate.now_ms = 0;
        TcpListener.accept_allowed_ip(TcpListener.internal);

        TEST_ASSERT_TRUE(TcpListener.ok);
    }
}

void test_per_ip_throttle_v6_distinct()
{
    TcpListener.per_ip_throttle_reset(TcpListener.internal);
    protocore_ip a;
    a.family = PROTOCORE_IP_NONE;
    protocore_ip b;
    b.family = PROTOCORE_IP_NONE;
    Ip.args.text = "2001:db8::1";
    Ip.args.out = &a;
    Ip.parse(Ip.internal);
    TEST_ASSERT_TRUE(Ip.ok);
    Ip.args.text = "2001:db8::2";
    Ip.args.out = &b;
    Ip.parse(Ip.internal);
    TEST_ASSERT_TRUE(Ip.ok);
    for (int i = 0; i < PROTOCORE_PER_IP_THROTTLE_MAX; i++)
    {
        TcpListener.gate.addr = &a;
        TcpListener.gate.now_ms = 0;
        TcpListener.accept_allowed_ip(TcpListener.internal);

        TEST_ASSERT_TRUE(TcpListener.ok);
    }
    TcpListener.gate.addr = &a;
    TcpListener.gate.now_ms = 0;
    TcpListener.accept_allowed_ip(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.addr = &b;
    TcpListener.gate.now_ms = 0;
    TcpListener.accept_allowed_ip(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
}

void test_per_ip_throttle_handles_rollover()
{
    TcpListener.per_ip_throttle_reset(TcpListener.internal);
    protocore_ip ip = v4w(0x0A000009u);
    uint32_t near_max = 0xFFFFFFFFu - 5;
    TcpListener.gate.addr = &ip;
    TcpListener.gate.now_ms = near_max;
    TcpListener.accept_allowed_ip(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &ip;
    TcpListener.gate.now_ms = near_max + PROTOCORE_PER_IP_THROTTLE_WINDOW_MS;
    TcpListener.accept_allowed_ip(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
}

void test_ip_allowlist_empty_allows_all()
{
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    protocore_ip a = v4w(0xC0A8010Au), b = v4w(0x08080808u);
    protocore_ip none;
    none.family = PROTOCORE_IP_NONE;
    TcpListener.gate.addr = &a;
    TcpListener.ip_allowed(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &b;
    TcpListener.ip_allowed(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &none;
    TcpListener.ip_allowed(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
}

void test_ip_allowlist_host_match()
{
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    protocore_ip net = v4w(0xC0A8010Au);
    TcpListener.gate.addr = &net;
    TcpListener.gate.prefix_len = 32;
    TcpListener.ip_allow_add(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
    protocore_ip host = v4w(0xC0A8010Au), near = v4w(0xC0A8010Bu), far = v4w(0x0A000001u);
    TcpListener.gate.addr = &host;
    TcpListener.ip_allowed(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &near;
    TcpListener.ip_allowed(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.addr = &far;
    TcpListener.ip_allowed(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_ip_allowlist_cidr_match()
{
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    protocore_ip net = v4w(0xC0A80100u);
    TcpListener.gate.addr = &net;
    TcpListener.gate.prefix_len = 24;
    TcpListener.ip_allow_add(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
    protocore_ip lo = v4w(0xC0A80101u), hi = v4w(0xC0A801FEu), out = v4w(0xC0A80201u);
    TcpListener.gate.addr = &lo;
    TcpListener.ip_allowed(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &hi;
    TcpListener.ip_allowed(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &out;
    TcpListener.ip_allowed(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_ip_allowlist_masks_host_bits()
{
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    protocore_ip net = v4w(0xC0A80137u);
    TcpListener.gate.addr = &net;
    TcpListener.gate.prefix_len = 24;
    TcpListener.ip_allow_add(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
    protocore_ip lo = v4w(0xC0A80101u), hi = v4w(0xC0A801C8u);
    TcpListener.gate.addr = &lo;
    TcpListener.ip_allowed(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &hi;
    TcpListener.ip_allowed(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
}

void test_ip_allowlist_multiple_rules()
{
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    protocore_ip r1 = v4w(0x0A000000u), r2 = v4w(0xC0A80000u);
    TcpListener.gate.addr = &r1;
    TcpListener.gate.prefix_len = 8;
    TcpListener.ip_allow_add(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &r2;
    TcpListener.gate.prefix_len = 16;
    TcpListener.ip_allow_add(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
    protocore_ip a = v4w(0x0A010203u), b = v4w(0xC0A80505u), out = v4w(0xAC100001u);
    TcpListener.gate.addr = &a;
    TcpListener.ip_allowed(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &b;
    TcpListener.ip_allowed(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &out;
    TcpListener.ip_allowed(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_ip_allowlist_zero_prefix_matches_all()
{
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    protocore_ip z = v4w(0u);
    TcpListener.gate.addr = &z;
    TcpListener.gate.prefix_len = 0;
    TcpListener.ip_allow_add(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
    protocore_ip a = v4w(0x01020304u), b = v4w(0xFFFFFFFFu);
    TcpListener.gate.addr = &a;
    TcpListener.ip_allowed(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &b;
    TcpListener.ip_allowed(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
}

void test_ip_allowlist_v6_cidr()
{
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    TcpListener.gate.cidr = "2001:db8::/32";
    TcpListener.ip_allow_add_cidr(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
    protocore_ip in;
    in.family = PROTOCORE_IP_NONE;
    protocore_ip out;
    out.family = PROTOCORE_IP_NONE;
    Ip.args.text = "2001:db8:0:0:1234::abcd";
    Ip.args.out = &in;
    Ip.parse(Ip.internal);
    TEST_ASSERT_TRUE(Ip.ok);
    Ip.args.text = "2001:db9::1";
    Ip.args.out = &out;
    Ip.parse(Ip.internal);
    TEST_ASSERT_TRUE(Ip.ok);
    TcpListener.gate.addr = &in;
    TcpListener.ip_allowed(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &out;
    TcpListener.ip_allowed(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);
    protocore_ip v4peer = v4w(0xC0A80101u);
    TcpListener.gate.addr = &v4peer;
    TcpListener.ip_allowed(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_ip_allowlist_rejects_bad_prefix()
{
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    protocore_ip net = v4w(0xC0A80100u);
    TcpListener.gate.addr = &net;
    TcpListener.gate.prefix_len = 33;
    TcpListener.ip_allow_add(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_ip_allowlist_table_full()
{
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    for (int i = 0; i < PROTOCORE_IP_ALLOWLIST_SLOTS; i++)
    {
        protocore_ip r = v4w(0x0A000000u + (uint32_t)i);
        TcpListener.gate.addr = &r;
        TcpListener.gate.prefix_len = 32;
        TcpListener.ip_allow_add(TcpListener.internal);

        TEST_ASSERT_TRUE(TcpListener.ok);
    }
    protocore_ip overflow = v4w(0x0A010000u);
    TcpListener.gate.addr = &overflow;
    TcpListener.gate.prefix_len = 32;
    TcpListener.ip_allow_add(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_per_ip_throttle_scans_expired_and_lru_across_a_full_table()
{
    TcpListener.per_ip_throttle_reset(TcpListener.internal);
    for (int i = 0; i < PROTOCORE_PER_IP_THROTTLE_SLOTS; i++)
    {
        protocore_ip ip = v4w(0x0A000000u + (uint32_t)(i + 1));
        uint32_t start = (uint32_t)(PROTOCORE_PER_IP_THROTTLE_SLOTS - 1 - i) * 100;
        TcpListener.gate.addr = &ip;
        TcpListener.gate.now_ms = start;
        TcpListener.accept_allowed_ip(TcpListener.internal);

        TEST_ASSERT_TRUE(TcpListener.ok);
    }
    uint32_t now = PROTOCORE_PER_IP_THROTTLE_WINDOW_MS + (uint32_t)PROTOCORE_PER_IP_THROTTLE_SLOTS * 100;
    protocore_ip fresh = v4w(0xAC100001u);
    TcpListener.gate.addr = &fresh;
    TcpListener.gate.now_ms = now;
    TcpListener.accept_allowed_ip(TcpListener.internal);

    TEST_ASSERT_TRUE(TcpListener.ok);
}

void test_ip_allowlist_rejects_null_args()
{
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    TcpListener.gate.addr = NULL;
    TcpListener.gate.prefix_len = 24;
    TcpListener.ip_allow_add(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.cidr = NULL;
    TcpListener.ip_allow_add_cidr(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);

    protocore_ip none;
    none.family = PROTOCORE_IP_NONE;
    TcpListener.gate.addr = &none;
    TcpListener.gate.prefix_len = 24;
    TcpListener.ip_allow_add(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_ip_allowlist_rejects_overlong_address_text()
{
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    char too_long[64];
    for (int i = 0; i < 60; i++)
    {
        too_long[i] = '1';
    }
    too_long[60] = '\0';
    TcpListener.gate.cidr = too_long;
    TcpListener.ip_allow_add_cidr(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_ip_allowlist_rejects_non_digit_prefix()
{
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    TcpListener.gate.cidr = "10.0.0.0/2x";
    TcpListener.ip_allow_add_cidr(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.cidr = "10.0.0.0/-1";
    TcpListener.ip_allow_add_cidr(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_enqueue_rejects_out_of_range_listener_id()
{
    TcpEvt evt = {EVT_DATA, 0, 0};
    TcpListener.idx = (uint8_t)MAX_LISTENERS;
    TcpListener.q.evt = &evt;
    TcpListener.enqueue(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);

    mock_queue_send_fail_once();
    TcpListener.idx = 0;
    TcpListener.q.evt = &evt;
    TcpListener.enqueue(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);

    listener_pool[0].active = PROTO_TRUE;
    listener_pool[0].queue = NULL;
    TcpListener.idx = 0;
    TcpListener.q.evt = &evt;
    TcpListener.enqueue(TcpListener.internal);

    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_dynamic_listener_lifecycle()
{
    TcpListener.idx = (uint8_t)MAX_LISTENERS;
    TcpListener.bind.port = 2222;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.add_dynamic(TcpListener.internal);

    TEST_ASSERT_EQUAL_INT32(-1, TcpListener.i32);

    mock_queue_create_fail_once();
    TcpListener.idx = 1;
    TcpListener.bind.port = 2222;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.add_dynamic(TcpListener.internal);

    TEST_ASSERT_EQUAL_INT32(-1, TcpListener.i32);
    TcpListener.idx = 1;
    TcpListener.bind.port = 2222;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.add_dynamic(TcpListener.internal);

    TEST_ASSERT_EQUAL_INT32(1, TcpListener.i32);
    TEST_ASSERT_TRUE(listener_pool[1].active);
    TEST_ASSERT_FALSE(listener_pool[1].tls);
    TEST_ASSERT_NOT_NULL(listener_pool[1].queue);
    TEST_ASSERT_NOT_NULL(listener_pool[1].listen_pcb);
    TcpListener.idx = 1;
    TcpListener.bind.port = 3333;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.add_dynamic(TcpListener.internal);

    TEST_ASSERT_EQUAL_INT32(1, TcpListener.i32);
    TEST_ASSERT_EQUAL_UINT16(3333, listener_pool[1].port);
    TcpListener.idx = (uint8_t)MAX_LISTENERS;
    TcpListener.stop_dynamic(TcpListener.internal);
    TcpListener.idx = 1;
    TcpListener.stop_dynamic(TcpListener.internal);
    TEST_ASSERT_FALSE(listener_pool[1].active);
    TEST_ASSERT_NULL(listener_pool[1].queue);
    TEST_ASSERT_NULL(listener_pool[1].listen_pcb);
    TcpListener.idx = 1;
    TcpListener.stop_dynamic(TcpListener.internal);
    TEST_ASSERT_FALSE(listener_pool[1].active);
}

void test_freeslot_bitmask_alloc()
{
    ConnPool.alloc_free(ConnPool.internal);

    TEST_ASSERT_EQUAL_INT32(0, ConnPool.i32);
    ConnPool.slot = 0;
    ConnPool.st = CONN_ACTIVE;
    ConnPool.set_state(ConnPool.internal);
    ConnPool.alloc_free(ConnPool.internal);

    TEST_ASSERT_EQUAL_INT32(1, ConnPool.i32);

    for (uint8_t i = 1; i < MAX_CONNS; i++)
    {
        ConnPool.slot = i;
        ConnPool.st = CONN_ACTIVE;
        ConnPool.set_state(ConnPool.internal);
    }
    ConnPool.alloc_free(ConnPool.internal);

    TEST_ASSERT_EQUAL_INT32(-1, ConnPool.i32);
    ConnPool.slot = 3;
    ConnPool.st = CONN_FREE;
    ConnPool.set_state(ConnPool.internal);
    ConnPool.alloc_free(ConnPool.internal);

    TEST_ASSERT_EQUAL_INT32(3, ConnPool.i32);
    ConnPool.slot = 1;
    ConnPool.st = CONN_FREE;
    ConnPool.set_state(ConnPool.internal);
    ConnPool.alloc_free(ConnPool.internal);

    TEST_ASSERT_EQUAL_INT32(1, ConnPool.i32);
    ConnPool.slot = 1;
    ConnPool.st = CONN_ACTIVE;
    ConnPool.set_state(ConnPool.internal);
    ConnPool.slot = 1;
    ConnPool.st = CONN_CLOSING;
    ConnPool.set_state(ConnPool.internal);
    ConnPool.alloc_free(ConnPool.internal);

    TEST_ASSERT_EQUAL_INT32(3, ConnPool.i32);
}

void test_bounds_guards_reject_out_of_range_slots()
{
    ConnPool.alloc_free(ConnPool.internal);

    int32_t before = ConnPool.i32;
    ConnPool.slot = (uint8_t)CONN_POOL_SLOTS;
    ConnPool.st = CONN_ACTIVE;
    ConnPool.set_state(ConnPool.internal);
    ConnPool.alloc_free(ConnPool.internal);

    TEST_ASSERT_EQUAL_INT32(before, ConnPool.i32);
    ConnPool.slot = (uint8_t)(MAX_CONNS + 50);
    ConnPool.ack_consumed(ConnPool.internal);
    ConnPool.slot = (uint8_t)(MAX_CONNS + 50);
    ConnPool.close(ConnPool.internal);
    ConnPool.slot = (uint8_t)(MAX_CONNS + 50);
    ConnPool.abort_slot(ConnPool.internal);
    ConnPool.slot = (uint8_t)(MAX_CONNS + 50);
    ConnPool.touch_active(ConnPool.internal);

    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].last_activity_ms = 0;
    ConnPool.slot = (uint8_t)(MAX_CONNS + 50);
    ConnPool.begin_close(ConnPool.internal);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

void test_null_pcb_slots_are_safe_no_ops()
{
    conn_pool[0].pcb = NULL;
    ConnPool.slot = 0;
    ConnPool.sndbuf(ConnPool.internal);

    TEST_ASSERT_EQUAL_UINT16(0, ConnPool.u16);
    ConnPool.slot = 0;
    ConnPool.close(ConnPool.internal);
    ConnPool.slot = 0;
    ConnPool.abort_slot(ConnPool.internal);

    protocore_pcb fake = {0};
    conn_pool[1].pcb = &fake;
    ConnPool.slot = 1;
    ConnPool.sndbuf(ConnPool.internal);

    TEST_ASSERT_EQUAL_UINT16(MOCK_SNDBUF_DEFAULT, ConnPool.u16);
}

void test_ack_consumed_bounds_inactive_and_real_advance()
{
    ConnPool.slot = (uint8_t)(MAX_CONNS + 1);
    ConnPool.ack_consumed(ConnPool.internal);

    conn_pool[0].state = CONN_FREE;
    conn_pool[0].rx_tail = 5;
    conn_pool[0].rx_acked = 0;
    ConnPool.slot = 0;
    ConnPool.ack_consumed(ConnPool.internal);
    TEST_ASSERT_EQUAL(0u, (size_t)conn_pool[0].rx_acked);

    protocore_pcb fake = {0};
    conn_pool[0].pcb = &fake;
    ConnPool.slot = 0;
    ConnPool.st = CONN_ACTIVE;
    ConnPool.set_state(ConnPool.internal);
    conn_pool[0].rx_tail = 5;
    conn_pool[0].rx_acked = 2;
    ConnPool.slot = 0;
    ConnPool.ack_consumed(ConnPool.internal);
    TEST_ASSERT_EQUAL(5u, (size_t)conn_pool[0].rx_acked);
    ConnPool.slot = 0;
    ConnPool.ack_consumed(ConnPool.internal);
    TEST_ASSERT_EQUAL(5u, (size_t)conn_pool[0].rx_acked);
}

void test_send_flush_success_and_write_failure()
{
    protocore_pcb fake = {0};
    conn_pool[0].pcb = &fake;
    ConnPool.slot = 0;
    ConnPool.io.data = "x";
    ConnPool.io.len = 1;
    ConnPool.send_flush(ConnPool.internal);

    TEST_ASSERT_TRUE(ConnPool.ok);

    mock_send_fail_after(0);
    ConnPool.slot = 0;
    ConnPool.io.data = "x";
    ConnPool.io.len = 1;
    ConnPool.send_flush(ConnPool.internal);

    TEST_ASSERT_FALSE(ConnPool.ok);
    mock_send_fail_after(-1);
}

void test_raw_send_null_success_and_failure()
{
    ConnPool.pcb = NULL;
    ConnPool.io.data = "x";
    ConnPool.io.len = 1;
    ConnPool.raw_send(ConnPool.internal);

    TEST_ASSERT_FALSE(ConnPool.ok);

    protocore_pcb fake = {0};
    ConnPool.pcb = &fake;
    ConnPool.io.data = "hello";
    ConnPool.io.len = 5;
    ConnPool.raw_send(ConnPool.internal);

    TEST_ASSERT_FALSE(ConnPool.ok);

    conn_pool[0].pcb = &fake;
    ConnPool.pcb = &fake;
    ConnPool.io.data = "hello";
    ConnPool.io.len = 5;
    ConnPool.raw_send(ConnPool.internal);

    TEST_ASSERT_TRUE(ConnPool.ok);

    mock_send_fail_after(0);
    ConnPool.pcb = &fake;
    ConnPool.io.data = "x";
    ConnPool.io.len = 1;
    ConnPool.raw_send(ConnPool.internal);

    TEST_ASSERT_FALSE(ConnPool.ok);
    mock_send_fail_after(-1);

    conn_pool[0].pcb = NULL;
}

void test_close_falls_back_to_abort_on_tcp_close_failure()
{
    protocore_pcb fake = {0};
    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    ConnPool.slot = 0;
    ConnPool.st = CONN_ACTIVE;
    ConnPool.set_state(ConnPool.internal);

    int before = mock_abort_call_count();
    mock_close_fail_once();
    ConnPool.slot = 0;
    ConnPool.close(ConnPool.internal);
    TEST_ASSERT_EQUAL_INT(before + 1, mock_abort_call_count());
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);

    conn_pool[0].pcb = &fake;
    ConnPool.slot = 0;
    ConnPool.st = CONN_ACTIVE;
    ConnPool.set_state(ConnPool.internal);
    before = mock_abort_call_count();
    ConnPool.slot = 0;
    ConnPool.close(ConnPool.internal);
    TEST_ASSERT_EQUAL_INT(before, mock_abort_call_count());
}

void test_begin_close_finalizes_immediately_with_and_without_a_pcb()
{

    conn_pool[1].id = 1;
    conn_pool[1].pcb = NULL;
    ConnPool.slot = 1;
    ConnPool.st = CONN_ACTIVE;
    ConnPool.set_state(ConnPool.internal);
    ConnPool.slot = 1;
    ConnPool.begin_close(ConnPool.internal);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state);

    protocore_pcb fake = {0};
    conn_pool[2].id = 2;
    conn_pool[2].pcb = &fake;
    ConnPool.slot = 2;
    ConnPool.st = CONN_ACTIVE;
    ConnPool.set_state(ConnPool.internal);
    int before = mock_abort_call_count();
    ConnPool.slot = 2;
    ConnPool.begin_close(ConnPool.internal);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[2].state);
    TEST_ASSERT_EQUAL_INT(before, mock_abort_call_count());

    protocore_pcb fake2 = {0};
    conn_pool[3].id = 3;
    conn_pool[3].pcb = &fake2;
    ConnPool.slot = 3;
    ConnPool.st = CONN_ACTIVE;
    ConnPool.set_state(ConnPool.internal);
    mock_close_fail_once();
    before = mock_abort_call_count();
    ConnPool.slot = 3;
    ConnPool.begin_close(ConnPool.internal);
    TEST_ASSERT_EQUAL_INT(before + 1, mock_abort_call_count());
}

void test_remote_addr_accessors_host_stub()
{
    ConnPool.slot = 0;
    ConnPool.remote_ip(ConnPool.internal);

    TEST_ASSERT_EQUAL_UINT32(0, ConnPool.u32);

    protocore_ip out;
    ConnPool.slot = 0;
    ConnPool.out = &out;
    ConnPool.remote_addr(ConnPool.internal);

    TEST_ASSERT_FALSE(ConnPool.ok);
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_IP_NONE, (int)out.family);
    ConnPool.slot = 0;
    ConnPool.out = NULL;
    ConnPool.remote_addr(ConnPool.internal);

    TEST_ASSERT_FALSE(ConnPool.ok);
}

void test_stop_aborts_live_slots_and_skips_the_rest()
{
    protocore_pcb fake_active = {0};
    protocore_pcb fake_closing = {0};

    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake_active;
    ConnPool.slot = 0;
    ConnPool.st = CONN_ACTIVE;
    ConnPool.set_state(ConnPool.internal);

    conn_pool[1].id = 1;
    conn_pool[1].pcb = &fake_closing;
    ConnPool.slot = 1;
    ConnPool.st = CONN_CLOSING;
    ConnPool.set_state(ConnPool.internal);

    conn_pool[2].id = 2;
    conn_pool[2].pcb = NULL;
    ConnPool.slot = 2;
    ConnPool.st = CONN_ACTIVE;
    ConnPool.set_state(ConnPool.internal);

    conn_pool[3].id = 3;
    conn_pool[3].pcb = NULL;
    ConnPool.slot = 3;
    ConnPool.st = CONN_FREE;
    ConnPool.set_state(ConnPool.internal);

    int before = mock_abort_call_count();
    ConnPool.stop(ConnPool.internal);
    TEST_ASSERT_EQUAL_INT(before + 2, mock_abort_call_count());

    for (int i = 0; i < 4; i++)
    {
        TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[i].state);
        TEST_ASSERT_NULL(conn_pool[i].pcb);
    }
}

void test_check_timeouts_reaps_stale_closing_slots()
{
    protocore_pcb fake = {0};

    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    ConnPool.slot = 0;
    ConnPool.st = CONN_CLOSING;
    ConnPool.set_state(ConnPool.internal);
    conn_pool[0].last_activity_ms = 0;

    conn_pool[1].id = 1;
    conn_pool[1].pcb = NULL;
    ConnPool.slot = 1;
    ConnPool.st = CONN_CLOSING;
    ConnPool.set_state(ConnPool.internal);
    conn_pool[1].last_activity_ms = 0;

    set_now_ms(PROTOCORE_CLOSING_TIMEOUT_MS - 1);
    ConnPool.life.worker_id = 0;
    ConnPool.check_timeouts(ConnPool.internal);
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[1].state);

    int before = mock_abort_call_count();
    set_now_ms(PROTOCORE_CLOSING_TIMEOUT_MS);
    ConnPool.life.worker_id = 0;
    ConnPool.check_timeouts(ConnPool.internal);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state);
    TEST_ASSERT_EQUAL_INT(before + 1, mock_abort_call_count());
}

void test_check_timeouts_detaches_and_aborts_a_real_pcb()
{
    protocore_pcb fake = {0};
    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    ConnPool.slot = 0;
    ConnPool.st = CONN_ACTIVE;
    ConnPool.set_state(ConnPool.internal);
    conn_pool[0].last_activity_ms = 0;

    int before = mock_abort_call_count();
    set_now_ms(CONN_TIMEOUT_MS);
    ConnPool.life.worker_id = 0;
    ConnPool.check_timeouts(ConnPool.internal);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
    TEST_ASSERT_EQUAL_INT(before + 1, mock_abort_call_count());
}

void test_touch_active_bounds_and_state_guard()
{
    ConnPool.slot = (uint8_t)(MAX_CONNS + 1);
    ConnPool.touch_active(ConnPool.internal);

    conn_pool[0].state = CONN_FREE;
    conn_pool[0].last_activity_ms = 111;
    set_now_ms(999);
    ConnPool.slot = 0;
    ConnPool.touch_active(ConnPool.internal);
    TEST_ASSERT_EQUAL_UINT32(111, conn_pool[0].last_activity_ms);
}

void test_recv_cb_null_arg_and_closing_reset()
{
    protocore_pcb fake = {0};
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_ERR_VAL, lowlevel_recv_cb(NULL, &fake, NULL, PROTOCORE_NET_OK));

    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    ConnPool.slot = 0;
    ConnPool.st = CONN_CLOSING;
    ConnPool.set_state(ConnPool.internal);

    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, lowlevel_recv_cb(&conn_pool[0], &fake, NULL, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL(CONN_CLOSING, (ConnState)conn_pool[0].state);

    protocore_pbuf seg = {0};
    uint8_t payload[4] = {1, 2, 3, 4};
    seg.payload = payload;
    seg.len = 4;
    seg.tot_len = 4;
    seg.next = NULL;
    int before = mock_abort_call_count();
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_ERR_ABRT, lowlevel_recv_cb(&conn_pool[0], &fake, &seg, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
    TEST_ASSERT_EQUAL_INT(before + 1, mock_abort_call_count());
}

void test_recv_cb_fin_close_falls_back_to_abort_on_tcp_close_failure()
{
    protocore_pcb fake = {0};
    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    conn_pool[0].listener_id = 0;
    ConnPool.slot = 0;
    ConnPool.st = CONN_ACTIVE;
    ConnPool.set_state(ConnPool.internal);

    mock_close_fail_once();
    int before = mock_abort_call_count();
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, lowlevel_recv_cb(&conn_pool[0], &fake, NULL, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_INT(before + 1, mock_abort_call_count());
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);
}

void test_recv_cb_fin_close_ordinary_path_does_not_abort()
{
    protocore_pcb fake = {0};
    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    conn_pool[0].listener_id = 0;
    ConnPool.slot = 0;
    ConnPool.st = CONN_ACTIVE;
    ConnPool.set_state(ConnPool.internal);

    int before = mock_abort_call_count();
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, lowlevel_recv_cb(&conn_pool[0], &fake, NULL, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_INT(before, mock_abort_call_count());
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

void test_recv_cb_rejects_non_active_slot()
{
    protocore_pcb fake = {0};
    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    ConnPool.slot = 0;
    ConnPool.st = CONN_FREE;
    ConnPool.set_state(ConnPool.internal);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_ERR_VAL, lowlevel_recv_cb(&conn_pool[0], &fake, NULL, PROTOCORE_NET_OK));
}

void test_recv_cb_refuses_a_segment_that_does_not_fit()
{
    protocore_pcb fake = {0};
    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    ConnPool.slot = 0;
    ConnPool.st = CONN_ACTIVE;
    ConnPool.set_state(ConnPool.internal);
    conn_pool[0].rx_head = RX_BUF_SIZE - 2;
    conn_pool[0].rx_tail = 0;
    conn_pool[0].last_activity_ms = 5;

    protocore_pbuf seg = {0};
    uint8_t payload[10] = {0};
    seg.payload = payload;
    seg.len = 10;
    seg.tot_len = 10;
    seg.next = NULL;

    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_ERR_MEM, lowlevel_recv_cb(&conn_pool[0], &fake, &seg, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_UINT32(5, conn_pool[0].last_activity_ms);
}

void test_recv_cb_accepts_and_copies_a_two_pbuf_segment()
{
    protocore_pcb fake = {0};
    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    conn_pool[0].listener_id = 0;
    ConnPool.slot = 0;
    ConnPool.st = CONN_ACTIVE;
    ConnPool.set_state(ConnPool.internal);
    conn_pool[0].rx_head = 0;
    conn_pool[0].rx_tail = 0;
    conn_pool[0].last_activity_ms = 0;
    set_now_ms(4242);

    uint8_t part1[3] = {'a', 'b', 'c'};
    uint8_t part2[2] = {'d', 'e'};
    protocore_pbuf seg2 = {0};
    seg2.payload = part2;
    seg2.len = 2;
    seg2.tot_len = 2;
    seg2.next = NULL;
    protocore_pbuf seg1 = {0};
    seg1.payload = part1;
    seg1.len = 3;
    seg1.tot_len = 5;
    seg1.next = &seg2;

    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, lowlevel_recv_cb(&conn_pool[0], &fake, &seg1, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_UINT32(4242, conn_pool[0].last_activity_ms);
    TEST_ASSERT_EQUAL(5u, (size_t)conn_pool[0].rx_head);

    uint8_t got[5];
    for (int i = 0; i < 5; i++)
    {
        got[i] = conn_pool[0].rx_buffer[i];
    }
    TEST_ASSERT_EQUAL_INT(0, memcmp("abcde", got, 5));

    uint8_t part3[1] = {'f'};
    protocore_pbuf seg3 = {0};
    seg3.payload = part3;
    seg3.len = 1;
    seg3.tot_len = 1;
    seg3.next = NULL;
    set_now_ms(5000);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, lowlevel_recv_cb(&conn_pool[0], &fake, &seg3, PROTOCORE_NET_OK));
}

void test_recv_cb_zero_clock_and_zero_length_segment_edge_cases()
{
    protocore_pcb fake = {0};
    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    conn_pool[0].listener_id = 0;
    ConnPool.slot = 0;
    ConnPool.st = CONN_ACTIVE;
    ConnPool.set_state(ConnPool.internal);
    conn_pool[0].rx_head = 0;
    conn_pool[0].rx_tail = 0;
    set_now_ms(0);

    uint8_t byte = 'z';
    protocore_pbuf seg = {0};
    seg.payload = &byte;
    seg.len = 1;
    seg.tot_len = 1;
    seg.next = NULL;
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, lowlevel_recv_cb(&conn_pool[0], &fake, &seg, PROTOCORE_NET_OK));

    protocore_pbuf empty_seg = {0};
    empty_seg.payload = NULL;
    empty_seg.len = 0;
    empty_seg.tot_len = 0;
    empty_seg.next = NULL;
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, lowlevel_recv_cb(&conn_pool[0], &fake, &empty_seg, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL(1u, (size_t)conn_pool[0].rx_head);
}

void test_sent_cb_null_active_and_closing()
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, lowlevel_sent_cb(NULL, NULL, 0));

    protocore_pcb fake = {0};
    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    ConnPool.slot = 0;
    ConnPool.st = CONN_ACTIVE;
    ConnPool.set_state(ConnPool.internal);
    conn_pool[0].last_activity_ms = 0;
    set_now_ms(777);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, lowlevel_sent_cb(&conn_pool[0], &fake, 10));
    TEST_ASSERT_EQUAL_UINT32(777, conn_pool[0].last_activity_ms);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);

    conn_pool[1].id = 1;
    conn_pool[1].pcb = &fake;
    ConnPool.slot = 1;
    ConnPool.st = CONN_CLOSING;
    ConnPool.set_state(ConnPool.internal);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, lowlevel_sent_cb(&conn_pool[1], &fake, 0));
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state);
}

void test_err_cb_null_active_and_closing()
{
    lowlevel_err_cb(NULL, PROTOCORE_NET_ERR_ABRT);

    protocore_pcb fake = {0};
    conn_pool[0].id = 0;
    conn_pool[0].pcb = &fake;
    ConnPool.slot = 0;
    ConnPool.st = CONN_ACTIVE;
    ConnPool.set_state(ConnPool.internal);
    lowlevel_err_cb(&conn_pool[0], PROTOCORE_NET_ERR_ABRT);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_NULL(conn_pool[0].pcb);

    conn_pool[1].id = 1;
    conn_pool[1].pcb = &fake;
    ConnPool.slot = 1;
    ConnPool.st = CONN_CLOSING;
    ConnPool.set_state(ConnPool.internal);
    lowlevel_err_cb(&conn_pool[1], PROTOCORE_NET_ERR_ABRT);
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[1].state);
    TEST_ASSERT_NULL(conn_pool[1].pcb);
}

void test_accept_cb_rejects_error_and_null_pcb()
{
    protocore_pcb fake = {0};
    ConnPool.alloc_free(ConnPool.internal);

    int32_t before = ConnPool.i32;

    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_ERR_VAL,
                          listener_accept_cb((void *)(uintptr_t)0, &fake, PROTOCORE_NET_ERR_ABRT));
    ConnPool.alloc_free(ConnPool.internal);

    TEST_ASSERT_EQUAL_INT32(before, ConnPool.i32);

    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_ERR_VAL, listener_accept_cb((void *)(uintptr_t)0, NULL, PROTOCORE_NET_OK));
    ConnPool.alloc_free(ConnPool.internal);

    TEST_ASSERT_EQUAL_INT32(before, ConnPool.i32);
}

void test_accept_cb_rejects_out_of_range_listener_idx()
{
    protocore_pcb fake = {0};
    int before_aborts = mock_abort_call_count();
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_ERR_VAL,
                          listener_accept_cb((void *)(uintptr_t)MAX_LISTENERS, &fake, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_INT(before_aborts, mock_abort_call_count());
}

void test_accept_cb_rejects_when_pool_full()
{
    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        ConnPool.slot = i;
        ConnPool.st = CONN_ACTIVE;
        ConnPool.set_state(ConnPool.internal);
    }
    ConnPool.alloc_free(ConnPool.internal);

    TEST_ASSERT_EQUAL_INT32(-1, ConnPool.i32);

    protocore_pcb fake = {0};
    int before_aborts = mock_abort_call_count();
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_ERR_ABRT, listener_accept_cb((void *)(uintptr_t)0, &fake, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_INT(before_aborts + 1, mock_abort_call_count());
}

void test_accept_cb_claims_slot_and_wires_connection()
{
    protocore_pcb fake = {0};
    set_now_ms(9001);

    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &fake, PROTOCORE_NET_OK));

    TcpConn *c = &conn_pool[0];
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)c->state);
    TEST_ASSERT_EQUAL_PTR(&fake, c->pcb);
    TEST_ASSERT_EQUAL_UINT32(9001, c->last_activity_ms);
    TEST_ASSERT_EQUAL_UINT32(0, http_req_start_ms[c->id]);
    TEST_ASSERT_EQUAL(0u, (size_t)c->rx_head);
    TEST_ASSERT_EQUAL(0u, (size_t)c->rx_tail);
    TEST_ASSERT_EQUAL_UINT8(0, c->listener_id);
    TEST_ASSERT_EQUAL_INT((int)PROTO_HTTP, (int)c->proto);

    TEST_ASSERT_EQUAL_UINT32(0, protocore_ap_ip);
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_IF_WIFI_STA, (int)c->iface);
    TEST_ASSERT_EQUAL_UINT8(0, c->tls);
}

void test_accept_cb_classifies_the_ap_interface()
{
    protocore_pcb fake;
    memset(&fake, 0, sizeof(fake));
    protocore_net_ip4_set(&fake.local_ip, 192, 168, 4, 1);
    protocore_ap_ip = protocore_net_ip4_u32(protocore_net_ip_as_v4(&fake.local_ip));

    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &fake, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_IF_WIFI_AP, (int)conn_pool[0].iface);

    protocore_ap_ip = 0;
}

void test_accept_cb_second_accept_claims_a_different_slot()
{
    protocore_pcb fake1 = {0}, fake2 = {0};
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &fake1, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &fake2, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_PTR(&fake1, conn_pool[0].pcb);
    TEST_ASSERT_EQUAL_PTR(&fake2, conn_pool[1].pcb);
}

void test_accept_cb_survives_a_failed_enqueue()
{
    listener_pool[0].active = PROTO_FALSE;
    protocore_pcb fake = {0};
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &fake, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL_PTR(&fake, conn_pool[0].pcb);
}

int main()
{
    UNITY_BEGIN();

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

    RUN_TEST(stress_ring_buffer_fill_drain_integrity);
    RUN_TEST(stress_ring_buffer_multi_cycle_no_corruption);
    RUN_TEST(stress_all_slots_timeout_simultaneously);
    RUN_TEST(stress_timeout_arm_recover_cycle);
    RUN_TEST(stress_check_timeouts_high_call_rate);
    RUN_TEST(stress_ring_buffer_byte_by_byte_fill_and_drain);

    RUN_TEST(test_accept_throttle_blocks_over_budget);
    RUN_TEST(test_accept_throttle_window_refills);
    RUN_TEST(test_accept_throttle_handles_rollover);

    RUN_TEST(test_per_ip_throttle_blocks_over_budget);
    RUN_TEST(test_per_ip_throttle_isolates_addresses);
    RUN_TEST(test_per_ip_throttle_window_refills);
    RUN_TEST(test_per_ip_throttle_evicts_when_full);
    RUN_TEST(test_per_ip_throttle_zero_ip_always_allowed);
    RUN_TEST(test_per_ip_throttle_v6_distinct);
    RUN_TEST(test_per_ip_throttle_handles_rollover);
    RUN_TEST(test_per_ip_throttle_scans_expired_and_lru_across_a_full_table);

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
    RUN_TEST(test_recv_cb_null_arg_and_closing_reset);
    RUN_TEST(test_recv_cb_fin_close_falls_back_to_abort_on_tcp_close_failure);
    RUN_TEST(test_recv_cb_fin_close_ordinary_path_does_not_abort);
    RUN_TEST(test_recv_cb_rejects_non_active_slot);
    RUN_TEST(test_recv_cb_refuses_a_segment_that_does_not_fit);
    RUN_TEST(test_recv_cb_accepts_and_copies_a_two_pbuf_segment);
    RUN_TEST(test_recv_cb_zero_clock_and_zero_length_segment_edge_cases);
    RUN_TEST(test_sent_cb_null_active_and_closing);
    RUN_TEST(test_err_cb_null_active_and_closing);

    RUN_TEST(test_accept_cb_rejects_error_and_null_pcb);
    RUN_TEST(test_accept_cb_rejects_out_of_range_listener_idx);
    RUN_TEST(test_accept_cb_rejects_when_pool_full);
    RUN_TEST(test_accept_cb_claims_slot_and_wires_connection);
    RUN_TEST(test_accept_cb_classifies_the_ap_interface);
    RUN_TEST(test_accept_cb_second_accept_claims_a_different_slot);
    RUN_TEST(test_accept_cb_survives_a_failed_enqueue);

    return UNITY_END();
}
