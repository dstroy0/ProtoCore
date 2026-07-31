# Vfs - a unified VFS over a real filesystem

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_VFS`

## What this example teaches

The `pc_vfs_*` API is one file interface with swappable backends: a RAM pool in
host tests, a real filesystem on the device. The point is that features target one
API and the application chooses the medium. Here it is mounted on LittleFS so writes
persist across reboots; mounting the RAM backend instead changes nothing in the
endpoints.

**Mount a backend once, then use the same calls everywhere:**

```cpp
LittleFS.begin(true);
pc_vfs_mount(pc_vfs_fs(&LittleFS)); // real flash...
// pc_vfs_mount(pc_vfs_ram());      // ...or pure RAM - endpoints identical
```

**The file operations are backend-agnostic:**

```cpp
pc_vfs_write_file(path, data, strlen(data)); // create / overwrite
long n = pc_vfs_read_file(path, buf, cap);   // n < 0 if absent
long sz = pc_vfs_size(path);                 // -1 if absent
pc_vfs_remove(path);
```

The four routes (`/save`, `/load`, `/size`, `/rm`) are thin wrappers over those
calls, with a small helper that turns a `?name=` query into a leading-slash path.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_VFS=1" \
  --lib="." examples/L7-Application/Vfs/Vfs.ino
```

```sh
curl "http://<ip>/save?name=greeting&data=hello"   # store /greeting
curl "http://<ip>/load?name=greeting"              # hello
curl "http://<ip>/size?name=greeting"              # 5
curl "http://<ip>/rm?name=greeting"                # delete
```

## Annotated source

The complete sketch ([Vfs.ino](Vfs.ino)), reproduced verbatim with added
explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_VFS 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/storage/vfs/vfs.h"
#include <LittleFS.h>

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// Resolve a query "name" to a leading-slash path in a small static buffer.
static const char *path_of(HttpReq *req, char *out, size_t cap)
{
    const char *name = http_get_query(req, "name");
    if (!name || !*name)
        return nullptr;
    snprintf(out, cap, "/%s", name);
    return out;
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

    LittleFS.begin(true); // format on first use
    pc_vfs_mount(pc_vfs_fs(&LittleFS));

    server.on("/save", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *req) {
        char path[PC_VFS_NAME_MAX];
        const char *data = http_get_query(req, "data");
        if (!path_of(req, path, sizeof(path)) || !data)
        {
            server.send(id, 400, "application/json", "{\"error\":\"name+data\"}");
            return;
        }
        bool ok = pc_vfs_write_file(path, data, strlen(data));
        server.send(id, ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    server.on("/load", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *req) {
        char path[PC_VFS_NAME_MAX];
        if (!path_of(req, path, sizeof(path)))
        {
            server.send(id, 400, "text/plain", "name?");
            return;
        }
        char buf[512];
        long n = pc_vfs_read_file(path, buf, sizeof(buf) - 1);
        if (n < 0)
        {
            server.send(id, 404, "text/plain", "not found");
            return;
        }
        buf[n] = '\0';
        server.send(id, 200, "text/plain", buf);
    });

    server.on("/size", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *req) {
        char path[PC_VFS_NAME_MAX];
        long n = path_of(req, path, sizeof(path)) ? pc_vfs_size(path) : -1;
        char b[24];
        snprintf(b, sizeof(b), "%ld", n);
        server.send(id, 200, "text/plain", b);
    });

    server.on("/rm", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *req) {
        char path[PC_VFS_NAME_MAX];
        bool ok = path_of(req, path, sizeof(path)) && pc_vfs_remove(path);
        server.send(id, ok ? 200 : 404, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    server.begin(80);
}

void loop()
{
    server.handle();
}
```
