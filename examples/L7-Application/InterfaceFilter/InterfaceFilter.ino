// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file InterfaceFilter.ino
 * @brief Gate routes to the station or softAP interface (ON_STA / ON_AP style).
 *
 * Brings up WiFi in AP+STA mode and registers the same kinds of routes on
 * different interfaces: a setup/config page visible only on the softAP, and an
 * app API visible only on the station link. The interface is determined by
 * comparing each connection's local IP to the softAP IP, so call
 * set_ap_ip(Physical.wifi->ap_ip()) once after starting the AP.
 *
 * Test from a station client (your LAN):    curl http://<sta-ip>/api/data   -> 200
 *                                            curl http://<sta-ip>/setup      -> 404
 * Test from a softAP client (join the AP):  curl http://192.168.4.1/setup   -> 200
 *                                            curl http://192.168.4.1/api/data-> 404
 */

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";
static const char *AP_SSID = "PC-Setup";
static const char *AP_PASS = "configme123";


void handle_setup(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    send_text(slot_id, 200, "text/html", "<h1>Setup</h1><p>softAP only</p>");
}

void handle_api(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    send_text(slot_id, 200, "application/json", "{\"data\":42,\"iface\":\"sta\"}");
}

void handle_root(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    send_text(slot_id, 200, "text/plain", "available on both interfaces");
}

void setup()
{
    Serial.begin(115200);

    // AP + STA so both interfaces exist simultaneously.
    Physical.wifi->init_ap(AP_SSID, AP_PASS); // softAP (also enables AP+STA coexistence)
    Physical.wifi->init(SSID, PASSWORD);      // station link
    Serial.print("Connecting to WiFi");
    while (!Physical.wifi->ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t sta_ip = Physical.link->egress_ip();
    uint32_t ap_ip = Physical.wifi->ap_ip();
    Serial.printf("\nSTA IP: %u.%u.%u.%u\n", (unsigned)(sta_ip & 0xFF), (unsigned)((sta_ip >> 8) & 0xFF),
                  (unsigned)((sta_ip >> 16) & 0xFF), (unsigned)((sta_ip >> 24) & 0xFF));
    Serial.printf("AP  IP: %u.%u.%u.%u\n", (unsigned)(ap_ip & 0xFF), (unsigned)((ap_ip >> 8) & 0xFF),
                  (unsigned)((ap_ip >> 16) & 0xFF), (unsigned)((ap_ip >> 24) & 0xFF));

    // Required for STA/AP classification.
    set_ap_ip(ap_ip);

    on_http_iface("/setup", HTTP_GET, handle_setup, PC_IFACE_AP);   // softAP only
    on_http_iface("/api/data", HTTP_GET, handle_api, PC_IFACE_STA); // station only
    on_http("/", HTTP_GET, handle_root);                            // any interface

    int32_t result = begin_http(80, NULL);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }
    Serial.println("Server started on port 80");
}

void loop()
{
    handle();
}
