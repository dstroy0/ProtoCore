// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file FileServing.ino
 * @brief Serve a static site from LittleFS with serve_static().
 *
 * serve_static(url_prefix, fs, fs_root) mounts a filesystem subtree at a URL
 * prefix: a request for "/" maps to "/www/index.html", "/app.js" to
 * "/www/app.js", and so on (content types inferred from the extension). File
 * serving is on by default (PC_ENABLE_FILE_SERVING).
 *
 * Put your assets under a `data/www/` folder and upload the LittleFS image
 * ("Upload Filesystem Image" in PlatformIO / Arduino) before running.
 *
 * Flash, open Serial @ 115200 for the IP, then browse to http://<ip>/.
 */

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include <LittleFS.h>

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


void setup()
{
    Serial.begin(115200);
    if (!LittleFS.begin(true))
    {
        Serial.println("LittleFS mount failed (upload a filesystem image)");
    }

    Physical.wifi->init(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!Physical.wifi->ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Map the URL tree "/" onto the "/www" directory in LittleFS.
    serve_static("/", LittleFS, "/www");
    // Cache assets for an hour; browsers still revalidate cheaply via the ETag.
    set_cache_control("max-age=3600");
    begin_http(80, NULL);
}

void loop()
{
    handle();
}
