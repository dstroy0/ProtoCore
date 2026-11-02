// DmaIngest - DMA peripheral ingest: DMA-complete -> preempting queue -> task.
//
// The high-throughput field-bus ingest path. A DMA channel moves peripheral bytes
// (here a UART) into a static buffer with the CPU free; the DMA-complete callback -
// ISR context on real silicon - does the tiniest possible thing: it posts the
// completion into the preempting work queue (see PreemptQueue), and a dedicated
// high-priority task is preempted to process the bytes off the interrupt.
//
// There is no physical loopback wire on the bench, so the channel runs the built-in
// ingress/egress SIMULATOR (PROTOCORE_DMA_SIMULATE=1): each second loop() submits a frame
// for egress DMA, the loopback jumper feeds it back into RX, praet.poll completes
// the transfer, and the frame arrives at the processing task - the whole pipeline,
// end to end, on the device itself. Swap the simulator for a real UART/SPI DMA driver
// (protocore_dma_hw_*, PROTOCORE_DMA_SIMULATE=0) and the sketch is unchanged.
//
// Build flags (whole build, not just this sketch):
//   PROTOCORE_ENABLE_DMA=1 MMGR_ENABLE_DMA=1 PROTOCORE_ENABLE_PREEMPT_QUEUE=1 PROTOCORE_DMA_SIMULATE=1

#include "protocore.h" // discovers the library (adds src/ to the include path)
#include "memoriam_praetereo/memoriam_praetereo.h"
#include "server/core/preempt_queue/preempt_queue.h"


// The preempting-queue item: a SELF-CONTAINED copy of the frame bytes, padded to the
// queue item size (the queue copies a fixed PROTOCORE_PQ_ITEM_SIZE bytes). We copy the bytes
// rather than post the event's `data` pointer: that pointer only stays valid until its
// ping-pong buffer is reused a transfer or two later, and a queue consumer draining in
// another task can lag past that under load - so the descriptor must own its bytes.
struct dma_msg
{
    uint16_t len;
    uint16_t seq;
    uint8_t channel;
    uint8_t bytes[8];
};
union pq_item {
    dma_msg msg;
    uint8_t raw[PROTOCORE_PQ_ITEM_SIZE];
};

// Runs in the high-priority processing task (NOT the DMA callback), so real work is
// safe here. A real app would decode the field-bus frame; we just print it.
static void on_dma_frame(const void *item, void *)
{
    const dma_msg *m = &((const pq_item *)item)->msg;
    Serial.printf("RX ch%u seq%u %u bytes: ", m->channel, m->seq, m->len);
    for (uint16_t i = 0; i < m->len; i++)
    {
        Serial.printf("%02X ", m->bytes[i]);
    }
    Serial.println();
}

// The DMA-complete callback: ISR context on silicon, so keep it tiny - copy the frame
// bytes into a queue item and post it onto the internal DMA lane (which runs above the
// user lane, so ingest preempts user work). The post asks the scheduler to switch
// straight to the processing task the moment we return.
// The direction a port reports for a completed receive, and the peripheral numbers it is opened on.
static const uint8_t DMA_DIRECTION_RX = 0u;
static const uint8_t DMA_PERIPHERAL_UART = 0u;
static const uint8_t DMA_PERIPHERAL_SPI = 2u;

static void on_dma_complete(const mmgr_praet_event *ev, void *)
{
    if (ev->direction != DMA_DIRECTION_RX)
    {
        return; // TX-complete just frees the channel; nothing to process
    }
    pq_item item = {};
    item.msg.len = ev->bytes;
    item.msg.seq = ev->sequence;
    item.msg.channel = ev->channel;
    uint16_t n = (ev->bytes < sizeof(item.msg.bytes)) ? ev->bytes : sizeof(item.msg.bytes);
    memcpy(item.msg.bytes, ev->data, n);
    Session.workers->queue->post_from_isr(protocore_pq_lane::PROTOCORE_PQ_LANE_DMA, &item);
}

static uint8_t g_seq = 0;

void setup()
{
    Serial.begin(115200);
    delay(300);

    // Start the internal DMA lane: a high-priority task (priority 0 -> the lane default,
    // which ranks above the user lane) that processes completed DMA frames.
    protocore_pq_config pq = {};
    pq.handler = on_dma_frame;
    pq.priority = 0; // 0 -> protocore_pq_lane::PROTOCORE_PQ_LANE_DMA's default priority (internal > user)
    pq.core = 1;
    pq.name = "dma_rx";
    if (!Session.workers->queue->start(protocore_pq_lane::PROTOCORE_PQ_LANE_DMA, &pq))
    {
        Serial.println("preempt queue failed to start");
        return;
    }

    // A UART DMA channel in loopback: its egress DMA feeds its own ingress (the
    // simulator's internal jumper), so submitted frames arrive back as RX completions.
    static const PraetCallbackCfg dma_complete = {on_dma_complete, nullptr};
    const PraetCfg ch = {0u, DMA_PERIPHERAL_UART, true, &dma_complete};
    if (!praet.open(&ch))
    {
        Serial.println("dma channel failed to open");
        return;
    }
    Serial.println("dma ingest: egress DMA -> loopback -> RX complete -> preempt queue");
}

void loop()
{
    // Submit one frame per second for egress DMA. On silicon the DMA-complete ISR fires
    // on its own; with the simulator we step the engine with praet.poll.
    uint8_t frame[4] = {0xDE, 0xAD, g_seq, (uint8_t)(0xB0 + g_seq)};
    const PraetTransferCfg frame_out = {0u, frame, sizeof(frame)};
    if (praet.tx_submit(&frame_out))
    {
        g_seq++;
    }
    static const PraetCfg poll_all = {0u, 0u, false, nullptr};
    praet.poll(&poll_all); // drains egress, runs the loopback, completes RX -> fires the callback
    delay(1000);
}
