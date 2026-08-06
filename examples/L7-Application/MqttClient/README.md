# MqttClient - the device publishes/subscribes to an MQTT broker

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_MQTT` (optional `PC_ENABLE_TLS` + `PC_ENABLE_MQTT_TLS` for `mqtts://`)

## What this example teaches

A full MQTT 3.1.1 client: the device connects to a broker, SUBSCRIBEs to a topic,
and PUBLISHEs to the same topic once a second at QoS 1 - so it receives its own
messages back through the `on_message` callback, a self-contained round trip you
can watch on Serial. QoS 0/1/2, keep-alive, and DUP retransmit are handled by
`pc_mqtt_loop()`.

**Register the receive callback, then connect with options:**

```cpp
pc_mqtt_set_message_cb(on_message);

MqttConnectOpts opts;
memset(&opts, 0, sizeof(opts));
opts.client_id = "pc-esp32-demo";
opts.keepalive_s = 30;
opts.clean_session = true;
if (pc_mqtt_connect(BROKER, PORT, false, &opts)) // 3rd arg: use TLS?
    pc_mqtt_subscribe(TOPIC, 1);                  // QoS 1
```

**Pump the protocol every loop.** `pc_mqtt_loop()` drives the state machine
(keep-alive PINGs, QoS handshakes, retransmits); skipping it stalls the session:

```cpp
void loop() {
    pc_mqtt_loop();
    if (pc_mqtt_connected() && /* once a second */) {
        pc_mqtt_publish(TOPIC, (const uint8_t *)msg, len, 1, false); // QoS 1, not retained
    }
}
```

`BROKER`/`TOPIC` default to a public test broker; point them at your own broker
for real telemetry or command. For `mqtts://`, pass `true` as the third argument
to `pc_mqtt_connect()` and build with the TLS flags.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_MQTT=1" \
  --lib="." examples/L7-Application/MqttClient/MqttClient.ino
```

```sh
# watch the same topic from a host while the device runs:
mosquitto_sub -h broker.hivemq.com -t pc/demo
```

## Annotated source

The complete sketch ([MqttClient.ino](MqttClient.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_MQTT 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/iot/mqtt/mqtt.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static const char *BROKER = "broker.hivemq.com"; // public test broker
static const uint16_t PORT = 1883;
static const char *TOPIC = "pc/demo";

// Delivered for every message on a subscribed topic.
void on_message(const char *topic, const uint8_t *payload, size_t len)
{
    Serial.printf("RX [%s]: %.*s\n", topic, (int)len, (const char *)payload);
}

void setup()
{
    Serial.begin(115200);

    Physical.wifi->init(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!Physical.wifi->ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    pc_mqtt_set_message_cb(on_message);

    MqttConnectOpts opts;
    memset(&opts, 0, sizeof(opts));
    opts.client_id = "pc-esp32-demo";
    opts.keepalive_s = 30;
    opts.clean_session = true;

    if (pc_mqtt_connect(BROKER, PORT, false, &opts)) // false = plaintext (true for mqtts://)
    {
        Serial.println("MQTT connected");
        pc_mqtt_subscribe(TOPIC, 1); // QoS 1
    }
    else
    {
        Serial.println("MQTT connect failed");
    }
}

void loop()
{
    pc_mqtt_loop(); // drive keep-alive, QoS handshakes, retransmits

    static uint32_t last = 0;
    static uint32_t n = 0;
    if (pc_mqtt_connected() && millis() - last >= 1000)
    {
        last = millis();
        char msg[48];
        int len = snprintf(msg, sizeof(msg), "hello from esp32 #%lu", (unsigned long)n++);
        pc_mqtt_publish(TOPIC, (const uint8_t *)msg, (size_t)len, 1, false); // QoS 1, not retained
    }
}
```
