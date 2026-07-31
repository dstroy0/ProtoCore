# PerIpThrottle - per-source-IP flood defense

**Layer:** L4 Transport · **Build flags:** `PC_ENABLE_PER_IP_THROTTLE`

## What this example teaches

The global [accept throttle](../AcceptThrottle) caps total accepts but cannot
tell one noisy client from many legitimate ones. This per-IP throttle closes that
gap: the accept callback rejects a new connection once a single source IPv4 has
opened more than `PC_PER_IP_THROTTLE_MAX` connections within
`PC_PER_IP_THROTTLE_WINDOW_MS`, so one abusive host is throttled without
affecting everyone else.

**Bounded memory, no heap.** A fixed BSS table of `PC_PER_IP_THROTTLE_SLOTS`
buckets tracks the busiest recent addresses (an LRU-ish set, not one slot per
possible IP), so the defense itself stays deterministic.

**Build-time only.** Like the global throttle, there is no runtime API - the
handler is plain; enabling the flag activates the defense in the accept path:

```cpp
server.on("/", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "per-IP throttled"); });
server.begin(80); // per-IP throttle is active automatically when the flag is built in
```

**Tuning + pairing.** Set the knobs as build flags (cap, window, table size), and
pair it with the global accept throttle for layered defense:

```text
build_flags = -DPC_ENABLE_PER_IP_THROTTLE=1 -DPC_PER_IP_THROTTLE_MAX=10 \
              -DPC_PER_IP_THROTTLE_WINDOW_MS=10000 -DPC_PER_IP_THROTTLE_SLOTS=16
```

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_PER_IP_THROTTLE=1" \
  --lib="." examples/L4-Transport/PerIpThrottle/PerIpThrottle.ino
```

From one host, open many rapid connections and watch that host get refused while
another host still connects.

## Annotated source

The complete sketch ([PerIpThrottle.ino](PerIpThrottle.ino)), reproduced
verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_PER_IP_THROTTLE 1

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

    server.on("/", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "per-IP throttled"); });
    server.begin(80); // per-IP throttle is active automatically when the flag is built in
}

void loop()
{
    server.handle();
}
```
