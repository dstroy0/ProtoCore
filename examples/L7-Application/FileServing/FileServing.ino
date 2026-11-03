// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file FileServing.ino
 * @brief Serve a static site from LittleFS with serve_static().
 *
 * serve_static(url_prefix, fs, fs_root) mounts a filesystem subtree at a URL
 * prefix: a request for "/" maps to "/www/index.html", "/app.js" to
 * "/www/app.js", and so on (content types inferred from the extension). File
 * serving needs PROTOCORE_ENABLE_FILE_SERVING, and the LittleFS mount PROTOCORE_ENABLE_MNT.
 *
 * Put your assets under a `data/www/` folder and upload the LittleFS image
 * ("Upload Filesystem Image" in PlatformIO / Arduino) before running.
 *
 * Flash, open Serial @ 115200 for the IP, then browse to http://<ip>/.
 */

#define PROTOCORE_ENABLE_FILE_SERVING 1
#define PROTOCORE_ENABLE_MNT 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/application/file_serving/file_serving.h"
#include "test/core_setup/hal/esp/esp_mnt_fs.h"
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

    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    Serial.print("Connecting to WiFi");
    while (!protocore_physical_wifi_up())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = protocore_physical_egress_ipv4(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Map the URL tree "/" onto the "/www" directory in LittleFS.
    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = protocore_mnt_fs(&LittleFS);
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span());
    // Cache assets for an hour; browsers still revalidate cheaply via the ETag.
    set_cache_control("max-age=3600");
    begin_http(80, NULL);
}

void loop()
{
    handle();
}
