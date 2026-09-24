// Nrf24Gateway - a real nRF24L01+ 2.4 GHz radio bridged to the gateway.
//
// A second radio driver plugged into the gateway (see RadioGateway / LoRaGateway).
// The nRF24L01+ uses an SPI command protocol and a separate CE pin, so the nrf_bus carries
// an SPI transfer plus a CE-set callback - the only board-specific code. Its hardware pipes
// address the frame, so the "source" is the pipe number (no in-payload codec).
//
//   nRF24 RX --SPI--> Nrf24.recv() -> pipe + payload -> Gateway.uplink(port, pipe, ...)
//                                                              |
//                                           envelope + topic  nrf24/0/<pipe>
//                                                              |
//                                                     northbound publish (MQTT/HTTP/WS)
//
// It needs an nRF24L01+ wired to the pins below to actually receive; the SPI command
// protocol (init / send / recv) is host-tested in test/test_nrf24.
//
// Build flags (whole build): PROTOCORE_ENABLE_NRF24=1 PROTOCORE_ENABLE_GATEWAY=1

#include "protocore.h" // discovers the library (adds src/ to the include path)
#include "server/net/gateway/gateway.h"
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
static uint8_t g_nrf24_work[16]; // the borrow an Nrf24 entry takes; the radio codec keeps no state in it

// Northbound publish (the uplink sink): a real build calls mqtt.publish(Gateway.topic(m), ...).
static bool northbound_publish(const protocore_gateway_msg *m, void *)
{
    char topic[48];
    Gateway.topic(protocore_gateway_span(), m, topic, sizeof(topic));
    Serial.printf("PUBLISH %s  (%u bytes)\n", topic, m->len);
    return true;
}

// Downlink: transmit a command out the radio (the gateway maps dst_addr -> the frame).
static bool radio_tx(uint8_t, uint16_t, const uint8_t *payload, uint16_t len, void *)
{
    if (len > PROTOCORE_NRF24_PAYLOAD || !Nrf24.send(g_nrf24_work, &g_bus, payload, (uint8_t)len))
    {
        return false;
    }
    uint32_t t0 = millis();
    while (!Nrf24.tx_done(g_nrf24_work, &g_bus) && millis() - t0 < 500)
    {
        delay(1);
    }
    Nrf24.set_rx(g_nrf24_work, &g_bus); // back to listening
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
    if (!Nrf24.init(g_nrf24_work, &g_bus, &cfg))
    {
        Serial.println("no nRF24L01+ found on SPI - check wiring");
        return;
    }

    Gateway.reset(protocore_gateway_span());
    protocore_gateway_port_config p = {};
    p.port_id = RADIO_PORT;
    p.kind = protocore_gateway_kind::PROTOCORE_GW_NRF24;
    p.tx = radio_tx;
    Gateway.add_port(protocore_gateway_span(), &p);
    Gateway.set_uplink_cb(protocore_gateway_span(), northbound_publish, nullptr);
    Gateway.set_topic_prefix(protocore_gateway_span(), "nrf24");

    Nrf24.set_rx(g_nrf24_work, &g_bus);
    Serial.println("nRF24 gateway: RX -> pipe/payload -> publish (nrf24/0/<pipe>)");
}

void loop()
{
    uint8_t buf[PROTOCORE_NRF24_PAYLOAD];
    uint8_t pipe = 0;
    int n = Nrf24.recv(g_nrf24_work, &g_bus, buf, sizeof(buf), &pipe);
    if (n > 0)
    {
        Gateway.uplink(protocore_gateway_span(), RADIO_PORT, pipe, buf, (uint16_t)n, 0); // pipe = source address
    }
    delay(2);
}
