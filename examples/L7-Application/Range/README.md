# Range - HTTP Range / 206 Partial Content

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_RANGE` (with file serving)

## What this example teaches

With Range enabled, the file-serving paths honor a single-range
`Range: bytes=...` request header: the server replies `206 Partial Content` with a
`Content-Range`, seeks the file, and streams only the requested bytes. It also
advertises `Accept-Ranges: bytes` on full responses and answers an unsatisfiable
range with `416 Range Not Satisfiable`. This is what makes resumable downloads and
audio/video seeking work - the browser requests byte ranges as the user scrubs.

**Automatic in `serve_file()`.** You serve a file normally; the Range handling is
added by the flag:

```cpp
server.on("/data.bin", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
    server.serve_file(id, LittleFS, "/data.bin", "application/octet-stream"); // honors Range
});
```

The sketch writes a known 1 KiB file (a repeating 0..255 pattern) at boot so the
range math is easy to verify with `curl -r`. Both `bytes=0-9` (first ten) and
`bytes=-16` (suffix) forms work, and an out-of-bounds range returns `416`.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_RANGE=1" \
  --lib="." examples/L7-Application/Range/Range.ino
```

```sh
curl -s -D - -o /dev/null http://<ip>/data.bin    # 200, Accept-Ranges: bytes
curl -s -r 0-9  http://<ip>/data.bin | xxd | head # 206, first 10 bytes
curl -s -r -16  http://<ip>/data.bin              # 206, last 16 bytes
curl -s -D - -r 999999- http://<ip>/data.bin      # 416 Range Not Satisfiable
```

## Annotated source

The complete sketch ([Range.ino](Range.ino)), reproduced verbatim with added
explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_RANGE 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include <LittleFS.h>

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// Create a known 1 KiB file (repeating 0..255 pattern) so range math is easy to verify.
static void make_demo_file()
{
    if (LittleFS.exists("/data.bin"))
        return;
    File f = LittleFS.open("/data.bin", "w");
    if (!f)
        return;
    uint8_t buf[256];
    for (int i = 0; i < 256; i++)
        buf[i] = (uint8_t)i;
    for (int k = 0; k < 4; k++) // 4 * 256 = 1024 bytes
        f.write(buf, sizeof(buf));
    f.close();
}

void setup()
{
    Serial.begin(115200);

    if (!LittleFS.begin(true)) // format on first use
        Serial.println("LittleFS mount failed");
    make_demo_file();

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

    // serve_file() honors Range automatically when PC_ENABLE_RANGE is set.
    server.on("/data.bin", HttpMethod::HTTP_GET,
              [](uint8_t id, HttpReq *) { server.serve_file(id, LittleFS, "/data.bin", "application/octet-stream"); });

    int32_t result = server.begin(80);
    if (result < 0)
        Serial.printf("begin() failed (error %d)\n", result);
    else
        Serial.println("Range-capable file server on :80 (try: curl -r 0-9 http://<ip>/data.bin)");
}

void loop()
{
    server.handle();
}
```
