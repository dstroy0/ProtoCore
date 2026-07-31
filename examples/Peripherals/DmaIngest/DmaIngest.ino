// DmaIngest - DMA peripheral ingest: DMA-complete -> preempting queue -> task.
//
// The high-throughput field-bus ingest path. A DMA channel moves peripheral bytes
// (here a UART) into a static buffer with the CPU free; the DMA-complete callback -
// ISR context on real silicon - does the tiniest possible thing: it posts the
// completion into the preempting work queue (see PreemptQueue), and a dedicated
// high-priority task is preempted to process the bytes off the interrupt.
//
// There is no physical loopback wire on the bench, so the channel runs the built-in
// ingress/egress SIMULATOR (PC_DMA_SIMULATE=1): each second loop() submits a frame
// for egress DMA, the loopback jumper feeds it back into RX, pc_dma_poll() completes
// the transfer, and the frame arrives at the processing task - the whole pipeline,
// end to end, on the device itself. Swap the simulator for a real UART/SPI DMA driver
// (pc_dma_hw_*, PC_DMA_SIMULATE=0) and the sketch is unchanged.
//
// Build flags (whole build, not just this sketch):
//   PC_ENABLE_DMA=1 PC_ENABLE_PREEMPT_QUEUE=1 PC_DMA_SIMULATE=1

#include "protocore.h" // discovers the library (adds src/ to the include path)
#include "services/system/dma/dma.h"
#include "services/system/preempt_queue/preempt_queue.h"
#include <string.h>

// The preempting-queue item: a SELF-CONTAINED copy of the frame bytes, padded to the
// queue item size (the queue copies a fixed PC_PQ_ITEM_SIZE bytes). We copy the bytes
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
    uint8_t raw[PC_PQ_ITEM_SIZE];
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
static void on_dma_complete(const pc_dma_event *ev, void *)
{
    if (ev->dir != pc_dma_dir::PC_DMA_RX)
    {
        return; // TX-complete just frees the channel; nothing to process
    }
    pq_item item = {};
    item.msg.len = ev->len;
    item.msg.seq = ev->seq;
    item.msg.channel = ev->channel;
    uint16_t n = (ev->len < sizeof(item.msg.bytes)) ? ev->len : sizeof(item.msg.bytes);
    memcpy(item.msg.bytes, ev->data, n);
    pc_pq_post_lane_from_isr(pc_pq_lane::PC_PQ_LANE_DMA, &item);
}

static uint8_t g_seq = 0;

void setup()
{
    Serial.begin(115200);
    delay(300);

    // Start the internal DMA lane: a high-priority task (priority 0 -> the lane default,
    // which ranks above the user lane) that processes completed DMA frames.
    pc_pq_config pq = {};
    pq.handler = on_dma_frame;
    pq.priority = 0; // 0 -> pc_pq_lane::PC_PQ_LANE_DMA's default priority (internal > user)
    pq.core = 1;
    pq.name = "dma_rx";
    if (!pc_pq_start_lane(pc_pq_lane::PC_PQ_LANE_DMA, &pq))
    {
        Serial.println("preempt queue failed to start");
        return;
    }

    // A UART DMA channel in loopback: its egress DMA feeds its own ingress (the
    // simulator's internal jumper), so submitted frames arrive back as RX completions.
    pc_dma_config ch = {};
    ch.channel = 0;
    ch.periph = pc_dma_periph::PC_DMA_UART;
    ch.loopback = true;
    ch.on_complete = on_dma_complete;
    if (!pc_dma_open(&ch))
    {
        Serial.println("dma channel failed to open");
        return;
    }
    Serial.println("dma ingest: egress DMA -> loopback -> RX complete -> preempt queue");
}

void loop()
{
    // Submit one frame per second for egress DMA. On silicon the DMA-complete ISR fires
    // on its own; with the simulator we step the engine with pc_dma_poll().
    uint8_t frame[4] = {0xDE, 0xAD, g_seq, (uint8_t)(0xB0 + g_seq)};
    if (pc_dma_tx_submit(0, frame, sizeof(frame)))
    {
        g_seq++;
    }
    pc_dma_poll(); // drains egress, runs the loopback, completes RX -> fires the callback
    delay(1000);
}
