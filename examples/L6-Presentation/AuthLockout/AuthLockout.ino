// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file AuthLockout.ino
 * @brief Brute-force lockout for HTTP auth (PC_ENABLE_AUTH_LOCKOUT).
 *
 * Adds a per-source-IP guard in front of authenticated routes: after a few wrong
 * passwords from one address, that address is locked out with an exponential
 * backoff and gets 429 Too Many Requests + Retry-After (no credential check)
 * instead of unlimited guesses. A correct login clears the address immediately.
 * State lives in a fixed BSS table (no heap).
 *
 * NOTE: enable the lockout for the whole build (a .ino #define does not reach the
 * separately compiled library). In platformio.ini:
 *     build_flags = -DPC_ENABLE_AUTH_LOCKOUT=1
 * (PC_ENABLE_AUTH is on by default. Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so
 * it builds as-is.)
 *
 * Try: repeat `curl -u admin:wrong http://<ip>/secret` until you get 429, then
 * `curl -u admin:s3cret http://<ip>/secret` once the Retry-After elapses.
 */

#define PC_ENABLE_AUTH_LOCKOUT 1

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
              [](uint8_t id, HttpReq *) { send_text(id, 200, "text/plain", "public page"); });

    // Protected route. Repeated wrong passwords from one IP trip the lockout
    // (429) with exponential backoff; the tuning lives in protocore_config.h
    // (PC_AUTH_LOCKOUT_THRESHOLD / _BASE_MS / _MAX_MS).
    on_http(
        "/secret", HTTP_GET,
        [](uint8_t id, HttpReq *) { send_text(id, 200, "text/plain", "authenticated!"); }, "Restricted", "admin",
        "s3cret");

    begin_http(80, NULL);
}

void loop()
{
    handle();
}
