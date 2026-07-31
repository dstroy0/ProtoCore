# WebSocketClient - the device connects to a WebSocket server

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_WS_CLIENT` (optional `PC_ENABLE_TLS` + `PC_ENABLE_WS_CLIENT_TLS` for `wss://`)

## What this example teaches

The mirror of the server-side WebSocket: here the device is the **client**. It
connects to a remote WebSocket endpoint, sends a text message once a second, and
prints whatever the server sends back (the demo points at a public echo service,
so it receives its own messages). Client frames are masked per RFC 6455; ping/pong
and close are handled by `ws_client_loop()`.

**Register the receive callback, then connect:**

```cpp
ws_client_on_message(on_message);
if (ws_client_connect(HOST, PORT, USE_TLS, PATH))   // USE_TLS -> wss://
    Serial.println("WebSocket connected");
```

**Pump and send each loop.** `ws_client_loop()` services the connection (ping/pong,
close, inbound frames); `ws_client_send_text()` sends:

```cpp
void loop() {
    ws_client_loop();
    if (ws_client_connected() && /* once a second */)
        ws_client_send_text(msg);
}
```

This demo uses a `wss://` echo so it needs the TLS flags; for a plain `ws://`
endpoint, build with only `-DPC_ENABLE_WS_CLIENT=1`, set `USE_TLS=false`, and
`PORT=80`.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_WS_CLIENT=1 -DPC_ENABLE_TLS=1 -DPC_ENABLE_WS_CLIENT_TLS=1 -DPC_WS_CLIENT_BUF_SIZE=768 -DMAX_CONNS=4 -DPC_TLS_ARENA_SIZE=32768" \
  --lib="." examples/L7-Application/WebSocketClient/WebSocketClient.ino
```

`PC_WS_CLIENT_BUF_SIZE=768` trims the four outbound-WS buffers from the 1 KB default so the
`wss://` stack (TLS arena + WS client) fits the classic ESP32's DRAM; 768 B is ample for this demo's
small text messages. Boards with more DRAM (ESP32-S3) can keep the default.

Flash and watch Serial @ 115200: the echo server returns each message the device
sends.

## Annotated source

The complete sketch ([WebSocketClient.ino](WebSocketClient.ino)), reproduced
verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

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

// Delivered for every inbound frame (opcode 1 = text, 2 = binary).
void on_message(uint8_t opcode, const uint8_t *payload, size_t len)
{
    Serial.printf("RX (op %u): %.*s\n", opcode, (int)len, (const char *)payload);
}

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
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    ws_client_on_message(on_message);

    if (ws_client_connect(HOST, PORT, USE_TLS, PATH))
        Serial.println("WebSocket connected");
    else
        Serial.println("WebSocket connect failed");
}

void loop()
{
    ws_client_loop(); // service ping/pong, close, inbound frames

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
```
