// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/session/session.h"
#include "network_drivers/transport/tcp/common.h"
#include "network_drivers/transport/tcp/protocol/protocol.h"
#include "network_drivers/transport/tcp/tcp.h"
#include "protocore.h"
#include "server/core/worker.h"
#include <unity.h>

static int g_ran = 0;
static void inc(void *arg)
{
    (*(int *)arg)++;
}

void setUp()
{
    ConnPool.life.cfg = NULL;
    ConnPool.init(ConnPool.internal);
    g_ran = 0;
    Workers.pump = NULL;
    Workers.start(Workers.internal);
    Workers.ok;
}
void tearDown()
{
}

void test_defer_queues_and_the_drain_runs_it_once()
{
    Workers.worker_id = 0;
    Workers.defer_args.fn = inc;
    Workers.defer_args.arg = &g_ran;
    Workers.defer(Workers.internal);
    TEST_ASSERT_TRUE(Workers.ok);
    TEST_ASSERT_EQUAL_INT(0, g_ran);
    Workers.worker_id = 0;
    Workers.run_deferred(Workers.internal);
    TEST_ASSERT_EQUAL_INT(1, g_ran);
    Workers.worker_id = 0;
    Workers.run_deferred(Workers.internal);
    TEST_ASSERT_EQUAL_INT(1, g_ran);
}

void test_server_defer_routes_by_owner()
{
    conn_pool[1].owner = 0;
    TEST_ASSERT_TRUE(defer(1, inc, &g_ran));
    Workers.worker_id = 0;
    Workers.run_deferred(Workers.internal);
    TEST_ASSERT_EQUAL_INT(1, g_ran);
    TEST_ASSERT_FALSE(defer(MAX_CONNS, inc, &g_ran));
}

void test_defer_null_fn_fails()
{

    Workers.worker_id = 0;
    Workers.defer_args.fn = NULL;
    Workers.defer_args.arg = NULL;
    Workers.defer(Workers.internal);
    TEST_ASSERT_FALSE(Workers.ok);
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
