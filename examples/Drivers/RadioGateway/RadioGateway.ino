// RadioGateway - bridge a southbound radio to the northbound stack (the capstone).
//
// The whole v5 pipeline as a wireless gateway: a radio (here a simulated LoRa module on a
// DMA channel) receives frames; the DMA-complete callback posts each onto the FORWARD lane;
// the lane's task runs a tiny per-radio codec (first two bytes are the source node address,
// the rest is the payload) and hands it to the gateway, which envelopes it (address / port /
// RSSI / seq) and PUBLISHES it northbound - wire that to MQTT / HTTP / WebSocket. A command
// runs the other way: pc_gateway_downlink() transmits on the radio.
//
//   radio RX --DMA--> callback --post--> FORWARD lane --> codec --> pc_gateway_uplink()
//                                                                        |
//                                                     envelope + topic <prefix>/<port>/<addr>
//                                                                        |
//                                                            northbound publish (MQTT/HTTP/WS)
//
// The radio TX and the northbound publish are callbacks, so this runs with no radio; a real
// build swaps the simulator feed for the module's SPI RX and the publish for an MQTT client.
//
// Build flags (whole build):
//   PC_ENABLE_DMA=1 PC_ENABLE_PREEMPT_QUEUE=1 PC_ENABLE_GATEWAY=1 PC_DMA_SIMULATE=1

#include "protocore.h" // discovers the library (adds src/ to the include path)
#include "services/net/gateway/gateway.h"
#include "mmgr/dma.h"
#include "network_drivers/session/preempt_queue.h"
#include <string.h>

static const uint8_t RADIO_PORT = 0; // DMA channel 0 == the LoRa module

// Northbound publish: a real build calls mqtt.publish(topic, payload, len). We format the
// routing key and print it.
static bool northbound_publish(const pc_gateway_msg *m, void *)
{
    char topic[48];
    pc_gateway_topic(m, topic, sizeof(topic));
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
    uint8_t raw[PC_PQ_ITEM_SIZE];
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
    pc_gateway_uplink(f->port, addr, f->bytes + 2, (uint16_t)(f->len - 2), /*rssi*/ -60);
}

// DMA-complete on the radio port: copy the frame and post it onto the FORWARD lane.
static void on_dma_complete(const pc_dma_event *ev, void *)
{
    if (ev->dir != pc_dma_dir::PC_DMA_RX)
    {
        return;
    }
    pq_item it = {};
    it.f.port = RADIO_PORT;
    it.f.len = ev->len;
    uint16_t n = (ev->len < sizeof(it.f.bytes)) ? ev->len : sizeof(it.f.bytes);
    memcpy(it.f.bytes, ev->data, n);
    pc_pq_post_lane_from_isr(pc_pq_lane::PC_PQ_LANE_FORWARD, &it);
}

void setup()
{
    Serial.begin(115200);
    delay(300);

    pc_pq_config fwd = {};
    fwd.handler = on_forward;
    fwd.priority = 0; // FORWARD lane default (above the user lane)
    fwd.core = 1;
    fwd.name = "gw_rx";
    pc_pq_start_lane(pc_pq_lane::PC_PQ_LANE_FORWARD, &fwd);

    pc_dma_config a = {};
    a.channel = RADIO_PORT;
    a.periph = pc_dma_periph::PC_DMA_SPI; // LoRa modules are SPI
    a.on_complete = on_dma_complete;
    pc_dma_open(&a);

    // The gateway: one LoRa port, publishing under "lora/<port>/<addr>".
    pc_gateway_reset();
    pc_gateway_port_config p = {};
    p.port_id = RADIO_PORT;
    p.kind = pc_gateway_kind::PC_GW_LORA;
    p.tx = radio_tx;
    pc_gateway_add_port(&p);
    pc_gateway_set_uplink_cb(northbound_publish, nullptr);
    pc_gateway_set_topic_prefix("lora");

    Serial.println("gateway: LoRa RX -> DMA -> FORWARD lane -> codec -> publish (lora/port/addr)");
}

static uint8_t g_seq = 0;

void loop()
{
    // A radio frame arrives from one of four nodes (0x40..0x43): [addr_hi][addr_lo][0xAB][seq].
    uint16_t addr = 0x0040 + (g_seq & 0x03);
    uint8_t frame[4] = {(uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF), 0xAB, g_seq};
    pc_dma_sim_feed(RADIO_PORT, frame, sizeof(frame));
    pc_dma_poll(); // completes RX -> FORWARD lane -> codec -> uplink publish
    g_seq++;

    if ((g_seq & 0x07) == 0)
    {
        uint8_t cmd[2] = {0x01, g_seq};
        pc_gateway_downlink(RADIO_PORT, 0x0040, cmd, sizeof(cmd)); // command node 0x40
        pc_gateway_stats st;
        pc_gateway_get_stats(&st);
        Serial.printf("stats: up_in=%lu published=%lu down_sent=%lu\n", (unsigned long)st.up_in,
                      (unsigned long)st.up_published, (unsigned long)st.down_sent);
    }
    delay(1000);
}
