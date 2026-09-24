// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file BasicAuth.ino
 * @brief Per-route HTTP Basic authentication (RFC 7617).
 *
 * The auth-aware on() overload protects a route with a realm + username +
 * password; the handler runs only after the credentials check passes, otherwise
 * the server answers 401 with a WWW-Authenticate challenge. Pass digest=true for
 * Digest auth instead (see the DigestAuth example). Auth is on by default
 * (PROTOCORE_ENABLE_AUTH).
 *
 * NOTE: Basic credentials are base64 (not encryption) - use it over HTTPS or an
 * SSH tunnel on untrusted networks.
 *
 * Flash, open Serial @ 115200 for the IP, then visit http://<ip>/secret
 * (user "admin", password "s3cret").
 */

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


void setup()
{
    Serial.begin(115200);
    // Every Physical entry reads its operands from PhysicalV and writes its outcome back there.
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

    on_http("/", HTTP_GET,
              [](uint8_t id, HttpReq *) { send_text(id, 200, "text/plain", "public page"); });

    // Basic auth (digest defaults to false): realm, username, password.
    on_http_auth(
        "/secret", HTTP_GET,
        [](uint8_t id, HttpReq *) { send_text(id, 200, "text/plain", "authenticated!"); }, "Restricted", "admin",
        "s3cret", PROTO_FALSE);

    begin_http(80, NULL);
}

void loop()
{
    handle();
}
