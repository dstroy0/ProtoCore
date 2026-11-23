// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

// IdemIpPing: one Wi-Fi station on the idemIP stack, with no esp_netif. The station joins the network of
// WIFI_SSID, takes the address STATION_ADDRESS, and answers ARP requests and pings at that address.
//
// Set the four values below for your network. Then flash, and ping STATION_ADDRESS from a host on the
// same network. The serial monitor shows the frames that each second of ticks drained and sent.

#include "protocore.h"

#include "network_drivers/network/stack/stack.h"
#include "network_drivers/physical/wifi_link/wifi_link.h"

#include <esp_random.h> // esp_fill_random, esp_random: the secret of the initial sequence numbers, and the tick word

static const char WIFI_SSID[] = "your-network";
static const char WIFI_PASSWORD[] = "your-passphrase";
static const uint32_t STATION_ADDRESS = 0xC0A80132u; // 192.168.1.50
static const uint32_t STATION_MASK = 0xFFFFFF00u;    // 255.255.255.0

static uint32_t s_frames;
static uint32_t s_sent;
static uint32_t s_report_ms;

void setup()
{
    Serial.begin(115200);

    // The link starts first: the radio gives its hardware address only after it starts.
    const WifiLinkStartArgs network = {.ssid = WIFI_SSID, .password = WIFI_PASSWORD};
    if (!protocore_wifi_link_start(&network))
    {
        Serial.println("the radio did not start");
        return;
    }

    static uint8_t secret[IDEMIP_TCP_ISN_SECRET_BYTES];
    esp_fill_random(secret, sizeof secret);
    StackV.start_args.isn_secret = secret;
    Stack.start(protocore_stack_span());

    const IdemIpPhyDriver *const driver = protocore_wifi_link_driver();
    StackV.bind_args.driver = driver;
    StackV.bind_args.mac = driver->mac();
    StackV.bind_args.rx_base = protocore_wifi_link_rx_base();
    StackV.bind_args.tx_base = protocore_wifi_link_tx_base();
    StackV.bind_args.phy_addr = 0u;
    StackV.bind_args.index = 0u;
    Stack.bind(protocore_stack_span());

    StackV.addr4_args.address = STATION_ADDRESS;
    StackV.addr4_args.mask = STATION_MASK;
    StackV.addr4_args.gateway = 0u;
    StackV.addr4_args.index = 0u;
    Stack.set_addr4(protocore_stack_span());
    Serial.println(StackV.ok ? "the stack is up" : "the stack did not take its address");
}

void loop()
{
    StackV.tick_args.now_ms = millis();
    StackV.tick_args.random_word = esp_random();
    Stack.tick(protocore_stack_span());
    s_frames += StackV.frames;
    s_sent += StackV.sent;
    if (millis() - s_report_ms >= 1000u)
    {
        s_report_ms = millis();
        const bool up = protocore_wifi_link_driver()->link().up;
        Serial.printf("link %s, frames %lu, sent %lu, dropped %lu\n", up ? "up" : "down",
                      (unsigned long)s_frames, (unsigned long)s_sent, (unsigned long)protocore_wifi_link_dropped());
        s_frames = 0u;
        s_sent = 0u;
    }
}
