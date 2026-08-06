// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the radio / wireless gateway bridge (services/net/gateway):
// pc_gateway_uplink() envelopes a received southbound frame (src address / port / rssi / seq) and
// publishes it northbound (fail-closed on no sink / unknown port / rate cap), pc_gateway_downlink()
// routes a command to a port's transmit callback, and pc_gateway_topic() formats the routing key -
// all pure dispatch/bookkeeping (no heap). Worked example for performance_benching/device/<service>/: a pure
// protocol/dispatch codec, no hardware involved - the northbound publish and the southbound radio
// transmit are both callbacks (the seam a real MQTT/HTTP binding or a real LoRa/nRF24/CC1101 driver
// plugs into), so this rig benches the bridge itself with trivial stub callbacks standing in for
// both, exactly as test/test_gateway/test_gateway.cpp does on the host. No radio/SPI/I2C/UART/socket
// I/O occurs here - out of scope, same as the DMA + FORWARD-lane ingest that feeds a real port.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/gateway -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/net/gateway/gateway.h"
#include <Arduino.h>

namespace
{
// Trivial northbound sink: accepts everything, does no work - stands in for the MQTT / HTTP /
// WebSocket / UDP publish a real binding would perform (see test_gateway.cpp's cap_uplink()).
bool stub_uplink(const pc_gateway_msg *, void *)
{
    return true;
}

// Trivial southbound transmit: accepts everything - stands in for the radio's SPI/UART write a
// real LoRa/nRF24/CC1101 driver would perform (see test_gateway.cpp's cap_tx()).
bool stub_tx(uint8_t, uint16_t, const uint8_t *, uint16_t, void *)
{
    return true;
}
} // namespace

static void gateway_bench_task(void *)
{
    pc_gateway_reset();
    pc_gateway_set_uplink_cb(stub_uplink, nullptr);

    // Receive-only port (no tx) for the uplink bench, unlimited rate cap so the publish path is
    // always taken (see test_gateway.cpp's test_uplink_envelopes_and_publishes()).
    pc_gateway_port_config up_cfg = {};
    up_cfg.port_id = 0;
    up_cfg.kind = pc_gateway_kind::PC_GW_LORA;
    up_cfg.tx = nullptr;
    up_cfg.rate_cap = 0;
    pc_gateway_add_port(&up_cfg);

    // A second port with a tx callback for the downlink bench (see test_downlink_transmits()).
    pc_gateway_port_config down_cfg = {};
    down_cfg.port_id = 1;
    down_cfg.kind = pc_gateway_kind::PC_GW_NRF24;
    down_cfg.tx = stub_tx;
    down_cfg.rate_cap = 0;
    pc_gateway_add_port(&down_cfg);

    // 2-byte payload, matches test_gateway.cpp's test_uplink_envelopes_and_publishes().
    static const uint8_t up_payload[2] = {'h', 'i'};
    // 3-byte payload, matches test_gateway.cpp's test_downlink_transmits().
    static const uint8_t down_payload[3] = {'c', 'm', 'd'};

    // Matches test_gateway.cpp's test_topic_format(): port 2, addr 0x42 -> "gw/2/66".
    pc_gateway_msg topic_msg = {};
    topic_msg.port_id = 2;
    topic_msg.src_addr = 0x42;
    static char topic_buf[32];

    for (;;)
    {
        Serial.printf("DB ==== gateway device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile bool sinkb = false;
        volatile uint16_t sink16 = 0;
        DBENCH_OP("pc_gateway_uplink (publish)", 50000,
                  sinkb = pc_gateway_uplink(0, 0x42, up_payload, sizeof(up_payload), -50));
        // Fail-closed reject: port 9 was never registered, so find_port() walks the whole table and
        // the frame is dropped + counted, never published (see test_uplink_unknown_port_drops()).
        DBENCH_OP("pc_gateway_uplink (drop:port)", 50000,
                  sinkb = pc_gateway_uplink(9, 0x42, up_payload, sizeof(up_payload), -50));
        DBENCH_OP("pc_gateway_downlink (tx)", 50000,
                  sinkb = pc_gateway_downlink(1, 0x10, down_payload, sizeof(down_payload)));
        DBENCH_OP("pc_gateway_topic (format)", 100000,
                  sink16 = pc_gateway_topic(&topic_msg, topic_buf, sizeof(topic_buf)));
        (void)sinkb;
        (void)sink16;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: gateway device microbench");
    xTaskCreatePinnedToCore(gateway_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
