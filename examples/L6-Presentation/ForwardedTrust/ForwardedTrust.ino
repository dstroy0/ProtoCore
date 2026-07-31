// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ForwardedTrust.ino
 * @brief Key the auth lockout on the real client behind a trusted reverse proxy
 *        (PC_ENABLE_FORWARDED_TRUST).
 *
 * When the device sits behind a reverse proxy / load balancer, every request arrives from the proxy's
 * one TCP address, so a per-IP brute-force lockout would lock out EVERY client at once (one abuser
 * trips it for all) or be useless. This keys the lockout on the ORIGINAL client address the proxy
 * reports in `Forwarded` (RFC 7239) / `X-Forwarded-For` - but ONLY when the request's real TCP peer is
 * a proxy you have explicitly trusted, because that header is client-spoofable. A direct, untrusted
 * client's header is ignored, so it can neither dodge its own lockout nor frame another address.
 *
 * Register each trusted upstream with pc_forwarded_trust_add_cidr(). The accept-time throttle and the
 * IP allowlist deliberately keep using the real TCP source.
 *
 * NOTE: enable the flags for the whole build (a .ino #define does not reach the separately compiled
 * library). PlatformIO: build_flags = -DPC_ENABLE_AUTH_LOCKOUT=1 -DPC_ENABLE_FORWARDED_TRUST=1.
 * Arduino IDE: already set in the build_opt.h beside this sketch, so it builds as-is.
 *
 * Try (from a host inside the trusted CIDR, standing in for the proxy):
 *   # lock out one client, keyed on its forwarded address:
 *   curl -u admin:wrong -H 'X-Forwarded-For: 203.0.113.7'  http://<ip>/secret   # ...x THRESHOLD -> 429
 *   # a DIFFERENT forwarded client is unaffected (per-client keying, not per-proxy):
 *   curl -u admin:wrong -H 'X-Forwarded-For: 198.51.100.9' http://<ip>/secret   # 401, not 429
 */

#define PC_ENABLE_AUTH_LOCKOUT 1
#define PC_ENABLE_FORWARDED_TRUST 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/security/forwarded_trust/forwarded_trust.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

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

    // Trust the reverse proxy(ies) in front of this device. Only a request whose real TCP peer falls in
    // one of these CIDRs has its Forwarded / X-Forwarded-For client address believed. Set this to YOUR
    // proxy's address / subnet; the RFC 5737 documentation range below is a placeholder.
    pc_forwarded_trust_add_cidr("192.0.2.0/24");

    server.on("/", HttpMethod::HTTP_GET,
              [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "public page"); });

    // Protected route. Behind a trusted proxy the lockout counts failures per ORIGINAL client, so one
    // abuser does not lock out everyone sharing the proxy's address.
    server.on(
        "/secret", HttpMethod::HTTP_GET,
        [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "authenticated!"); }, "Restricted", "admin",
        "s3cret");

    server.begin(80);
}

void loop()
{
    server.handle();
}
