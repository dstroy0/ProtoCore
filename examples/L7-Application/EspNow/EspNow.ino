// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file EspNow.ino
 * @brief ESP-NOW peer messaging (PC_ENABLE_ESPNOW).
 *
 * Connectionless peer-to-peer radio messaging - no AP, no IP. Each board
 * broadcasts a counter every 2 s and prints any framed message it receives.
 * Flash two boards (same channel) and they see each other over the Serial monitor.
 *
 * Messages carry a 1-byte type so a receiver can demux; here type 1 = "counter".
 * To bridge into the web server, call this library's ws_send_* from the receive
 * callback to fan ESP-NOW traffic out to browser WebSocket clients.
 *
 * NOTE: enable it for the whole build. In platformio.ini:
 *     build_flags = -DPC_ENABLE_ESPNOW=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_ESPNOW 1

#include "protocore.h" // discovers the library (adds src/ to the include path)
#include "network_drivers/physical/physical.h"
#include "services/radio/espnow/espnow.h"

static const uint8_t MSG_COUNTER = 1;
static const uint8_t CHANNEL = 1;

static void on_espnow(const uint8_t mac[6], uint8_t type, const uint8_t *payload, size_t len)
{
    Serial.printf("rx from %02x:%02x:%02x:%02x:%02x:%02x type=%u: ", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  type);
    Serial.write(payload, len);
    Serial.println();
}

void setup()
{
    Serial.begin(115200);
    // ESP-NOW needs the radio up but not associated; STA mode pinned to a fixed channel.
    Physical.wifi->init_radio(CHANNEL);

    if (!pc_espnow_begin(CHANNEL, on_espnow))
    {
        Serial.println("ESP-NOW init failed");
        return;
    }
    uint8_t mac[6];
    Physical.link->mac(mac);
    Serial.printf("ESP-NOW up on channel %u, my MAC %02x:%02x:%02x:%02x:%02x:%02x\n", CHANNEL, mac[0], mac[1], mac[2],
                  mac[3], mac[4], mac[5]);
}

void loop()
{
    static uint32_t last = 0;
    static uint32_t n = 0;
    if (millis() - last >= 2000)
    {
        last = millis();
        char msg[24];
        int len = snprintf(msg, sizeof(msg), "count=%lu", (unsigned long)n++);
        bool ok = pc_espnow_broadcast(MSG_COUNTER, (const uint8_t *)msg, (size_t)len);
        Serial.printf("broadcast %s -> %s\n", msg, ok ? "ok" : "FAIL");
    }
}
