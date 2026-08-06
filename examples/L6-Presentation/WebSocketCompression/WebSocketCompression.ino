// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file WebSocketCompression.ino
 * @brief WebSocket permessage-deflate (RFC 7692): transparent two-way compression.
 *
 * Identical to WebSocket (a /ws echo endpoint) except it enables
 * PC_ENABLE_WS_DEFLATE. With the extension compiled in, the server advertises
 * `permessage-deflate; client_no_context_takeover; server_no_context_takeover`
 * during the handshake; any browser that offered the extension (Chrome, Firefox,
 * Safari all do by default) then sends its frames DEFLATE-compressed with the
 * RSV1 bit set, and the library decompresses them transparently before
 * ws_message() runs - the handler still reads plaintext from ws_pool[ws_id].buf.
 *
 * Replies are compressed too: ws_send_text() / ws_send_binary() DEFLATE the
 * payload and set RSV1 when the result is smaller (a frame that would not shrink
 * goes out uncompressed - the per-message flag makes that legal). Both directions
 * use bounded INFLATE/DEFLATE whose tables come from the shared per-dispatch
 * scratch arena (no per-connection buffer); a message must both arrive and
 * decompress within WS_FRAME_SIZE.
 *
 * Flash, open Serial @ 115200 for the IP, browse to http://<ip>/ and type. In the
 * browser devtools Network tab the /ws request shows
 * "Sec-WebSocket-Extensions: permessage-deflate" on the 101 response, and the
 * echoed frames show the compressed length under their "Length" column.
 */

// Enable WebSocket permessage-deflate for this sketch (overrides default-off).
#define PC_ENABLE_WS_DEFLATE 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


static const char PAGE[] = "<!doctype html><meta charset=utf-8><title>WS deflate echo</title>"
                           "<input id=i autofocus><pre id=o></pre><script>"
                           "var w=new WebSocket('ws://'+location.host+'/ws');"
                           "w.onmessage=function(e){o.textContent+=e.data+'\\n'};"
                           "i.onkeydown=function(e){if(e.key=='Enter'){w.send(i.value);i.value=''}}</script>";

void ws_connect(uint8_t ws_id)
{
    ws_send_text(ws_id, "connected to /ws (permessage-deflate) - type something");
}

void ws_message(uint8_t ws_id)
{
    // buf is already decompressed plaintext, even when the frame arrived compressed.
    char out[WS_FRAME_SIZE + 8];
    snprintf(out, sizeof(out), "echo: %s", (const char *)ws_pool[ws_id].buf);
    ws_send_text(ws_id, out);
}

void ws_close(uint8_t ws_id)
{
    (void)ws_id;
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

    on_http("/", HTTP_GET, [](uint8_t id, HttpReq *) { send_text(id, 200, "text/html", PAGE); });
    on_ws("/ws", ws_connect, ws_message, ws_close);
    begin_http(80, NULL);
}

void loop()
{
    handle();
}
