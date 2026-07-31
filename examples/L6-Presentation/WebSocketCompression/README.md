# WebSocketCompression - permessage-deflate

**Layer:** L6 Presentation · **Build flags:** `PC_ENABLE_WS_DEFLATE` (requires `PC_ENABLE_WEBSOCKET`)

## What this example teaches

This is the [WebSocket echo](../WebSocket) with one flag added:
`PC_ENABLE_WS_DEFLATE` turns on permessage-deflate (RFC 7692), transparent
two-way compression. The handler code is unchanged - it still reads plaintext -
but frames travel compressed on the wire.

**Negotiated in the handshake.** With the extension compiled in, the server
advertises `permessage-deflate; client_no_context_takeover;
server_no_context_takeover` during the upgrade. Any browser that offered the
extension (Chrome/Firefox/Safari do by default) then sends DEFLATE-compressed
frames with the RSV1 bit set, and the library inflates them before `ws_message()`
runs:

```cpp
void ws_message(uint8_t ws_id) {
    // buf is already decompressed plaintext, even when the frame arrived compressed.
    char out[WS_FRAME_SIZE + 8];
    snprintf(out, sizeof(out), "echo: %s", (const char *)ws_pool[ws_id].buf);
    server.ws_send_text(ws_id, out);
}
```

**Replies compress too, opportunistically.** `ws_send_text()` / `ws_send_binary()`
DEFLATE the payload and set RSV1 only when the result is smaller; a frame that
would not shrink goes out uncompressed (the per-message flag makes that legal).

**Bounded, no per-connection buffers.** Both INFLATE and DEFLATE use bounded
tables drawn from the shared per-dispatch scratch arena rather than a buffer per
connection, so the determinism guarantee holds; a message must both arrive and
decompress within `WS_FRAME_SIZE`.

You can confirm it in browser devtools: the `/ws` 101 response shows
`Sec-WebSocket-Extensions: permessage-deflate`, and echoed frames show a
compressed length.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_WS_DEFLATE=1" \
  --lib="." examples/L6-Presentation/WebSocketCompression/WebSocketCompression.ino
```

Flash, browse to `http://<ip>/`, type, and inspect the `/ws` frames in devtools.

## Annotated source

The complete sketch ([WebSocketCompression.ino](WebSocketCompression.ino)),
reproduced verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

// Enable WebSocket permessage-deflate for this sketch (overrides default-off).
#define PC_ENABLE_WS_DEFLATE 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

static const char PAGE[] = "<!doctype html><meta charset=utf-8><title>WS deflate echo</title>"
                           "<input id=i autofocus><pre id=o></pre><script>"
                           "var w=new WebSocket('ws://'+location.host+'/ws');"
                           "w.onmessage=function(e){o.textContent+=e.data+'\\n'};"
                           "i.onkeydown=function(e){if(e.key=='Enter'){w.send(i.value);i.value=''}}</script>";

void ws_connect(uint8_t ws_id)
{
    server.ws_send_text(ws_id, "connected to /ws (permessage-deflate) - type something");
}

void ws_message(uint8_t ws_id)
{
    // buf is already decompressed plaintext, even when the frame arrived compressed.
    char out[WS_FRAME_SIZE + 8];
    snprintf(out, sizeof(out), "echo: %s", (const char *)ws_pool[ws_id].buf);
    server.ws_send_text(ws_id, out); // reply is DEFLATE'd only if it ends up smaller
}

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
