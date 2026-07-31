// Nrf24Gateway - a real nRF24L01+ 2.4 GHz radio bridged to the gateway.
//
// A second radio driver plugged into the gateway (see RadioGateway / LoRaGateway).
// The nRF24L01+ uses an SPI command protocol and a separate CE pin, so the nrf_bus carries
// an SPI transfer plus a CE-set callback - the only board-specific code. Its hardware pipes
// address the frame, so the "source" is the pipe number (no in-payload codec).
//
//   nRF24 RX --SPI--> pc_nrf24_recv() -> pipe + payload -> pc_gateway_uplink(port, pipe, ...)
//                                                              |
//                                           envelope + topic  nrf24/0/<pipe>
//                                                              |
//                                                     northbound publish (MQTT/HTTP/WS)
//
// It needs an nRF24L01+ wired to the pins below to actually receive; the SPI command
// protocol (init / send / recv) is host-tested in test/test_nrf24.
//
// Build flags (whole build): PC_ENABLE_NRF24=1 PC_ENABLE_GATEWAY=1

#include "protocore.h" // discovers the library (adds src/ to the include path)
#include "services/net/gateway/gateway.h"
#include "services/radio/nrf24/nrf24.h"
#include <SPI.h>

static const int PIN_CE = 4;  // RX/TX enable
static const int PIN_CSN = 5; // SPI chip select
static const uint8_t RADIO_PORT = 0;
static const uint8_t ADDRESS[5] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};

// Board-specific: a full-duplex SPI transfer (CSN low..high) and the CE pin.
static void nrf_spi(const uint8_t *tx, uint8_t *rx, uint8_t len, void *)
{
    digitalWrite(PIN_CSN, LOW);
    for (uint8_t i = 0; i < len; i++)
    {
        rx[i] = SPI.transfer(tx[i]);
    }
    digitalWrite(PIN_CSN, HIGH);
}
static void nrf_ce(bool level, void *)
{
    digitalWrite(PIN_CE, level ? HIGH : LOW);
}
static nrf_bus g_bus = {nrf_spi, nrf_ce, nullptr};

// Northbound publish (the uplink sink): a real build calls mqtt.publish(pc_gateway_topic(m), ...).
static bool northbound_publish(const pc_gateway_msg *m, void *)
{
    char topic[48];
    pc_gateway_topic(m, topic, sizeof(topic));
    Serial.printf("PUBLISH %s  (%u bytes)\n", topic, m->len);
    return true;
}

// Downlink: transmit a command out the radio (the gateway maps dst_addr -> the frame).
static bool radio_tx(uint8_t, uint16_t, const uint8_t *payload, uint16_t len, void *)
{
    if (len > PC_NRF24_PAYLOAD || !pc_nrf24_send(&g_bus, payload, (uint8_t)len))
    {
        return false;
    }
    uint32_t t0 = millis();
    while (!pc_nrf24_tx_done(&g_bus) && millis() - t0 < 500)
    {
        delay(1);
    }
    pc_nrf24_set_rx(&g_bus); // back to listening
    return true;
}

void setup()
{
    Serial.begin(115200);
    delay(300);

    pinMode(PIN_CE, OUTPUT);
    digitalWrite(PIN_CE, LOW);
    pinMode(PIN_CSN, OUTPUT);
    digitalWrite(PIN_CSN, HIGH);
    SPI.begin();
    SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

    nrf_config cfg = {};
    cfg.address = ADDRESS;
    cfg.channel = 76;
    cfg.data_rate = 0; // 1 Mbps
    cfg.tx_power = 3;  // 0 dBm
    if (!pc_nrf24_init(&g_bus, &cfg))
    {
        Serial.println("no nRF24L01+ found on SPI - check wiring");
        return;
    }

    pc_gateway_reset();
    pc_gateway_port_config p = {};
    p.port_id = RADIO_PORT;
    p.kind = pc_gateway_kind::PC_GW_NRF24;
    p.tx = radio_tx;
    pc_gateway_add_port(&p);
    pc_gateway_set_uplink_cb(northbound_publish, nullptr);
    pc_gateway_set_topic_prefix("nrf24");

    pc_nrf24_set_rx(&g_bus);
    Serial.println("nRF24 gateway: RX -> pipe/payload -> publish (nrf24/0/<pipe>)");
}

void loop()
{
    uint8_t buf[PC_NRF24_PAYLOAD];
    uint8_t pipe = 0;
    int n = pc_nrf24_recv(&g_bus, buf, sizeof(buf), &pipe);
    if (n > 0)
    {
        pc_gateway_uplink(RADIO_PORT, pipe, buf, (uint16_t)n, 0); // pipe = source address
    }
    delay(2);
}
