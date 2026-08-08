// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Phase 2 core-partitioning invariant (built with PC_WORKER_COUNT=2): a worker
// only ever touches the connection slots it owns. The idle-timeout sweep is the
// one place a worker writes slot state from its own task, so it must reap ONLY its
// owned slots - otherwise two workers could write the same slot. The per-worker
// event-queue routing is checked here: the host queue is keyed by the handle create
// returns, so a post lands in one worker's queue and not another's.

#include "network_drivers/session/worker.h"
#include "network_drivers/transport/tcp.h"
#include <Arduino.h> // set_millis
#include <unity.h>

void setUp(void)
{
    Tcp.conn->init(NULL);
}
void tearDown(void)
{
}

void test_worker_count_is_two(void)
{
    TEST_ASSERT_EQUAL_INT(2, pc_worker_count());
}

void test_check_timeouts_reaps_only_owned_slots(void)
{
    set_millis(100000); // far past CONN_TIMEOUT_MS so both slots are stale

    conn_pool[0].owner = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].pcb = NULL; // no pcb -> sweep frees the slot without a tcp_abort
    conn_pool[0].last_activity_ms = 0;

    conn_pool[1].owner = 1;
    conn_pool[1].state = CONN_ACTIVE;
    conn_pool[1].pcb = NULL;
    conn_pool[1].last_activity_ms = 0;

    // Worker 0 sweeps: only its own slot is reaped; worker 1's slot is untouched.
    Tcp.conn->check_timeouts(0);
    TEST_ASSERT_EQUAL_INT(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL_INT(CONN_ACTIVE, (ConnState)conn_pool[1].state);

    // Worker 1 sweeps: now its own slot is reaped.
    Tcp.conn->check_timeouts(1);
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
    // pc_worker_set_self binds the calling context's worker id; pc_worker_self reads it back.
    pc_worker_set_self(1);
    TEST_ASSERT_EQUAL_INT(1, pc_worker_self());
    pc_worker_set_self(0);
    TEST_ASSERT_EQUAL_INT(0, pc_worker_self());
}

// start() raises the run flag and creates the per-worker queues; stop() lowers it. The task the
// platform starts does not run here, so the pump is whatever the caller drives afterwards.
void test_worker_lifecycle_raises_and_lowers_the_run_flag(void)
{
    TEST_ASSERT_FALSE(Workers.running());
    Workers.start(NULL);
    TEST_ASSERT_TRUE(Workers.running());
    Workers.start(NULL); // idempotent: a second start does not restack the tasks
    TEST_ASSERT_TRUE(Workers.running());
    Workers.wake(0);  // a bound worker
    Workers.wake(-1); // out of range, no crash
    Workers.wake(PC_WORKER_COUNT);
    Workers.stop();
    TEST_ASSERT_FALSE(Workers.running());
    Workers.stop(); // idempotent
    TEST_ASSERT_FALSE(Workers.running());
}

static void set_flag_to_42(void *arg)
{
    *(int *)arg = 42;
}

// defer() hands the callback to the owning worker's queue; run_deferred() is what runs it, in the
// worker's own context. The queue exists only once start() has created it.
void test_defer_queues_and_run_deferred_runs_it(void)
{
    int flag = 0;
    Workers.start(NULL); // the queues are created here, and outlive a stop

    TEST_ASSERT_FALSE(Workers.defer(0, NULL, NULL));             // a null callback is refused
    TEST_ASSERT_FALSE(Workers.defer(-1, set_flag_to_42, &flag)); // and an id outside the pool
    TEST_ASSERT_FALSE(Workers.defer(PC_WORKER_COUNT, set_flag_to_42, &flag));

    TEST_ASSERT_TRUE(Workers.defer(0, set_flag_to_42, &flag));
    TEST_ASSERT_EQUAL_INT(0, flag); // queued, not yet run
    Workers.run_deferred(0);
    TEST_ASSERT_EQUAL_INT(42, flag);

    // Draining twice does not re-run it, and a drain on an id outside the pool is a no-op.
    flag = 0;
    Workers.run_deferred(0);
    Workers.run_deferred(-1);
    TEST_ASSERT_EQUAL_INT(0, flag);

    Workers.stop();
}

// The per-worker event-queue table (PC_WORKER_COUNT > 1 only): created idempotently,
// looked up by worker id, out-of-range ids (negative or >= count) report no queue.
void test_listener_worker_queues_init_and_lookup(void)
{
    Tcp.listener->worker_queues_init();
    TEST_ASSERT_NOT_NULL(Tcp.listener->worker_queue(0));
    TEST_ASSERT_NOT_NULL(Tcp.listener->worker_queue(1));
    TEST_ASSERT_NULL(Tcp.listener->worker_queue(-1));
    TEST_ASSERT_NULL(Tcp.listener->worker_queue(PC_WORKER_COUNT));

    Tcp.listener->worker_queues_init(); // idempotent: a second call must not crash or reset queues
    TEST_ASSERT_NOT_NULL(Tcp.listener->worker_queue(0));
}

// Tcp.listener->enqueue() in multi-worker mode routes by the event's slot owner (not the
// listener id) and rejects an out-of-range owner before touching any queue.
void test_enqueue_routes_by_slot_owner_and_rejects_bad_owner(void)
{
    Tcp.listener->worker_queues_init();
    Tcp.conn->init(NULL);

    conn_pool[0].owner = 1; // route to worker 1's queue
    TcpEvt evt = {EVT_DATA, 0, 0};
    TEST_ASSERT_TRUE(Tcp.listener->enqueue(0, &evt)); // listener_id is ignored in multi-worker mode

    conn_pool[0].owner = PC_WORKER_COUNT; // out-of-range owner -> rejected
    TEST_ASSERT_FALSE(Tcp.listener->enqueue(0, &evt));

    conn_pool[0].owner = 0;
    mock_queue_send_fail_once();
    TEST_ASSERT_FALSE(Tcp.listener->enqueue(0, &evt)); // full queue reported, not silently dropped
}

// listener_accept_cb() round-robins each new connection's owner across the workers
// (PC_WORKER_COUNT > 1 only) so slots partition evenly, wrapping back to 0.
void test_accept_cb_round_robins_slot_owner(void)
{
    Tcp.conn->init(NULL);
    TEST_ASSERT_EQUAL_INT32(1, Tcp.listener->add(0, 80, PROTO_HTTP, PROTO_FALSE)); // also exercises the
                                                                                   // WORKER_COUNT>1 branch
                                                                                   // of Tcp.listener->add() itself
    pc_pcb pcb1 = {0}, pcb2 = {0}, pcb3 = {0};
    TEST_ASSERT_EQUAL_INT(PC_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb1, PC_NET_OK));
    TEST_ASSERT_EQUAL_INT(PC_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb2, PC_NET_OK));
    TEST_ASSERT_EQUAL_INT(PC_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb3, PC_NET_OK));
    // Three accepts across 2 workers: owners cycle 0,1,0 (exact slot indices aren't asserted -
    // only that a full round-robin cycle, including the wrap back to 0, actually ran).
    TEST_ASSERT_TRUE(conn_pool[0].owner <= 1);
    TEST_ASSERT_TRUE(conn_pool[1].owner <= 1);
    TEST_ASSERT_TRUE(conn_pool[2].owner <= 1);
    TEST_ASSERT_NOT_EQUAL(conn_pool[0].owner, conn_pool[1].owner);
    TEST_ASSERT_EQUAL_UINT8(conn_pool[0].owner, conn_pool[2].owner); // wrapped back to the first owner
    Tcp.listener->stop(0);
}

// Tcp.listener->add_dynamic() also creates the per-worker queues (idempotent with the static
// Tcp.listener->add() path above).
void test_dynamic_listener_creates_worker_queues(void)
{
    Tcp.conn->init(NULL);
    TEST_ASSERT_EQUAL_INT32(1, Tcp.listener->add_dynamic(2, 4444, PROTO_HTTP));
    TEST_ASSERT_NOT_NULL(Tcp.listener->worker_queue(0));
    TEST_ASSERT_NOT_NULL(Tcp.listener->worker_queue(1));
    Tcp.listener->stop_dynamic(2);
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
