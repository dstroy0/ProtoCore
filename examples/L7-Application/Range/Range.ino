// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Range.ino
 * @brief HTTP Range requests / 206 Partial Content (RFC 7233) for served files.
 *
 * With PC_ENABLE_RANGE the file-serving paths (serve_file / serve_static)
 * honor a single-range `Range: bytes=...` request header: the server replies
 * `206 Partial Content` with a `Content-Range` header and streams only the
 * requested bytes (seeking the file), advertises `Accept-Ranges: bytes` on full
 * responses, and answers an unsatisfiable range with `416 Range Not Satisfiable`.
 * This is what makes resumable downloads and audio/video seeking work - the
 * browser asks for byte ranges as the user scrubs.
 *
 * This sketch creates a known file on LittleFS at boot, then serves it. Flash,
 * open Serial @ 115200 for the IP, then:
 *   curl -s -D - -o /dev/null http://<ip>/data.bin           # 200, Accept-Ranges: bytes
 *   curl -s -r 0-9    http://<ip>/data.bin | xxd | head      # 206, first 10 bytes
 *   curl -s -r -16    http://<ip>/data.bin                   # 206, last 16 bytes
 *   curl -s -D - -r 999999-  http://<ip>/data.bin            # 416 Range Not Satisfiable
 *
 * NOTE: optional features are gated by a compile flag the *library* sources must
 * also see. The `#define` below documents intent, but for PlatformIO enable it
 * for the whole build, e.g. in platformio.ini:
 *     build_flags = -DPC_ENABLE_RANGE=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.) A define in the
 * sketch alone does not reach the separately-compiled library .cpp.
 */

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
    {
        return;
    }
    File f = LittleFS.open("/data.bin", "w");
    if (!f)
    {
        return;
    }
    uint8_t buf[256];
    for (int i = 0; i < 256; i++)
    {
        buf[i] = (uint8_t)i;
    }
    for (int k = 0; k < 4; k++) // 4 * 256 = 1024 bytes
    {
        f.write(buf, sizeof(buf));
    }
    f.close();
}

void setup()
{
    Serial.begin(115200);

    if (!LittleFS.begin(true)) // format on first use
    {
        Serial.println("LittleFS mount failed");
    }
    make_demo_file();

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

    // serve_file() honors Range automatically when PC_ENABLE_RANGE is set.
    server.on("/data.bin", HttpMethod::HTTP_GET,
              [](uint8_t id, HttpReq *) { server.serve_file(id, LittleFS, "/data.bin", "application/octet-stream"); });

    int32_t result = server.begin(80);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
    }
    else
    {
        Serial.println("Range-capable file server on :80 (try: curl -r 0-9 http://<ip>/data.bin)");
    }
}

void loop()
{
    server.handle();
}
