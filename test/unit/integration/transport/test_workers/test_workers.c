// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/transport/tcp/common.h"
#include "network_drivers/transport/tcp/protocol/protocol.h"
#include "network_drivers/transport/tcp/server/server.h"
#include "network_drivers/transport/tcp/tcp.h"
#include "server/clock/clock.h" // Clock.ms: the stamp check_timeouts judges against
#include "server/core/worker.h"
#include <Arduino.h>
#include <unity.h>

void setUp(void)
{
    ConnPool.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(protocore_conn_pool_span());
}
void tearDown(void)
{
}

void test_worker_count_is_two(void)
{
    TEST_ASSERT_EQUAL_INT(2, protocore_worker_count());
}

void test_check_timeouts_reaps_only_owned_slots(void)
{
    set_millis(100000);

    conn_pool[0].owner = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = NULL;
    conn_pool[0].last_activity_ms = 0;

    conn_pool[1].owner = 1;
    conn_pool[1].state = CONN_ACTIVE;
    conn_pool[1].pcb = NULL;
    conn_pool[1].last_activity_ms = 0;

    ConnPool.life.worker_id = 0;
    Clock.ms = CONN_TIMEOUT_MS + 1u; // the slots were stamped at 0, so this makes them stale
    ConnPool.check_timeouts(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_INT(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL_INT(CONN_ACTIVE, (ConnState)conn_pool[1].state);

    ConnPool.life.worker_id = 1;
    Clock.ms = CONN_TIMEOUT_MS + 1u; // the slots were stamped at 0, so this makes them stale
    ConnPool.check_timeouts(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_INT(CONN_FREE, (ConnState)conn_pool[1].state);
}

void test_pool_init_defaults_owner_zero(void)
{
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(0, conn_pool[i].owner);
    }
}

void test_worker_self_id_roundtrip(void)
{

    protocore_worker_set_self(1);
    TEST_ASSERT_EQUAL_INT(1, protocore_worker_self());
    protocore_worker_set_self(0);
    TEST_ASSERT_EQUAL_INT(0, protocore_worker_self());
}

void test_worker_lifecycle_raises_and_lowers_the_run_flag(void)
{
    Workers.running(protocore_worker_span());
    TEST_ASSERT_FALSE(Workers.ok);
    Workers.pump = NULL;
    Workers.start(protocore_worker_span());
    Workers.running(protocore_worker_span());
    TEST_ASSERT_TRUE(Workers.ok);
    Workers.pump = NULL;
    Workers.start(protocore_worker_span());
    Workers.running(protocore_worker_span());
    TEST_ASSERT_TRUE(Workers.ok);
    Workers.worker_id = 0;
    Workers.wake(protocore_worker_span());
    Workers.worker_id = -1;
    Workers.wake(protocore_worker_span());
    Workers.worker_id = PROTOCORE_WORKER_COUNT;
    Workers.wake(protocore_worker_span());
    Workers.stop(protocore_worker_span());
    Workers.running(protocore_worker_span());
    TEST_ASSERT_FALSE(Workers.ok);
    Workers.stop(protocore_worker_span());
    Workers.running(protocore_worker_span());
    TEST_ASSERT_FALSE(Workers.ok);
}

static void set_flag_to_42(void *arg)
{
    *(int *)arg = 42;
}

void test_defer_queues_and_run_deferred_runs_it(void)
{
    int flag = 0;
    Workers.pump = NULL;
    Workers.start(protocore_worker_span());

    Workers.worker_id = 0;
    Workers.defer_args.fn = NULL;
    Workers.defer_args.arg = NULL;
    Workers.defer(protocore_worker_span());
    TEST_ASSERT_FALSE(Workers.ok);
    Workers.worker_id = -1;
    Workers.defer_args.fn = set_flag_to_42;
    Workers.defer_args.arg = &flag;
    Workers.defer(protocore_worker_span());
    TEST_ASSERT_FALSE(Workers.ok);
    Workers.worker_id = PROTOCORE_WORKER_COUNT;
    Workers.defer_args.fn = set_flag_to_42;
    Workers.defer_args.arg = &flag;
    Workers.defer(protocore_worker_span());
    TEST_ASSERT_FALSE(Workers.ok);

    Workers.worker_id = 0;
    Workers.defer_args.fn = set_flag_to_42;
    Workers.defer_args.arg = &flag;
    Workers.defer(protocore_worker_span());
    TEST_ASSERT_TRUE(Workers.ok);
    TEST_ASSERT_EQUAL_INT(0, flag);
    Workers.worker_id = 0;
    Workers.run_deferred(protocore_worker_span());
    TEST_ASSERT_EQUAL_INT(42, flag);

    flag = 0;
    Workers.worker_id = 0;
    Workers.run_deferred(protocore_worker_span());
    Workers.worker_id = -1;
    Workers.run_deferred(protocore_worker_span());
    TEST_ASSERT_EQUAL_INT(0, flag);

    Workers.stop(protocore_worker_span());
}

void test_listener_worker_queues_init_and_lookup(void)
{
    TcpListener.worker_queues_init(protocore_tcp_listener_span());
    TcpListener.q.worker_id = 0;
    TcpListener.worker_queue(protocore_tcp_listener_span());
    TEST_ASSERT_NOT_NULL(TcpListener.queue);
    TcpListener.q.worker_id = 1;
    TcpListener.worker_queue(protocore_tcp_listener_span());
    TEST_ASSERT_NOT_NULL(TcpListener.queue);
    TcpListener.q.worker_id = -1;
    TcpListener.worker_queue(protocore_tcp_listener_span());
    TEST_ASSERT_NULL(TcpListener.queue);
    TcpListener.q.worker_id = PROTOCORE_WORKER_COUNT;
    TcpListener.worker_queue(protocore_tcp_listener_span());
    TEST_ASSERT_NULL(TcpListener.queue);

    TcpListener.worker_queues_init(protocore_tcp_listener_span());
    TcpListener.q.worker_id = 0;
    TcpListener.worker_queue(protocore_tcp_listener_span());
    TEST_ASSERT_NOT_NULL(TcpListener.queue);
}

void test_enqueue_routes_by_slot_owner_and_rejects_bad_owner(void)
{
    TcpListener.worker_queues_init(protocore_tcp_listener_span());
    ConnPool.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(protocore_conn_pool_span());

    conn_pool[0].owner = 1;
    TcpEvt evt = {EVT_DATA, 0, 0};
    TcpListener.idx = 0;
    TcpListener.q.evt = &evt;
    TcpListener.enqueue(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);

    conn_pool[0].owner = PROTOCORE_WORKER_COUNT;
    TcpListener.idx = 0;
    TcpListener.q.evt = &evt;
    TcpListener.enqueue(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);

    conn_pool[0].owner = 0;
    mock_queue_send_fail_once();
    TcpListener.idx = 0;
    TcpListener.q.evt = &evt;
    TcpListener.enqueue(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_accept_cb_round_robins_slot_owner(void)
{
    ConnPool.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(protocore_conn_pool_span());
    TcpListener.idx = 0;
    TcpListener.bind.port = 80;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(protocore_tcp_listener_span());
    TEST_ASSERT_EQUAL_INT32(1, TcpListener.i32);

    protocore_pcb pcb1 = {0}, pcb2 = {0}, pcb3 = {0};
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb1, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb2, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb3, PROTOCORE_NET_OK));

    TEST_ASSERT_TRUE(conn_pool[0].owner <= 1);
    TEST_ASSERT_TRUE(conn_pool[1].owner <= 1);
    TEST_ASSERT_TRUE(conn_pool[2].owner <= 1);
    TEST_ASSERT_NOT_EQUAL(conn_pool[0].owner, conn_pool[1].owner);
    TEST_ASSERT_EQUAL_UINT8(conn_pool[0].owner, conn_pool[2].owner);
    TcpListener.idx = 0;
    TcpListener.stop(protocore_tcp_listener_span());
}

void test_dynamic_listener_creates_worker_queues(void)
{
    ConnPool.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(protocore_conn_pool_span());
    TcpListener.bind.port = 2;
    TcpListener.bind.proto = 4444;
    TcpListener.bind.tls = PROTO_HTTP;
    TcpListener.add_dynamic(protocore_tcp_listener_span());
    TEST_ASSERT_EQUAL_INT32(1, TcpListener.i32);
    TcpListener.q.worker_id = 0;
    TcpListener.worker_queue(protocore_tcp_listener_span());
    TEST_ASSERT_NOT_NULL(TcpListener.queue);
    TcpListener.q.worker_id = 1;
    TcpListener.worker_queue(protocore_tcp_listener_span());
    TEST_ASSERT_NOT_NULL(TcpListener.queue);
    TcpListener.idx = 2;
    TcpListener.stop_dynamic(protocore_tcp_listener_span());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_worker_count_is_two);
    RUN_TEST(test_check_timeouts_reaps_only_owned_slots);
    RUN_TEST(test_pool_init_defaults_owner_zero);
    RUN_TEST(test_worker_self_id_roundtrip);
    RUN_TEST(test_worker_lifecycle_raises_and_lowers_the_run_flag);
    RUN_TEST(test_listener_worker_queues_init_and_lookup);
    RUN_TEST(test_enqueue_routes_by_slot_owner_and_rejects_bad_owner);
    RUN_TEST(test_accept_cb_round_robins_slot_owner);
    RUN_TEST(test_dynamic_listener_creates_worker_queues);
    RUN_TEST(test_defer_queues_and_run_deferred_runs_it);
    return UNITY_END();
}
