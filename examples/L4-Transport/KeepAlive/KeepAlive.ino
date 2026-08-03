// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file KeepAlive.ino
 * @brief HTTP/1.1 persistent connections (keep-alive).
 *
 * With PC_ENABLE_KEEPALIVE the server reuses one TCP connection for many
 * requests instead of closing after each response - the big win when a browser
 * loads a page plus its assets, or a client polls an endpoint. Behavior:
 *   - HTTP/1.1: connection stays open unless the client sends `Connection: close`
 *   - HTTP/1.0: closes unless the client sends `Connection: keep-alive`
 *   - error responses (400/413/414) always close (unknown next-request boundary)
 *   - each connection serves at most PC_KEEPALIVE_MAX_REQUESTS, then closes
 *   - idle connections are still reclaimed by the conn_timeout sweep
 *
 * It is fully transparent to handler code - you write the same routes; the
 * server picks keep-alive vs close and emits the right `Connection` header.
 *
 * Flash, open Serial @ 115200 for the IP, then watch one connection serve many
 * requests:
 *   curl -v http://<ip>/                 # note "Connection: keep-alive"
 *   curl -v http://<ip>/ http://<ip>/time http://<ip>/         # reuses the socket
 *
 * NOTE: optional features are gated by a compile flag the *library* sources must
 * also see. The `#define` below documents intent, but for PlatformIO enable it
 * for the whole build, e.g. in platformio.ini:
 *     build_flags = -DPC_ENABLE_KEEPALIVE=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.) A define in the
 * sketch alone does not reach the separately-compiled library .cpp.
 */

#define PC_ENABLE_KEEPALIVE 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


static const char PAGE[] = "<!doctype html><html><body><h1>Keep-Alive demo</h1>"
                           "<p>This page and its requests are served over one persistent connection.</p>"
                           "<p><a href=\"/time\">/time</a></p></body></html>";

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

    on_http("/", HTTP_GET, [](uint8_t id, HttpReq *) { send_text(id, 200, "text/html", PAGE); });

    // A tiny endpoint a client can poll repeatedly over the same socket.
    on_http("/time", HTTP_GET, [](uint8_t id, HttpReq *) {
        char buf[32];
        snprintf(buf, sizeof(buf), "uptime_ms=%lu", (unsigned long)millis());
        send_text(id, 200, "text/plain", buf);
    });

    int32_t result = begin_http(80);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
    }
    else
    {
        Serial.println("HTTP server (keep-alive) on :80");
    }
}

void loop()
{
    handle();
}
