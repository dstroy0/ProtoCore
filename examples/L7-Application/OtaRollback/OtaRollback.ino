// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file OtaRollback.ino
 * @brief OTA rollback protection / soft-brick safeguard (PROTOCORE_ENABLE_OTA_ROLLBACK).
 *
 * After an OTA update the new image boots PENDING_VERIFY. Each loop this runs a
 * self-test (here: WiFi up + healthy heap) and ticks the rollback service: a
 * passing self-test commits the image, a failing one (or no confirm within
 * PROTOCORE_OTA_CONFIRM_WINDOW_MS) rolls back to the previous image - so a bad update
 * self-heals instead of soft-bricking. GET /ota-state shows the current state.
 *
 * Requires the bootloader's app-rollback support
 * (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) to actually roll back.
 *
 * NOTE: enable it for the whole build. In platformio.ini:
 *     build_flags = -DPROTOCORE_ENABLE_OTA_ROLLBACK=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PROTOCORE_ENABLE_OTA_ROLLBACK 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "server/update/ota_rollback/ota_rollback.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static uint8_t ota_rollback_work[16]; // the borrow an OtaRollback entry takes; it keeps its state in OtaRollbackV

static bool self_test()
{
    Physical.wifi_ready(protocore_physical_span());
    return PhysicalV.ok && ESP.getFreeHeap() > 20000; // your real health checks here
}

void setup()
{
    Serial.begin(115200);
    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    on_http("/ota-state", HTTP_GET, [](uint8_t id, HttpReq *) {
        char b[48];
        OtaRollback.state(ota_rollback_work);
        snprintf(b, sizeof(b), "{\"img_state\":%u}", OtaRollbackV.img_state);
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
        OtaRollbackV.self_test_ok = self_test();
        OtaRollback.tick(ota_rollback_work);
        protocore_ota_action a = OtaRollbackV.action;
        if (a == protocore_ota_action::PROTOCORE_OTA_COMMIT)
        {
            Serial.println("[ota] image committed");
            done = true;
        }
    }
    handle();
}
