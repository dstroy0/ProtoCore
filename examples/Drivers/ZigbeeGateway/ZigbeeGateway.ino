// ZigbeeGateway - a Zigbee (EZSP/ASH) NCP bridged to the gateway over UART.
//
// A UART radio plugin (see RadioGateway): a Silicon Labs EmberZNet network co-processor
// speaks EZSP over the ASH data-link protocol on a UART. This resets the NCP and decodes
// the ASH frames it sends; a DATA frame carrying an EZSP callback (e.g. an incoming Zigbee
// message) is bridged northbound.
//
//   Zigbee NCP --UART--> Zigbee.ash_frame_decode() --> EZSP payload -> Gateway.uplink()
//                                                                     |
//                                              envelope + topic  zigbee/0/<node>
//                                                                     |
//                                                      northbound publish (MQTT/HTTP/WS)
//
// The ASH framing (byte-stuffing + CRC16) is services/radio/zigbee, host-tested in
// test/test_zigbee. Interpreting the EZSP command inside a DATA frame (version negotiation,
// sequence numbers, the incomingMessageHandler fields) is application work; this sketch
// bridges the raw EZSP payload to show the transport path.
//
// Build flags (whole build): PROTOCORE_ENABLE_ZIGBEE=1 PROTOCORE_ENABLE_GATEWAY=1

#include "protocore.h" // discovers the library (adds src/ to the include path)
#include "server/net/gateway/gateway.h"
#include "services/radio/zigbee/zigbee.h"

static const uint8_t RADIO_PORT = 0;
static const int PIN_RX = 16, PIN_TX = 17; // UART2 to the Zigbee NCP

static uint8_t g_buf[512];
static uint16_t g_len = 0;
static uint8_t g_zigbee_work[16]; // the borrow a Zigbee entry takes; the ASH codec keeps no state in it

static bool northbound_publish(const protocore_gateway_msg *m, void *)
{
    char topic[48];
    Gateway.topic(protocore_gateway_span(), m, topic, sizeof(topic));
    Serial.printf("PUBLISH %s  (%u bytes)\n", topic, m->len);
    return true;
}

static void drop_front(uint16_t n)
{
    memmove(g_buf, g_buf + n, g_len - n);
    g_len = (uint16_t)(g_len - n);
}

void setup()
{
    Serial.begin(115200);
    Serial2.begin(115200, SERIAL_8N1, PIN_RX, PIN_TX);
    delay(300);

    Gateway.reset(protocore_gateway_span());
    protocore_gateway_port_config p = {};
    p.port_id = RADIO_PORT;
    p.kind = protocore_gateway_kind::PROTOCORE_GW_ZIGBEE;
    Gateway.add_port(protocore_gateway_span(), &p);
    Gateway.set_uplink_cb(protocore_gateway_span(), northbound_publish, nullptr);
    Gateway.set_topic_prefix(protocore_gateway_span(), "zigbee");

    uint8_t rst[8];
    uint16_t n = Zigbee.ash_frame_encode(g_zigbee_work, ASH_RST, nullptr, 0, rst, sizeof(rst)); // reset the NCP
    Serial2.write(rst, n);
    Serial.println("Zigbee gateway: EZSP/ASH -> codec -> publish (zigbee/0/<node>)");
}

void loop()
{
    while (Serial2.available() && g_len < sizeof(g_buf))
    {
        g_buf[g_len++] = (uint8_t)Serial2.read();
    }

    for (;;)
    {
        if (g_len == 0)
        {
            break;
        }
        uint8_t control = 0, payload[PROTOCORE_ZIGBEE_MAX_DATA];
        uint16_t plen = 0;
        int n = Zigbee.ash_frame_decode(g_zigbee_work, g_buf, g_len, &control, payload, sizeof(payload), &plen);
        if (n == 0)
        {
            break; // need more
        }
        if (n < 0)
        {
            drop_front(1); // resync past a bad frame
            continue;
        }
        // A DATA frame (control bit 7 clear) carries an EZSP command; ACK/RSTACK/ERROR are
        // control frames. Bridge the EZSP payload of a DATA frame northbound.
        if ((control & 0x80) == 0 && plen > 0)
        {
            uint16_t node = plen >= 2 ? (uint16_t)((payload[0] << 8) | payload[1]) : payload[0];
            Gateway.uplink(protocore_gateway_span(), RADIO_PORT, node, payload, plen, 0);
        }
        drop_front((uint16_t)n);
    }
}
