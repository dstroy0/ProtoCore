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
 * handled by protocore_mqtt_loop(); call it every loop().
 *
 * NOTE: optional services are gated by a compile flag the *library* sources must
 * also see; for PlatformIO enable it for the whole build, e.g.:
 *     build_flags = -DPROTOCORE_ENABLE_MQTT=1
 * (Arduino IDE: they are already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PROTOCORE_ENABLE_MQTT 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "services/iot/mqtt/mqtt/mqtt.h"

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

    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    Serial.print("Connecting to WiFi");
    while (!protocore_physical_wifi_up())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = protocore_physical_egress_ipv4(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    MqttV.delivery.on_message = on_message;
    Mqtt.on_message(protocore_mqtt_span());

    MqttV.server.host = BROKER;
    MqttV.server.port = PORT;
    MqttV.server.use_tls = false;
    MqttV.session.client_id = "pc-esp32-demo";
    MqttV.session.user_name = nullptr;
    MqttV.session.password = nullptr;
    MqttV.session.keep_alive = 30;
    MqttV.session.clean_session = true;
    Mqtt.connect(protocore_mqtt_span());
    if (MqttV.ok)
    {
        Serial.println("MQTT connected");
        MqttV.filter.topic_filter = TOPIC;
        MqttV.filter.qos = 1;
        Mqtt.subscribe(protocore_mqtt_span());
    }
    else
    {
        Serial.println("MQTT connect failed");
    }
}

void loop()
{
    Mqtt.loop(protocore_mqtt_span());

    static uint32_t last = 0;
    static uint32_t n = 0;
    Mqtt.connected(protocore_mqtt_span());
    if (MqttV.ok && millis() - last >= 1000)
    {
        last = millis();
        char msg[48];
        int len = snprintf(msg, sizeof(msg), "hello from esp32 #%lu", (unsigned long)n++);
        MqttV.message.topic_name = TOPIC;
        MqttV.message.payload = (const uint8_t *)msg;
        MqttV.message.payload_len = (size_t)len;
        MqttV.message.qos = 1;
        MqttV.message.retain = false;
        Mqtt.publish(protocore_mqtt_span());
    }
}
