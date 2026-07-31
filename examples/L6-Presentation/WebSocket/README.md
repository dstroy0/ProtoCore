# WebSocket - bidirectional WebSocket echo

**Layer:** L6 Presentation · **Build flags:** none (core; WebSocket is on by default)

## What this example teaches

This registers a WebSocket endpoint (RFC 6455) at `/ws` with three callbacks -
connect, message, close - and echoes each text frame back with an `echo: `
prefix. A tiny test page at `/` connects and lets you type.

**One call, three callbacks.** `on_ws(path, connect, message, close)` wires the
whole endpoint:

```cpp
server.on_ws("/ws", ws_connect, ws_message, ws_close);
```

**Reading the incoming frame.** When a message arrives, the payload is in
`ws_pool[ws_id].buf` (null-terminated, up to `WS_FRAME_SIZE`). You reply with
`ws_send_text()` (or `ws_send_binary()`):

```cpp
void ws_message(uint8_t ws_id) {
    char out[WS_FRAME_SIZE + 8];
    snprintf(out, sizeof(out), "echo: %s", (const char *)ws_pool[ws_id].buf);
    server.ws_send_text(ws_id, out);
}
```

The connect callback greets the new client; the close callback is where you would
release any per-connection state (none here). Each WebSocket occupies one
connection slot (capped at `MAX_WS_CONNS`). For `wss://` over TLS see
[SecureWebSocket](../SecureWebSocket); for transparent compression see
[WebSocketCompression](../WebSocketCompression).

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --lib="." examples/L6-Presentation/WebSocket/WebSocket.ino
```

Flash, browse to `http://<ip>/`, and type - each line comes back echoed.

## Annotated source

The complete sketch ([WebSocket.ino](WebSocket.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// Test page: open a WebSocket to /ws, append messages, send on Enter.
static const char PAGE[] = "<!doctype html><meta charset=utf-8><title>WS echo</title>"
                           "<input id=i autofocus><pre id=o></pre><script>"
                           "var w=new WebSocket('ws://'+location.host+'/ws');"
                           "w.onmessage=function(e){o.textContent+=e.data+'\\n'};"
                           "i.onkeydown=function(e){if(e.key=='Enter'){w.send(i.value);i.value=''}}</script>";

// Fires when a client finishes the WebSocket handshake.
void ws_connect(uint8_t ws_id)
{
    server.ws_send_text(ws_id, "connected to /ws - type something");
}

// Fires per inbound frame; the payload is in ws_pool[ws_id].buf (null-terminated).
void ws_message(uint8_t ws_id)
{
    char out[WS_FRAME_SIZE + 8];
    snprintf(out, sizeof(out), "echo: %s", (const char *)ws_pool[ws_id].buf);
    server.ws_send_text(ws_id, out);
}

// Fires on disconnect; release any per-connection state here (none in this demo).
void ws_close(uint8_t ws_id)
{
    (void)ws_id;
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

    server.on("/", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.send(id, 200, "text/html", PAGE); });
    server.on_ws("/ws", ws_connect, ws_message, ws_close);
    server.begin(80);
}

void loop()
{
    server.handle();
}
```
