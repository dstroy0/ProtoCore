# WebSocketClient - the device connects to a WebSocket server

**Layer:** L7 Application · **Build flags:** `PROTOCORE_ENABLE_WS_CLIENT`

## What this example teaches

The mirror of the server-side WebSocket: here the device is the **client**. It
connects to a remote WebSocket endpoint, sends a text message once a second, and
prints whatever the server sends back (the demo points at a public echo service,
so it receives its own messages). Client frames are masked per RFC 6455; ping/pong
and close are handled by `ws_client_loop()`.

**Register the receive callback, then connect:**

```cpp
ws_client_on_message(on_message);
if (ws_client_connect(HOST, PORT, PATH))
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

This demo talks to a plain `ws://` echo endpoint on port 80.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPROTOCORE_ENABLE_WS_CLIENT=1 -DPROTOCORE_WS_CLIENT_BUF_SIZE=768 -DMAX_CONNS=4 -DPROTOCORE_ENABLE_TCP_CLIENT=1 -DPROTOCORE_ENABLE_DNS_RESOLVER=1" \
  --lib="." examples/L7-Application/WebSocketClient/WebSocketClient.ino
```

`PROTOCORE_WS_CLIENT_BUF_SIZE=768` trims the four outbound-WS buffers from the 1 KB default; 768 B
is ample for this demo's small text messages. Boards with more DRAM can keep the default.

Flash and watch Serial @ 115200: the echo server returns each message the device
sends.

## Annotated source

The complete sketch ([WebSocketClient.ino](WebSocketClient.ino)), reproduced
verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PROTOCORE_ENABLE_WS_CLIENT 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "services/net/ws_client/ws_client.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static const char *HOST = "ws.postman-echo.com"; // public WebSocket echo
static const uint16_t PORT = 80;
static const char *PATH = "/raw";

// Delivered for every inbound frame (opcode 1 = text, 2 = binary).
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
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    ws_client_on_message(on_message);

    if (ws_client_connect(HOST, PORT, PATH))
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
