// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Phase 3a: the thread-safe app->worker deferred-callback path. defer() hands the callback to the
// owning worker's queue and run_deferred() runs it in that worker's own context, so the two are
// separate steps here as they are on the target. Covers the routing by slot owner and the
// fail-closed cases.

#include "network_drivers/session/worker.h"
#include "network_drivers/transport/tcp.h"
#include "protocore.h"
#include <unity.h>

static int g_ran = 0;
static void inc(void *arg)
{
    (*(int *)arg)++;
}

void setUp()
{
    Tcp.conn->init(NULL);
    g_ran = 0;
    Session.workers->start(NULL); // creates the per-worker queues; idempotent
}
void tearDown()
{
}

void test_defer_queues_and_the_drain_runs_it_once()
{
    TEST_ASSERT_TRUE(Session.workers->defer(0, inc, &g_ran));
    TEST_ASSERT_EQUAL_INT(0, g_ran); // queued, and the owning worker has not drained yet
    Session.workers->run_deferred(0);
    TEST_ASSERT_EQUAL_INT(1, g_ran);
    Session.workers->run_deferred(0); // the queue is empty now: no double-run
    TEST_ASSERT_EQUAL_INT(1, g_ran);
}

void test_server_defer_routes_by_owner()
{
    conn_pool[1].owner = 0;
    TEST_ASSERT_TRUE(defer(1, inc, &g_ran));
    Session.workers->run_deferred(0); // slot 1's owner is worker 0, so worker 0's drain runs it
    TEST_ASSERT_EQUAL_INT(1, g_ran);
    TEST_ASSERT_FALSE(defer(MAX_CONNS, inc, &g_ran)); // out-of-range slot fails closed
}

void test_defer_null_fn_fails()
{
    // A null callback fails closed on every build (host and target).
    TEST_ASSERT_FALSE(Session.workers->defer(0, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(0, g_ran);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_defer_queues_and_the_drain_runs_it_once);
    RUN_TEST(test_server_defer_routes_by_owner);
    RUN_TEST(test_defer_null_fn_fails);
    return UNITY_END();
}
