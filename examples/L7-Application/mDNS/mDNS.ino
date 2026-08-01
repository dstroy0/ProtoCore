// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mDNS.ino
 * @brief Advertise the device over mDNS / DNS-SD (PC_ENABLE_MDNS).
 *
 * pc_mdns_begin(hostname, port) makes the device reachable at
 * `<hostname>.local` and advertises an `_http._tcp` service, so clients on the
 * LAN can find it without knowing its IP.
 *
 * NOTE: this service is compiled into the library only when PC_ENABLE_MDNS
 * is set for the whole build (a .ino #define does not reach the separately
 * compiled library). In platformio.ini:
 *     build_flags = -DPC_ENABLE_MDNS=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 *
 * Flash, then browse to http://pc-demo.local/.
 */

#define PC_ENABLE_MDNS 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/net/mdns_service/mdns_service.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";
static const char *HOSTNAME = "pc-demo";


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

    on_http("/", HttpMethod::HTTP_GET,
              [](uint8_t id, HttpReq *) { send_text(id, 200, "text/plain", "hello via mDNS"); });
    begin_http(80);

    if (pc_mdns_begin(HOSTNAME, 80))
    {
        // Bonjour TXT records (shown by DNS-SD browsers) + advertise HTTPS too.
        pc_mdns_txt("path", "/");
        pc_mdns_txt("fw", "1.0");
        pc_mdns_add_service("_https", "_tcp", 443);
        Serial.printf("mDNS: http://%s.local/\n", HOSTNAME);
    }
}

void loop()
{
    handle();
}
