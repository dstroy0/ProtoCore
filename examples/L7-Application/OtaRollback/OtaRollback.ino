// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file OtaRollback.ino
 * @brief OTA rollback protection / soft-brick safeguard (PC_ENABLE_OTA_ROLLBACK).
 *
 * After an OTA update the new image boots PENDING_VERIFY. Each loop this runs a
 * self-test (here: WiFi up + healthy heap) and ticks the rollback service: a
 * passing self-test commits the image, a failing one (or no confirm within
 * PC_OTA_CONFIRM_WINDOW_MS) rolls back to the previous image - so a bad update
 * self-heals instead of soft-bricking. GET /ota-state shows the current state.
 *
 * Requires the bootloader's app-rollback support
 * (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) to actually roll back.
 *
 * NOTE: enable it for the whole build. In platformio.ini:
 *     build_flags = -DPC_ENABLE_OTA_ROLLBACK=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_OTA_ROLLBACK 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "server/update/ota_rollback.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


static bool self_test()
{
    return Physical.wifi->ready() && ESP.getFreeHeap() > 20000; // your real health checks here
}

void setup()
{
    Serial.begin(115200);
    Physical.wifi->init(SSID, PASSWORD);
    while (!Physical.wifi->ready())
    {
        delay(250);
    }
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    on_http("/ota-state", HTTP_GET, [](uint8_t id, HttpReq *) {
        char b[48];
        snprintf(b, sizeof(b), "{\"img_state\":%u}", pc_ota_img_state());
        send_text(id, 200, "application/json", b);
    });
    begin_http(80, NULL);
}

void loop()
{
    // Confirm (or roll back) a freshly-updated image. A no-op once committed or on
    // a normally-booted image.
    static bool done = false;
    if (!done)
    {
        pc_ota_action a = pc_ota_rollback_tick(self_test());
        if (a == pc_ota_action::PC_OTA_COMMIT)
        {
            Serial.println("[ota] image committed");
            done = true;
        }
    }
    handle();
}
