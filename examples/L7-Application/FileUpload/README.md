# FileUpload - stream a POST body straight to a file

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_UPLOAD`

## What this example teaches

This streams a request body into a LittleFS file in `FILE_CHUNK_SIZE` pieces via
the parser's streaming-body hook - the same mechanism OTA uses - so an upload
never has to fit in RAM. A GET route serves the stored file back to verify the
round-trip.

**One call wires the upload sink.** `pc_upload_begin(path, dest)`
registers a POST route whose body is streamed to `dest` as it arrives:

```cpp
pc_mnt_mount(pc_mnt_fs(&LittleFS));
pc_upload_begin("/upload", DEST);     // POST body -> file, chunk by chunk
server.on("/file", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {   // read it back
    server.serve_file(id, LittleFS, DEST, "application/octet-stream");
});
```

**Two important constraints (from the header):**

- The upload sink shares the parser's streaming hook with OTA - **enable one or
  the other, not both.**
- Enabling upload raises `RX_BUF_SIZE` to the streaming floor of **8192** (a full
  TCP receive window) automatically - a smaller ring keeps a large upload in
  backpressure long enough to trip the idle-timeout reap and the connection resets
  mid-transfer (HW-measured: a 2048 ring resets a 64 KB upload; 8192 round-trips
  256 KB byte-exact). Because that ring is real DRAM per connection, this build
  dials `MAX_CONNS` down to **4** so `8192 * MAX_CONNS` fits the classic ESP32's
  ~122 KB internal DRAM; a PSRAM/large-SRAM board can raise it.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_UPLOAD=1 -DMAX_CONNS=4" \
  --lib="." examples/L7-Application/FileUpload/FileUpload.ino
```

```sh
curl --data-binary @somefile.bin http://<ip>/upload   # 200 OK <n> bytes
curl http://<ip>/file > roundtrip.bin                 # read it back
```

## Annotated source

The complete sketch ([FileUpload.ino](FileUpload.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_UPLOAD 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/upload_service.h"
#include <LittleFS.h>

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static const char *DEST = "/uploaded.bin";

PC server;

void setup()
{
    Serial.begin(115200);

    if (!LittleFS.begin(true)) // format on first use
        Serial.println("LittleFS mount failed");

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

    // POST /upload -> stream the body into DEST on LittleFS (chunked, never in RAM).
    pc_mnt_mount(pc_mnt_fs(&LittleFS));
pc_upload_begin("/upload", DEST);

    // GET /file -> serve the stored file back.
    server.on("/file", HttpMethod::HTTP_GET,
              [](uint8_t id, HttpReq *) { server.serve_file(id, LittleFS, DEST, "application/octet-stream"); });

    int32_t result = server.begin(80);
    if (result < 0)
        Serial.printf("begin() failed (error %d)\n", result);
    else
        Serial.println("Upload server on :80 (curl --data-binary @file http://<ip>/upload)");
}

void loop()
{
    server.handle();
}
```
