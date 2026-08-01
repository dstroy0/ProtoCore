// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file IpAllowlist.ino
 * @brief Restrict who may connect with a source-IP allowlist (PC_ENABLE_IP_ALLOWLIST).
 *
 * The accept callback drops any TCP connection whose source address falls
 * outside the configured CIDR rules - a coarse first-line firewall in front of
 * every listener (HTTP, WS, etc.). An empty allowlist allows everything, so add
 * at least one rule. Rules live in a fixed BSS table (no heap).
 *
 * NOTE: enable the feature for the whole build (a .ino #define does not reach the
 * separately compiled library). In platformio.ini:
 *     build_flags = -DPC_ENABLE_IP_ALLOWLIST=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 *
 * Here only the 192.168.1.0/24 LAN, a single host 10.0.0.5, and an IPv6 prefix
 * may connect; a spoofed source can still pass, so pair it with the accept throttles.
 * Rules are written as CIDR text (IPv4 or IPv6), matched against the peer's full
 * address per family - a v4 rule never admits a v6 peer and vice versa.
 */

#define PC_ENABLE_IP_ALLOWLIST 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/listener.h" // listener_ip_allow_add_cidr

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


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

    // Only these sources may connect; everything else is dropped at accept time.
    listener_ip_allow_add_cidr("192.168.1.0/24"); // local /24
    listener_ip_allow_add_cidr("10.0.0.5");       // one trusted host (bare address -> /32)
    listener_ip_allow_add_cidr("2001:db8::/32");  // an IPv6 prefix

    on_http("/", HttpMethod::HTTP_GET,
              [](uint8_t id, HttpReq *) { send_text(id, 200, "text/plain", "hello from an allowed address"); });
    begin_http(80);
}

void loop()
{
    handle();
}
