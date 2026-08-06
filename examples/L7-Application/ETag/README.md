# ETag - conditional GET for static files

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_ETAG` (with file serving)

## What this example teaches

With ETag enabled, `serve_file()` / `serve_static()` emit a strong `ETag` (derived
from the file size and mtime) and answer a matching `If-None-Match` with
`304 Not Modified` - so a browser re-fetching an unchanged asset gets an empty 304
instead of the whole file, saving bandwidth and time.

**It is automatic.** You serve files exactly as in [FileServing](../FileServing);
enabling the flag adds the ETag and the conditional-request handling:

```cpp
server.serve_static("/", LittleFS, "/www"); // ETag + If-None-Match handled automatically
```

The first request returns `200` with `ETag: "..."`; a follow-up that echoes that
value in `If-None-Match` returns `304` with no body. This pairs naturally with
`set_cache_control()` (see [FileServing](../FileServing)): the cache header
lets the browser skip the request entirely until expiry, and the ETag makes the
eventual revalidation cheap.

Put a file at `data/www/index.html` and upload the LittleFS image before running.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_ETAG=1" \
  --lib="." examples/L7-Application/ETag/ETag.ino
```

```sh
curl -i http://<ip>/                                 # 200 + ETag: "..."
curl -i -H 'If-None-Match: "<etag>"' http://<ip>/    # 304 Not Modified
```

## Annotated source

The complete sketch ([ETag.ino](ETag.ino)), reproduced verbatim with added
explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_ETAG 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include <LittleFS.h>

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

void setup()
{
    Serial.begin(115200);
    if (!LittleFS.begin(true))
        Serial.println("LittleFS mount failed (upload a filesystem image)");

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

    // ETag + If-None-Match (-> 304) handled automatically because the flag is on.
    server.serve_static("/", LittleFS, "/www");
    server.begin(80);
}

void loop()
{
    server.handle();
}
```
