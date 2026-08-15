// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the preempting work queues (server/core/preempt_queue.h) and the DMA ingest that
// feeds them (mmgr/dma.h).
//
// No standard governs a work queue, so every expectation here is a PROPERTY of what the two modules
// publish, stated as such: a lane is FIFO, an urgent post goes ahead of what is queued, a full lane
// refuses instead of blocking, lanes do not see each other's items, and the internal lanes outrank
// the user lane. The DMA half is driven through the host driver, so the completion callback runs
// where the ISR would.
//
// test_a_dma_completion_is_processed_off_the_interrupt is the load-bearing case: the whole reason
// this pipe exists is that a DMA-complete callback runs in interrupt context, so it must do nothing
// but hand the bytes over, and the work must happen in the lane's task. If the handler ran inside
// the callback the module would be an interrupt-context work queue, which is the failure it exists
// to prevent.
//
// The env sizes PROTOCORE_PQ_DEPTH 4, PROTOCORE_PQ_ITEM_SIZE 16, PROTOCORE_DMA_BUF_SIZE 8,
// PROTOCORE_DMA_CHANNELS 2.

#include "mmgr/dma.h"
#include "server/core/preempt_queue.h"
// The host DMA driver defines the protocore_dma_hw_* hooks with external linkage, so it belongs to
// exactly one translation unit. Unity's runner generator copies every line matching "#include" at
// the start of a line into the runner; the space after the '#' keeps this one out of that copy and
// out of a duplicate-definition link error.
#include "protocore_dma_host.h"
#include <string.h>

#include <unity.h>

// What a producer puts on a lane. A DMA completion runs in ISR context and the ping-pong buffer it
// points at is refilled a transfer or two later, so the item carries the bytes rather than the
// pointer.
typedef struct
{
    uint32_t v;                           // plain payload, and the completion sequence on a DMA item
    uint16_t len;                         // bytes the transfer moved
    uint8_t channel;                      // the channel it completed on
    uint8_t dir;                          // protocore_dma_dir
    uint8_t data[PROTOCORE_DMA_BUF_SIZE]; // RX: the completed buffer's bytes; TX: zero
} PqItem;
_Static_assert(sizeof(PqItem) == PROTOCORE_PQ_ITEM_SIZE, "a lane item carries one DMA completion whole");

#define SEEN_MAX 64

static PqItem g_user[SEEN_MAX];
static size_t g_user_n;
static PqItem g_dma[SEEN_MAX];
static size_t g_dma_n;

static void on_user(const void *item, void *ctx)
{
    (void)ctx;
    if (g_user_n < SEEN_MAX)
    {
        memcpy(&g_user[g_user_n++], item, sizeof(PqItem));
    }
}

static void on_dma(const void *item, void *ctx)
{
    (void)ctx;
    if (g_dma_n < SEEN_MAX)
    {
        memcpy(&g_dma[g_dma_n++], item, sizeof(PqItem));
    }
}

// --- the calls, each as one helper -------------------------------------------------------------

static proto_bool lane_start(protocore_pq_lane lane, const protocore_pq_config *cfg)
{
    PreemptQueue.lane = lane;
    PreemptQueue.cfg = cfg;
    PreemptQueue.start(PreemptQueue.internal);
    return PreemptQueue.ok;
}

static proto_bool lane_post(protocore_pq_lane lane, const void *item)
{
    PreemptQueue.lane = lane;
    PreemptQueue.post_args.item = item;
    PreemptQueue.post_args.timeout_ticks = 0;
    PreemptQueue.post(PreemptQueue.internal);
    return PreemptQueue.ok;
}

static proto_bool lane_post_urgent(protocore_pq_lane lane, const void *item)
{
    PreemptQueue.lane = lane;
    PreemptQueue.post_args.item = item;
    PreemptQueue.post_args.timeout_ticks = 0;
    PreemptQueue.post_urgent(PreemptQueue.internal);
    return PreemptQueue.ok;
}

static proto_bool lane_post_isr(protocore_pq_lane lane, const void *item)
{
    PreemptQueue.lane = lane;
    PreemptQueue.post_args.item = item;
    PreemptQueue.post_from_isr(PreemptQueue.internal);
    return PreemptQueue.ok;
}

static proto_bool lane_running(protocore_pq_lane lane)
{
    PreemptQueue.lane = lane;
    PreemptQueue.running(PreemptQueue.internal);
    return PreemptQueue.ok;
}

static size_t lane_high_water(protocore_pq_lane lane)
{
    PreemptQueue.lane = lane;
    PreemptQueue.high_water(PreemptQueue.internal);
    return PreemptQueue.n;
}

static uint8_t lane_rank(protocore_pq_lane lane)
{
    PreemptQueue.lane = lane;
    PreemptQueue.priority(PreemptQueue.internal);
    return PreemptQueue.u8;
}

static void lane_stop(protocore_pq_lane lane)
{
    PreemptQueue.lane = lane;
    PreemptQueue.stop(PreemptQueue.internal);
}

// The host has no scheduler, so a lane's task runs when this says so: the entry drains its own
// queue and unwinds when the queue reports empty. Naming the task is what keeps one lane's drain
// from touching another's.
static void pump(const char *lane_task)
{
    (void)protocore_platform_host_task_run_named(lane_task);
}

static PqItem item_u32(uint32_t v)
{
    PqItem it;
    memset(&it, 0, sizeof(it));
    it.v = v;
    return it;
}

static proto_bool post_user(uint32_t v)
{
    PqItem it = item_u32(v);
    return protocore_pq_post(&it, 0);
}

// --- the DMA channel that feeds the DMA lane ---------------------------------------------------

static size_t g_isr_posted;
static size_t g_isr_dropped;

// Where the ISR runs on silicon: copy the completed bytes out of the ping-pong buffer and hand them
// to the DMA lane, so the work happens in the lane's task instead of in the interrupt.
static void on_dma_complete(const protocore_dma_event *ev, void *ctx)
{
    (void)ctx;
    PqItem it;
    memset(&it, 0, sizeof(it));
    it.v = ev->seq;
    it.len = ev->len;
    it.channel = ev->channel;
    it.dir = (uint8_t)ev->dir;
    if (ev->dir == PROTOCORE_DMA_RX && ev->data != NULL)
    {
        uint16_t n = ev->len;
        if (n > (uint16_t)sizeof(it.data))
        {
            n = (uint16_t)sizeof(it.data);
        }
        memcpy(it.data, ev->data, n);
    }
    if (lane_post_isr(PROTOCORE_PQ_LANE_DMA, &it))
    {
        g_isr_posted++;
    }
    else
    {
        g_isr_dropped++;
    }
}

static void open_dma_lane(uint8_t ch, proto_bool loopback)
{
    protocore_pq_config lane;
    memset(&lane, 0, sizeof(lane));
    lane.handler = on_dma;
    TEST_ASSERT_TRUE(lane_start(PROTOCORE_PQ_LANE_DMA, &lane));

    protocore_dma_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.channel = ch;
    cfg.periph = PROTOCORE_DMA_UART;
    cfg.loopback = loopback;
    cfg.on_complete = on_dma_complete;
    TEST_ASSERT_TRUE(protocore_dma_open(&cfg));
}

void setUp(void)
{
    g_user_n = 0;
    g_dma_n = 0;
    g_isr_posted = 0;
    g_isr_dropped = 0;
    for (int l = 0; l < (int)PROTOCORE_PQ_LANE_COUNT; l++)
    {
        lane_stop((protocore_pq_lane)l);
    }
    queue_stage_reset(); // a lane's queue outlives its stop, so no case inherits another's backlog
    protocore_dma_host_reset();

    protocore_pq_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.handler = on_user;
    cfg.priority = 5;
    cfg.core = 1;
    cfg.name = "test_pq";
    TEST_ASSERT_TRUE(protocore_pq_start(&cfg));
}

void tearDown(void)
{
    for (int l = 0; l < (int)PROTOCORE_PQ_LANE_COUNT; l++)
    {
        lane_stop((protocore_pq_lane)l);
    }
}

// Items come out in the order they went in, so a stream posted from one producer keeps its
// sequence.
void test_a_lane_is_fifo(void)
{
    TEST_ASSERT_TRUE(post_user(10));
    TEST_ASSERT_TRUE(post_user(20));
    TEST_ASSERT_TRUE(post_user(30));
    pump("test_pq");
    TEST_ASSERT_EQUAL_size_t(3u, g_user_n);
    TEST_ASSERT_EQUAL_UINT32(10u, g_user[0].v);
    TEST_ASSERT_EQUAL_UINT32(20u, g_user[1].v);
    TEST_ASSERT_EQUAL_UINT32(30u, g_user[2].v);
}

// An urgent post is processed before everything already queued, which is what makes it urgent.
void test_an_urgent_post_goes_to_the_front(void)
{
    TEST_ASSERT_TRUE(post_user(1));
    TEST_ASSERT_TRUE(post_user(2));
    PqItem u = item_u32(99);
    TEST_ASSERT_TRUE(protocore_pq_post_urgent(&u, 0));
    pump("test_pq");
    TEST_ASSERT_EQUAL_size_t(3u, g_user_n);
    TEST_ASSERT_EQUAL_UINT32(99u, g_user[0].v);
    TEST_ASSERT_EQUAL_UINT32(1u, g_user[1].v);
    TEST_ASSERT_EQUAL_UINT32(2u, g_user[2].v);
}

// The lane holds PROTOCORE_PQ_DEPTH items. The one past that is refused rather than waited on, so a
// producer's latency stays bounded whatever the consumer is doing, and nothing already queued is
// displaced.
void test_a_full_lane_refuses_rather_than_blocks(void)
{
    for (uint32_t i = 0; i < PROTOCORE_PQ_DEPTH; i++)
    {
        TEST_ASSERT_TRUE(post_user(i));
    }
    TEST_ASSERT_FALSE(post_user(999));
    PqItem u = item_u32(998);
    TEST_ASSERT_FALSE(protocore_pq_post_urgent(&u, 0)); // urgency does not evict a queued item
    TEST_ASSERT_FALSE(protocore_pq_post_from_isr(&u));

    pump("test_pq");
    TEST_ASSERT_EQUAL_size_t((size_t)PROTOCORE_PQ_DEPTH, g_user_n);
    for (uint32_t i = 0; i < PROTOCORE_PQ_DEPTH; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(i, g_user[i].v);
    }
}

// The high-water mark is a sizing aid, so it records the peak and keeps it after the lane drains.
void test_the_high_water_mark_is_the_peak(void)
{
    TEST_ASSERT_TRUE(post_user(1));
    TEST_ASSERT_TRUE(post_user(2));
    TEST_ASSERT_TRUE(post_user(3));
    TEST_ASSERT_EQUAL_size_t(3u, lane_high_water(PROTOCORE_PQ_LANE_USER));
    pump("test_pq");
    TEST_ASSERT_EQUAL_size_t(3u, lane_high_water(PROTOCORE_PQ_LANE_USER));   // the peak outlives the drain
    TEST_ASSERT_EQUAL_size_t(0u, lane_high_water(PROTOCORE_PQ_LANE_DEVICE)); // an untouched lane has none
}

// A drained lane is reusable: the ring wraps rather than filling up permanently.
void test_a_drained_lane_is_reusable(void)
{
    for (uint32_t i = 0; i < PROTOCORE_PQ_DEPTH; i++)
    {
        TEST_ASSERT_TRUE(post_user(i));
    }
    pump("test_pq");
    TEST_ASSERT_EQUAL_size_t((size_t)PROTOCORE_PQ_DEPTH, g_user_n);
    g_user_n = 0;
    pump("test_pq"); // nothing queued: no handler call
    TEST_ASSERT_EQUAL_size_t(0u, g_user_n);
    for (uint32_t i = 0; i < PROTOCORE_PQ_DEPTH; i++)
    {
        TEST_ASSERT_TRUE(post_user(100u + i));
    }
    pump("test_pq");
    TEST_ASSERT_EQUAL_size_t((size_t)PROTOCORE_PQ_DEPTH, g_user_n);
    TEST_ASSERT_EQUAL_UINT32(100u, g_user[0].v);
}

// Each lane owns its own queue and handler, so draining one leaves the other's items where they
// are.
void test_lanes_are_isolated(void)
{
    protocore_pq_config dma;
    memset(&dma, 0, sizeof(dma));
    dma.handler = on_dma;
    TEST_ASSERT_TRUE(lane_start(PROTOCORE_PQ_LANE_DMA, &dma));

    PqItem u = item_u32(11);
    PqItem d = item_u32(22);
    TEST_ASSERT_TRUE(protocore_pq_post(&u, 0));
    TEST_ASSERT_TRUE(lane_post(PROTOCORE_PQ_LANE_DMA, &d));

    pump("protocore_pq_dma");
    TEST_ASSERT_EQUAL_size_t(0u, g_user_n);
    TEST_ASSERT_EQUAL_size_t(1u, g_dma_n);
    TEST_ASSERT_EQUAL_UINT32(22u, g_dma[0].v);

    pump("test_pq");
    TEST_ASSERT_EQUAL_size_t(1u, g_user_n);
    TEST_ASSERT_EQUAL_UINT32(11u, g_user[0].v);
}

// Internal ingest must preempt application work, so the internal lanes rank above the user lane and
// DMA ranks above the rest of them.
void test_internal_lanes_outrank_the_user_lane(void)
{
    TEST_ASSERT_GREATER_THAN_UINT8(lane_rank(PROTOCORE_PQ_LANE_FORWARD), lane_rank(PROTOCORE_PQ_LANE_DMA));
    TEST_ASSERT_GREATER_THAN_UINT8(lane_rank(PROTOCORE_PQ_LANE_DEVICE), lane_rank(PROTOCORE_PQ_LANE_FORWARD));
    TEST_ASSERT_GREATER_THAN_UINT8(lane_rank(PROTOCORE_PQ_LANE_USER), lane_rank(PROTOCORE_PQ_LANE_DEVICE));
}

// A lane with no handler has nothing to do with an item, so it does not start; and starting one
// that is already up changes nothing.
void test_start_requires_a_handler_and_is_idempotent(void)
{
    protocore_pq_stop();
    TEST_ASSERT_FALSE(protocore_pq_running());
    TEST_ASSERT_FALSE(protocore_pq_start(NULL));

    protocore_pq_config no_handler;
    memset(&no_handler, 0, sizeof(no_handler));
    TEST_ASSERT_FALSE(protocore_pq_start(&no_handler));
    TEST_ASSERT_FALSE(protocore_pq_running());

    protocore_pq_config ok;
    memset(&ok, 0, sizeof(ok));
    ok.handler = on_user;
    TEST_ASSERT_TRUE(protocore_pq_start(&ok));
    TEST_ASSERT_TRUE(protocore_pq_running());
    TEST_ASSERT_FALSE(protocore_pq_start(&ok)); // already up
    TEST_ASSERT_TRUE(protocore_pq_running());
}

// Stopping one lane leaves the others running.
void test_stop_is_per_lane(void)
{
    protocore_pq_config dma;
    memset(&dma, 0, sizeof(dma));
    dma.handler = on_dma;
    TEST_ASSERT_TRUE(lane_start(PROTOCORE_PQ_LANE_DMA, &dma));
    TEST_ASSERT_TRUE(lane_running(PROTOCORE_PQ_LANE_DMA));
    TEST_ASSERT_TRUE(lane_running(PROTOCORE_PQ_LANE_USER));

    lane_stop(PROTOCORE_PQ_LANE_DMA);
    TEST_ASSERT_FALSE(lane_running(PROTOCORE_PQ_LANE_DMA));
    TEST_ASSERT_TRUE(lane_running(PROTOCORE_PQ_LANE_USER));
}

// A lane that was never started owns no queue, so a post has nowhere to land and every entry point
// says so rather than dropping it silently.
void test_a_lane_that_never_started_refuses_every_post(void)
{
    PqItem v = item_u32(42);
    TEST_ASSERT_FALSE(lane_running(PROTOCORE_PQ_LANE_FORWARD));
    TEST_ASSERT_FALSE(lane_post(PROTOCORE_PQ_LANE_FORWARD, &v));
    TEST_ASSERT_FALSE(lane_post_urgent(PROTOCORE_PQ_LANE_FORWARD, &v));
    TEST_ASSERT_FALSE(lane_post_isr(PROTOCORE_PQ_LANE_FORWARD, &v));
    pump("protocore_pq_fwd");
    TEST_ASSERT_EQUAL_size_t(0u, g_user_n);
    TEST_ASSERT_EQUAL_size_t(0u, g_dma_n);
}

// A lane id outside the table, and an item that is not there, are refused on every entry point
// rather than indexing past the lane array.
void test_a_lane_out_of_range_and_a_null_item_fail_closed(void)
{
    const protocore_pq_lane bad = (protocore_pq_lane)PROTOCORE_PQ_LANE_COUNT;
    protocore_pq_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.handler = on_dma;
    PqItem v = item_u32(1);

    TEST_ASSERT_FALSE(lane_start(bad, &cfg));
    TEST_ASSERT_FALSE(lane_post(bad, &v));
    TEST_ASSERT_FALSE(lane_post_urgent(bad, &v));
    TEST_ASSERT_FALSE(lane_post_isr(bad, &v));
    TEST_ASSERT_FALSE(lane_running(bad));
    TEST_ASSERT_EQUAL_size_t(0u, lane_high_water(bad));
    lane_stop(bad); // no state to change, and nothing to index

    TEST_ASSERT_FALSE(protocore_pq_post(NULL, 0));
    TEST_ASSERT_FALSE(protocore_pq_post_urgent(NULL, 0));
    TEST_ASSERT_FALSE(protocore_pq_post_from_isr(NULL));
}

// The completion callback runs where the interrupt would, and it only hands the bytes over: the
// handler runs later, in the lane's task, with the payload it was given.
void test_a_dma_completion_is_processed_off_the_interrupt(void)
{
    open_dma_lane(0, PROTO_FALSE);
    const uint8_t rx[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    TEST_ASSERT_TRUE(protocore_dma_host_feed(0, rx, (uint16_t)sizeof(rx)));

    protocore_dma_poll(); // the transfer completes and the callback posts, in ISR context
    TEST_ASSERT_EQUAL_size_t(1u, g_isr_posted);
    TEST_ASSERT_EQUAL_size_t(0u, g_dma_n); // the callback itself did no work

    pump("protocore_pq_dma");
    TEST_ASSERT_EQUAL_size_t(1u, g_dma_n);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_DMA_RX, g_dma[0].dir);
    TEST_ASSERT_EQUAL_UINT8(0, g_dma[0].channel);
    TEST_ASSERT_EQUAL_UINT16(sizeof(rx), g_dma[0].len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, g_dma[0].data, sizeof(rx));
    TEST_ASSERT_EQUAL_size_t(0u, g_user_n); // and the user lane never saw it
}

// Two buffers' worth in one feed: the engine completes one transfer per poll and flips banks, so
// each completion reaches the lane carrying its own bytes, in the order they arrived.
void test_ping_pong_completions_reach_the_lane_in_order(void)
{
    open_dma_lane(1, PROTO_FALSE);
    uint8_t rx[PROTOCORE_DMA_BUF_SIZE * 2];
    for (size_t i = 0; i < sizeof(rx); i++)
    {
        rx[i] = (uint8_t)(0x10 + i);
    }
    TEST_ASSERT_TRUE(protocore_dma_host_feed(1, rx, (uint16_t)sizeof(rx)));
    protocore_dma_poll();
    protocore_dma_poll();
    TEST_ASSERT_EQUAL_size_t(2u, g_isr_posted);

    pump("protocore_pq_dma");
    TEST_ASSERT_EQUAL_size_t(2u, g_dma_n);
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_DMA_BUF_SIZE, g_dma[0].len);
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_DMA_BUF_SIZE, g_dma[1].len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, g_dma[0].data, PROTOCORE_DMA_BUF_SIZE);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx + PROTOCORE_DMA_BUF_SIZE, g_dma[1].data, PROTOCORE_DMA_BUF_SIZE);
    TEST_ASSERT_EQUAL_UINT32(g_dma[0].v + 1u, g_dma[1].v); // consecutive completions
}

// An egress completion says the buffer is free, so it carries a length and no bytes.
void test_a_tx_completion_carries_no_bytes(void)
{
    open_dma_lane(0, PROTO_FALSE);
    const uint8_t tx[3] = {1, 2, 3};
    TEST_ASSERT_TRUE(protocore_dma_tx_submit(0, tx, (uint16_t)sizeof(tx)));
    protocore_dma_poll();
    pump("protocore_pq_dma");
    TEST_ASSERT_EQUAL_size_t(1u, g_dma_n);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_DMA_TX, g_dma[0].dir);
    TEST_ASSERT_EQUAL_UINT16(sizeof(tx), g_dma[0].len);

    uint8_t back[PROTOCORE_DMA_BUF_SIZE];
    memset(back, 0, sizeof(back));
    TEST_ASSERT_EQUAL_UINT16(sizeof(tx), protocore_dma_host_capture(0, back, (uint16_t)sizeof(back)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, back, sizeof(tx));
}

// A loopback channel's egress arrives as its own ingress, so one submit puts two completions on the
// lane: the TX that freed the buffer, then the RX carrying the same bytes back.
void test_loopback_round_trips_through_the_lane(void)
{
    open_dma_lane(1, PROTO_TRUE);
    const uint8_t tx[4] = {0xA0, 0xA1, 0xA2, 0xA3};
    TEST_ASSERT_TRUE(protocore_dma_tx_submit(1, tx, (uint16_t)sizeof(tx)));
    protocore_dma_poll();
    pump("protocore_pq_dma");
    TEST_ASSERT_EQUAL_size_t(2u, g_dma_n);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_DMA_TX, g_dma[0].dir);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_DMA_RX, g_dma[1].dir);
    TEST_ASSERT_EQUAL_UINT16(sizeof(tx), g_dma[1].len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, g_dma[1].data, sizeof(tx));
}

// A completion past the lane's depth has nowhere to go, and the callback is in an interrupt, so its
// post fails there instead of waiting for room.
void test_a_full_lane_drops_the_completion_in_the_isr(void)
{
    open_dma_lane(0, PROTO_FALSE);
    uint8_t rx[PROTOCORE_DMA_BUF_SIZE];
    memset(rx, 0x5A, sizeof(rx));
    for (uint32_t i = 0; i < PROTOCORE_PQ_DEPTH + 1u; i++)
    {
        TEST_ASSERT_TRUE(protocore_dma_host_feed(0, rx, (uint16_t)sizeof(rx)));
        protocore_dma_poll();
    }
    TEST_ASSERT_EQUAL_size_t((size_t)PROTOCORE_PQ_DEPTH, g_isr_posted);
    TEST_ASSERT_EQUAL_size_t(1u, g_isr_dropped);

    pump("protocore_pq_dma");
    TEST_ASSERT_EQUAL_size_t((size_t)PROTOCORE_PQ_DEPTH, g_dma_n);
    TEST_ASSERT_EQUAL_size_t((size_t)PROTOCORE_PQ_DEPTH, lane_high_water(PROTOCORE_PQ_LANE_DMA));
}
