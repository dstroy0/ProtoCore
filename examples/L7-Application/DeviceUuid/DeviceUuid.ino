// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file DeviceUuid.ino
 * @brief Stable MAC-derived device UUID (PC_ENABLE_DEVICE_ID).
 *
 * pc_device_uuid() derives a deterministic RFC 4122 v5 UUID from the chip's
 * factory MAC - the same value on every boot, with no storage. Use it as a
 * stable identity for mDNS hostnames, MQTT client IDs, telemetry tags, etc.
 *
 * Flash, open Serial @ 115200 for the IP + UUID, then GET http://<ip>/id.
 */

#define PC_ENABLE_DEVICE_ID 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "server/signaling/device_id.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static char g_uuid[PC_UUID_STR_LEN];

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

    pc_device_uuid(g_uuid); // stable per-chip UUID
    Serial.printf("device UUID: %s\n", g_uuid);

    on_http("/id", HTTP_GET, [](uint8_t id, HttpReq *) {
        char body[64];
        snprintf(body, sizeof(body), "{\"uuid\":\"%s\"}", g_uuid);
        send_text(id, 200, "application/json", body);
    });
    begin_http(80);
}

void loop()
{
    handle();
}
