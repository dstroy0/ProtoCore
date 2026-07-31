// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Guardrails.ino
 * @brief Runtime heap/stack guardrails (PC_ENABLE_GUARDRAILS).
 *
 * Installs a breach callback and checks the guardrails once a second: free heap,
 * heap low-water, largest free block (fragmentation), and this task's remaining
 * stack. When any crosses its PC_GUARDRAIL_* floor the callback fires so the
 * app can shed load / drop to a safe state / reboot before exhaustion bites. The
 * live snapshot is also served as JSON at GET /health.
 *
 * NOTE: enable it for the whole build (a .ino #define does not reach the
 * separately compiled library). In platformio.ini:
 *     build_flags = -DPC_ENABLE_GUARDRAILS=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_GUARDRAILS 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/security/guardrails/guardrails.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

static void on_breach(uint8_t breaches, const pc_health *h)
{
    Serial.printf("[guardrail] breach=0x%02x heap=%u frag=%u stack=%u\n", breaches, (unsigned)h->free_heap,
                  (unsigned)h->largest_free_block, (unsigned)h->stack_free);
    // Real app: shed load, drop to a safe state, or ESP.restart().
}

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

    pc_guardrails_begin(on_breach);

    server.on("/health", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
        pc_health h;
        pc_guardrails_sample(&h);
        char buf[128];
        pc_health_json(&h, buf, sizeof(buf));
        server.send(id, 200, "application/json", buf);
    });
    server.begin(80);
}

void loop()
{
    static uint32_t last = 0;
    if (millis() - last >= 1000)
    {
        last = millis();
        pc_guardrails_check(); // fires on_breach() if any floor is crossed
    }
}
