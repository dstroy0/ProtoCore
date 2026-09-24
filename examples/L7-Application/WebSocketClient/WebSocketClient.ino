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
 * ping/pong and close are handled by WsClient.loop().
 *
 * The client speaks ws://. Optional services are gated by a compile flag the *library*
 * sources must also see; for PlatformIO enable it for the whole build:
 *     build_flags = -DPROTOCORE_ENABLE_WS_CLIENT=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds
 * as-is.)
 */

#define PROTOCORE_ENABLE_WS_CLIENT 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "services/net/ws_client/ws_client.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static const char *HOST = "ws.postman-echo.com"; // public WebSocket echo
static const uint16_t PORT = 80;
static const char *PATH = "/raw";

void on_message(uint8_t opcode, const uint8_t *payload, size_t len)
{
    Serial.printf("RX (op %u): %.*s\n", opcode, (int)len, (const char *)payload);
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

    uint8_t *span = protocore_ws_client_span();
    WsClientV.msg.on_message = on_message;
    WsClient.on_message(span); // record the callback before connecting

    WsClientV.handshake.host = HOST;
    WsClientV.handshake.port = PORT;
    WsClientV.handshake.secure = false; // ws://
    WsClientV.handshake.resource_name = PATH;
    WsClientV.handshake.subprotocol = nullptr;
    WsClient.connect(span);
    if (WsClientV.ok)
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
    WsClient.loop(protocore_ws_client_span());

    static uint32_t last = 0;
    static uint32_t n = 0;
    WsClient.connected(protocore_ws_client_span());
    if (WsClientV.ok && millis() - last >= 1000)
    {
        last = millis();
        char msg[48];
        snprintf(msg, sizeof(msg), "hello from esp32 #%lu", (unsigned long)n++);
        WsClientV.msg.text = msg;
        WsClient.send_text(protocore_ws_client_span());
    }
}
