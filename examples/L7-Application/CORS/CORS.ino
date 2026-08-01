// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file CORS.ino
 * @brief Cross-Origin Resource Sharing headers via set_cors().
 *
 * set_cors(origin) builds an Access-Control-Allow-* block once and injects it
 * into every response (and answers CORS preflight OPTIONS requests), so a web
 * app served from another origin can call this device's JSON API from the
 * browser. Use a specific origin in production rather than "*".
 *
 * Flash, open Serial @ 115200 for the IP, then from a page on another origin:
 *   fetch('http://<ip>/api').then(r=>r.json()).then(console.log)
 */

#include "protocore.h"
#include "network_drivers/physical/physical.h"

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

    set_cors("*"); // allow any origin (tighten to your web app's origin in production)
    on_http("/api", HttpMethod::HTTP_GET,
              [](uint8_t id, HttpReq *) { send_text(id, 200, "application/json", "{\"ok\":true}"); });
    begin_http(80);
}

void loop()
{
    handle();
}
