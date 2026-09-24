// RadioGateway - bridge a southbound radio to the northbound stack (the capstone).
//
// The whole v5 pipeline as a wireless gateway: a radio (here a simulated LoRa module on a
// DMA channel) receives frames; the DMA-complete callback posts each onto the FORWARD lane;
// the lane's task runs a tiny per-radio codec (first two bytes are the source node address,
// the rest is the payload) and hands it to the gateway, which envelopes it (address / port /
// RSSI / seq) and PUBLISHES it northbound - wire that to MQTT / HTTP / WebSocket. A command
// runs the other way: Gateway.downlink() transmits on the radio.
//
//   radio RX --DMA--> callback --post--> FORWARD lane --> codec --> Gateway.uplink()
//                                                                        |
//                                                     envelope + topic <prefix>/<port>/<addr>
//                                                                        |
//                                                            northbound publish (MQTT/HTTP/WS)
//
// The radio TX and the northbound publish are callbacks, so this runs with no radio; a real
// build swaps the simulator feed for the module's SPI RX and the publish for an MQTT client.
//
// Build flags (whole build):
//   PROTOCORE_ENABLE_DMA=1 PROTOCORE_ENABLE_PREEMPT_QUEUE=1 PROTOCORE_ENABLE_GATEWAY=1 PROTOCORE_DMA_SIMULATE=1

#include "protocore.h" // discovers the library (adds src/ to the include path)
#include "server/net/gateway/gateway.h"
#include "mmgr/dma/dma.h"
#include "server/core/preempt_queue/preempt_queue.h"


static const uint8_t RADIO_PORT = 0; // DMA channel 0 == the LoRa module

// Northbound publish: a real build calls mqtt.publish(topic, payload, len). We format the
// routing key and print it.
static bool northbound_publish(const protocore_gateway_msg *m, void *)
{
    char topic[48];
    Gateway.topic(protocore_gateway_span(), m, topic, sizeof(topic));
    Serial.printf("  PUBLISH %s  (%u bytes, rssi %d, seq %u)\n", topic, m->len, m->rssi, (unsigned)m->seq);
    return true;
}

// Southbound transmit (downlink): a real build calls the radio's send(dst, payload).
static bool radio_tx(uint8_t port, uint16_t dst, const uint8_t *, uint16_t len, void *)
{
    Serial.printf("  TX port%u -> node %u (%u bytes)\n", port, dst, len);
    return true;
}

// FORWARD-lane item: a self-contained copy of the radio frame (DmaIngest explains why).
struct radio_frame
{
    uint16_t len;
    uint8_t port;
    uint8_t bytes[24];
};
union pq_item {
    radio_frame f;
    uint8_t raw[PROTOCORE_PQ_ITEM_SIZE];
};

// FORWARD lane task (high priority): the per-radio codec + the northbound bridge, off the
// "ISR". Frame layout: [addr_hi][addr_lo][payload...].
static void on_forward(const void *item, void *)
{
    const radio_frame *f = &((const pq_item *)item)->f;
    if (f->len < 2)
    {
        return; // need the 2-byte node address header
    }
    uint16_t addr = ((uint16_t)f->bytes[0] << 8) | f->bytes[1];
    Gateway.uplink(protocore_gateway_span(), f->port, addr, f->bytes + 2, (uint16_t)(f->len - 2), /*rssi*/ -60);
}

// DMA-complete on the radio port: copy the frame and post it onto the FORWARD lane.
static void on_dma_complete(const protocore_dma_event *ev, void *)
{
    if (ev->dir != protocore_dma_dir::PROTOCORE_DMA_RX)
    {
        return;
    }
    pq_item it = {};
    it.f.port = RADIO_PORT;
    it.f.len = ev->len;
    uint16_t n = (ev->len < sizeof(it.f.bytes)) ? ev->len : sizeof(it.f.bytes);
    memcpy(it.f.bytes, ev->data, n);
    PreemptQueueV.lane = protocore_pq_lane::PROTOCORE_PQ_LANE_FORWARD;
    PreemptQueueV.post_args.item = &it;
    PreemptQueue.post_from_isr(protocore_preempt_queue_span());
}

// Simulated radio RX: the in-library DMA simulator now lives on the host test bench, so the
// sketch stands in for the radio's DMA engine and completes an RX transfer itself - the same
// event a real SPI DMA completion delivers to on_dma_complete().
static uint16_t g_rx_seq = 0;
static void radio_sim_rx(uint8_t ch, const uint8_t *bytes, uint16_t len)
{
    protocore_dma_event ev = {};
    ev.data = bytes;
    ev.t_ms = millis();
    ev.t_us = micros();
    ev.len = len;
    ev.seq = g_rx_seq++;
    ev.channel = ch;
    ev.periph = protocore_dma_periph::PROTOCORE_DMA_SPI;
    ev.dir = protocore_dma_dir::PROTOCORE_DMA_RX;
    on_dma_complete(&ev, nullptr);
}

void setup()
{
    Serial.begin(115200);
    delay(300);

    protocore_pq_config fwd = {};
    fwd.handler = on_forward;
    fwd.priority = 0; // FORWARD lane default (above the user lane)
    fwd.core = 1;
    fwd.name = "gw_rx";
    PreemptQueueV.lane = protocore_pq_lane::PROTOCORE_PQ_LANE_FORWARD;
    PreemptQueueV.cfg = &fwd;
    PreemptQueue.start(protocore_preempt_queue_span());

    protocore_dma_config a = {};
    a.channel = RADIO_PORT;
    a.periph = protocore_dma_periph::PROTOCORE_DMA_SPI; // LoRa modules are SPI
    a.on_complete = on_dma_complete;
    protocore_dma_open(&a);

    // The gateway: one LoRa port, publishing under "lora/<port>/<addr>".
    Gateway.reset(protocore_gateway_span());
    protocore_gateway_port_config p = {};
    p.port_id = RADIO_PORT;
    p.kind = protocore_gateway_kind::PROTOCORE_GW_LORA;
    p.tx = radio_tx;
    Gateway.add_port(protocore_gateway_span(), &p);
    Gateway.set_uplink_cb(protocore_gateway_span(), northbound_publish, nullptr);
    Gateway.set_topic_prefix(protocore_gateway_span(), "lora");

    Serial.println("gateway: LoRa RX -> DMA -> FORWARD lane -> codec -> publish (lora/port/addr)");
}

static uint8_t g_seq = 0;

void loop()
{
    // A radio frame arrives from one of four nodes (0x40..0x43): [addr_hi][addr_lo][0xAB][seq].
    uint16_t addr = 0x0040 + (g_seq & 0x03);
    uint8_t frame[4] = {(uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF), 0xAB, g_seq};
    radio_sim_rx(RADIO_PORT, frame, sizeof(frame)); // completes RX -> FORWARD lane -> codec -> uplink publish
    protocore_dma_poll();                            // steps a real DMA driver (a no-op with none bound)
    g_seq++;

    if ((g_seq & 0x07) == 0)
    {
        uint8_t cmd[2] = {0x01, g_seq};
        Gateway.downlink(protocore_gateway_span(), RADIO_PORT, 0x0040, cmd, sizeof(cmd)); // command node 0x40
        protocore_gateway_stats st;
        Gateway.get_stats(protocore_gateway_span(), &st);
        Serial.printf("stats: up_in=%lu published=%lu down_sent=%lu\n", (unsigned long)st.up_in,
                      (unsigned long)st.up_published, (unsigned long)st.down_sent);
    }
    delay(1000);
}
