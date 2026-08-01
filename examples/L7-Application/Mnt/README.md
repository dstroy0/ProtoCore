# Mnt - mounted storage over a real filesystem

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_MNT`

## What this example teaches

Storage in ProtoCore is two pieces that do two jobs. The **mount** (`pc_mnt_*`) says
_what is behind the filesystem_ - a RAM pool in host tests, real flash on the device.
The **accessor** (`pc_fs_*`) is how you use it, and it owns the mount root and the
resolved path. Features target the accessor, and the application chooses the medium.

Here it is mounted on LittleFS so writes persist across reboots; mounting the RAM
backend instead changes nothing in the endpoints.

**Mount a backend once, set the root once:**

```cpp
LittleFS.begin(true);
pc_mnt_mount(pc_mnt_fs(&LittleFS)); // real flash...
// pc_mnt_mount(pc_mnt_ram());      // ...or pure RAM - endpoints identical
pc_fs_begin("/");                   // what every name below is resolved against
```

**The file operations are backend-agnostic:**

```cpp
pc_fs_write_file(name, data, strlen(data)); // create / overwrite
long n = pc_fs_read_file(name, buf, cap);   // n < 0 if absent
long sz = pc_fs_size(name);                 // -1 if absent
pc_fs_remove(name);
```

**Notice what the handlers do not do: they never build a path.** They pass the
client's `?name=` straight through. The accessor joins it onto the root and refuses
any `..` before storage is touched, so `?name=../../secret` is rejected without this
sketch containing a line about it. That is the reason path handling lives in one
place instead of in every protocol server - there is one guard to get right, and one
buffer, rather than one of each per caller.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_MNT=1" \
  --lib="." examples/L7-Application/Mnt/Mnt.ino
```

```sh
curl "http://<ip>/save?name=greeting&data=hello"   # store /greeting
curl "http://<ip>/load?name=greeting"              # hello
curl "http://<ip>/size?name=greeting"              # 5
curl "http://<ip>/rm?name=greeting"                # delete
curl "http://<ip>/load?name=../../secret"          # refused by the accessor
```

## Annotated source

The complete sketch ([Mnt.ino](Mnt.ino)), reproduced verbatim with added
explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_MNT 1

#include "protocore.h"
#include "board_drivers/hal/esp/esp_mnt_fs.h" // the Arduino FS backend lives in the board layer
#include "network_drivers/physical/physical.h"
#include "server/filesystem/filesystem.h"
#include <LittleFS.h>

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

void setup()
{
    Serial.begin(115200);
    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
        delay(250);
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    LittleFS.begin(true); // format on first use
    pc_mnt_mount(pc_mnt_fs(&LittleFS));
    pc_fs_begin("/"); // every name below is resolved against this root

    server.on("/save", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *name = http_get_query(req, "name");
        const char *data = http_get_query(req, "data");
        if (!name || !*name || !data)
        {
            server.send(id, 400, "application/json", "{\"error\":\"name+data\"}");
            return;
        }
        // The raw query value goes straight in - resolution and the `..` guard are the accessor's.
        bool ok = pc_fs_write_file(name, data, strlen(data));
        server.send(id, ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    server.on("/load", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *name = http_get_query(req, "name");
        if (!name || !*name)
        {
            server.send(id, 400, "text/plain", "name?");
            return;
        }
        char buf[512];
        long n = pc_fs_read_file(name, buf, sizeof(buf) - 1);
        if (n < 0) // absent, too big for buf, or a refused path - all one fail-closed answer
        {
            server.send(id, 404, "text/plain", "not found");
            return;
        }
        buf[n] = '\0';
        server.send(id, 200, "text/plain", buf);
    });

    server.on("/size", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *name = http_get_query(req, "name");
        long n = (name && *name) ? pc_fs_size(name) : -1;
        char b[24];
        snprintf(b, sizeof(b), "%ld", n);
        server.send(id, 200, "text/plain", b);
    });

    server.on("/rm", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *name = http_get_query(req, "name");
        bool ok = (name && *name) && pc_fs_remove(name);
        server.send(id, ok ? 200 : 404, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    server.begin(80);
}

void loop()
{
    server.handle();
}
```
