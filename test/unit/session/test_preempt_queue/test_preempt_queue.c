// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the preempting work queue (services/system/preempt_queue) host core: the
// fixed ring's order (FIFO), urgent-to-front, fail-closed-when-full, high-water,
// and the drain/handler dispatch. The post and the drain run here through the keyed
// queue mock; only the interrupt itself and the preempt latency need hardware.

#include "network_drivers/session/preempt_queue.h"
// memcpy
#include <string.h>
#include <unity.h>

static uint32_t g_seen[256];
static size_t g_seen_n;
static uint32_t g_seen_dma[256]; // items drained on the internal DMA lane
static size_t g_seen_dma_n;

static void on_item(const void *item, void *ctx)
{
    (void)ctx;
    uint32_t v;
    memcpy(&v, item, sizeof(v));
    if (g_seen_n < 256)
    {
        g_seen[g_seen_n++] = v;
    }
}

static void on_item_dma(const void *item, void *ctx)
{
    (void)ctx;
    uint32_t v;
    memcpy(&v, item, sizeof(v));
    if (g_seen_dma_n < 256)
    {
        g_seen_dma[g_seen_dma_n++] = v;
    }
}

static proto_bool post_u32(uint32_t v)
{
    return pc_pq_post(&v, 0);
}

static void stop_all_lanes()
{
    for (int l = 0; l < (int)PC_PQ_LANE_COUNT; l++)
    {
        PreemptQueue.stop((pc_pq_lane)l);
    }
}

void setUp()
{
    g_seen_n = 0;
    g_seen_dma_n = 0;
    stop_all_lanes();
    pc_pq_config cfg = {0};
    cfg.handler = on_item;
    cfg.ctx = NULL;
    cfg.priority = 5;
    cfg.core = 1;
    cfg.name = "test_pq";
    pc_pq_start(&cfg); // starts the USER lane (no-arg API)
}
void tearDown()
{
    stop_all_lanes();
}

void test_start_validates_and_runs()
{
    pc_pq_stop();
    TEST_ASSERT_FALSE(pc_pq_start(NULL)); // null config
    pc_pq_config bad = {0};
    bad.handler = NULL;
    TEST_ASSERT_FALSE(pc_pq_start(&bad)); // null handler
    pc_pq_config ok = {0};
    ok.handler = on_item;
    TEST_ASSERT_TRUE(pc_pq_start(&ok));
    TEST_ASSERT_TRUE(pc_pq_running());
    TEST_ASSERT_FALSE(pc_pq_start(&ok)); // double start is a no-op
}

void test_fifo_order()
{
    TEST_ASSERT_TRUE(post_u32(10));
    TEST_ASSERT_TRUE(post_u32(20));
    TEST_ASSERT_TRUE(post_u32(30));
    pc_pq_drain();
    TEST_ASSERT_EQUAL_size_t(3, g_seen_n);
    TEST_ASSERT_EQUAL_UINT32(10, g_seen[0]);
    TEST_ASSERT_EQUAL_UINT32(20, g_seen[1]);
    TEST_ASSERT_EQUAL_UINT32(30, g_seen[2]);
}

void test_urgent_goes_to_front()
{
    post_u32(1);
    post_u32(2);
    uint32_t u = 99;
    TEST_ASSERT_TRUE(pc_pq_post_urgent(&u, 0));
    pc_pq_drain();
    TEST_ASSERT_EQUAL_size_t(3, g_seen_n);
    TEST_ASSERT_EQUAL_UINT32(99, g_seen[0]); // urgent first
    TEST_ASSERT_EQUAL_UINT32(1, g_seen[1]);
    TEST_ASSERT_EQUAL_UINT32(2, g_seen[2]);
}

void test_fail_closed_when_full()
{
    // The test env sizes PC_PQ_DEPTH = 4.
    for (uint32_t i = 0; i < PC_PQ_DEPTH; i++)
    {
        TEST_ASSERT_TRUE(post_u32(i));
    }
    TEST_ASSERT_FALSE(post_u32(999)); // full -> dropped, not blocked
    pc_pq_drain();
    TEST_ASSERT_EQUAL_size_t(PC_PQ_DEPTH, g_seen_n);
}

void test_high_water_tracks_peak()
{
    post_u32(1);
    post_u32(2);
    post_u32(3);
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(3, pc_pq_high_water());
    pc_pq_drain();
    // peak persists after draining
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(3, pc_pq_high_water());
}

void test_from_isr_enqueues()
{
    uint32_t v = 7;
    TEST_ASSERT_TRUE(pc_pq_post_from_isr(&v));
    pc_pq_drain();
    TEST_ASSERT_EQUAL_size_t(1, g_seen_n);
    TEST_ASSERT_EQUAL_UINT32(7, g_seen[0]);
}

void test_drain_empties_and_reuses()
{
    post_u32(1);
    pc_pq_drain();
    g_seen_n = 0;
    pc_pq_drain(); // empty: no-op
    TEST_ASSERT_EQUAL_size_t(0, g_seen_n);
    // ring wraps cleanly after a drain
    for (uint32_t i = 0; i < PC_PQ_DEPTH; i++)
    {
        TEST_ASSERT_TRUE(post_u32(100 + i));
    }
    pc_pq_drain();
    TEST_ASSERT_EQUAL_size_t(PC_PQ_DEPTH, g_seen_n);
    TEST_ASSERT_EQUAL_UINT32(100, g_seen[0]);
}

// --- Named-lane tests -----------------------------------------------------------------

void test_internal_lanes_outrank_user()
{
    // DMA highest, then forward, then device, all above the user lane.
    TEST_ASSERT_GREATER_THAN_UINT8(PreemptQueue.priority(PC_PQ_LANE_FORWARD), PreemptQueue.priority(PC_PQ_LANE_DMA));
    TEST_ASSERT_GREATER_THAN_UINT8(PreemptQueue.priority(PC_PQ_LANE_DEVICE), PreemptQueue.priority(PC_PQ_LANE_FORWARD));
    TEST_ASSERT_GREATER_THAN_UINT8(PreemptQueue.priority(PC_PQ_LANE_USER), PreemptQueue.priority(PC_PQ_LANE_DEVICE));
}

void test_lanes_are_isolated()
{
    // The USER lane is already started by setUp; start the internal DMA lane too.
    pc_pq_config dma = {0};
    dma.handler = on_item_dma;
    dma.core = 1;
    TEST_ASSERT_TRUE(PreemptQueue.start(PC_PQ_LANE_DMA, &dma));

    uint32_t u = 11, d = 22;
    TEST_ASSERT_TRUE(pc_pq_post(&u, 0));                        // -> USER
    TEST_ASSERT_TRUE(PreemptQueue.post(PC_PQ_LANE_DMA, &d, 0)); // -> DMA

    // Draining one lane must not touch the other's queue or handler.
    PreemptQueue.drain(PC_PQ_LANE_DMA);
    TEST_ASSERT_EQUAL_size_t(0, g_seen_n);
    TEST_ASSERT_EQUAL_size_t(1, g_seen_dma_n);
    TEST_ASSERT_EQUAL_UINT32(22, g_seen_dma[0]);

    pc_pq_drain(); // USER
    TEST_ASSERT_EQUAL_size_t(1, g_seen_n);
    TEST_ASSERT_EQUAL_UINT32(11, g_seen[0]);
}

void test_lane_start_stop_running_independent()
{
    TEST_ASSERT_TRUE(PreemptQueue.running(PC_PQ_LANE_USER)); // setUp started it
    TEST_ASSERT_FALSE(PreemptQueue.running(PC_PQ_LANE_DMA));

    pc_pq_config dma = {0};
    dma.handler = on_item_dma;
    TEST_ASSERT_TRUE(PreemptQueue.start(PC_PQ_LANE_DMA, &dma));
    TEST_ASSERT_TRUE(PreemptQueue.running(PC_PQ_LANE_DMA));
    TEST_ASSERT_FALSE(PreemptQueue.start(PC_PQ_LANE_DMA, &dma)); // double start is a no-op

    PreemptQueue.stop(PC_PQ_LANE_DMA);
    TEST_ASSERT_FALSE(PreemptQueue.running(PC_PQ_LANE_DMA));
    TEST_ASSERT_TRUE(PreemptQueue.running(PC_PQ_LANE_USER)); // USER unaffected
}

void test_lane_high_water_is_per_lane()
{
    pc_pq_config dma = {0};
    dma.handler = on_item_dma;
    TEST_ASSERT_TRUE(PreemptQueue.start(PC_PQ_LANE_DMA, &dma));
    uint32_t v = 5;
    PreemptQueue.post(PC_PQ_LANE_DMA, &v, 0);
    PreemptQueue.post(PC_PQ_LANE_DMA, &v, 0);
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(2, PreemptQueue.high_water(PC_PQ_LANE_DMA));
    TEST_ASSERT_EQUAL_size_t(0, PreemptQueue.high_water(PC_PQ_LANE_DEVICE)); // untouched lane
}

void test_lane_api_urgent_and_drain()
{
    stop_all_lanes();
    pc_pq_config cfg = {0};
    cfg.handler = on_item_dma;
    TEST_ASSERT_TRUE(PreemptQueue.start(PC_PQ_LANE_DMA, &cfg));
    uint32_t a = 10, b = 20;
    TEST_ASSERT_TRUE(PreemptQueue.post(PC_PQ_LANE_DMA, &a, 0));
    TEST_ASSERT_TRUE(PreemptQueue.post_urgent(PC_PQ_LANE_DMA, &b, 0)); // urgent -> jumps the queue
    PreemptQueue.drain(PC_PQ_LANE_DMA);
    TEST_ASSERT_EQUAL_UINT32(2u, (uint32_t)g_seen_dma_n);
    TEST_ASSERT_EQUAL_UINT32(20u, g_seen_dma[0]); // urgent item first
    TEST_ASSERT_EQUAL_UINT32(10u, g_seen_dma[1]);
    // Guards: urgent-post to a bad lane / with a null item fails closed; drain of a bad lane is a no-op.
    TEST_ASSERT_FALSE(PreemptQueue.post_urgent((pc_pq_lane)PC_PQ_LANE_COUNT, &a, 0));
    TEST_ASSERT_FALSE(PreemptQueue.post_urgent(PC_PQ_LANE_DMA, NULL, 0));
    PreemptQueue.drain((pc_pq_lane)PC_PQ_LANE_COUNT);
    PreemptQueue.stop(PC_PQ_LANE_DMA);
}

void test_lane_guards_reject_bad_lane_and_null_item()
{
    // A bad lane (>= PC_PQ_LANE_COUNT) must fail closed / return safe defaults on every
    // lane-scoped entry point, and a null item must be rejected on the plain post path
    // (mirrors the already-covered null-item guard on the urgent post path).
    pc_pq_lane bad = (pc_pq_lane)PC_PQ_LANE_COUNT;
    pc_pq_config cfg = {0};
    cfg.handler = on_item_dma;
    TEST_ASSERT_FALSE(PreemptQueue.start(bad, &cfg));
    uint32_t v = 1;
    TEST_ASSERT_FALSE(PreemptQueue.post(bad, &v, 0));
    TEST_ASSERT_FALSE(PreemptQueue.running(bad));
    TEST_ASSERT_EQUAL_size_t(0, PreemptQueue.high_water(bad));
    PreemptQueue.stop(bad); // must not crash; no state to change

    TEST_ASSERT_FALSE(PreemptQueue.post(PC_PQ_LANE_FORWARD, NULL, 0));
}

void test_post_lane_urgent_fails_closed_when_full()
{
    stop_all_lanes();
    pc_pq_config cfg = {0};
    cfg.handler = on_item_dma;
    TEST_ASSERT_TRUE(PreemptQueue.start(PC_PQ_LANE_DMA, &cfg));
    for (uint32_t i = 0; i < PC_PQ_DEPTH; i++)
    {
        TEST_ASSERT_TRUE(PreemptQueue.post(PC_PQ_LANE_DMA, &i, 0));
    }
    uint32_t urgent = 999;
    TEST_ASSERT_FALSE(PreemptQueue.post_urgent(PC_PQ_LANE_DMA, &urgent, 0)); // full -> dropped, not bumped in
    PreemptQueue.drain(PC_PQ_LANE_DMA);
    TEST_ASSERT_EQUAL_size_t(PC_PQ_DEPTH, g_seen_dma_n);
    PreemptQueue.stop(PC_PQ_LANE_DMA);
}

void test_drain_lane_without_handler_skips_call_safely()
{
    // FORWARD is never started elsewhere in this suite, so its handler stays null. The host
    // post_lane() doesn't require the lane to be 'started', so an item can still be queued
    // directly; draining it must skip the callback instead of invoking a null handler.
    uint32_t v = 42;
    TEST_ASSERT_TRUE(PreemptQueue.post(PC_PQ_LANE_FORWARD, &v, 0));
    PreemptQueue.drain(PC_PQ_LANE_FORWARD); // must not crash with a null handler
    TEST_ASSERT_EQUAL_size_t(0, g_seen_n);
    TEST_ASSERT_EQUAL_size_t(0, g_seen_dma_n);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_start_validates_and_runs);
    RUN_TEST(test_fifo_order);
    RUN_TEST(test_urgent_goes_to_front);
    RUN_TEST(test_fail_closed_when_full);
    RUN_TEST(test_high_water_tracks_peak);
    RUN_TEST(test_from_isr_enqueues);
    RUN_TEST(test_drain_empties_and_reuses);
    RUN_TEST(test_internal_lanes_outrank_user);
    RUN_TEST(test_lanes_are_isolated);
    RUN_TEST(test_lane_start_stop_running_independent);
    RUN_TEST(test_lane_high_water_is_per_lane);
    RUN_TEST(test_lane_api_urgent_and_drain);
    RUN_TEST(test_lane_guards_reject_bad_lane_and_null_item);
    RUN_TEST(test_post_lane_urgent_fails_closed_when_full);
    RUN_TEST(test_drain_lane_without_handler_skips_call_safely);
    return UNITY_END();
}
