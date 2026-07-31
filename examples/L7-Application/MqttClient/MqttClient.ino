// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file MqttClient.ino
 * @brief MQTT 3.1.1 client: the device publishes/subscribes to a broker.
 *
 * Connects to a broker, SUBSCRIBEs to a topic, and PUBLISHEs to the same topic
 * once a second at QoS 1 - so it receives its own messages back through the
 * on_message callback (a self-contained round trip). Point BROKER/TOPIC at your
 * own broker for real telemetry / command.
 *
 * Flash, open Serial @ 115200. Full QoS 0/1/2, keep-alive, and DUP retransmit are
 * handled by pc_mqtt_loop(); call it every loop().
 *
 * NOTE: optional services are gated by a compile flag the *library* sources must
 * also see; for PlatformIO enable it for the whole build, e.g.:
 *     build_flags = -DPC_ENABLE_MQTT=1
 *     ; for mqtts:// add: -DPC_ENABLE_TLS=1 -DPC_ENABLE_MQTT_TLS=1
 * (Arduino IDE: they are already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_MQTT 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/iot/mqtt/mqtt.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static const char *BROKER = "broker.hivemq.com"; // public test broker
static const uint16_t PORT = 1883;
static const char *TOPIC = "pc/demo";

void on_message(const char *topic, const uint8_t *payload, size_t len)
{
    Serial.printf("RX [%s]: %.*s\n", topic, (int)len, (const char *)payload);
}

void setup()
{
    Serial.begin(115200);

    init_wifi_physical(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!wifi_ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    pc_mqtt_set_message_cb(on_message);

    MqttConnectOpts opts;
    memset(&opts, 0, sizeof(opts));
    opts.client_id = "pc-esp32-demo";
    opts.keepalive_s = 30;
    opts.clean_session = true;

    if (pc_mqtt_connect(BROKER, PORT, false, &opts))
    {
        Serial.println("MQTT connected");
        pc_mqtt_subscribe(TOPIC, 1);
    }
    else
    {
        Serial.println("MQTT connect failed");
    }
}

void loop()
{
    pc_mqtt_loop();

    static uint32_t last = 0;
    static uint32_t n = 0;
    if (pc_mqtt_connected() && millis() - last >= 1000)
    {
        last = millis();
        char msg[48];
        int len = snprintf(msg, sizeof(msg), "hello from esp32 #%lu", (unsigned long)n++);
        pc_mqtt_publish(TOPIC, (const uint8_t *)msg, (size_t)len, 1, false);
    }
}
