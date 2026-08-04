// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file DnsResolver.ino
 * @brief DNS resolver with answer verification (PC_ENABLE_DNS_RESOLVER).
 *
 * Resolves a hostname to an IPv4 address and rejects suspicious answers (0.0.0.0,
 * loopback, broadcast, multicast - DNS-rebinding / spoof indicators).
 *   GET /resolve?host=dns.google -> {"ip":"8.8.8.8","verified":true}
 *
 * The resolve is blocking; this demo runs it in the handler for clarity. In a real
 * app, resolve off the request hot path (e.g. from loop() / a setup step) and cache.
 *
 * NOTE: enable it for the whole build. In platformio.ini:
 *     build_flags = -DPC_ENABLE_DNS_RESOLVER=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_DNS_RESOLVER 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/net/dns_resolver/dns_resolver.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


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

    on_http("/resolve", HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *host = http_get_query(req, "host");
        if (!host)
        {
            send_text(id, 400, "application/json", "{\"error\":\"missing host\"}");
            return;
        }
        uint32_t ip = 0;
        bool ok = pc_dns_resolver_resolve(host, &ip);
        if (!ok)
        {
            send_text(id, 502, "application/json", "{\"error\":\"resolve failed\"}");
            return;
        }
        char b[80];
        snprintf(b, sizeof(b), "{\"ip\":\"%u.%u.%u.%u\",\"verified\":%s}", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                 (ip >> 8) & 0xFF, ip & 0xFF, pc_dns_resolver_verify(ip) ? "true" : "false");
        send_text(id, 200, "application/json", b);
    });
    begin_http(80, NULL);
}

void loop()
{
    handle();
}
