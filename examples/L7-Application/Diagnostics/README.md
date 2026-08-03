# Diagnostics - a build configuration endpoint

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_DIAG`

## What this example teaches

`diag(slot_id)` serves a JSON snapshot of which features are enabled and how the
buffers are sized. It is handy while developing - one route tells you exactly what
the firmware was built with - but because it exposes the build configuration, keep
it **off (or behind auth) in production**.

**One call renders the build snapshot:**

```cpp
on_http("/diag", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { diag(id); });
```

Every value in the document is a compile-time constant: each feature flag renders
as `true` or `false`, each sizing constant as a decimal. Nothing is discovered at
runtime, so the report cannot drift from the real configuration.

The document itself is a frame spec - a `DIAG_DOC` table of literal segments
interleaved with the values - which `diag()` fills into 975 bytes borrowed from the
plaintext arena, sends, and then releases. If the borrow or the fill fails the route
answers 503 with an empty body, so no partial document reaches the wire.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_DIAG=1" \
  --lib="." examples/L7-Application/Diagnostics/Diagnostics.ino
```

```sh
curl http://<ip>/diag   # JSON: enabled features + buffer sizes
```

## Annotated source

The complete sketch ([Diagnostics.ino](Diagnostics.ino)), reproduced
verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_DIAG 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

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
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Serves the build configuration snapshot. Keep this off in production.
    on_http("/diag", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { diag(id); });
    begin_http(80);
}

void loop()
{
    handle();
}
```
