// ThreadGateway - an OpenThread RCP bridged to the gateway over spinel / HDLC-lite.
//
// A UART radio plugin (see RadioGateway): an OpenThread radio co-processor (RCP - an
// nRF52840 / EFR32, or an ESP32-C6 running ot_rcp) speaks spinel over the HDLC-lite framing
// on a UART, so an 802.15.4 / Thread mesh is bridged to IP and the web. This decodes the
// HDLC-lite frames and both (a) decodes the named spinel property each one carries and (b)
// bridges the raw spinel payload northbound.
//
//   Thread RCP --UART--> pc_spinel_frame_decode() --> spinel command -> property registry
//                                                                     -> pc_gateway_uplink()
//                                                                         |
//                                                  envelope + topic  thread/0/<tid>
//                                                                         |
//                                                          northbound publish (MQTT/HTTP/WS)
//
// The HDLC-lite framing (byte-stuffing + CRC-16/X-25 FCS), the spinel command layer, and the
// property registry + typed value accessors are services/radio/thread, host-tested in
// test/test_thread. This sketch queries NCP_VERSION / PROTOCOL_VERSION at boot and prints
// each inbound property update by name, so the value semantics are exercised end to end.
//
// Build flags (whole build): PC_ENABLE_THREAD=1 PC_ENABLE_GATEWAY=1

#include "protocore.h" // discovers the library (adds src/ to the include path)
#include "services/net/gateway/gateway.h"
#include "services/radio/thread/thread.h"

static const uint8_t RADIO_PORT = 0;
static const int PIN_RX = 16, PIN_TX = 17; // UART2 to the Thread RCP

static uint8_t g_buf[1024];
static uint16_t g_len = 0;

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

// Frame a spinel PROP_VALUE_GET(prop) and write it to the RCP.
static void rcp_get(uint32_t prop)
{
    uint8_t payload[16];
    uint16_t plen = pc_spinel_command_build(pc_spinel_header(0, 1), SpinelCmd::SPINEL_CMD_PROP_VALUE_GET, prop, nullptr,
                                            0, payload, sizeof(payload));
    uint8_t frame[32];
    uint16_t n = pc_spinel_frame_encode(payload, plen, frame, sizeof(frame));
    if (n)
    {
        Serial2.write(frame, n);
    }
}

// Decode a spinel command payload by its registered property type and print it by name.
static void print_property(const uint8_t *payload, uint16_t plen)
{
    uint8_t header = 0;
    uint32_t cmd = 0, prop = 0;
    const uint8_t *val = nullptr;
    uint16_t vlen = 0;
    if (pc_spinel_command_parse(payload, plen, &header, &cmd, &prop, &val, &vlen) < 0)
    {
        return;
    }
    if (cmd != SpinelCmd::SPINEL_CMD_PROP_VALUE_IS)
    {
        return; // only report property updates
    }

    SpinelReader r;
    pc_spinel_reader_init(&r, val, vlen);
    const char *name = pc_spinel_prop_name(prop);
    if (prop == SpinelProp::SPINEL_PROP_LAST_STATUS)
    {
        uint32_t status = 0;
        if (pc_spinel_get_uint(&r, &status))
        {
            Serial.printf("  %s = %s (%u)\n", name, pc_spinel_status_name(status), status);
        }
    }
    else if (prop == SpinelProp::SPINEL_PROP_NCP_VERSION || prop == SpinelProp::SPINEL_PROP_NET_NETWORK_NAME)
    {
        const char *s = nullptr;
        uint16_t slen = 0;
        if (pc_spinel_get_utf8(&r, &s, &slen))
        {
            Serial.printf("  %s = %.*s\n", name, (int)slen, s);
        }
    }
    else if (prop == SpinelProp::SPINEL_PROP_PROTOCOL_VERSION)
    {
        uint32_t major = 0, minor = 0;
        if (pc_spinel_get_uint(&r, &major) && pc_spinel_get_uint(&r, &minor))
        {
            Serial.printf("  %s = %u.%u\n", name, major, minor);
        }
    }
    else if (prop == SpinelProp::SPINEL_PROP_MAC_15_4_PANID || prop == SpinelProp::SPINEL_PROP_MAC_15_4_SADDR)
    {
        uint16_t v = 0;
        if (pc_spinel_get_u16(&r, &v))
        {
            Serial.printf("  %s = 0x%04X\n", name, v);
        }
    }
    else if (prop == SpinelProp::SPINEL_PROP_HWADDR || prop == SpinelProp::SPINEL_PROP_MAC_15_4_LADDR)
    {
        const uint8_t *eui = nullptr;
        if (pc_spinel_get_eui64(&r, &eui))
        {
            Serial.printf("  %s = %02X%02X%02X%02X%02X%02X%02X%02X\n", name, eui[0], eui[1], eui[2], eui[3], eui[4],
                          eui[5], eui[6], eui[7]);
        }
    }
    else
    {
        Serial.printf("  %s (%u bytes)\n", name, vlen);
    }
}

void setup()
{
    Serial.begin(115200);
    Serial2.begin(115200, SERIAL_8N1, PIN_RX, PIN_TX);
    delay(300);

    pc_gateway_reset();
    pc_gateway_port_config p = {};
    p.port_id = RADIO_PORT;
    p.kind = pc_gateway_kind::PC_GW_THREAD;
    pc_gateway_add_port(&p);
    pc_gateway_set_uplink_cb(northbound_publish, nullptr);
    pc_gateway_set_topic_prefix("thread");

    // Reset the NCP: spinel RESET command (header | CMD_RESET), then query identity.
    const uint8_t reset[2] = {pc_spinel_header(0, 0), SpinelCmd::SPINEL_CMD_RESET};
    uint8_t frame[8];
    uint16_t n = pc_spinel_frame_encode(reset, 2, frame, sizeof(frame));
    Serial2.write(frame, n);
    delay(200);
    rcp_get(SpinelProp::SPINEL_PROP_PROTOCOL_VERSION);
    rcp_get(SpinelProp::SPINEL_PROP_NCP_VERSION);
    rcp_get(SpinelProp::SPINEL_PROP_HWADDR);
    Serial.println("Thread gateway: spinel/HDLC -> codec -> property decode + publish (thread/0/<tid>)");
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
        uint8_t payload[PC_THREAD_MAX_DATA];
        uint16_t plen = 0;
        int n = pc_spinel_frame_decode(g_buf, g_len, payload, sizeof(payload), &plen);
        if (n == 0)
        {
            break; // need more
        }
        if (n < 0)
        {
            drop_front(1); // resync past a bad frame
            continue;
        }
        // Decode the named property, then bridge the spinel payload; the header's low 4 bits
        // are the transaction id.
        if (plen > 0)
        {
            print_property(payload, plen);
            uint16_t tid = payload[0] & 0x0F;
            pc_gateway_uplink(RADIO_PORT, tid, payload, plen, 0);
        }
        drop_front((uint16_t)n);
    }
}
