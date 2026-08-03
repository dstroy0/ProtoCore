// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file PerIpThrottle.ino
 * @brief Per-source-IP connection-flood defense (PC_ENABLE_PER_IP_THROTTLE).
 *
 * When enabled, the accept callback rejects a new connection once one source IPv4
 * address has opened more than PC_PER_IP_THROTTLE_MAX connections within
 * PC_PER_IP_THROTTLE_WINDOW_MS. A fixed BSS table of PC_PER_IP_THROTTLE_SLOTS
 * buckets tracks the busiest recent addresses, so one noisy client is throttled
 * without affecting others - the gap the global accept throttle cannot close (it
 * cannot tell one noisy client from many). There is no runtime API: it is a
 * build-time defense, and this sketch just shows enabling it. Pairs well with the
 * global accept throttle (PC_ENABLE_ACCEPT_THROTTLE) - see the AcceptThrottle example.
 *
 * NOTE: this feature is compiled into the library only when the flag is set for
 * the whole build (a .ino #define does not reach the separately compiled
 * library). In platformio.ini:
 *     build_flags = -DPC_ENABLE_PER_IP_THROTTLE=1
 *                   -DPC_PER_IP_THROTTLE_MAX=10
 *                   -DPC_PER_IP_THROTTLE_WINDOW_MS=10000
 *                   -DPC_PER_IP_THROTTLE_SLOTS=16
 * (Arduino IDE: they are already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_PER_IP_THROTTLE 1

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
              [](uint8_t id, HttpReq *) { send_text(id, 200, "text/plain", "per-IP throttled"); });
    begin_http(80); // per-IP throttle is active automatically when the flag is built in
}

void loop()
{
    handle();
}
