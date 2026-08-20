// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/session/session.h"
#include "network_drivers/transport/tcp/common.h"
#include "network_drivers/transport/tcp/protocol/protocol.h"
#include "network_drivers/transport/tcp/tcp.h"
#include "protocore.h"
#include "server/core/worker/worker.h"
#include <unity.h>

static int g_ran = 0;
static void inc(void *arg)
{
    (*(int *)arg)++;
}

void setUp()
{
    ConnPoolV.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(protocore_conn_pool_span());
    g_ran = 0;
    WorkersV.pump = NULL;
    Workers.start(protocore_worker_span());
}
void tearDown()
{
}

void test_defer_queues_and_the_drain_runs_it_once()
{
    WorkersV.worker_id = 0;
    WorkersV.defer_args.fn = inc;
    WorkersV.defer_args.arg = &g_ran;
    Workers.defer(protocore_worker_span());
    TEST_ASSERT_TRUE(WorkersV.ok);
    TEST_ASSERT_EQUAL_INT(0, g_ran);
    WorkersV.worker_id = 0;
    Workers.run_deferred(protocore_worker_span());
    TEST_ASSERT_EQUAL_INT(1, g_ran);
    WorkersV.worker_id = 0;
    Workers.run_deferred(protocore_worker_span());
    TEST_ASSERT_EQUAL_INT(1, g_ran);
}

void test_server_defer_routes_by_owner()
{
    conn_pool[1].owner = 0;
    TEST_ASSERT_TRUE(defer(1, inc, &g_ran));
    WorkersV.worker_id = 0;
    Workers.run_deferred(protocore_worker_span());
    TEST_ASSERT_EQUAL_INT(1, g_ran);
    TEST_ASSERT_FALSE(defer(MAX_CONNS, inc, &g_ran));
}

void test_defer_null_fn_fails()
{

    WorkersV.worker_id = 0;
    WorkersV.defer_args.fn = NULL;
    WorkersV.defer_args.arg = NULL;
    Workers.defer(protocore_worker_span());
    TEST_ASSERT_FALSE(WorkersV.ok);
    TEST_ASSERT_EQUAL_INT(0, g_ran);
}
