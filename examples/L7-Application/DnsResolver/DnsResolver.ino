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
 * The resolve is blocking; this demo runs it in the handler for clarity. In a real
 * app, resolve off the request hot path (e.g. from loop() / a setup step) and cache.
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
    while (!protocore_physical_wifi_up())
    {
        delay(250);
    }
    uint32_t ip = protocore_physical_egress_ipv4(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    on_http("/resolve", HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *host = HttpParser.get_query(protocore_http_parser_span(), req, "host");
        if (!host)
        {
            send_text(id, 400, "application/json", "{\"error\":\"missing host\"}");
            return;
        }
        // The resolver is polled: the first call sends the query, a later one finds the answer.
        ResolverV.query.host = host;
        Resolver.resolve(protocore_dns_resolver_span());
        if (ResolverV.state == PROTOCORE_DNS_BUSY)
        {
            send_text(id, 503, "application/json", "{\"error\":\"resolving, retry\"}");
            return;
        }
        if (ResolverV.state != PROTOCORE_DNS_READY)
        {
            send_text(id, 502, "application/json", "{\"error\":\"resolve failed\"}");
            return;
        }
        const uint32_t ip = ResolverV.u32;
        ResolverV.addr.ip = ip;
        Resolver.verify(protocore_dns_resolver_span());
        const bool verified = ResolverV.ok;
        char b[80];
        snprintf(b, sizeof(b), "{\"ip\":\"%u.%u.%u.%u\",\"verified\":%s}", (unsigned)((ip >> 24) & 0xFF),
                 (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 8) & 0xFF), (unsigned)(ip & 0xFF),
                 verified ? "true" : "false");
        send_text(id, 200, "application/json", b);
    });
    begin_http(80, NULL);
}

void loop()
{
    handle();
}
