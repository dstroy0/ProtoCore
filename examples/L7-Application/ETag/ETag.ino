// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ETag.ino
 * @brief Conditional GET with ETag for served files (PROTOCORE_ENABLE_ETAG).
 *
 * With ETag enabled, FileServing.serve_file()/serve_static() emit a strong ETag (derived
 * from the file size + mtime) and answer a matching If-None-Match with
 * 304 Not Modified - saving bandwidth on repeat fetches of static assets.
 *
 * NOTE: this feature is compiled into the library only when PROTOCORE_ENABLE_ETAG
 * is set for the whole build (a .ino #define does not reach the separately
 * compiled library). In platformio.ini:
 *     build_flags = -DPROTOCORE_ENABLE_ETAG=1 -DPROTOCORE_ENABLE_FILE_SERVING=1 -DPROTOCORE_ENABLE_MNT=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 *
 * Put a file at `data/www/index.html`, upload the LittleFS image, then:
 *   curl -i http://<ip>/            # 200 + ETag: "..."
 *   curl -i -H 'If-None-Match: "<etag>"' http://<ip>/   # 304 Not Modified
 */

#define PROTOCORE_ENABLE_ETAG 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "test/core_setup/hal/esp/esp_mnt_fs.h" // protocore_mnt_fs(): bind an Arduino FS to the storage seam
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
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
        Serial.print('.');
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    FileServingV.serve_static_args.url_prefix = "/";
    FileServingV.serve_static_args.file_sys = protocore_mnt_fs(&LittleFS);
    FileServingV.serve_static_args.fs_root = "/www";
    FileServing.serve_static(protocore_file_serving_span()); // ETag + If-None-Match handled automatically
    begin_http(80, NULL);
}

void loop()
{
    handle();
}
