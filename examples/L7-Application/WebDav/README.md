# WebDav - a WebDAV file share backed by LittleFS

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_WEBDAV`

## What this example teaches

WebDAV (RFC 4918) extends HTTP with file-management methods, so a remote client
can mount and edit the device's filesystem like a network drive.
`server.dav(url_prefix, fs, fs_root)` mounts a LittleFS subtree as a WebDAV share
in one call: here `/dav` on disk is exposed at the URL `/dav`.

**One call mounts the share:**

```cpp
server.dav("/dav", LittleFS, "/dav"); // URL "/dav" -> LittleFS "/dav"
```

Supported methods: `OPTIONS`, `PROPFIND` (Depth 0/1), `PROPPATCH`, `GET`, `HEAD`,
`PUT`, `DELETE`, `MKCOL`, `COPY` (files + collections), `MOVE`, and advisory `LOCK`/`UNLOCK`.
`PROPPATCH` is answered `207 Multi-Status` with each requested property refused
`403 Forbidden` (the properties are read-only) - this keeps Windows Explorer and
macOS Finder, which `PROPPATCH` a timestamp right after a `PUT`, from erroring on
a `405`. `PUT` streams the body straight to the file (so an upload is not bounded
by `BODY_BUF_SIZE`) and the locks are advisory only.

**Security note.** A writable share is dangerous on an open network: add per-route
auth ([Foundation/Sysadmin](../../Foundation/Sysadmin)), HTTPS
([L4-Transport/HTTPS](../../L4-Transport/HTTPS)), and the per-IP throttle
([L4-Transport/PerIpThrottle](../../L4-Transport/PerIpThrottle)) before
exposing it. The sketch seeds one file so a fresh share is not empty.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_WEBDAV=1 -DMAX_CONNS=3 -DMAX_WS_CONNS=1 -DMAX_SSE_CONNS=1 -DMAX_ROUTES=8 -DPC_WEBDAV_MAX_ENTRIES=8 -DPC_WEBDAV_BUF_SIZE=1024" \
  --lib="." examples/L7-Application/WebDav/WebDav.ino
```

```sh
curl -X PROPFIND -H "Depth: 1" http://<ip>/dav/        # list
curl -T file.txt http://<ip>/dav/file.txt              # upload (PUT)
curl -X PROPPATCH --data-binary @patch.xml http://<ip>/dav/file.txt  # 207 (props refused 403)
curl -X MKCOL http://<ip>/dav/sub                      # make a collection
curl -X MOVE -H "Destination: /dav/b.txt" http://<ip>/dav/file.txt
curl -X DELETE http://<ip>/dav/b.txt
rclone lsd :webdav: --webdav-url http://<ip>/dav --webdav-vendor other
```

## Annotated source

The complete sketch ([WebDav.ino](WebDav.ino)), reproduced verbatim with
added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_WEBDAV 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include <LittleFS.h>

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

    if (!LittleFS.begin(true)) // format on first run
    {
        Serial.println("LittleFS mount failed");
        return;
    }
    LittleFS.mkdir("/dav");
    // Seed one file so a fresh share is not empty.
    File f = LittleFS.open("/dav/hello.txt", "w");
    if (f)
    {
        f.print("hello from ProtoCore\n");
        f.close();
    }

    // Mount the LittleFS subtree "/dav" as a WebDAV share at URL "/dav".
    server.dav("/dav", LittleFS, "/dav");
    server.begin(80);
    Serial.println("WebDAV share at http://<ip>/dav");
}

void loop()
{
    server.handle();
}
```
