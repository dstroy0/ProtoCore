// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file DnsResolver.ino
 * @brief DNS resolver with answer verification (PROTOCORE_ENABLE_DNS_RESOLVER).
 *
 * Resolves a hostname to an IPv4 address and rejects suspicious answers (0.0.0.0,
 * loopback, broadcast, multicast - DNS-rebinding / spoof indicators).
 *   GET /resolve?host=dns.google -> {"ip":"8.8.8.8","verified":true}
 *
 * Resolver.resolve is non-blocking (it reports PROTOCORE_DNS_BUSY until the answer lands);
 * this demo polls it to completion in the handler for clarity. In a real app, resolve off
 * the request hot path (e.g. from loop() / a setup step) and cache.
 *
 * NOTE: enable it for the whole build. In platformio.ini:
 *     build_flags = -DPROTOCORE_ENABLE_DNS_RESOLVER=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PROTOCORE_ENABLE_DNS_RESOLVER 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/network/dns/dns_resolver/dns_resolver.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


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

    on_http("/resolve", HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *host = HttpParser.get_query(protocore_http_parser_span(), req, "host");
        if (!host)
        {
            send_text(id, 400, "application/json", "{\"error\":\"missing host\"}");
            return;
        }
        ResolverV.query.host = host;
        for (Resolver.resolve(protocore_dns_resolver_span()); ResolverV.state == PROTOCORE_DNS_BUSY;
             Resolver.resolve(protocore_dns_resolver_span()))
        {
            delay(10);
        }
        if (ResolverV.state != PROTOCORE_DNS_READY)
        {
            send_text(id, 502, "application/json", "{\"error\":\"resolve failed\"}");
            return;
        }
        uint32_t ip = ResolverV.u32; // host order
        ResolverV.addr.ip = ip;
        Resolver.verify(protocore_dns_resolver_span());
        char b[80];
        snprintf(b, sizeof(b), "{\"ip\":\"%u.%u.%u.%u\",\"verified\":%s}", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                 (ip >> 8) & 0xFF, ip & 0xFF, ResolverV.ok ? "true" : "false");
        send_text(id, 200, "application/json", b);
    });
    begin_http(80, NULL);
}

void loop()
{
    handle();
}
