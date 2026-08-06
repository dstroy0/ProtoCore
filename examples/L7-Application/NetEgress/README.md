# NetEgress - report which interface outbound traffic uses

**Layer:** L7 Application · **Build flags:** none

## What this example teaches

A device with both WiFi and Ethernet has a "default route" the OS picks for
outbound traffic, and it can flip on a cable pull. `Physical.link->egress()` reports the
live egress interface and `Physical.link->egress_ip()` its IP, queried on demand - no
manager and no polling loop. The OS (`esp_netif`) does the failover; this just
reports the current state, useful for logging, telemetry tags, or an "online via
Ethernet / WiFi" badge in a UI.

**Query on demand:**

```cpp
pc_iface i = Physical.link->egress();       // PC_IFACE_ETH / PC_IFACE_STA / PC_IFACE_AP / none
uint32_t ip = Physical.link->egress_ip();   // current egress IP (network byte order)
```

The handler maps the enum to a name and formats the IP as JSON. Wire an Ethernet
PHY alongside WiFi and the reported interface flips when you pull the cable.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --lib="." examples/L7-Application/NetEgress/NetEgress.ino
```

```sh
curl http://<ip>/net   # {"egress":"wifi-sta","ip":"192.168.1.42"}
```

## Annotated source

The complete sketch ([NetEgress.ino](NetEgress.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// Map the egress interface enum to a human name.
static const char *iface_name(pc_iface i)
{
    switch (i)
    {
    case pc_iface::PC_IFACE_ETH:
        return "ethernet";
    case pc_iface::PC_IFACE_AP:
        return "softap";
    case pc_iface::PC_IFACE_STA:
        return "wifi-sta";
    default:
        return "none";
    }
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

    Serial.printf("egress interface: %s\n", iface_name(Physical.link->egress()));

    server.on("/net", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
        uint32_t ip = Physical.link->egress_ip(); // network byte order
        char body[96];
        snprintf(body, sizeof(body), "{\"egress\":\"%s\",\"ip\":\"%u.%u.%u.%u\"}", iface_name(Physical.link->egress()),
                 (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
                 (unsigned)((ip >> 24) & 0xFF));
        server.send(id, 200, "application/json", body);
    });
    server.begin(80);
}

void loop()
{
    server.handle();
}
```
