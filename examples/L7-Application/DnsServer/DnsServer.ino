// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file DnsServer.ino
 * @brief Run a tiny DNS server so LAN devices can use names, not IPs (PROTOCORE_ENABLE_DNS_SERVER).
 *
 * On a network with no real DNS (offline, air-gapped, a lab bench), nothing turns
 * "printer.lan" into an address. This makes the ESP32 answer those lookups from a small table
 * you fill in - a companion to the NTP server (example 58) for self-hosted infrastructure.
 * It also registers its own name, so `esp32.lan` resolves to this board.
 *
 * Point another device's DNS at this board's IP, then `nslookup printer.lan <board-ip>`.
 *
 * Build flags (PlatformIO): `-DPROTOCORE_ENABLE_DNS_SERVER=1`
 */

#define PROTOCORE_ENABLE_DNS_SERVER 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/network/dns/dns_server/dns_server.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// Record one name and its A record in the server's table.
static void dns_add(const char *name, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    DnsServerV.rec.name = name;
    DnsServerV.rec.a = a;
    DnsServerV.rec.b = b;
    DnsServerV.rec.c = c;
    DnsServerV.rec.d = d;
    DnsServer.add(protocore_dns_server_span());
}

void setup()
{
    Serial.begin(115200);

    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    Serial.print("Connecting to WiFi");
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
        Serial.print('.');
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // The name -> IPv4 records this server will answer. Edit these for your network.
    dns_add("esp32.lan", (uint8_t)(ip & 0xFF), (uint8_t)((ip >> 8) & 0xFF), (uint8_t)((ip >> 16) & 0xFF),
            (uint8_t)((ip >> 24) & 0xFF)); // this board, by name
    dns_add("printer.lan", 192, 168, 1, 50);
    dns_add("nas.lan", 192, 168, 1, 60);

    DnsServer.begin(protocore_dns_server_span());
    if (DnsServerV.ok)
    {
        Serial.println("DNS server on UDP/53 (point a device's DNS here, then: nslookup printer.lan <this-ip>)");
    }
    else
    {
        Serial.println("DNS server failed to bind :53");
    }
}

void loop()
{
    delay(1000);
}
