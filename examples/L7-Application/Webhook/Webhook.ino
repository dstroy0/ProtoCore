// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Webhook.ino
 * @brief Outbound webhooks / IFTTT (PROTOCORE_ENABLE_WEBHOOK).
 *
 * Pushes an event FROM the device: builds an IFTTT Maker URL + value1/2/3 JSON
 * and POSTs it through the outbound http_client. GET /fire triggers it on demand;
 * point WEBHOOK_URL at IFTTT, a Slack/Discord incoming webhook, or your own API.
 *
 * NOTE: enable both flags for the whole build. In platformio.ini:
 *     build_flags = -DPROTOCORE_ENABLE_HTTP_CLIENT=1 -DPROTOCORE_ENABLE_WEBHOOK=1
 * (Arduino IDE: they are already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PROTOCORE_ENABLE_HTTP_CLIENT 1
#define PROTOCORE_ENABLE_WEBHOOK 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "services/net/webhook/webhook.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// A plain webhook endpoint (Slack/Discord/your API). For IFTTT use the helper below.
static const char *WEBHOOK_URL = "http://192.168.1.10:8080/hook";

static uint8_t webhook_work[16]; // Webhook keeps no state in its borrow


void setup()
{
    Serial.begin(115200);
    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    begin_http(80, NULL);
}

void loop()
{
    // Fire a webhook once at startup (and you could fire on any event). Done from
    // loop() (not a handler) so the blocking POST never stalls the worker that
    // serves this server.
    static bool fired = false;
    Physical.wifi_ready(protocore_physical_span());
    if (!fired && PhysicalV.ok)
    {
        fired = true;
        char body[128];
        WebhookV.ifttt.value1 = "boot";
        WebhookV.ifttt.value2 = "esp32";
        WebhookV.ifttt.value3 = nullptr;
        WebhookV.build.out = body;
        WebhookV.build.cap = sizeof(body);
        Webhook.ifttt_payload(webhook_work);
        WebhookV.request.target_uri = WEBHOOK_URL;
        WebhookV.request.content = body;
        Webhook.post(webhook_work);
        int status = WebhookV.i32;
        Serial.printf("[webhook] POST -> status %d\n", status);

        // IFTTT Maker form (needs your real key):
        //   WebhookV.ifttt.event = "device_boot";
        //   WebhookV.ifttt.key = "YOUR_IFTTT_KEY";
        //   WebhookV.ifttt.value1 = "esp32"; // value2 / value3 = nullptr
        //   Webhook.ifttt_trigger(webhook_work);
    }
}
