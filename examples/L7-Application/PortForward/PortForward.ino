// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file PortForward.ino
 * @brief Publish an internal service through the ESP32 with the TCP relay / DNAT (PC_ENABLE_RELAY).
 *
 * The board fronts a port: anything that connects to the ESP32 on FRONT_PORT is relayed to an
 * internal `host:port` that the board can reach, and the return path comes back automatically. This
 * is DNAT / reverse port forwarding - e.g. expose a machine on a locked-down segment through the
 * device that bridges the two networks.
 *
 * Wiring is two calls: `listen(FRONT_PORT, ConnProto::PROTO_RELAY)` opens the front port, and
 * `pc_relay_publish()` binds it to the origin. The server's own poll loop pumps the bytes.
 *
 * Edit the lines marked "CHANGE ME" below, flash, and open Serial @ 115200. Then, from another
 * machine, connect to the board on FRONT_PORT and you reach the origin service.
 *
 * SECURITY: this forwards to whatever origin you publish, with no authentication of the inbound
 * side. Only publish trusted internal targets, and keep FRONT_PORT off untrusted networks.
 *
 * NOTE (PlatformIO): the relay is compiled into the *library*, so the flag must reach the whole
 * build: `build_flags = -DPC_ENABLE_RELAY=1`. In the Arduino IDE it is set for you in build_opt.h.
 */

#define PC_ENABLE_RELAY 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/net/relay/relay_listener.h" // pc_relay_publish

// --- CHANGE ME: your WiFi ---
static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// --- CHANGE ME: the front port on the board, and the internal service to publish through it ---
static const uint16_t FRONT_PORT = 8080;         // connect here on the ESP32
static const char *ORIGIN_HOST = "192.168.1.60"; // the internal host the board relays to
static const uint16_t ORIGIN_PORT = 80;          // ...and its port


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

    // Open the front port for the relay, then bind it to the origin.
    int32_t li = listen(FRONT_PORT, PROTO_RELAY);
    if (li < 0 || !pc_relay_publish((uint8_t)li, ORIGIN_HOST, ORIGIN_PORT))
    {
        Serial.println("relay publish failed - check the front port and origin");
        return;
    }
    begin();
    Serial.printf("relaying %u.%u.%u.%u:%u  ->  %s:%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF), FRONT_PORT, ORIGIN_HOST, ORIGIN_PORT);
    Serial.printf("connect to this board on port %u and you reach the origin\n", FRONT_PORT);
}

void loop()
{
    handle(); // the server poll loop pumps the relay both ways
}
