# EspNow - ESP-NOW peer messaging

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_ESPNOW`

## What this example teaches

ESP-NOW is connectionless peer-to-peer radio messaging between ESP devices - no AP,
no IP stack, no association. Each board broadcasts a counter every two seconds and
prints any framed message it receives; flash two boards on the same channel and they
see each other over Serial. Messages carry a 1-byte type so a receiver can demux.

**Bring the radio up (but not associated), then begin:**

```cpp
pc_espnow_begin(CHANNEL, on_espnow);
```

**Receive callback gets the sender MAC, a type byte, and the payload:**

```cpp
static void on_espnow(const uint8_t mac[6], uint8_t type, const uint8_t *payload, size_t len) {
    Serial.printf("rx from %02x:...:%02x type=%u: ", mac[0], mac[5], type);
    Serial.write(payload, len);
}
```

**Broadcast with a type tag:**

```cpp
pc_espnow_broadcast(MSG_COUNTER, (const uint8_t *)msg, len);
```

This sketch has no web server - it is pure radio - but to bridge ESP-NOW into the
web stack you would call this library's `ws_send_*` from `on_espnow` to fan peer
traffic out to browser WebSocket clients.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_ESPNOW=1" \
  --lib="." examples/L7-Application/EspNow/EspNow.ino
```

Flash two boards on the same channel, open both Serial monitors @ 115200, and each
prints the other's broadcast counter.

## Annotated source

The complete sketch ([EspNow.ino](EspNow.ino)), reproduced verbatim with added
explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_ESPNOW 1

#include "services/radio/espnow/espnow.h"
#include <esp_wifi.h> // esp_wifi_set_channel(); not pulled in transitively by WiFi.h

static const uint8_t MSG_COUNTER = 1;
static const uint8_t CHANNEL = 1;

// Delivered for each framed message a peer broadcasts on this channel.
static void on_espnow(const uint8_t mac[6], uint8_t type, const uint8_t *payload, size_t len)
{
    Serial.printf("rx from %02x:%02x:%02x:%02x:%02x:%02x type=%u: ", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  type);
    Serial.write(payload, len);
    Serial.println();
}

void setup()
{
    Serial.begin(115200);
    // ESP-NOW needs the radio up but not associated; STA mode on a fixed channel.

    if (!pc_espnow_begin(CHANNEL, on_espnow))
    {
        Serial.println("ESP-NOW init failed");
        return;
    }
    Serial.print("ESP-NOW up on channel ");
    Serial.print(CHANNEL);
    Serial.print(", my MAC ");
    Serial.println(pc_net_mac());
}

void loop()
{
    static uint32_t last = 0;
    static uint32_t n = 0;
    if (millis() - last >= 2000)
    {
        last = millis();
        char msg[24];
        int len = snprintf(msg, sizeof(msg), "count=%lu", (unsigned long)n++);
        bool ok = pc_espnow_broadcast(MSG_COUNTER, (const uint8_t *)msg, (size_t)len);
        Serial.printf("broadcast %s -> %s\n", msg, ok ? "ok" : "FAIL");
    }
}
```
