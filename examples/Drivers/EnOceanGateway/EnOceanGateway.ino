// EnOceanGateway - an EnOcean (ESP3) radio bridged to the gateway over UART.
//
// A UART radio plugin (see RadioGateway): an EnOcean serial gateway module (TCM 310 /
// USB 300) streams ESP3 telegrams at 57600 baud. Unlike the SPI radios, there is no chip
// driver - the module does the RF, and the "driver" is purely the ESP3 framing codec.
// We accumulate incoming bytes, frame telegrams with pc_esp3_parse(), pull the sender id +
// payload out of a RADIO_ERP1 telegram, and bridge it northbound.
//
//   TCM310 --UART--> pc_esp3_parse() --> RADIO_ERP1 sender + payload -> pc_gateway_uplink()
//                                                                         |
//                                                  envelope + topic  enocean/0/<sender>
//                                                                         |
//                                                          northbound publish (MQTT/HTTP/WS)
//
// Needs an EnOcean serial module on UART2 to receive; the ESP3 codec is host-tested in
// test/test_enocean.
//
// Build flags (whole build): PC_ENABLE_ENOCEAN=1 PC_ENABLE_GATEWAY=1

#include "protocore.h" // discovers the library (adds src/ to the include path)
#include "services/net/gateway/gateway.h"
#include "services/radio/enocean/enocean.h"

static const uint8_t RADIO_PORT = 0;
static const int PIN_RX = 16, PIN_TX = 17; // UART2 to the EnOcean module

static uint8_t g_buf[256]; // ESP3 accumulation buffer
static uint16_t g_len = 0;

static bool northbound_publish(const pc_gateway_msg *m, void *)
{
    char topic[48];
    pc_gateway_topic(m, topic, sizeof(topic));
    Serial.printf("PUBLISH %s  (%u bytes)\n", topic, m->len);
    return true;
}

void setup()
{
    Serial.begin(115200);
    Serial2.begin(57600, SERIAL_8N1, PIN_RX, PIN_TX); // ESP3 is 57600 8N1
    delay(300);

    pc_gateway_reset();
    pc_gateway_port_config p = {};
    p.port_id = RADIO_PORT;
    p.kind = pc_gateway_kind::PC_GW_ENOCEAN;
    pc_gateway_add_port(&p);
    pc_gateway_set_uplink_cb(northbound_publish, nullptr);
    pc_gateway_set_topic_prefix("enocean");

    Serial.println("EnOcean gateway: ESP3 UART -> codec -> publish (enocean/0/<sender>)");
}

void loop()
{
    while (Serial2.available() && g_len < sizeof(g_buf))
    {
        g_buf[g_len++] = (uint8_t)Serial2.read();
    }

    // Frame as many telegrams as the buffer holds.
    for (;;)
    {
        pc_esp3_packet pkt = {};
        int n = pc_esp3_parse(g_buf, g_len, &pkt);
        if (n == 0)
        {
            break; // need more bytes
        }
        if (n < 0) // junk at the front: drop one byte and resync
        {
            memmove(g_buf, g_buf + 1, --g_len);
            continue;
        }
        // A RADIO_ERP1 telegram ends with a 4-byte sender id + 1 status byte.
        if (pkt.type == pc_esp3_type::ESP3_RADIO_ERP1 && pkt.data_len >= 6)
        {
            const uint8_t *sender = pkt.data + pkt.data_len - 5;
            uint16_t src = (uint16_t)((sender[2] << 8) | sender[3]); // low 16 bits of the id
            pc_gateway_uplink(RADIO_PORT, src, pkt.data, (uint16_t)(pkt.data_len - 5), 0);
        }
        memmove(g_buf, g_buf + n, g_len - n); // consume the telegram
        g_len = (uint16_t)(g_len - n);
    }
}
