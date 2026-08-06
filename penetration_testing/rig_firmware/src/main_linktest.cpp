// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device physical (Layer 1) link bring-up test: exercises the reorganized physical/esp backend on real
// silicon end-to-end - init_wifi_physical / init_eth_physical, wifi_ready / eth_ready, pc_net_egress_ip /
// pc_net_egress, pc_net_mac / pc_net_ssid / pc_net_channel / pc_net_rssi - and prints "LT " lines over
// serial. A build with PC_ENABLE_ETHERNET (the P4-POE-ETH) brings up the RMII Ethernet PHY; otherwise the S3
// brings up the 802.11 station. A live RIG_IP + egress interface class proves the L1 vendor-partition move
// (physical/esp/physical_esp.cpp under #if PC_VENDOR_ESP) works on hardware, not just at compile time.
//
// Build/flash: S3 via pio (env rig_s3_linktest, WiFi); P4 via arduino-cli (p4/build_p4_linktest.sh, Ethernet).
#include <Arduino.h>

#include "network_drivers/physical/physical.h"

// Credentials come from the build environment (sysenv RIG_WIFI_SSID / RIG_WIFI_PASS via the pio env), never
// committed - the placeholders below only keep an unconfigured build compiling.
#ifndef WIFI_SSID
#define WIFI_SSID "CHANGEME_SSID"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "CHANGEME_PASS"
#endif

static void lt_report(const char *tag)
{
    uint32_t ip = Physical.link->egress_ip();
    Serial.printf("LT %s RIG_IP=%u.%u.%u.%u\n", tag, ip & 0xFFu, (ip >> 8) & 0xFFu, (ip >> 16) & 0xFFu,
                  (ip >> 24) & 0xFFu);
    pc_if_kind iface = Physical.link->egress();
    static const char *const names[] = {"ANY", "STA", "AP", "ETH"};
    int i = (int)iface;
    Serial.printf("LT %s egress_iface=%d (%s)\n", tag, i, (i >= 0 && i <= 3) ? names[i] : "?");
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // USB-CDC enumeration window on the S3 native port
    Serial.println("LT boot: physical (L1) link-bringup test");

    uint8_t mac[6] = {0};
    if (Physical.link->mac(mac)) // WiFi STA MAC (zeros on an Ethernet-only device that never starts WiFi)
    {
        Serial.printf("LT wifi_sta_mac=%02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4],
                      mac[5]);
    }

#if PC_ENABLE_ETHERNET
    Serial.println("LT link=ethernet (init_eth_physical)");
    bool started = Physical.eth->init();
    Serial.printf("LT init_eth_physical=%d\n", (int)started);
    uint32_t t0 = millis();
    while (!Physical.eth->ready() && (millis() - t0) < 20000u)
    {
        delay(250);
    }
    Serial.printf("LT eth_ready=%d after %lums\n", (int)Physical.eth->ready(), (unsigned long)(millis() - t0));
#else
    Serial.printf("LT link=wifi-sta ssid=%s (init_wifi_physical)\n", WIFI_SSID);
    bool started = Physical.wifi->init(WIFI_SSID, WIFI_PASS);
    Serial.printf("LT init_wifi_physical=%d\n", (int)started);
    uint32_t t0 = millis();
    while (!Physical.wifi->ready() && (millis() - t0) < 25000u)
    {
        delay(250);
    }
    Serial.printf("LT wifi_ready=%d after %lums\n", (int)Physical.wifi->ready(), (unsigned long)(millis() - t0));
    if (Physical.wifi->ready())
    {
        char ssid[33] = {0};
        Physical.wifi->ssid(ssid, sizeof(ssid));
        Serial.printf("LT wifi ssid=%s channel=%u rssi=%d\n", ssid, (unsigned)Physical.wifi->channel(),
                      (int)Physical.wifi->rssi());
    }
#endif

    uint8_t emac[6] = {0}; // the MAC actually on the wire (Ethernet PHY MAC on the P4, WiFi STA MAC on the S3)
    if (Physical.link->egress_mac(emac))
    {
        Serial.printf("LT egress_mac=%02x:%02x:%02x:%02x:%02x:%02x\n", emac[0], emac[1], emac[2], emac[3], emac[4],
                      emac[5]);
    }
    else
    {
        Serial.println("LT egress_mac=<none> (no egress interface up)");
    }

    lt_report("up");
}

void loop()
{
    delay(3000);
    lt_report("tick");
}
