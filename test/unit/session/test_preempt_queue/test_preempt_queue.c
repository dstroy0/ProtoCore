// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the preempting work queue (network_drivers/session/preempt_queue): a lane's FIFO
// order, urgent-to-front, fail-closed-when-full, high-water, and the hand-off from a post to the
// lane task's handler.
//
// The DMA lane is driven end to end by the host DMA driver (test/mocks/pc_dma_host.h): a fed
// transfer completes where the ISR would fire, the completion callback copies the bytes onto the
// lane, and the lane's task does the work. That is the pipe mmgr/dma.h names as the intended
// pattern, so it is what the lane is tested through rather than a synthetic post.
//
// The env sizes PC_PQ_DEPTH = 4, PC_PQ_ITEM_SIZE = 16, PC_DMA_BUF_SIZE = 8, PC_DMA_CHANNELS = 2.

#include "../../../mocks/pc_dma_host.h"
#include "mmgr/dma.h"
#include "network_drivers/session/preempt_queue.h"
// memcpy
#include <string.h>
#include <unity.h>

// What a producer puts on a lane. A DMA completion runs in ISR context and the ping-pong buffer it
// points at is refilled a transfer or two later, so the item carries the bytes rather than the
// pointer (mmgr/dma.h).
typedef struct
{
    uint32_t v;                    // plain payload, and the completion sequence on a DMA item
    uint16_t len;                  // bytes the transfer moved
    uint8_t channel;               // the channel it completed on
    uint8_t dir;                   // pc_dma_dir
    uint8_t data[PC_DMA_BUF_SIZE]; // RX: the completed buffer's bytes; TX: zero
} PqItem;
static_assert(sizeof(PqItem) == PC_PQ_ITEM_SIZE, "a lane item carries one DMA completion whole");

#define SEEN_MAX 64

static PqItem g_seen[SEEN_MAX];
static size_t g_seen_n;
static PqItem g_seen_dma[SEEN_MAX]; // items drained on the internal DMA lane
static size_t g_seen_dma_n;

static void on_item(const void *item, void *ctx)
{
    (void)ctx;
    if (g_seen_n < SEEN_MAX)
    {
        memcpy(&g_seen[g_seen_n++], item, sizeof(PqItem));
    }
}

static void on_item_dma(const void *item, void *ctx)
{
    (void)ctx;
    if (g_seen_dma_n < SEEN_MAX)
    {
        memcpy(&g_seen_dma[g_seen_dma_n++], item, sizeof(PqItem));
    }
}

// The host has no scheduler, so a lane's task runs when this says so: the entry drains its own
// queue and unwinds when the queue reports empty. The lane is named at start, which is what picks
// out one task here - draining one lane must leave the others alone.
static void pump(const char *lane_task)
{
    (void)pc_platform_host_task_run_named(lane_task);
}

// The USER lane, whichever name started it: setUp names it, and the start-validation case takes
// preempt_queue.c's default.
static void pump_user(void)
{
    if (!pc_platform_host_task_run_named("test_pq"))
    {
        (void)pc_platform_host_task_run_named("pc_pq_user");
    }
}

static PqItem item_u32(uint32_t v)
{
    PqItem it = {0};
    it.v = v;
    return it;
}

static proto_bool post_u32(uint32_t v)
{
    PqItem it = item_u32(v);
    return pc_pq_post(&it, 0);
}

static void stop_all_lanes()
{
    for (int l = 0; l < (int)PC_PQ_LANE_COUNT; l++)
    {
        PreemptQueue.stop((pc_pq_lane)l);
    }
}

// --- The DMA channel that feeds the DMA lane ------------------------------------------

static size_t g_dma_posted;  // completions the callback handed to the lane
static size_t g_dma_dropped; // completions a full lane refused

// Where the ISR runs on silicon: copy the completed bytes out of the ping-pong buffer and post
// them to the DMA lane, so the work happens in the lane's task instead of in the interrupt.
static void on_dma_complete(const pc_dma_event *ev, void *ctx)
{
    (void)ctx;
    PqItem it = {0};
    it.v = ev->seq;
    it.len = ev->len;
    it.channel = ev->channel;
    it.dir = (uint8_t)ev->dir;
    if (ev->dir == PC_DMA_RX && ev->data != NULL)
    {
        uint16_t n = ev->len;
        if (n > (uint16_t)sizeof(it.data))
        {
            n = (uint16_t)sizeof(it.data);
        }
        memcpy(it.data, ev->data, n);
    }
    if (PreemptQueue.post_from_isr(PC_PQ_LANE_DMA, &it))
    {
        g_dma_posted++;
    }
    else
    {
        g_dma_dropped++;
    }
}

// The lane's task is what runs on_item_dma; the channel's completion callback is what posts to it.
static void open_dma_lane(uint8_t ch, proto_bool loopback)
{
    pc_pq_config lane = {0};
    lane.handler = on_item_dma;
    TEST_ASSERT_TRUE(PreemptQueue.start(PC_PQ_LANE_DMA, &lane));

    pc_dma_config cfg = {0};
    cfg.channel = ch;
    cfg.periph = PC_DMA_UART;
    cfg.loopback = loopback;
    cfg.on_complete = on_dma_complete;
    TEST_ASSERT_TRUE(pc_dma_open(&cfg));
}

void setUp()
{
    g_seen_n = 0;
    g_seen_dma_n = 0;
    g_dma_posted = 0;
    g_dma_dropped = 0;
    stop_all_lanes();
    queue_stage_reset(); // a lane's queue outlives its stop, so no case inherits another's backlog
    pc_dma_host_reset();
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
    pump_user();
    TEST_ASSERT_EQUAL_size_t(3, g_seen_n);
    TEST_ASSERT_EQUAL_UINT32(10, g_seen[0].v);
    TEST_ASSERT_EQUAL_UINT32(20, g_seen[1].v);
    TEST_ASSERT_EQUAL_UINT32(30, g_seen[2].v);
}

void test_urgent_goes_to_front()
{
    post_u32(1);
    post_u32(2);
    PqItem u = item_u32(99);
    TEST_ASSERT_TRUE(pc_pq_post_urgent(&u, 0));
    pump_user();
    TEST_ASSERT_EQUAL_size_t(3, g_seen_n);
    TEST_ASSERT_EQUAL_UINT32(99, g_seen[0].v); // urgent first
    TEST_ASSERT_EQUAL_UINT32(1, g_seen[1].v);
    TEST_ASSERT_EQUAL_UINT32(2, g_seen[2].v);
}

void test_fail_closed_when_full()
{
    // The test env sizes PC_PQ_DEPTH = 4.
    for (uint32_t i = 0; i < PC_PQ_DEPTH; i++)
    {
        TEST_ASSERT_TRUE(post_u32(i));
    }
    TEST_ASSERT_FALSE(post_u32(999)); // full -> dropped, not blocked
    pump_user();
    TEST_ASSERT_EQUAL_size_t(PC_PQ_DEPTH, g_seen_n);
}

void test_high_water_tracks_peak()
{
    post_u32(1);
    post_u32(2);
    post_u32(3);
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(3, pc_pq_high_water());
    pump_user();
    // peak persists after draining
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(3, pc_pq_high_water());
}

void test_from_isr_enqueues()
{
    PqItem v = item_u32(7);
    TEST_ASSERT_TRUE(pc_pq_post_from_isr(&v));
    pump_user();
    TEST_ASSERT_EQUAL_size_t(1, g_seen_n);
    TEST_ASSERT_EQUAL_UINT32(7, g_seen[0].v);
}

void test_drain_empties_and_reuses()
{
    post_u32(1);
    pump_user();
    g_seen_n = 0;
    pump_user(); // empty: no-op
    TEST_ASSERT_EQUAL_size_t(0, g_seen_n);
    // ring wraps cleanly after a drain
    for (uint32_t i = 0; i < PC_PQ_DEPTH; i++)
    {
        TEST_ASSERT_TRUE(post_u32(100 + i));
    }
    pump_user();
    TEST_ASSERT_EQUAL_size_t(PC_PQ_DEPTH, g_seen_n);
    TEST_ASSERT_EQUAL_UINT32(100, g_seen[0].v);
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

    PqItem u = item_u32(11), d = item_u32(22);
    TEST_ASSERT_TRUE(pc_pq_post(&u, 0));                        // -> USER
    TEST_ASSERT_TRUE(PreemptQueue.post(PC_PQ_LANE_DMA, &d, 0)); // -> DMA

    // Draining one lane must not touch the other's queue or handler.
    pump("pc_pq_dma");
    TEST_ASSERT_EQUAL_size_t(0, g_seen_n);
    TEST_ASSERT_EQUAL_size_t(1, g_seen_dma_n);
    TEST_ASSERT_EQUAL_UINT32(22, g_seen_dma[0].v);

    pump_user(); // USER
    TEST_ASSERT_EQUAL_size_t(1, g_seen_n);
    TEST_ASSERT_EQUAL_UINT32(11, g_seen[0].v);
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
    PqItem v = item_u32(5);
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
    PqItem a = item_u32(10), b = item_u32(20);
    TEST_ASSERT_TRUE(PreemptQueue.post(PC_PQ_LANE_DMA, &a, 0));
    TEST_ASSERT_TRUE(PreemptQueue.post_urgent(PC_PQ_LANE_DMA, &b, 0)); // urgent -> jumps the queue
    pump("pc_pq_dma");                                                 // the lane task drains its queue
    TEST_ASSERT_EQUAL_UINT32(2u, (uint32_t)g_seen_dma_n);
    TEST_ASSERT_EQUAL_UINT32(20u, g_seen_dma[0].v); // urgent item first
    TEST_ASSERT_EQUAL_UINT32(10u, g_seen_dma[1].v);
    // Guards: urgent-post to a bad lane / with a null item fails closed; drain of a bad lane is a no-op.
    TEST_ASSERT_FALSE(PreemptQueue.post_urgent((pc_pq_lane)PC_PQ_LANE_COUNT, &a, 0));
    TEST_ASSERT_FALSE(PreemptQueue.post_urgent(PC_PQ_LANE_DMA, NULL, 0));
    PreemptQueue.drain((pc_pq_lane)PC_PQ_LANE_COUNT); // out of range: still a no-op
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
    PqItem v = item_u32(1);
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
        PqItem it = item_u32(i);
        TEST_ASSERT_TRUE(PreemptQueue.post(PC_PQ_LANE_DMA, &it, 0));
    }
    PqItem urgent = item_u32(999);
    TEST_ASSERT_FALSE(PreemptQueue.post_urgent(PC_PQ_LANE_DMA, &urgent, 0)); // full -> dropped, not bumped in
    pump("pc_pq_dma");                                                       // the lane task drains its queue
    TEST_ASSERT_EQUAL_size_t(PC_PQ_DEPTH, g_seen_dma_n);
    PreemptQueue.stop(PC_PQ_LANE_DMA);
}

void test_post_to_a_lane_that_was_never_started_fails_closed()
{
    // FORWARD is never started in this suite, so it owns no queue. A post has nowhere to land and
    // is refused on every entry point rather than dropped silently, and pumping the lane's task
    // name runs nothing because there is no such task.
    PqItem v = item_u32(42);
    TEST_ASSERT_FALSE(PreemptQueue.post(PC_PQ_LANE_FORWARD, &v, 0));
    TEST_ASSERT_FALSE(PreemptQueue.post_urgent(PC_PQ_LANE_FORWARD, &v, 0));
    TEST_ASSERT_FALSE(PreemptQueue.post_from_isr(PC_PQ_LANE_FORWARD, &v));
    TEST_ASSERT_FALSE(PreemptQueue.running(PC_PQ_LANE_FORWARD));
    pump("pc_pq_fwd");
    TEST_ASSERT_EQUAL_size_t(0, g_seen_n);
    TEST_ASSERT_EQUAL_size_t(0, g_seen_dma_n);
}

// --- The DMA lane, driven by the host DMA driver ---------------------------------------

void test_dma_completion_posts_to_the_lane_and_the_task_does_the_work()
{
    open_dma_lane(0, PROTO_FALSE);
    const uint8_t rx[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    TEST_ASSERT_TRUE(pc_dma_host_feed(0, rx, (uint16_t)sizeof(rx)));

    pc_dma_poll(); // the transfer completes and the callback posts, where the ISR would
    TEST_ASSERT_EQUAL_size_t(1, g_dma_posted);
    TEST_ASSERT_EQUAL_size_t(0, g_seen_dma_n); // the callback itself did no work

    pump("pc_pq_dma");
    TEST_ASSERT_EQUAL_size_t(1, g_seen_dma_n);
    TEST_ASSERT_EQUAL_UINT8(PC_DMA_RX, g_seen_dma[0].dir);
    TEST_ASSERT_EQUAL_UINT8(0, g_seen_dma[0].channel);
    TEST_ASSERT_EQUAL_UINT16(sizeof(rx), g_seen_dma[0].len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, g_seen_dma[0].data, sizeof(rx));
    TEST_ASSERT_EQUAL_size_t(0, g_seen_n); // the user lane is untouched
}

void test_dma_ping_pong_transfers_reach_the_lane_in_order()
{
    // Two buffers' worth in one feed: the engine completes one transfer per poll and flips banks,
    // so both completions reach the lane carrying their own bytes, in the order they arrived.
    open_dma_lane(1, PROTO_FALSE);
    uint8_t rx[PC_DMA_BUF_SIZE * 2];
    for (size_t i = 0; i < sizeof(rx); i++)
    {
        rx[i] = (uint8_t)(0x10 + i);
    }
    TEST_ASSERT_TRUE(pc_dma_host_feed(1, rx, (uint16_t)sizeof(rx)));
    pc_dma_poll();
    pc_dma_poll();
    TEST_ASSERT_EQUAL_size_t(2, g_dma_posted);

    pump("pc_pq_dma");
    TEST_ASSERT_EQUAL_size_t(2, g_seen_dma_n);
    TEST_ASSERT_EQUAL_UINT16(PC_DMA_BUF_SIZE, g_seen_dma[0].len);
    TEST_ASSERT_EQUAL_UINT16(PC_DMA_BUF_SIZE, g_seen_dma[1].len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, g_seen_dma[0].data, PC_DMA_BUF_SIZE);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx + PC_DMA_BUF_SIZE, g_seen_dma[1].data, PC_DMA_BUF_SIZE);
    TEST_ASSERT_EQUAL_UINT32(g_seen_dma[0].v + 1u, g_seen_dma[1].v); // consecutive completions
}

void test_dma_tx_completion_reaches_the_lane_with_no_bytes()
{
    open_dma_lane(0, PROTO_FALSE);
    const uint8_t tx[3] = {1, 2, 3};
    TEST_ASSERT_TRUE(pc_dma_tx_submit(0, tx, (uint16_t)sizeof(tx)));
    pc_dma_poll();
    pump("pc_pq_dma");
    TEST_ASSERT_EQUAL_size_t(1, g_seen_dma_n);
    TEST_ASSERT_EQUAL_UINT8(PC_DMA_TX, g_seen_dma[0].dir);
    TEST_ASSERT_EQUAL_UINT16(sizeof(tx), g_seen_dma[0].len);

    uint8_t back[PC_DMA_BUF_SIZE] = {0};
    TEST_ASSERT_EQUAL_UINT16(sizeof(tx), pc_dma_host_capture(0, back, (uint16_t)sizeof(back)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, back, sizeof(tx));
}

void test_dma_loopback_round_trips_through_the_lane()
{
    // A loopback channel's egress arrives as its own ingress, so one submit puts two completions on
    // the lane: the TX that freed the buffer, then the RX carrying the same bytes back.
    open_dma_lane(1, PROTO_TRUE);
    const uint8_t tx[4] = {0xA0, 0xA1, 0xA2, 0xA3};
    TEST_ASSERT_TRUE(pc_dma_tx_submit(1, tx, (uint16_t)sizeof(tx)));
    pc_dma_poll();
    pump("pc_pq_dma");
    TEST_ASSERT_EQUAL_size_t(2, g_seen_dma_n);
    TEST_ASSERT_EQUAL_UINT8(PC_DMA_TX, g_seen_dma[0].dir);
    TEST_ASSERT_EQUAL_UINT8(PC_DMA_RX, g_seen_dma[1].dir);
    TEST_ASSERT_EQUAL_UINT16(sizeof(tx), g_seen_dma[1].len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, g_seen_dma[1].data, sizeof(tx));
}

void test_dma_lane_full_drops_the_completion_in_the_isr()
{
    // The lane holds PC_PQ_DEPTH items. A completion past that has nowhere to go, and the callback
    // runs in an interrupt, so its post fails closed there instead of waiting for room.
    open_dma_lane(0, PROTO_FALSE);
    uint8_t rx[PC_DMA_BUF_SIZE];
    memset(rx, 0x5A, sizeof(rx));
    for (uint32_t i = 0; i < PC_PQ_DEPTH + 1u; i++)
    {
        TEST_ASSERT_TRUE(pc_dma_host_feed(0, rx, (uint16_t)sizeof(rx)));
        pc_dma_poll();
    }
    TEST_ASSERT_EQUAL_size_t(PC_PQ_DEPTH, g_dma_posted);
    TEST_ASSERT_EQUAL_size_t(1, g_dma_dropped);

    pump("pc_pq_dma");
    TEST_ASSERT_EQUAL_size_t(PC_PQ_DEPTH, g_seen_dma_n);
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(PC_PQ_DEPTH, PreemptQueue.high_water(PC_PQ_LANE_DMA));
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
    RUN_TEST(test_post_to_a_lane_that_was_never_started_fails_closed);
    RUN_TEST(test_dma_completion_posts_to_the_lane_and_the_task_does_the_work);
    RUN_TEST(test_dma_ping_pong_transfers_reach_the_lane_in_order);
    RUN_TEST(test_dma_tx_completion_reaches_the_lane_with_no_bytes);
    RUN_TEST(test_dma_loopback_round_trips_through_the_lane);
    RUN_TEST(test_dma_lane_full_drops_the_completion_in_the_isr);
    return UNITY_END();
}
