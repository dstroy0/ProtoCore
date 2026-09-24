// PreemptLanes - internal preempting lanes vs the single user lane.
//
// The preempting work queue is not one queue but several named LANES, each with its own
// task at its own priority:
//   - protocore_pq_lane::PROTOCORE_PQ_LANE_USER    - the one lane exposed to your app (the no-arg protocore_pq_*
//                             API drives it). Lowest priority.
//   - protocore_pq_lane::PROTOCORE_PQ_LANE_DMA / _FORWARD / _DEVICE - internal lanes the library uses for its
//                             own real-time work; they run ABOVE the user lane (DMA
//                             highest), so internal ingest always preempts user work.
//
// This sketch starts one internal lane (DMA) and the user lane, prints the priority map
// (proving internal > user), and posts to each. On hardware the higher-priority DMA-lane
// task is scheduled ahead of the user-lane task whenever both have work.
//
// Build flag (whole build): PROTOCORE_ENABLE_PREEMPT_QUEUE=1

#include "protocore.h" // discovers the library (adds src/ to the include path)
#include "server/core/preempt_queue/preempt_queue.h"

// A queue item padded to the lane item size (the queue copies a fixed PROTOCORE_PQ_ITEM_SIZE
// bytes, so the posted object must be at least that large).
union pq_item {
    uint32_t seq;
    uint8_t raw[PROTOCORE_PQ_ITEM_SIZE];
};

static void on_critical(const void *item, void *)
{
    Serial.printf("  [DMA  lane] critical #%u\n", ((const pq_item *)item)->seq);
}
static void on_background(const void *item, void *)
{
    Serial.printf("  [USER lane] background #%u\n", ((const pq_item *)item)->seq);
}

// A lane's default task priority. PreemptQueue reads the lane from PreemptQueueV and reports
// the priority back into PreemptQueueV.u8.
static uint8_t lane_priority(protocore_pq_lane lane)
{
    PreemptQueueV.lane = lane;
    PreemptQueue.priority(protocore_preempt_queue_span());
    return PreemptQueueV.u8;
}

void setup()
{
    Serial.begin(115200);
    delay(300);

    // Internal DMA lane: priority 0 -> the lane default (ranks above the user lane).
    protocore_pq_config dma = {};
    dma.handler = on_critical;
    dma.core = 1;
    PreemptQueueV.lane = protocore_pq_lane::PROTOCORE_PQ_LANE_DMA;
    PreemptQueueV.cfg = &dma;
    PreemptQueue.start(protocore_preempt_queue_span());

    // User lane via the no-arg API (unchanged from PreemptQueue).
    protocore_pq_config user = {};
    user.handler = on_background;
    user.priority = 5; // stays below the internal lanes
    user.core = 1;
    protocore_pq_start(&user);

    Serial.printf("lane priorities  DMA=%u  FORWARD=%u  DEVICE=%u  USER=%u\n",
                  lane_priority(protocore_pq_lane::PROTOCORE_PQ_LANE_DMA), lane_priority(protocore_pq_lane::PROTOCORE_PQ_LANE_FORWARD),
                  lane_priority(protocore_pq_lane::PROTOCORE_PQ_LANE_DEVICE), lane_priority(protocore_pq_lane::PROTOCORE_PQ_LANE_USER));
    Serial.println("internal lanes outrank the user lane -> internal work preempts user work");
}

static uint32_t g_seq = 0;

void loop()
{
    // Post one item to each lane. The DMA-lane task (higher priority) runs its handler
    // before the user-lane task whenever both are runnable.
    pq_item it = {};
    it.seq = g_seq++;
    PreemptQueueV.lane = protocore_pq_lane::PROTOCORE_PQ_LANE_DMA; // critical -> internal lane
    PreemptQueueV.post_args.item = &it;
    PreemptQueueV.post_args.timeout_ticks = 0;
    PreemptQueue.post(protocore_preempt_queue_span());
    protocore_pq_post(&it, 0); // background -> user lane
    delay(1000);
}
