// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file WebDav.ino
 * @brief WebDAV file share (RFC 4918) backed by LittleFS.
 *
 * Mounts the LittleFS subtree "/dav" as a WebDAV share at the URL "/dav". A
 * client can browse and edit the files over standard WebDAV methods:
 *     curl -X PROPFIND  -H "Depth: 1" http://<ip>/dav/
 *     curl -T file.txt  http://<ip>/dav/file.txt          # PUT
 *     curl -X MKCOL     http://<ip>/dav/sub               # make a collection
 *     curl -X MOVE -H "Destination: /dav/b.txt" http://<ip>/dav/file.txt
 *     curl -X DELETE    http://<ip>/dav/b.txt
 *     rclone lsd :webdav: --webdav-url http://<ip>/dav --webdav-vendor other
 *
 * Supported: OPTIONS, PROPFIND (Depth 0/1), PROPPATCH, GET, HEAD, PUT, DELETE,
 * MKCOL, COPY (files + collections), MOVE, and advisory LOCK/UNLOCK. PROPPATCH is answered 207
 * with each property refused 403 (read-only properties) - so Explorer/Finder,
 * which set a timestamp right after a PUT, do not error. PUT streams the body
 * straight to the file, so an upload is not bounded by BODY_BUF_SIZE; locks are
 * advisory. Add per-route auth + HTTPS (and the per-IP throttle) before exposing a
 * writable share.
 *
 * NOTE: optional services are gated by a compile flag the *library* sources must
 * also see; for PlatformIO enable it for the whole build, e.g.:
 *     build_flags = -DPC_ENABLE_WEBDAV=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_WEBDAV 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include <LittleFS.h>

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


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
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
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

    dav("/dav", LittleFS, "/dav");
    begin_http(80, NULL);
    Serial.println("WebDAV share at http://<ip>/dav");
}

void loop()
{
    handle();
}
