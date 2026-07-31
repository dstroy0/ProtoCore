# FileServing - serve a static site from LittleFS

**Layer:** L7 Application · **Build flags:** none (`PC_ENABLE_FILE_SERVING` is on by default)

## What this example teaches

`serve_static(url_prefix, fs, fs_root)` mounts a filesystem subtree at a URL
prefix and serves files from it with content types inferred from the extension -
the one-call way to ship a web UI (HTML/CSS/JS) from flash storage.

**Mapping a URL tree to a directory.** A request for `/` maps to
`/www/index.html`, `/app.js` to `/www/app.js`, and so on:

```cpp
server.serve_static("/", LittleFS, "/www"); // URL "/" -> LittleFS "/www"
server.set_cache_control("max-age=3600");    // sent alongside each static file
```

`set_cache_control()` adds a `Cache-Control` header to served files so browsers
cache them; combined with [ETag](../ETag) the eventual revalidation is a
cheap `304`. Files are streamed in `FILE_CHUNK_SIZE` pieces, so even large assets
use constant memory.

You must put your assets under a `data/www/` folder and upload the LittleFS image
("Upload Filesystem Image" in PlatformIO / Arduino) before running - the sketch
mounts LittleFS with `begin(true)` (format-on-fail).

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --lib="." examples/L7-Application/FileServing/FileServing.ino
```

Upload a filesystem image containing `www/index.html`, flash, then browse to
`http://<ip>/`.

## Annotated source

The complete sketch ([FileServing.ino](FileServing.ino)), reproduced
verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include <LittleFS.h>

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

void setup()
{
    Serial.begin(115200);
    if (!LittleFS.begin(true)) // mount; format on first run if needed
        Serial.println("LittleFS mount failed (upload a filesystem image)");

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

    // Map the URL tree "/" onto the "/www" directory in LittleFS.
    server.serve_static("/", LittleFS, "/www");
    // Cache assets for an hour; browsers still revalidate cheaply via the ETag.
    server.set_cache_control("max-age=3600");
    server.begin(80);
}

void loop()
{
    server.handle();
}
```
