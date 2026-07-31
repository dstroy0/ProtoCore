// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Phase 3a: the thread-safe app->worker deferred-callback path. On host there is
// no worker task, so a deferred callback runs inline immediately (same observable
// effect as a worker draining it). These tests cover the host inline path, the
// server.defer() owner routing, and the fail-closed cases.

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
    DeterministicAsyncTCP::pool_init();
    g_ran = 0;
}
void tearDown()
{
}

void test_defer_runs_inline_on_host()
{
    TEST_ASSERT_TRUE(pc_defer(0, inc, &g_ran));
    TEST_ASSERT_EQUAL_INT(1, g_ran); // executed inline (no worker task on host)
    pc_worker_run_deferred(0);       // no-op on host: must not double-run
    TEST_ASSERT_EQUAL_INT(1, g_ran);
}

void test_server_defer_routes_by_owner()
{
    PC srv;
    conn_pool[1].owner = 0;
    TEST_ASSERT_TRUE(srv.defer(1, inc, &g_ran));
    TEST_ASSERT_EQUAL_INT(1, g_ran);
    TEST_ASSERT_FALSE(srv.defer(MAX_CONNS, inc, &g_ran)); // out-of-range slot fails closed
}

void test_defer_null_fn_fails()
{
    // A null callback fails closed on every build (host and target).
    TEST_ASSERT_FALSE(pc_defer(0, nullptr, nullptr));
    TEST_ASSERT_EQUAL_INT(0, g_ran);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_defer_runs_inline_on_host);
    RUN_TEST(test_server_defer_routes_by_owner);
    RUN_TEST(test_defer_null_fn_fails);
    return UNITY_END();
}
