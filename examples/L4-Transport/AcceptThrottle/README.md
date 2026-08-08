# AcceptThrottle - global connection-flood defense

**Layer:** L4 Transport · **Build flags:** `PC_ENABLE_ACCEPT_THROTTLE`

## What this example teaches

This is a build-time defense, not an API. When enabled, the accept callback
rejects new connections once more than `PC_ACCEPT_THROTTLE_MAX` have been
accepted within `PC_ACCEPT_THROTTLE_WINDOW_MS` - a global fixed window using
two counters, no per-IP table. It bounds connection churn (reconnect/brute-force
floods) on top of the already-bounded connection pool. The sketch's only job is
to show that enabling the flag is all it takes.

**Zero runtime surface.** There is nothing to call - the throttle lives in the
accept path. The handler is a plain route; the defense is active simply because
the flag was compiled in:

```cpp
server.on("/", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "throttled server"); });
server.begin(80); // the accept throttle is active automatically when the flag is built in
```

**Tuning.** Set the two knobs as build flags alongside the enable flag - for
example a window of 1000 ms and a cap of 20 accepts/window:

```text
build_flags = -DPC_ENABLE_ACCEPT_THROTTLE=1 -DPC_ACCEPT_THROTTLE_MAX=20 -DPC_ACCEPT_THROTTLE_WINDOW_MS=1000
```

For a per-source-IP throttle (so one noisy host cannot starve everyone), see
[PerIpThrottle](../PerIpThrottle).

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_ACCEPT_THROTTLE=1" \
  --lib="." examples/L4-Transport/AcceptThrottle/AcceptThrottle.ino
```

Hammer it with many rapid connections (e.g. `ab -n 500 -c 50 http://<ip>/`) and
watch excess connections get refused at accept time.

## Annotated source

The complete sketch ([AcceptThrottle.ino](AcceptThrottle.ino)), reproduced
verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_ACCEPT_THROTTLE 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

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

    server.on("/", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "throttled server"); });
    server.begin(80); // accept throttle is active automatically when the flag is built in
}

void loop()
{
    server.handle();
}
```
