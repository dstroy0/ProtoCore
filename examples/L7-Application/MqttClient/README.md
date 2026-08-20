# MqttClient - the device publishes/subscribes to an MQTT broker

**Layer:** L7 Application · **Build flags:** `PROTOCORE_ENABLE_MQTT` (optional `PROTOCORE_ENABLE_TLS` + `PROTOCORE_ENABLE_MQTT_TLS` for `mqtts://`)

## What this example teaches

A full MQTT 3.1.1 client: the device connects to a broker, SUBSCRIBEs to a topic,
and PUBLISHEs to the same topic once a second at QoS 1 - so it receives its own
messages back through the `on_message` callback, a self-contained round trip you
can watch on Serial. QoS 0/1/2, keep-alive, and DUP retransmit are handled by
`protocore_mqtt_loop()`.

**Register the receive callback, then connect with options:**

```cpp
protocore_mqtt_set_message_cb(on_message);

MqttConnectOpts opts;
memset(&opts, 0, sizeof(opts));
opts.client_id = "pc-esp32-demo";
opts.keepalive_s = 30;
opts.clean_session = true;
if (protocore_mqtt_connect(BROKER, PORT, false, &opts)) // 3rd arg: use TLS?
    protocore_mqtt_subscribe(TOPIC, 1);                  // QoS 1
```

**Pump the protocol every loop.** `protocore_mqtt_loop()` drives the state machine
(keep-alive PINGs, QoS handshakes, retransmits); skipping it stalls the session:

```cpp
void loop() {
    protocore_mqtt_loop();
    if (protocore_mqtt_connected() && /* once a second */) {
        protocore_mqtt_publish(TOPIC, (const uint8_t *)msg, len, 1, false); // QoS 1, not retained
    }
}
```

`BROKER`/`TOPIC` default to a public test broker; point them at your own broker
for real telemetry or command. For `mqtts://`, pass `true` as the third argument
to `protocore_mqtt_connect()` and build with the TLS flags.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPROTOCORE_ENABLE_MQTT=1 -DPROTOCORE_ENABLE_TCP_CLIENT=1 -DPROTOCORE_ENABLE_DNS_RESOLVER=1" \
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

#define PROTOCORE_ENABLE_MQTT 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "services/iot/mqtt/mqtt/mqtt.h"

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

    protocore_mqtt_set_message_cb(on_message);

    MqttConnectOpts opts;
    memset(&opts, 0, sizeof(opts));
    opts.client_id = "pc-esp32-demo";
    opts.keepalive_s = 30;
    opts.clean_session = true;

    if (protocore_mqtt_connect(BROKER, PORT, false, &opts)) // false = plaintext (true for mqtts://)
    {
        Serial.println("MQTT connected");
        protocore_mqtt_subscribe(TOPIC, 1); // QoS 1
    }
    else
    {
        Serial.println("MQTT connect failed");
    }
}

void loop()
{
    protocore_mqtt_loop(); // drive keep-alive, QoS handshakes, retransmits

    static uint32_t last = 0;
    static uint32_t n = 0;
    if (protocore_mqtt_connected() && millis() - last >= 1000)
    {
        last = millis();
        char msg[48];
        int len = snprintf(msg, sizeof(msg), "hello from esp32 #%lu", (unsigned long)n++);
        protocore_mqtt_publish(TOPIC, (const uint8_t *)msg, (size_t)len, 1, false); // QoS 1, not retained
    }
}
```
