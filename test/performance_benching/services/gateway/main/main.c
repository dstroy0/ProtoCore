// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the radio / wireless gateway bridge (server/net/gateway):
// protocore_gateway_uplink() envelopes a received southbound frame (src address / port / rssi / seq) and
// publishes it northbound (fail-closed on no sink / unknown port / rate cap), protocore_gateway_downlink()
// routes a command to a port's transmit callback, and protocore_gateway_topic() formats the routing key -
// all pure dispatch/bookkeeping (no heap). Worked example for performance_benching/device/<service>/: a pure
// protocol/dispatch codec, no hardware involved - the northbound publish and the southbound radio
// transmit are both callbacks (the seam a real MQTT/HTTP binding or a real LoRa/nRF24/CC1101 driver
// plugs into), so this rig benches the bridge itself with trivial stub callbacks standing in for
// both, exactly as test/test_gateway/test_gateway.cpp does on the host. No radio/SPI/I2C/UART/socket
// I/O occurs here - out of scope, same as the DMA + FORWARD-lane ingest that feeds a real port.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/gateway -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/net/gateway/gateway.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Trivial northbound sink: accepts everything, does no work - stands in for the MQTT / HTTP /
// WebSocket / UDP publish a real binding would perform (see test_gateway.cpp's cap_uplink()).
bool stub_uplink(const protocore_gateway_msg *, void *)
{
    return true;
}

// Trivial southbound transmit: accepts everything - stands in for the radio's SPI/UART write a
// real LoRa/nRF24/CC1101 driver would perform (see test_gateway.cpp's cap_tx()).
bool stub_tx(uint8_t, uint16_t, const uint8_t *, uint16_t, void *)
{
    return true;
}

void dbench_run(void)
{
    Gateway.reset(protocore_gateway_span());
    GatewayV.set_uplink_cb_args.fn = stub_uplink;
    GatewayV.set_uplink_cb_args.ctx = NULL;
    Gateway.set_uplink_cb(protocore_gateway_span());

    // Receive-only port (no tx) for the uplink bench, unlimited rate cap so the publish path is
    // always taken (see test_gateway.cpp's test_uplink_envelopes_and_publishes()).
    protocore_gateway_port_config up_cfg = {};
    up_cfg.port_id = 0;
    up_cfg.kind = PROTOCORE_GW_LORA;
    up_cfg.tx = NULL;
    up_cfg.rate_cap = 0;
    GatewayV.add_port_args.cfg = &up_cfg;
    Gateway.add_port(protocore_gateway_span());

    // A second port with a tx callback for the downlink bench (see test_downlink_transmits()).
    protocore_gateway_port_config down_cfg = {};
    down_cfg.port_id = 1;
    down_cfg.kind = PROTOCORE_GW_NRF24;
    down_cfg.tx = stub_tx;
    down_cfg.rate_cap = 0;
    GatewayV.add_port_args.cfg = &down_cfg;
    Gateway.add_port(protocore_gateway_span());

    // 2-byte payload, matches test_gateway.cpp's test_uplink_envelopes_and_publishes().
    static const uint8_t up_payload[2] = {'h', 'i'};
    // 3-byte payload, matches test_gateway.cpp's test_downlink_transmits().
    static const uint8_t down_payload[3] = {'c', 'm', 'd'};

    // Matches test_gateway.cpp's test_topic_format(): port 2, addr 0x42 -> "gw/2/66".
    protocore_gateway_msg topic_msg = {};
    topic_msg.port_id = 2;
    topic_msg.src_addr = 0x42;
    static char topic_buf[32];

    for (;;)
    {
        DBENCH_BANNER("gateway");
        volatile bool sinkb = false;
        volatile uint16_t sink16 = 0;
        DBENCH_OP("protocore_gateway_uplink (publish)", 50000,
                  sinkb = protocore_gateway_uplink(0, 0x42, up_payload, sizeof(up_payload), -50));
        // Fail-closed reject: port 9 was never registered, so find_port() walks the whole table and
        // the frame is dropped + counted, never published (see test_uplink_unknown_port_drops()).
        DBENCH_OP("protocore_gateway_uplink (drop:port)", 50000,
                  sinkb = protocore_gateway_uplink(9, 0x42, up_payload, sizeof(up_payload), -50));
        DBENCH_OP("protocore_gateway_downlink (tx)", 50000,
                  sinkb = protocore_gateway_downlink(1, 0x10, down_payload, sizeof(down_payload)));
        DBENCH_OP("protocore_gateway_topic (format)", 100000,
                  sink16 = protocore_gateway_topic(&topic_msg, topic_buf, sizeof(topic_buf)));
        (void)sinkb;
        (void)sink16;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("gateway")
