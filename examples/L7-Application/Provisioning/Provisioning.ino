// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Provisioning.ino
 * @brief First-boot WiFi provisioning via a captive portal (PC_ENABLE_PROVISIONING).
 *
 * On first boot (no stored credentials) the device starts a softAP
 * "PC-Setup" and a catch-all DNS responder (raw lwIP UDP - no add-on
 * library), so connecting a phone pops the credentials form. Submitted SSID/PSK
 * persist to NVS and the device reboots into station mode and serves normally.
 *
 * No external libraries: only WiFi (softAP), lwIP UDP, and Preferences (NVS).
 * To re-provision, call pc_provisioning_clear() (e.g. from a button handler).
 */

#define PC_ENABLE_PROVISIONING 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/system/provisioning_service/provisioning_service.h"


void handle_root(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    send_text(slot_id, 200, "text/plain", "Provisioned - hello from station mode!");
}

void setup()
{
    Serial.begin(115200);

    char ssid[33];
    char psk[64];
    if (pc_provisioning_load(ssid, sizeof(ssid), psk, sizeof(psk)))
    {
        // Credentials present: connect as a normal station.
        init_wifi_physical(ssid, psk);
        Serial.print("Connecting to ");
        Serial.println(ssid);
        while (!wifi_ready())
        {
            delay(250);
        }
        uint32_t ip = pc_net_egress_ip();
        Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                      (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

        on_http("/", HTTP_GET, handle_root);
        begin_http(80);
        Serial.println("Station mode; serving on port 80");
    }
    else
    {
        // No credentials: bring up the captive portal.
        begin_http(80);
        pc_provisioning_begin("PC-Setup");
        Serial.println("Provisioning: join WiFi 'PC-Setup' and open any page");
    }
}

void loop()
{
    handle();
}
