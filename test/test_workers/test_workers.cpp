// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Phase 2 core-partitioning invariant (built with PC_WORKER_COUNT=2): a worker
// only ever touches the connection slots it owns. The idle-timeout sweep is the
// one place a worker writes slot state from its own task, so it must reap ONLY its
// owned slots - otherwise two workers could write the same slot. The per-worker
// event-queue ROUTING is verified on hardware (the host FreeRTOS queue is a stub
// that ignores the queue handle).

#include "network_drivers/session/worker.h"
#include "network_drivers/transport/listener.h"
#include "network_drivers/transport/tcp.h"
#include <Arduino.h> // set_millis
#include <unity.h>

void setUp()
{
    DeterministicAsyncTCP::pool_init();
}
void tearDown()
{
}

void test_worker_count_is_two()
{
    TEST_ASSERT_EQUAL_INT(2, pc_worker_count());
}

void test_check_timeouts_reaps_only_owned_slots()
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
    DeterministicAsyncTCP::check_timeouts(0);
    TEST_ASSERT_EQUAL_INT(CONN_FREE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL_INT(CONN_ACTIVE, (ConnState)conn_pool[1].state);

    // Worker 1 sweeps: now its own slot is reaped.
    DeterministicAsyncTCP::check_timeouts(1);
    TEST_ASSERT_EQUAL_INT(CONN_FREE, (ConnState)conn_pool[1].state);
}

void test_pool_init_defaults_owner_zero()
{
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(0, conn_pool[i].owner);
    }
}

void test_worker_self_id_roundtrip()
{
    // pc_worker_set_self binds the calling context's worker id; pc_worker_self reads it back.
    pc_worker_set_self(1);
    TEST_ASSERT_EQUAL_INT(1, pc_worker_self());
    pc_worker_set_self(0);
    TEST_ASSERT_EQUAL_INT(0, pc_worker_self());
}

void test_host_worker_lifecycle_is_noops()
{
    // On host there is no worker task: start/stop/wake are no-ops and running() stays false.
    pc_workers_start(NULL);
    TEST_ASSERT_FALSE(pc_workers_running());
    pc_worker_wake(0);
    pc_workers_stop();
    TEST_ASSERT_FALSE(pc_workers_running());
}

static void set_flag_to_42(void *arg)
{
    *(int *)arg = 42;
}
void test_host_defer_runs_inline_and_rejects_null()
{
    // On host the caller and pipeline are the same thread, so pc_defer runs the callback inline
    // immediately; a null callback is rejected.
    TEST_ASSERT_FALSE(pc_defer(0, NULL, NULL));
    int flag = 0;
    TEST_ASSERT_TRUE(pc_defer(0, set_flag_to_42, &flag));
    TEST_ASSERT_EQUAL_INT(42, flag);
}

// The per-worker event-queue table (PC_WORKER_COUNT > 1 only): created idempotently,
// looked up by worker id, out-of-range ids (negative or >= count) report no queue.
void test_listener_worker_queues_init_and_lookup()
{
    listener_worker_queues_init();
    TEST_ASSERT_NOT_NULL(listener_worker_queue(0));
    TEST_ASSERT_NOT_NULL(listener_worker_queue(1));
    TEST_ASSERT_NULL(listener_worker_queue(-1));
    TEST_ASSERT_NULL(listener_worker_queue(PC_WORKER_COUNT));

    listener_worker_queues_init(); // idempotent: a second call must not crash or reset queues
    TEST_ASSERT_NOT_NULL(listener_worker_queue(0));
}

// listener_enqueue() in multi-worker mode routes by the event's slot owner (not the
// listener id) and rejects an out-of-range owner before touching any queue.
void test_enqueue_routes_by_slot_owner_and_rejects_bad_owner()
{
    listener_worker_queues_init();
    DeterministicAsyncTCP::pool_init();

    conn_pool[0].owner = 1; // route to worker 1's queue
    TcpEvt evt = {EVT_DATA, 0, 0};
    TEST_ASSERT_TRUE(listener_enqueue(0, &evt)); // listener_id is ignored in multi-worker mode

    conn_pool[0].owner = PC_WORKER_COUNT; // out-of-range owner -> rejected
    TEST_ASSERT_FALSE(listener_enqueue(0, &evt));

    conn_pool[0].owner = 0;
    mock_queue_send_fail_once() = true;
    TEST_ASSERT_FALSE(listener_enqueue(0, &evt)); // full queue reported, not silently dropped
}

// listener_accept_cb() round-robins each new connection's owner across the workers
// (PC_WORKER_COUNT > 1 only) so slots partition evenly, wrapping back to 0.
void test_accept_cb_round_robins_slot_owner()
{
    DeterministicAsyncTCP::pool_init();
    TEST_ASSERT_EQUAL_INT32(1, listener_add(0, 80, PROTO_HTTP)); // also exercises the
                                                                 // WORKER_COUNT>1 branch
                                                                 // of listener_add() itself
    struct tcp_pcb pcb1 = {}, pcb2 = {}, pcb3 = {};
    TEST_ASSERT_EQUAL_INT(ERR_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb1, ERR_OK));
    TEST_ASSERT_EQUAL_INT(ERR_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb2, ERR_OK));
    TEST_ASSERT_EQUAL_INT(ERR_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb3, ERR_OK));
    // Three accepts across 2 workers: owners cycle 0,1,0 (exact slot indices aren't asserted -
    // only that a full round-robin cycle, including the wrap back to 0, actually ran).
    TEST_ASSERT_TRUE(conn_pool[0].owner <= 1);
    TEST_ASSERT_TRUE(conn_pool[1].owner <= 1);
    TEST_ASSERT_TRUE(conn_pool[2].owner <= 1);
    TEST_ASSERT_NOT_EQUAL(conn_pool[0].owner, conn_pool[1].owner);
    TEST_ASSERT_EQUAL_UINT8(conn_pool[0].owner, conn_pool[2].owner); // wrapped back to the first owner
    listener_stop(0);
}

// listener_add_dynamic() also creates the per-worker queues (idempotent with the static
// listener_add() path above).
void test_dynamic_listener_creates_worker_queues()
{
    DeterministicAsyncTCP::pool_init();
    TEST_ASSERT_EQUAL_INT32(1, listener_add_dynamic(2, 4444, PROTO_HTTP));
    TEST_ASSERT_NOT_NULL(listener_worker_queue(0));
    TEST_ASSERT_NOT_NULL(listener_worker_queue(1));
    listener_stop_dynamic(2);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_worker_count_is_two);
    RUN_TEST(test_check_timeouts_reaps_only_owned_slots);
    RUN_TEST(test_pool_init_defaults_owner_zero);
    RUN_TEST(test_worker_self_id_roundtrip);
    RUN_TEST(test_host_worker_lifecycle_is_noops);
    RUN_TEST(test_listener_worker_queues_init_and_lookup);
    RUN_TEST(test_enqueue_routes_by_slot_owner_and_rejects_bad_owner);
    RUN_TEST(test_accept_cb_round_robins_slot_owner);
    RUN_TEST(test_dynamic_listener_creates_worker_queues);
    RUN_TEST(test_host_defer_runs_inline_and_rejects_null);
    return UNITY_END();
}
