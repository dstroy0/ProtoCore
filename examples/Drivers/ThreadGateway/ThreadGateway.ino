// ThreadGateway - an OpenThread RCP bridged to the gateway over spinel / HDLC-lite.
//
// A UART radio plugin (see RadioGateway): an OpenThread radio co-processor (RCP - an
// nRF52840 / EFR32, or an ESP32-C6 running ot_rcp) speaks spinel over the HDLC-lite framing
// on a UART, so an 802.15.4 / Thread mesh is bridged to IP and the web. This decodes the
// HDLC-lite frames and both (a) decodes the named spinel property each one carries and (b)
// bridges the raw spinel payload northbound.
//
//   Thread RCP --UART--> Thread.spinel_frame_decode() --> spinel command -> property registry
//                                                                     -> Gateway.uplink()
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
// Build flags (whole build): PROTOCORE_ENABLE_THREAD=1 PROTOCORE_ENABLE_GATEWAY=1

#include "protocore.h" // discovers the library (adds src/ to the include path)
#include "server/net/gateway/gateway.h"
#include "services/radio/thread/thread.h"

static const uint8_t RADIO_PORT = 0;
static const int PIN_RX = 16, PIN_TX = 17; // UART2 to the Thread RCP

static uint8_t g_buf[1024];
static uint16_t g_len = 0;
static uint8_t g_thread_work[16]; // the borrow a Thread entry takes; the spinel codec keeps no state in it

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

// Frame a spinel PROP_VALUE_GET(prop) and write it to the RCP.
static void rcp_get(uint32_t prop)
{
    uint8_t payload[16];
    uint16_t plen = Thread.spinel_command_build(g_thread_work, protocore_spinel_header(0, 1), SPINEL_CMD_PROP_VALUE_GET,
                                                prop, nullptr, 0, payload, sizeof(payload));
    uint8_t frame[32];
    uint16_t n = Thread.spinel_frame_encode(g_thread_work, payload, plen, frame, sizeof(frame));
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
    if (Thread.spinel_command_parse(g_thread_work, payload, plen, &header, &cmd, &prop, &val, &vlen) < 0)
    {
        return;
    }
    if (cmd != SPINEL_CMD_PROP_VALUE_IS)
    {
        return; // only report property updates
    }

    SpinelReader r;
    Thread.spinel_reader_init(g_thread_work, &r, val, vlen);
    const char *name = Thread.spinel_prop_name(g_thread_work, prop);
    if (prop == SPINEL_PROP_LAST_STATUS)
    {
        uint32_t status = 0;
        if (Thread.spinel_get_uint(g_thread_work, &r, &status))
        {
            Serial.printf("  %s = %s (%u)\n", name, Thread.spinel_status_name(g_thread_work, status), status);
        }
    }
    else if (prop == SPINEL_PROP_NCP_VERSION || prop == SPINEL_PROP_NET_NETWORK_NAME)
    {
        const char *s = nullptr;
        uint16_t slen = 0;
        if (Thread.spinel_get_utf8(g_thread_work, &r, &s, &slen))
        {
            Serial.printf("  %s = %.*s\n", name, (int)slen, s);
        }
    }
    else if (prop == SPINEL_PROP_PROTOCOL_VERSION)
    {
        uint32_t major = 0, minor = 0;
        if (Thread.spinel_get_uint(g_thread_work, &r, &major) && Thread.spinel_get_uint(g_thread_work, &r, &minor))
        {
            Serial.printf("  %s = %u.%u\n", name, major, minor);
        }
    }
    else if (prop == SPINEL_PROP_MAC_15_4_PANID || prop == SPINEL_PROP_MAC_15_4_SADDR)
    {
        uint16_t v = 0;
        if (Thread.spinel_get_u16(g_thread_work, &r, &v))
        {
            Serial.printf("  %s = 0x%04X\n", name, v);
        }
    }
    else if (prop == SPINEL_PROP_HWADDR || prop == SPINEL_PROP_MAC_15_4_LADDR)
    {
        const uint8_t *eui = nullptr;
        if (Thread.spinel_get_eui64(g_thread_work, &r, &eui))
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

    Gateway.reset(protocore_gateway_span());
    protocore_gateway_port_config p = {};
    p.port_id = RADIO_PORT;
    p.kind = protocore_gateway_kind::PROTOCORE_GW_THREAD;
    Gateway.add_port(protocore_gateway_span(), &p);
    Gateway.set_uplink_cb(protocore_gateway_span(), northbound_publish, nullptr);
    Gateway.set_topic_prefix(protocore_gateway_span(), "thread");

    // Reset the NCP: spinel RESET command (header | CMD_RESET), then query identity.
    const uint8_t reset[2] = {protocore_spinel_header(0, 0), SPINEL_CMD_RESET};
    uint8_t frame[8];
    uint16_t n = Thread.spinel_frame_encode(g_thread_work, reset, 2, frame, sizeof(frame));
    Serial2.write(frame, n);
    delay(200);
    rcp_get(SPINEL_PROP_PROTOCOL_VERSION);
    rcp_get(SPINEL_PROP_NCP_VERSION);
    rcp_get(SPINEL_PROP_HWADDR);
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
        uint8_t payload[PROTOCORE_THREAD_MAX_DATA];
        uint16_t plen = 0;
        int n = Thread.spinel_frame_decode(g_thread_work, g_buf, g_len, payload, sizeof(payload), &plen);
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
            Gateway.uplink(protocore_gateway_span(), RADIO_PORT, tid, payload, plen, 0);
        }
        drop_front((uint16_t)n);
    }
}
