// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file AcceptThrottle.ino
 * @brief Connection-flood defense via the accept-rate throttle (PC_ENABLE_ACCEPT_THROTTLE).
 *
 * When enabled, the accept callback rejects new connections once more than
 * PC_ACCEPT_THROTTLE_MAX have been accepted within
 * PC_ACCEPT_THROTTLE_WINDOW_MS (a global fixed window, two counters, no per-IP
 * table). It bounds connection churn (e.g. reconnect brute force) on top of the
 * bounded connection pool. There is no runtime API - it is purely a build-time
 * defense; this sketch just shows enabling it.
 *
 * NOTE: this feature is compiled into the library only when the flag is set for
 * the whole build (a .ino #define does not reach the separately compiled
 * library). In platformio.ini:
 *     build_flags = -DPC_ENABLE_ACCEPT_THROTTLE=1
 *                   -DPC_ACCEPT_THROTTLE_MAX=20
 *                   -DPC_ACCEPT_THROTTLE_WINDOW_MS=1000
 * (Arduino IDE: they are already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_ACCEPT_THROTTLE 1

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

    on_http("/", HTTP_GET,
              [](uint8_t id, HttpReq *) { send_text(id, 200, "text/plain", "throttled server"); });
    begin_http(80); // accept throttle is active automatically when the flag is built in
}

void loop()
{
    handle();
}
