// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file WebSocket.ino
 * @brief Bidirectional WebSocket echo (RFC 6455) via on_ws().
 *
 * Registers a WebSocket endpoint at /ws: each text frame the browser sends is
 * echoed back with an "echo: " prefix. The incoming message is read from
 * ws_pool[ws_id].buf (null-terminated). A tiny test page is served at /.
 *
 * Flash, open Serial @ 115200 for the IP, then browse to http://<ip>/ and type.
 */

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


static const char PAGE[] = "<!doctype html><meta charset=utf-8><title>WS echo</title>"
                           "<input id=i autofocus><pre id=o></pre><script>"
                           "var w=new WebSocket('ws://'+location.host+'/ws');"
                           "w.onmessage=function(e){o.textContent+=e.data+'\\n'};"
                           "i.onkeydown=function(e){if(e.key=='Enter'){w.send(i.value);i.value=''}}</script>";

void ws_connect(uint8_t ws_id)
{
    ws_send_text(ws_id, "connected to /ws - type something");
}

void ws_message(uint8_t ws_id)
{
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
    init_wifi_physical(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!wifi_ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    on_http("/", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { send_text(id, 200, "text/html", PAGE); });
    on_ws("/ws", ws_connect, ws_message, ws_close);
    begin_http(80);
}

void loop()
{
    handle();
}
