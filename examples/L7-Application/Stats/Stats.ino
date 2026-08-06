// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Stats.ino
 * @brief Runtime statistics endpoint (PC_ENABLE_STATS).
 *
 * stats(slot_id) writes a JSON snapshot - uptime, request/error counts,
 * connection-pool usage, free heap - straight to the response. Wire it to a
 * route to expose live diagnostics.
 *
 * NOTE: this feature is compiled into the library only when PC_ENABLE_STATS
 * is set for the whole build (a .ino #define does not reach the separately
 * compiled library). In platformio.ini:
 *     build_flags = -DPC_ENABLE_STATS=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 *
 * Flash, open Serial @ 115200 for the IP, then GET http://<ip>/stats.
 */

#define PC_ENABLE_STATS 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


void setup()
{
    Serial.begin(115200);
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

    on_http("/stats", HTTP_GET, [](uint8_t id, HttpReq *) { stats(id); });
    begin_http(80, NULL);
}

void loop()
{
    handle();
}
