// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Webhook.ino
 * @brief Outbound webhooks / IFTTT (PC_ENABLE_WEBHOOK).
 *
 * Pushes an event FROM the device: builds an IFTTT Maker URL + value1/2/3 JSON
 * and POSTs it through the outbound http_client. GET /fire triggers it on demand;
 * point WEBHOOK_URL at IFTTT, a Slack/Discord incoming webhook, or your own API.
 *
 * NOTE: enable both flags for the whole build. In platformio.ini:
 *     build_flags = -DPC_ENABLE_HTTP_CLIENT=1 -DPC_ENABLE_WEBHOOK=1
 * (Arduino IDE: they are already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_HTTP_CLIENT 1
#define PC_ENABLE_WEBHOOK 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/net/webhook/webhook.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// A plain webhook endpoint (Slack/Discord/your API). For IFTTT use the helper below.
static const char *WEBHOOK_URL = "http://192.168.1.10:8080/hook";


void setup()
{
    Serial.begin(115200);
    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
    {
        delay(250);
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
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
    if (!fired && wifi_ready())
    {
        fired = true;
        char body[128];
        pc_ifttt_payload("boot", "esp32", nullptr, body, sizeof(body));
        int status = pc_webhook_post(WEBHOOK_URL, body);
        Serial.printf("[webhook] POST -> status %d\n", status);

        // IFTTT Maker form (needs your real key):
        //   pc_ifttt_trigger("device_boot", "YOUR_IFTTT_KEY", "esp32", nullptr, nullptr);
    }
}
