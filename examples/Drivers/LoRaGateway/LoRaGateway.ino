// LoRaGateway - a real LoRa (SX127x / RFM95) radio bridged to the gateway.
//
// The radio-plugin half of RadioGateway: instead of a simulated feed, this drives an
// actual Semtech SX127x over SPI. The protocore_lora_bus register-access callbacks are the only
// hardware-specific code (a few SPI transfers); everything above - the RadioHead frame
// codec, the gateway envelope + publish, the downlink - is portable.
//
//   RFM95 RX --SPI--> Lora.recv() --> Lora.frame_parse() --> Gateway.uplink()
//                                                                 |
//                                              envelope + topic  lora/0/<from>
//                                                                 |
//                                                        northbound publish (MQTT/HTTP/WS)
//
// A production build triggers RX off the module's DIO0 interrupt and rides the DMA +
// FORWARD-lane path (see RadioGateway); this sketch polls to stay simple. It needs an
// RFM95 / SX1276 wired to the pins below to actually receive; the codec + register protocol
// are host-tested in test/test_lora.
//
// Build flags (whole build): PROTOCORE_ENABLE_LORA=1 PROTOCORE_ENABLE_GATEWAY=1

#include "protocore.h" // discovers the library (adds src/ to the include path)
#include "server/net/gateway/gateway.h"
#include "services/radio/lora/lora.h"
#include <SPI.h>

static const int PIN_NSS = 5;   // SPI chip select
static const int PIN_RST = 14;  // radio reset
static const int PIN_DIO0 = 26; // RxDone / TxDone interrupt (polled here)
static const uint8_t NODE_SELF = 0x01;
static const uint8_t RADIO_PORT = 0;

// The only hardware-specific code: read / write one SX127x register over SPI.
static uint8_t spi_read(uint8_t reg, void *)
{
    digitalWrite(PIN_NSS, LOW);
    SPI.transfer(reg & 0x7F);
    uint8_t v = SPI.transfer(0x00);
    digitalWrite(PIN_NSS, HIGH);
    return v;
}
static void spi_write(uint8_t reg, uint8_t val, void *)
{
    digitalWrite(PIN_NSS, LOW);
    SPI.transfer(reg | 0x80);
    SPI.transfer(val);
    digitalWrite(PIN_NSS, HIGH);
}
static protocore_lora_bus g_bus = {spi_read, spi_write, nullptr};

static uint8_t g_tx_id = 0;
static uint8_t g_lora_work[16]; // the borrow a Lora entry takes; the SX127x codec keeps no state in it

// Northbound publish (the uplink sink): a real build calls mqtt.publish(Gateway.topic(m), ...).
static bool northbound_publish(const protocore_gateway_msg *m, void *)
{
    char topic[48];
    Gateway.topic(protocore_gateway_span(), m, topic, sizeof(topic));
    Serial.printf("PUBLISH %s  (%u bytes, rssi %d)\n", topic, m->len, m->rssi);
    return true;
}

// Downlink: build a frame to dst and transmit it on the radio.
static bool radio_tx(uint8_t, uint16_t dst, const uint8_t *payload, uint16_t len, void *)
{
    protocore_lora_header h = {(uint8_t)dst, NODE_SELF, g_tx_id++, 0x00};
    uint8_t frame[PROTOCORE_LORA_MAX_PAYLOAD + 4];
    uint16_t n = Lora.frame_build(g_lora_work, &h, payload, len, frame, sizeof(frame));
    if (n == 0 || !Lora.send(g_lora_work, &g_bus, frame, (uint8_t)n))
    {
        return false;
    }
    uint32_t t0 = millis();
    while (!Lora.tx_done(g_lora_work, &g_bus) && millis() - t0 < 2000)
    {
        delay(1);
    }
    Lora.set_rx(g_lora_work, &g_bus); // back to listening
    return true;
}

void setup()
{
    Serial.begin(115200);
    delay(300);

    pinMode(PIN_NSS, OUTPUT);
    digitalWrite(PIN_NSS, HIGH);
    pinMode(PIN_RST, OUTPUT);
    pinMode(PIN_DIO0, INPUT);
    SPI.begin();
    SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

    digitalWrite(PIN_RST, LOW); // reset pulse
    delay(10);
    digitalWrite(PIN_RST, HIGH);
    delay(10);

    protocore_lora_config cfg = {};
    cfg.freq_hz = 915000000UL; // 868100000 in EU868
    cfg.spreading = 7;
    cfg.bandwidth = 7; // 125 kHz
    cfg.coding_rate = 1;
    cfg.sync_word = 0x12; // private network
    cfg.tx_power = 17;
    if (!Lora.init(g_lora_work, &g_bus, &cfg))
    {
        Serial.println("no SX127x found on SPI - check wiring");
        return;
    }

    Gateway.reset(protocore_gateway_span());
    protocore_gateway_port_config p = {};
    p.port_id = RADIO_PORT;
    p.kind = protocore_gateway_kind::PROTOCORE_GW_LORA;
    p.tx = radio_tx;
    Gateway.add_port(protocore_gateway_span(), &p);
    Gateway.set_uplink_cb(protocore_gateway_span(), northbound_publish, nullptr);
    Gateway.set_topic_prefix(protocore_gateway_span(), "lora");

    Lora.set_rx(g_lora_work, &g_bus);
    Serial.println("LoRa gateway: SX127x RX -> codec -> publish (lora/0/<from>)");
}

void loop()
{
    // Poll for a received frame; a production build waits on the DIO0 interrupt instead.
    uint8_t buf[PROTOCORE_LORA_MAX_PAYLOAD + 4];
    int16_t rssi = 0;
    int n = Lora.recv(g_lora_work, &g_bus, buf, sizeof(buf), &rssi);
    if (n > 0)
    {
        protocore_lora_header h = {};
        const uint8_t *payload = nullptr;
        uint16_t plen = 0;
        if (Lora.frame_parse(g_lora_work, buf, (uint16_t)n, &h, &payload, &plen))
        {
            Gateway.uplink(protocore_gateway_span(), RADIO_PORT, h.from, payload, plen, rssi); // bridge northbound
        }
    }
    delay(5);
}
