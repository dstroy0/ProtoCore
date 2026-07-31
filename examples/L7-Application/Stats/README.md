# Stats - a runtime statistics endpoint

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_STATS`

## What this example teaches

`server.stats(slot_id)` writes a JSON snapshot of the server's live state -
uptime, request and error counts, connection-pool usage, and free heap - straight
to the response. Wire it to a route and you have a zero-effort health/diagnostics
endpoint. The same counters back the Prometheus exporter in
[PrometheusMetrics](../PrometheusMetrics); this one emits plain JSON.

**One call renders the snapshot:**

```cpp
server.on("/stats", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.stats(id); });
```

The JSON is written directly into the response buffer (no heap), so it is safe to
poll frequently.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_STATS=1" \
  --lib="." examples/L7-Application/Stats/Stats.ino
```

```sh
curl http://<ip>/stats   # JSON: uptime, requests, errors, pool usage, free heap
```

## Annotated source

The complete sketch ([Stats.ino](Stats.ino)), reproduced verbatim with added
explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_STATS 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

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

    // Serve a live JSON snapshot of the server's counters.
    server.on("/stats", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.stats(id); });
    server.begin(80);
}

void loop()
{
    server.handle();
}
```
