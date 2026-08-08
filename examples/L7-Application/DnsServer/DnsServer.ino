// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file DnsServer.ino
 * @brief Run a tiny DNS server so LAN devices can use names, not IPs (PC_ENABLE_DNS_SERVER).
 *
 * On a network with no real DNS (offline, air-gapped, a lab bench), nothing turns
 * "printer.lan" into an address. This makes the ESP32 answer those lookups from a small table
 * you fill in - a companion to the NTP server (example 58) for self-hosted infrastructure.
 * It also registers its own name, so `esp32.lan` resolves to this board.
 *
 * Point another device's DNS at this board's IP, then `nslookup printer.lan <board-ip>`.
 *
 * Build flags (PlatformIO): `-DPC_ENABLE_DNS_SERVER=1`
 */

#define PC_ENABLE_DNS_SERVER 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/net/dns_server/dns_server.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

void setup()
{
    Serial.begin(115200);

    Physical.wifi->init(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!Physical.wifi->ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // The name -> IPv4 records this server will answer. Edit these for your network.
    pc_dns_server_add("esp32.lan", (uint8_t)(ip & 0xFF), (uint8_t)((ip >> 8) & 0xFF), (uint8_t)((ip >> 16) & 0xFF),
                      (uint8_t)((ip >> 24) & 0xFF)); // this board, by name
    pc_dns_server_add("printer.lan", 192, 168, 1, 50);
    pc_dns_server_add("nas.lan", 192, 168, 1, 60);

    if (pc_dns_server_begin())
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
