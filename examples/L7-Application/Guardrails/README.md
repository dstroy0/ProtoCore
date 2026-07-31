# Guardrails - runtime heap/stack guardrails

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_GUARDRAILS`

## What this example teaches

A long-running device dies quietly when it runs out of heap or stack. Guardrails
watch four resources - free heap, heap low-water, largest free block
(fragmentation), and this task's remaining stack - and fire a callback when any
crosses its `PC_GUARDRAIL_*` floor, so the app can shed load, drop to a safe
state, or reboot _before_ exhaustion bites. The live snapshot is also served as
JSON at `/health`.

**Install a breach callback, then check periodically:**

```cpp
pc_guardrails_begin(on_breach);

void loop() {
    if (/* once a second */)
        pc_guardrails_check(); // fires on_breach() if any floor is crossed
}
```

The callback receives a bitmask of which floors were breached and the full health
snapshot:

```cpp
static void on_breach(uint8_t breaches, const pc_health *h) {
    Serial.printf("[guardrail] breach=0x%02x heap=%u frag=%u stack=%u\n",
                  breaches, (unsigned)h->free_heap, (unsigned)h->largest_free_block, (unsigned)h->stack_free);
    // Real app: shed load, drop to a safe state, or ESP.restart().
}
```

`pc_guardrails_sample(&h)` fills a snapshot on demand and `pc_health_json()`
serializes it for the `/health` endpoint.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_GUARDRAILS=1" \
  --lib="." examples/L7-Application/Guardrails/Guardrails.ino
```

```sh
curl http://<ip>/health   # JSON: free heap, largest block, stack free
```

## Annotated source

The complete sketch ([Guardrails.ino](Guardrails.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_GUARDRAILS 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/security/guardrails/guardrails.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// Fired when any guardrail floor is crossed; breaches is a bitmask.
static void on_breach(uint8_t breaches, const pc_health *h)
{
    Serial.printf("[guardrail] breach=0x%02x heap=%u frag=%u stack=%u\n", breaches, (unsigned)h->free_heap,
                  (unsigned)h->largest_free_block, (unsigned)h->stack_free);
    // Real app: shed load, drop to a safe state, or ESP.restart().
}

void setup()
{
    Serial.begin(115200);
    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
        delay(250);
    Serial.print("IP: ");
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    pc_guardrails_begin(on_breach);

    server.on("/health", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
        pc_health h;
        pc_guardrails_sample(&h);
        char buf[128];
        pc_health_json(&h, buf, sizeof(buf));
        server.send(id, 200, "application/json", buf);
    });
    server.begin(80);
}

void loop()
{
    static uint32_t last = 0;
    if (millis() - last >= 1000)
    {
        last = millis();
        pc_guardrails_check(); // fires on_breach() if any floor is crossed
    }
}
```
