// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file OTA.ino
 * @brief Authenticated over-the-air firmware update (PC_ENABLE_OTA).
 *
 * Registers a streaming `POST /update` endpoint: a firmware image POSTed with
 * valid HTTP Basic credentials is fed straight into the ESP32 Update API via
 * the parser's streaming-body hook (the image never has to fit in RAM), then
 * the device reboots into the new firmware.
 *
 * Upload with:
 *   curl -u admin:s3cret --data-binary @firmware.bin http://<ip>/update
 *
 * (Generate firmware.bin with `pio run` - it is .pio/build/<env>/firmware.bin.)
 */

#define PC_ENABLE_OTA 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "server/update/ota_service.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


void handle_root(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    send_text(slot_id, 200, "text/plain", "OTA demo - POST a firmware image to /update");
}

void setup()
{
    Serial.begin(115200);

    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
    {
        delay(250);
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    on_http("/", HTTP_GET, handle_root);
    if (begin_http(80) < 0)
    {
        Serial.println("begin() failed");
        return;
    }

    // Authenticated streaming OTA at POST /update.
    pc_ota_begin("/update", "admin", "s3cret");

    Serial.println("Server up; OTA at POST /update");
}

void loop()
{
    handle();
}
