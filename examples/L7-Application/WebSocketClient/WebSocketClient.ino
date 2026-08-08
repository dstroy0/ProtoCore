// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file WebSocketClient.ino
 * @brief Outbound WebSocket client: the device connects to a WS server.
 *
 * Connects to a remote WebSocket endpoint, sends a text message once a second,
 * and prints whatever the server sends back (this demo points at a public echo
 * service, so it receives its own messages). Point HOST/PORT/PATH at your own
 * dashboard / control plane.
 *
 * Flash, open Serial @ 115200. Client frames are masked per RFC 6455;
 * ping/pong and close are handled by ws_client_loop().
 *
 * This demo uses a wss:// echo, so it needs the TLS flags. Optional services are
 * gated by a compile flag the *library* sources must also see; for PlatformIO
 * enable them for the whole build:
 *     build_flags = -DPC_ENABLE_WS_CLIENT=1 -DPC_ENABLE_TLS=1 -DPC_ENABLE_WS_CLIENT_TLS=1
 * For a plain ws:// endpoint, just -DPC_ENABLE_WS_CLIENT=1 and set USE_TLS=false
 * / PORT=80. (Arduino IDE: they are already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_WS_CLIENT 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/net/ws_client/ws_client.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static const char *HOST = "ws.postman-echo.com"; // public WebSocket echo (wss)
static const bool USE_TLS = true;
static const uint16_t PORT = 443;
static const char *PATH = "/raw";

void on_message(uint8_t opcode, const uint8_t *payload, size_t len)
{
    Serial.printf("RX (op %u): %.*s\n", opcode, (int)len, (const char *)payload);
}

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

    ws_client_on_message(on_message);

    if (ws_client_connect(HOST, PORT, USE_TLS, PATH))
    {
        Serial.println("WebSocket connected");
    }
    else
    {
        Serial.println("WebSocket connect failed");
    }
}

void loop()
{
    ws_client_loop();

    static uint32_t last = 0;
    static uint32_t n = 0;
    if (ws_client_connected() && millis() - last >= 1000)
    {
        last = millis();
        char msg[48];
        snprintf(msg, sizeof(msg), "hello from esp32 #%lu", (unsigned long)n++);
        ws_client_send_text(msg);
    }
}
