# RegexRoutes - bounded, allocation-free regex routes

**Layer:** L7 Application · **Build flags:** none (core features only)

## What this example teaches

`on_regex()` matches the whole request path against a small regular-expression
subset, for routing rules that `:param` segments cannot express (numeric-only ids,
specific file extensions). The matcher is allocation-free and bounded by
`RE_MAX_STEPS` so it stays deterministic - no catastrophic backtracking.

**The supported subset.** `.`, the quantifiers `* + ?`, character classes
`[...]` / `[^...]` with `a-z` ranges, the shorthands `\d \w \s`, and `\` escapes.
It is non-capturing (use `:name` path params if you need to capture):

```cpp
server.on_regex("/sensor/[0-9]+", HttpMethod::HTTP_GET, handle_sensor); // only numeric ids
server.on_regex("/img/.+\\.png", HttpMethod::HTTP_GET, handle_png);     // only *.png paths
```

`/sensor/42` matches but `/sensor/abc` does not (404); `/img/cat.png` matches but
`/img/cat.gif` does not. The whole path must match (it is anchored), and the match
cost is capped by `RE_MAX_STEPS`, which is why this is safe to expose to untrusted
input.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --lib="." examples/L7-Application/RegexRoutes/RegexRoutes.ino
```

```sh
curl http://<ip>/sensor/42     # 200 (digits)
curl http://<ip>/sensor/abc    # 404
curl http://<ip>/img/cat.png   # 200
curl http://<ip>/img/cat.gif   # 404
```

## Annotated source

The complete sketch ([RegexRoutes.ino](RegexRoutes.ino)), reproduced
verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

void handle_sensor(uint8_t slot_id, HttpReq *req)
{
    char body[96];
    snprintf(body, sizeof(body), "{\"path\":\"%s\",\"matched\":\"numeric sensor id\"}", req->path);
    server.send(slot_id, 200, "application/json", body);
}

void handle_png(uint8_t slot_id, HttpReq *req)
{
    char body[96];
    snprintf(body, sizeof(body), "{\"path\":\"%s\",\"matched\":\"png image\"}", req->path);
    server.send(slot_id, 200, "application/json", body);
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


    server.on_regex("/sensor/[0-9]+", HttpMethod::HTTP_GET, handle_sensor); // only numeric ids
    server.on_regex("/img/.+\\.png", HttpMethod::HTTP_GET, handle_png);     // only *.png paths

    int32_t result = server.begin(80);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }
    Serial.println("Server started on port 80");
}

void loop()
{
    server.handle();
}
```
