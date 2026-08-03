// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ServerSentEvents.ino
 * @brief Server-Sent Events (text/event-stream) push via on_sse() + pc_sse_broadcast().
 *
 * Subscribes browsers at /events; the loop pushes a counter to every subscriber
 * once a second with pc_sse_broadcast(). A test page at / shows the live stream.
 *
 * Flash, open Serial @ 115200 for the IP, then browse to http://<ip>/.
 */

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


static const char PAGE[] = "<!doctype html><meta charset=utf-8><title>SSE</title><pre id=o></pre><script>"
                           "var s=new EventSource('/events');"
                           "s.addEventListener('tick',function(e){o.textContent+=e.data+'\\n'})</script>";

void pc_sse_connect(uint8_t pc_sse_id)
{
    pc_sse_send(pc_sse_id, "subscribed", "tick");
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

    on_http("/", HTTP_GET, [](uint8_t id, HttpReq *) { send_text(id, 200, "text/html", PAGE); });
    on_sse("/events", pc_sse_connect);
    begin_http(80);
}

void loop()
{
    handle();

    static unsigned long last = 0;
    static unsigned long n = 0;
    if (millis() - last >= 1000)
    {
        last = millis();
        char buf[24];
        snprintf(buf, sizeof(buf), "%lu", n++);
        pc_sse_broadcast("/events", buf, "tick");
    }
}
