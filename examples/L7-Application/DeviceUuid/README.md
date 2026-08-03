# DeviceUuid - a stable MAC-derived device UUID

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_DEVICE_ID`

## What this example teaches

A fleet of identical firmware images needs a stable per-device identity.
`pc_device_uuid()` derives a deterministic RFC 4122 v5 UUID from the chip's
factory MAC: the same value on every boot, with no storage to wear out or
provision. Use it for mDNS hostnames, MQTT client IDs, telemetry tags, and the
like.

**Compute once, reuse everywhere:**

```cpp
static char g_uuid[PC_UUID_STR_LEN];
pc_device_uuid(g_uuid);          // stable per-chip UUID string
```

`PC_UUID_STR_LEN` sizes the caller-owned buffer (no heap). Because it is derived
(hashed from the MAC, not random) it is reproducible and needs no NVS. `GET /id`
returns it as JSON.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_DEVICE_ID=1" \
  --lib="." examples/L7-Application/DeviceUuid/DeviceUuid.ino
```

```sh
curl http://<ip>/id   # {"uuid":"...."} - identical across reboots
```

## Annotated source

The complete sketch ([DeviceUuid.ino](DeviceUuid.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_DEVICE_ID 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "server/signaling/device_id.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;
static char g_uuid[PC_UUID_STR_LEN]; // caller-owned (no heap)

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
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    pc_device_uuid(g_uuid); // stable per-chip UUID (v5, derived from the MAC)
    Serial.printf("device UUID: %s\n", g_uuid);

    server.on("/id", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
        char body[64];
        snprintf(body, sizeof(body), "{\"uuid\":\"%s\"}", g_uuid);
        server.send(id, 200, "application/json", body);
    });
    server.begin(80);
}

void loop()
{
    server.handle();
}
```
