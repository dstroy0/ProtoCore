// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mDNS.ino
 * @brief Advertise the device over mDNS / DNS-SD (PROTOCORE_ENABLE_MDNS).
 *
 * protocore_mdns_begin(hostname, port) makes the device reachable at
 * `<hostname>.local` and advertises an `_http._tcp` service, so clients on the
 * LAN can find it without knowing its IP.
 *
 * NOTE: this service is compiled into the library only when PROTOCORE_ENABLE_MDNS
 * is set for the whole build (a .ino #define does not reach the separately
 * compiled library). In platformio.ini:
 *     build_flags = -DPROTOCORE_ENABLE_MDNS=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 *
 * Flash, then browse to http://pc-demo.local/.
 */

#define PROTOCORE_ENABLE_MDNS 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/application/mdns_service/mdns_service.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";
static const char *HOSTNAME = "pc-demo";


void setup()
{
    Serial.begin(115200);
    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    Serial.print("Connecting to WiFi");
    while (!protocore_physical_wifi_up())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = protocore_physical_egress_ipv4(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    on_http("/", HTTP_GET,
              [](uint8_t id, HttpReq *) { send_text(id, 200, "text/plain", "hello via mDNS"); });
    begin_http(80, NULL);

    MdnsServiceV.begin_args.hostname = HOSTNAME;
    MdnsServiceV.begin_args.http_port = 80;
    MdnsService.begin(protocore_mdns_service_span());
    if (MdnsServiceV.ok)
    {
        // Bonjour TXT records (shown by DNS-SD browsers) + advertise HTTPS too.
        MdnsServiceV.txt_args.key = "path";
        MdnsServiceV.txt_args.value = "/";
        MdnsService.txt(protocore_mdns_service_span());
        MdnsServiceV.txt_args.key = "fw";
        MdnsServiceV.txt_args.value = "1.0";
        MdnsService.txt(protocore_mdns_service_span());
        MdnsServiceV.add_service_args.service_type = "_https";
        MdnsServiceV.add_service_args.proto = "_tcp";
        MdnsServiceV.add_service_args.port = 443;
        MdnsService.add_service(protocore_mdns_service_span());
        Serial.printf("mDNS: http://%s.local/\n", HOSTNAME);
    }
}

void loop()
{
    handle();
}
