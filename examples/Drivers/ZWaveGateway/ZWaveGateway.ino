// ZWaveGateway - a Z-Wave mesh bridged to the gateway over the Serial API.
//
// A UART radio plugin (see RadioGateway): a Silicon Labs 500 / 700-series Z-Wave
// controller speaks the Serial API over UART, and this bridges its mesh to the web. When a
// node reports (an ApplicationCommandHandler frame), we pull the source node id + payload
// and publish it northbound. Each data frame is acknowledged with a single ACK byte.
//
//   Z-Wave mesh --UART--> pc_zwave_parse_frame() --> node + payload -> pc_gateway_uplink()
//                                                                        |
//                                                 envelope + topic  zwave/0/<node>
//                                                                        |
//                                                         northbound publish (MQTT/HTTP/WS)
//
// Needs a Z-Wave Serial API controller on UART2; the frame codec is host-tested in
// test/test_zwave.
//
// Build flags (whole build): PC_ENABLE_ZWAVE=1 PC_ENABLE_GATEWAY=1

#include "protocore.h" // discovers the library (adds src/ to the include path)
#include "services/net/gateway/gateway.h"
#include "services/radio/zwave/zwave.h"

static const uint8_t RADIO_PORT = 0;
static const int PIN_RX = 16, PIN_TX = 17; // UART2 to the Z-Wave controller

static uint8_t g_buf[256];
static uint16_t g_len = 0;

// FUNC_ID_APPLICATION_COMMAND_HANDLER: a node sent an application command.
static const uint8_t FUNC_APP_CMD_HANDLER = 0x04;

static bool northbound_publish(const pc_gateway_msg *m, void *)
{
    char topic[48];
    pc_gateway_topic(m, topic, sizeof(topic));
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

    pc_gateway_reset();
    pc_gateway_port_config p = {};
    p.port_id = RADIO_PORT;
    p.kind = pc_gateway_kind::PC_GW_ZWAVE;
    pc_gateway_add_port(&p);
    pc_gateway_set_uplink_cb(northbound_publish, nullptr);
    pc_gateway_set_topic_prefix("zwave");

    uint8_t frame[8];
    uint16_t n = pc_zwave_build_frame(pc_zwave_type::ZWAVE_REQ, 0x15, nullptr, 0, frame, sizeof(frame)); // GetVersion
    Serial2.write(frame, n);
    Serial.println("Z-Wave gateway: Serial API -> codec -> publish (zwave/0/<node>)");
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
        // Single-byte control frames (ACK / NAK / CAN) are consumed and ignored.
        if (pc_zwave_is_ack(g_buf[0]) || pc_zwave_is_nak(g_buf[0]) || pc_zwave_is_can(g_buf[0]))
        {
            drop_front(1);
            continue;
        }
        uint8_t type = 0, cmd = 0, pdlen = 0;
        const uint8_t *pd = nullptr;
        int n = pc_zwave_parse_frame(g_buf, g_len, &type, &cmd, &pd, &pdlen);
        if (n == 0)
        {
            break; // need more
        }
        if (n < 0)
        {
            drop_front(1); // junk: resync
            continue;
        }
        uint8_t ack = Zwave::ZWAVE_ACK;
        Serial2.write(&ack, 1); // acknowledge the data frame
        // ApplicationCommandHandler payload: rxStatus | sourceNode | cmdLen | ZW cmd...
        if (cmd == FUNC_APP_CMD_HANDLER && pdlen >= 3)
        {
            uint8_t node = pd[1];
            pc_gateway_uplink(RADIO_PORT, node, &pd[3], (uint16_t)(pdlen - 3), 0);
        }
        drop_front((uint16_t)n);
    }
}
